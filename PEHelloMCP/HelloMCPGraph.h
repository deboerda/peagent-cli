#pragma once

#include "HelloMCPCommon.h"
#include "BufferIO.h"

#include <cstring>
#include <process.h>

namespace hello_mcp {
namespace graph {

// This adapter deliberately calls the PE RelaGraph ABI, rather than writing
// the .rg binary format. The layout/version below is verified against the
// installed Release RelaGraph export contract and its matching Debug PDB.
struct DirectCreateResult {
    void* uiObj;
    DWORD exceptionCode;
};

static DirectCreateResult InvokeReleaseCreateUI(
    uintptr_t createAddress, void* project, void* uiModule, const char* graphName) {
    DirectCreateResult result = {NULL, 0};
    __try {
        typedef void* (__fastcall* CreateUIObjByIDFn)(
            void*, void*, unsigned int, unsigned int,
            unsigned __int64, int, const char*, int,
            unsigned __int64, unsigned long, void*);
        CreateUIObjByIDFn createUIObjByID =
            reinterpret_cast<CreateUIObjByIDFn>(createAddress);
        result.uiObj = createUIObjByID(
            project, uiModule, 0x31u, 0u,
            0ull, 1, graphName, 0,
            0ull, 0ul, NULL);
        if (result.uiObj) {
            void** objectVtable = *reinterpret_cast<void***>(result.uiObj);
            typedef void (__fastcall* SetNameFn)(void*, const char*);
            SetNameFn setName = reinterpret_cast<SetNameFn>(objectVtable[1]);
            setName(result.uiObj, graphName);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result.exceptionCode = GetExceptionCode();
    }
    return result;
}
struct DirectSaveResult {
    int result;
    DWORD exceptionCode;
};

static DirectSaveResult InvokeReleaseProjectSave(
    uintptr_t saveAddress, void* project) {
    DirectSaveResult result = {0, 0};
    __try {
        typedef int (__fastcall* ProjectSaveFn)(void*, const char*);
        ProjectSaveFn saveProject = reinterpret_cast<ProjectSaveFn>(saveAddress);
        result.result = saveProject(project, NULL);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result.exceptionCode = GetExceptionCode();
    }
    return result;
}
class NativeGraphService {
public:
    json Process(const std::string& action, const json& payload) {
        if (action == "graph_read" && payload.value("probe", false)) return ProbeProjectMgr();
        if (action == "graph_read" && payload.contains("registry_name")) return ProbeGraphRegistration(payload);
        if (action == "graph_create_native") { std::string _err; return CreateNative(payload, _err); }
        const std::string filename = payload.value("filename", std::string());
        if (filename.empty()) return Error("missing 'filename'");

        std::string error;
        if (action == "graph_register") return RegisterExisting(payload, filename, error);
        if (action == "graph_build_boot") return BuildBoot(payload, filename, error);
        if (action == "graph_create") return CreateAndOpen(payload, filename, error);
        if (action == "graph_remove") return RemoveTestGraph(payload, filename, error);

        Session session;
        if (!session.Open(filename, error)) return Error(error);

        if (action == "graph_read") return session.Describe(filename);
        if (action == "graph_node_create") {
            const int op = payload.value("op_type", OpType(payload.value("type", std::string())));
            if (!op) return Error("unsupported node type");
            if (!session.CreateNode(op, Utf8ToWide(payload.value("name", std::string("node"))),
                payload.value("x", 0), payload.value("y", 0), error)) return Error(error);
            json result = session.SaveAndVerify(filename, "node_created", error);
            if (result.value("success", false)) BindEditorToMainModule(filename, result, error);
            return result;
        }
        if (action == "graph_node_delete") {
            if (!session.DeleteNode(payload.value("id", 0), error)) return Error(error);
            json result = session.SaveAndVerify(filename, "node_deleted", error);
            if (result.value("success", false)) BindEditorToMainModule(filename, result, error);
            return result;
        }
        if (action == "graph_save") return session.SaveAndVerify(filename, "saved", error);
        return Error("unsupported native graph action");
    }

private:
    json ProbeGraphRegistration(const json& payload) const {
        const std::string name = payload.value("registry_name", std::string());
        CVsModule* module = reinterpret_cast<CVsModule*>(FxPluginMgr_GetData(NULL, "Ptr_MainVsModule"));
        GraphInfo* info = module && !name.empty() ? module->FindGraphInfo(name.c_str()) : NULL;
        return {{"success", true}, {"native", true}, {"registry_name", name},
            {"registered", info != NULL},
            {"registered_filename", info && info->filename ? info->filename : ""}};
    }

    // Calls the exact ProjectMgr path used by the PE project-tree "添加行为" menu:
    //   CPmProject::GetUIModule(CVsModule*) -> CUIModule*
    //   CPmProject::CreateUIObjByID(pUIModule, uID=49, uSubID=0, dwData=0,
    //       bNoShowNewDlg=1, newName, nGroupID=0, pMvoObj=0, mask=0, pUIObjParent=NULL)
    // uID 49 == UI_OBJ_TYPE_GRAPH (from ProjectMgr/Main.xml menu config).
    // Debug-only: these RVAs are only valid for the current ProjectMgrd.pdb build.
    json RegisterExisting(const json& payload, const std::string& filename, std::string& error) {
        const std::string requestedName = payload.value("name", std::string());
        if (requestedName.empty()) return Error("graph_register requires non-empty name");
        if (filename.empty() || !fs::exists(filename))
            return Error("graph_register requires an existing .rg file: " + filename);

        Session graph;
        if (!graph.Open(filename, error))
            return Error("existing graph failed native parse: " + error);

        std::string graphName = requestedName;
        if (graphName.size() < 3 || _stricmp(graphName.c_str() + graphName.size() - 3, ".rg") != 0)
            graphName += ".rg";

        std::ifstream originalFile(filename.c_str(), std::ios::binary);
        if (!originalFile) return Error("cannot read existing graph before registration");
        const std::vector<unsigned char> originalBytes(
            (std::istreambuf_iterator<char>(originalFile)), std::istreambuf_iterator<char>());

        CVsModule* module = reinterpret_cast<CVsModule*>(FxPluginMgr_GetData(NULL, "Ptr_MainVsModule"));
        if (!module) return Error("PE main module is unavailable");
        GraphInfo* existing = module->FindGraphInfo(graphName.c_str());
        if (existing && existing->filename && _stricmp(existing->filename, filename.c_str()) != 0) {
            const std::string registeredLeaf = fs::path(existing->filename).filename().string();
            const std::string requestedLeaf = fs::path(filename).filename().string();
            if (_stricmp(registeredLeaf.c_str(), requestedLeaf.c_str()) != 0)
                return Error("graph name already exists with another file");
        }

#ifdef _DEBUG
        if (!existing) module->AddGraphInfo(graphName.c_str(), filename.c_str(), 0);
#else
        if (!existing) {
            HMODULE projectMgr = GetModuleHandleA("ProjectMgr.plu");
            if (!projectMgr) return Error("Release ProjectMgr module is not loaded");
            const uintptr_t base = reinterpret_cast<uintptr_t>(projectMgr);
            void* project = *reinterpret_cast<void**>(base + 0x1BF698);
            if (!project || *reinterpret_cast<uintptr_t*>(project) != base + 0x15D3D0)
                return Error("Release ProjectMgr singleton/vtable mismatch");
            CVsModule* mainModule = module;
            std::string baseName = graphName.substr(0, graphName.size() - 3);
            typedef int (__fastcall* CreateGraphByNameFn)(CVsModule*, const char*, const char*, const char*);
            CreateGraphByNameFn createGraph =
                reinterpret_cast<CreateGraphByNameFn>(base + 0xC1040);
            if (!createGraph(mainModule, baseName.c_str(), "graph", NULL))
                return Error("Release ProjectMgr graph registration helper rejected the request");
        }
#endif

        std::ifstream afterFile(filename.c_str(), std::ios::binary);
        const std::vector<unsigned char> afterBytes(
            (std::istreambuf_iterator<char>(afterFile)), std::istreambuf_iterator<char>());
        if (afterBytes != originalBytes) {
            std::ofstream restore(filename.c_str(), std::ios::binary | std::ios::trunc);
            restore.write(reinterpret_cast<const char*>(originalBytes.data()), originalBytes.size());
            restore.close();
            return Error("registration helper modified the existing graph; original bytes were restored");
        }

        GraphInfo* registered = module->FindGraphInfo(graphName.c_str());
        if (!registered || !registered->filename)
            return Error("graph registration helper did not produce GraphInfo");

        int projectSaveResult = 0;
#ifdef _DEBUG
        FxSendMessage(PEM_MAIN_SET_MODIFY, 0, TRUE);
        projectSaveResult = static_cast<int>(FxSendMessage(PEM_MAIN_MSG, MAINMSG_ProjectSave, 0));
#else
        HMODULE projectMgrForSave = GetModuleHandleA("ProjectMgr.plu");
        if (!projectMgrForSave) return Error("Release ProjectMgr module is not loaded");
        const uintptr_t saveBase = reinterpret_cast<uintptr_t>(projectMgrForSave);
        void* saveProject = *reinterpret_cast<void**>(saveBase + 0x1BF698);
        if (!saveProject || *reinterpret_cast<uintptr_t*>(saveProject) != saveBase + 0x15D3D0)
            return Error("Release ProjectMgr singleton/vtable mismatch during save");
        FxSendMessage(PEM_MAIN_SET_MODIFY, 0, TRUE);
        DirectSaveResult nativeSave = InvokeReleaseProjectSave(saveBase + 0xF2750, saveProject);
        if (nativeSave.exceptionCode != 0)
            return Error("Release CPmProject::Save raised exception 0x" +
                std::to_string(nativeSave.exceptionCode));
        projectSaveResult = nativeSave.result;
#endif

        json result = graph.Describe(filename);
        result["success"] = true;
        result["native"] = true;
        result["action"] = "graph_register";
        result["graph_name"] = graphName;
        result["graph_file"] = filename;
        result["registered"] = module->FindGraphInfo(graphName.c_str()) != NULL;
        result["registered_filename"] = registered->filename;
        result["already_registered"] = existing != NULL;
        result["native_parse_verified"] = true;
        result["serialized_bytes"] = static_cast<long long>(fs::file_size(filename));
        result["round_trip_verified"] = false;
        result["project_save_result"] = static_cast<long long>(projectSaveResult);
        result["editor_session"] = "registered_only_release_editor_can_open_from_project_tree";
        return result;
    }

    json BuildBoot(const json& payload, const std::string& filename, std::string& error) {
        const std::string name = payload.value("name", std::string());
        if (name.empty()) return Error("graph_build_boot requires non-empty name");
        if (fs::exists(filename)) return Error("refusing to overwrite existing graph");

        std::string root = payload.value("tool_root", std::string());
        if (root.empty()) {
            char envRoot[2048] = {};
            const DWORD length = GetEnvironmentVariableA("PEAGENT_ROOT", envRoot, sizeof(envRoot));
            root = length > 0 && length < sizeof(envRoot) ? envRoot : "E:\\PEagent";
        }
        const std::string builder = payload.value("builder", root + "\\tools\\build_boot_graph.exe");
#ifdef _DEBUG
        const std::string defaultRelaGraph = root + "\\bin\\Debug-x64\\RelaGraphd.dll";
#else
        const std::string defaultRelaGraph = root + "\\bin\\Release-x64\\RelaGraph.dll";
#endif
        const std::string relaGraph = payload.value("relagraph_dll", defaultRelaGraph);
        const std::string includeFile = payload.value("include_file", "main\\Script\\include.script");
        if (!fs::exists(builder)) return Error("boot graph adapter was not found: " + builder);
        if (!fs::exists(relaGraph)) return Error("matching RelaGraph.dll was not found: " + relaGraph);

        std::vector<const char*> args;
        args.push_back(builder.c_str());
        args.push_back(filename.c_str());
        args.push_back(relaGraph.c_str());
        args.push_back(includeFile.c_str());
        args.push_back(NULL);
        const int exitCode = _spawnv(_P_WAIT, builder.c_str(), args.data());
        if (exitCode != 0) return Error("boot graph adapter failed with exit code " + std::to_string(exitCode));
        if (!fs::exists(filename)) return Error("boot graph adapter returned success but produced no graph file");

        Session graph;
        if (!graph.Open(filename, error)) return Error("generated graph failed native parse: " + error);
        json verified;
#ifdef _DEBUG
        verified = graph.SaveAndVerify(filename, "boot_created", error);
        if (!verified.value("success", false)) return verified;
#else
        // Release RelaGraph can parse the adapter output, but its legacy writer
        // drops boot/script objects; preserve the native adapter serialization.
        verified = graph.Describe(filename);
        verified["operation"] = "boot_created";
        verified["serialized_bytes"] = static_cast<long long>(fs::file_size(filename));
        verified["round_trip_verified"] = false;
        verified["native_parse_verified"] = true;
#endif

        CVsModule* module = reinterpret_cast<CVsModule*>(FxPluginMgr_GetData(NULL, "Ptr_MainVsModule"));
        if (!module) return Error("PE main module is unavailable");
#ifdef _DEBUG
        if (module->FindGraphInfo(name.c_str())) return Error("graph name already exists in PE module");
        module->AddGraphInfo(name.c_str(), filename.c_str(), 0);
#else
        // The Release product helper creates both the ProjectMgr UIObject and
        // the module GraphInfo. It preserves an already-written non-empty .rg.
        HMODULE projectMgrForUi = GetModuleHandleA("ProjectMgr.plu");
        if (!projectMgrForUi) return Error("Release ProjectMgr module is not loaded");
        const uintptr_t projectBaseForUi = reinterpret_cast<uintptr_t>(projectMgrForUi);
        void* projectForUi = *reinterpret_cast<void**>(projectBaseForUi + 0x1BF698);
        if (!projectForUi || *reinterpret_cast<uintptr_t*>(projectForUi) != projectBaseForUi + 0x15D3D0)
            return Error("Release ProjectMgr singleton/vtable mismatch");
        std::string baseNameForUi = name;
        if (baseNameForUi.size() > 3 &&
            _stricmp(baseNameForUi.c_str() + baseNameForUi.size() - 3, ".rg") == 0)
            baseNameForUi.resize(baseNameForUi.size() - 3);
        typedef int (__fastcall* CreateGraphByNameFn)(
            CVsModule*, const char*, const char*, const char*);
        CreateGraphByNameFn createGraphForUi =
            reinterpret_cast<CreateGraphByNameFn>(projectBaseForUi + 0xC1040);
        if (!createGraphForUi(module, baseNameForUi.c_str(), "graph", NULL))
            return Error("Release ProjectMgr graph helper failed to create the project-tree UIObject");
#endif
        GraphInfo* registeredInfo = module->FindGraphInfo(name.c_str());
        if (!registeredInfo || !registeredInfo->filename)
            return Error("boot graph was not registered in the PE module");        const bool opened = OpenNativeEditor(module, filename, error);
        int projectSaveResult = 0;
#ifdef _DEBUG
        FxSendMessage(PEM_MAIN_SET_MODIFY, 0, TRUE);
        projectSaveResult = static_cast<int>(FxSendMessage(PEM_MAIN_MSG, MAINMSG_ProjectSave, 0));
#else
        HMODULE projectMgr = GetModuleHandleA("ProjectMgr.plu");
        if (!projectMgr) return Error("Release ProjectMgr module is not loaded");
        const uintptr_t projectBase = reinterpret_cast<uintptr_t>(projectMgr);
        void* project = *reinterpret_cast<void**>(projectBase + 0x1BF698);
        if (!project || *reinterpret_cast<uintptr_t*>(project) != projectBase + 0x15D3D0)
            return Error("Release ProjectMgr singleton/vtable mismatch");
        FxSendMessage(PEM_MAIN_SET_MODIFY, 0, TRUE);
        DirectSaveResult nativeSave = InvokeReleaseProjectSave(
            projectBase + 0xF2750, project);
        if (nativeSave.exceptionCode != 0)
            return Error("Release CPmProject::Save raised exception 0x" +
                std::to_string(nativeSave.exceptionCode));
        projectSaveResult = nativeSave.result;
#endif
        verified["action"] = "graph_build_boot";
        verified["graph_name"] = name;
        verified["graph_file"] = filename;
        verified["include_file"] = includeFile;
        verified["adapter"] = builder;
        verified["adapter_exit_code"] = exitCode;
        verified["registered"] = module->FindGraphInfo(name.c_str()) != NULL;
        verified["editor_session"] = opened ? "native_projectmgr_editor_session_loaded" : "graph_initialized_editor_view_not_bound";
        verified["project_save_result"] = static_cast<long long>(projectSaveResult);
        if (!opened) verified["editor_error"] = error;
        return verified;
    }

    struct NativeCreateRequest {
        NativeGraphService* service;
        const json* payload;
        std::string error;
        json result;
    };

    static UINT GraphUiMessage() { return WM_APP + 0x4A7; }
    static HWND& GraphUiHwnd() { static HWND hwnd = NULL; return hwnd; }
    static WNDPROC& GraphUiPreviousProc() { static WNDPROC proc = NULL; return proc; }

    static LRESULT CALLBACK GraphUiWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == GraphUiMessage() && lParam != 0) {
            NativeCreateRequest* request = reinterpret_cast<NativeCreateRequest*>(lParam);
            request->result = request->service->CreateNative(*request->payload, request->error);
            return 0;
        }
        WNDPROC previous = GraphUiPreviousProc();
        return previous ? CallWindowProcW(previous, hwnd, message, wParam, lParam)
                         : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static bool InstallGraphUiDispatch(std::string& error) {
        HWND hwnd = FxGetMainAppHwnd();
        if (!hwnd) { error = "PE main window is unavailable"; return false; }
        if (GraphUiPreviousProc() != NULL) { error = "graph UI dispatch is already active"; return false; }
        LONG_PTR previous = SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&GraphUiWndProc));
        if (previous == 0) { error = "could not install temporary PE main-window dispatch"; return false; }
        GraphUiHwnd() = hwnd;
        GraphUiPreviousProc() = reinterpret_cast<WNDPROC>(previous);
        return true;
    }

    static void RemoveGraphUiDispatch() {
        if (GraphUiHwnd() != NULL && GraphUiPreviousProc() != NULL)
            SetWindowLongPtrW(GraphUiHwnd(), GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(GraphUiPreviousProc()));
        GraphUiHwnd() = NULL;
        GraphUiPreviousProc() = NULL;
    }
    json CreateNative(const json& payload, std::string& error) {
#ifdef _DEBUG
        const std::string name = payload.value("name", std::string());
        if (name.empty()) return Error("graph_create_native requires non-empty name");

        HMODULE projectMgr = GetModuleHandleA("ProjectMgrd.plu");
        if (!projectMgr) return Error("Debug ProjectMgr module is not loaded");

        const uintptr_t base = reinterpret_cast<uintptr_t>(projectMgr);

        // Verify theProjectMgr singleton identity via PDB-matched vtable check.
        const uintptr_t projectSlot = base + 0x916a08;
        void* project = *reinterpret_cast<void**>(projectSlot);
        if (!project) return Error("theProjectMgr singleton is NULL — is a project open?");
        const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(project);
        if (vtable != base + 0x7389e8)
            return Error("theProjectMgr vtable mismatch — ProjectMgrd.pdb no longer matches the loaded binary");

        // Get the CUIModule* for the PE main module.
        typedef void* (__fastcall* GetUIModuleFn)(void*, CVsModule*);
        GetUIModuleFn getUIModule = reinterpret_cast<GetUIModuleFn>(base + 0x19f080);
        CVsModule* mainModule = reinterpret_cast<CVsModule*>(FxPluginMgr_GetData(NULL, "Ptr_MainVsModule"));
        if (!mainModule) return Error("PE main VS module is unavailable — is a project open?");
        void* uiModule = getUIModule(project, mainModule);
        if (!uiModule) return Error("GetUIModule returned NULL — project tree may not be initialised");

        // CreateUIObjByID signature (from ProjectMgrd.pdb):
        // CUIObject* CreateUIObjByID(CUIModule* pUIModuleParent, uint uID, uint uSubID,
        //     uint64 dwData, int bNoShowNewDlg, const char* newName, int nGroupID,
        //     uint64 pMvoObj, ulong mask, CUIObject* pUIObjParent)
        typedef void* (__fastcall* CreateUIObjByIDFn)(
            void* self,
            void* pUIModuleParent,
            unsigned int uID,
            unsigned int uSubID,
            unsigned __int64 dwData,
            int bNoShowNewDlg,
            const char* newName,
            int nGroupID,
            unsigned __int64 pMvoObj,
            unsigned long mask,
            void* pUIObjParent
        );
        CreateUIObjByIDFn createUIObjByID =
            reinterpret_cast<CreateUIObjByIDFn>(base + 0x31fb90);

        // UI_OBJ_TYPE_GRAPH = 49 (verified from ProjectMgr/Main.xml "添加行为" entry).
        // bNoShowNewDlg=1 suppresses the interactive rename dialog.
        void* uiObj = createUIObjByID(
            project,
            uiModule,
            49u,   // uID = UI_OBJ_TYPE_GRAPH
            0u,    // uSubID
            0ull,  // dwData
            1,     // bNoShowNewDlg — suppress dialog, use newName directly
            name.c_str(),
            0,     // nGroupID
            0ull,  // pMvoObj
            0ul,   // mask
            NULL   // pUIObjParent
        );

        if (!uiObj)
            return Error("CreateUIObjByID returned NULL — name collision or ProjectMgr rejected creation");

        // Retrieve the filename that ProjectMgr assigned to the new graph object.
        // CUIGraph::GetFileName(char* filename) is the accessor (RVA 0x160680).
        typedef void (__fastcall* GetFileNameFn)(void*, char*);
        GetFileNameFn getFileName = reinterpret_cast<GetFileNameFn>(base + 0x160680);
        char assignedFile[1024] = {};
        getFileName(uiObj, assignedFile);

        // Save the project so the new entry is persisted on disk.
        int projectSaveResult = 0;
#ifdef _DEBUG
        FxSendMessage(PEM_MAIN_SET_MODIFY, 0, TRUE);
        projectSaveResult = static_cast<int>(FxSendMessage(PEM_MAIN_MSG, MAINMSG_ProjectSave, 0));
#else
        HMODULE projectMgr = GetModuleHandleA("ProjectMgr.plu");
        if (!projectMgr) return Error("Release ProjectMgr module is not loaded");
        const uintptr_t projectBase = reinterpret_cast<uintptr_t>(projectMgr);
        void* project = *reinterpret_cast<void**>(projectBase + 0x1BF698);
        if (!project || *reinterpret_cast<uintptr_t*>(project) != projectBase + 0x15D3D0)
            return Error("Release ProjectMgr singleton/vtable mismatch");
        FxSendMessage(PEM_MAIN_SET_MODIFY, 0, TRUE);
        DirectSaveResult nativeSave = InvokeReleaseProjectSave(
            projectBase + 0xF2750, project);
        if (nativeSave.exceptionCode != 0)
            return Error("Release CPmProject::Save raised exception 0x" +
                std::to_string(nativeSave.exceptionCode));
        projectSaveResult = nativeSave.result;
#endif

        return {
            {"success", true},
            {"native", true},
            {"action", "graph_create_native"},
            {"graph_name", name},
            {"ui_obj_ptr", reinterpret_cast<unsigned long long>(uiObj)},
            {"assigned_file", assignedFile},
            {"rg_open_message", 0},
            {"project_save_result", static_cast<long long>(projectSaveResult)},
            {"note", "Created via CPmProject::CreateUIObjByID — verify in PE project tree"}
        };
#else
        const std::string name = payload.value("name", std::string());
        if (name.empty()) return Error("graph_create_native requires non-empty name");

        HMODULE projectMgr = GetModuleHandleA("ProjectMgr.plu");
        if (!projectMgr) return Error("Release ProjectMgr module is not loaded");
        const uintptr_t base = reinterpret_cast<uintptr_t>(projectMgr);

        // Release ProjectMgr.plu build 2026-08-05: theProjectMgr and CPmProject vtable.
        const uintptr_t projectSlot = base + 0x1BF698;
        void* project = *reinterpret_cast<void**>(projectSlot);
        if (!project) return Error("theProjectMgr singleton is NULL — is a project open?");
        const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(project);
        if (vtable != base + 0x15D3D0)
            return Error("theProjectMgr vtable mismatch — loaded Release ProjectMgr.plu is not the analyzed build");

        CVsModule* mainModule = reinterpret_cast<CVsModule*>(FxPluginMgr_GetData(NULL, "Ptr_MainVsModule"));
        if (!mainModule) return Error("PE main VS module is unavailable — is a project open?");

        // Release inlines CPmProject::GetUIModule as CMap::operator[] on m_mapModule.
        typedef long long* (__fastcall* ModuleMapLookupFn)(void*, void*);
        ModuleMapLookupFn lookup = reinterpret_cast<ModuleMapLookupFn>(base + 0x763A0);
        void* moduleMap = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(project) + 0x150);
        long long* uiModuleSlot = lookup(moduleMap, mainModule);
        void* uiModule = uiModuleSlot ? reinterpret_cast<void*>(*uiModuleSlot) : NULL;
        if (!uiModule) return Error("Release ProjectMgr did not resolve a UI module for the PE main module");

        HWND mainWindow = FxGetMainAppHwnd();
        DWORD mainThread = mainWindow ? GetWindowThreadProcessId(mainWindow, NULL) : 0;
        if (!mainWindow || !mainThread) return Error("PE main-window thread is unavailable");
        if (GetCurrentThreadId() != mainThread) {
            std::string dispatchError;
            if (!InstallGraphUiDispatch(dispatchError)) return Error(dispatchError);
            NativeCreateRequest request;
            request.service = this;
            request.payload = &payload;
            SendMessageW(mainWindow, GraphUiMessage(), 0, reinterpret_cast<LPARAM>(&request));
            RemoveGraphUiDispatch();
            if (request.result.is_null()) return Error("PE main-window dispatch returned no result");
            return request.result;
        }

        // Release ProjectMgr build 2026-08-05: use the product's own graph
        // creation flow.  Its call site supplies the complete 11-argument
        // CreateUIObjByID ABI and builds the module-relative GraphInfo entry.
        std::string baseName = name;
        if (baseName.size() > 3 && _stricmp(baseName.c_str() + baseName.size() - 3, ".rg") == 0)
            baseName.resize(baseName.size() - 3);

        // Optional Release diagnostic: reproduce the product call site directly.
        // The returned UI object is named through vtable+8 before GraphInfo is added;
        // do not use the older guessed GetFileName RVA here.
        if (payload.value("direct_ui", false)) {
            char graphName[1024] = {};
            sprintf_s(graphName, sizeof(graphName), "%s.rg", baseName.c_str());

            DirectCreateResult direct = InvokeReleaseCreateUI(
                base + 0xF33F0, project, uiModule, graphName);
            if (direct.exceptionCode != 0)
                return Error("direct CreateUIObjByID raised exception 0x" +
                    std::to_string(direct.exceptionCode));
            void* uiObj = direct.uiObj;
            if (!uiObj) return Error("direct CreateUIObjByID returned NULL");

            const std::string registeredName = graphName;
            const std::string registeredPath = std::string("main\\") + registeredName;
            GraphInfo* registeredInfo = mainModule->FindGraphInfo(registeredName.c_str());
            if (!registeredInfo) {
                mainModule->AddGraphInfo(registeredName.c_str(), registeredPath.c_str(), 0);
                registeredInfo = mainModule->FindGraphInfo(registeredName.c_str());
            }
            if (!registeredInfo || !registeredInfo->filename)
                return Error("direct CreateUIObjByID did not produce GraphInfo");

            const std::string openPath = registeredInfo->filename;
            alignas(16) unsigned char openInfo[0x600] = {};
            *reinterpret_cast<int*>(openInfo + 0x28) = 1;
            *reinterpret_cast<const char**>(openInfo + 0x578) = openPath.c_str();
            *reinterpret_cast<int*>(openInfo + 0x5e4) = 0;
            LRESULT openResult = 0;
            if (payload.value("open_editor", true)) {
                openResult = SendMessageA(
                    mainWindow, PEM_MAIN_RG_OPEN, 0,
                    reinterpret_cast<LPARAM>(openInfo));
            }
            FxSendMessage(PEM_MAIN_SET_MODIFY, 0, TRUE);
            DirectSaveResult nativeSave = InvokeReleaseProjectSave(
                base + 0xF2750, project);
            if (nativeSave.exceptionCode != 0)
                return Error("Release CPmProject::Save raised exception 0x" +
                    std::to_string(nativeSave.exceptionCode));
            const int saveResult = nativeSave.result;
            return {
                {"success", true},
                {"native", true},
                {"action", "graph_create_native"},
                {"direct_ui", true},
                {"graph_name", registeredName},
                {"registered", true},
                {"registered_filename", registeredInfo->filename},
                {"rg_open_message", static_cast<long long>(openResult)},
                {"project_save_result", static_cast<long long>(saveResult)},
                {"note", "Release CreateUIObjByID call-site reproduction with vtable+8 naming"}
            };
        }
        typedef int (__fastcall* CreateGraphByNameFn)(
            CVsModule*, const char*, const char*, const char*);
        CreateGraphByNameFn createGraph =
            reinterpret_cast<CreateGraphByNameFn>(base + 0xC1040);
        const int nativeResult = createGraph(
            mainModule, baseName.c_str(), "graph", NULL);
        if (!nativeResult)
            return Error("Release ProjectMgr graph helper returned failure");

        const std::string registeredName = baseName + ".rg";
        GraphInfo* registeredInfo = mainModule->FindGraphInfo(registeredName.c_str());
        if (!registeredInfo || !registeredInfo->filename)
            return Error("Release graph helper did not produce GraphInfo");


        // Host PostEngineer receives PEM_MAIN_RG_OPEN with a private frame
        // whose path member is at LPARAM+0x578 (confirmed in PostEngineer.exe).
        const std::string openPath = registeredInfo->filename;
        alignas(16) unsigned char openInfo[0x600] = {};
        *reinterpret_cast<int*>(openInfo + 0x28) = 1;
        *reinterpret_cast<const char**>(openInfo + 0x578) = openPath.c_str();
        *reinterpret_cast<int*>(openInfo + 0x5e4) = 0;
        LRESULT openResult = 0;
        if (payload.value("open_editor", true)) {
            openResult = SendMessageA(
                mainWindow, PEM_MAIN_RG_OPEN, 0,
                reinterpret_cast<LPARAM>(openInfo));
        }
        FxSendMessage(PEM_MAIN_SET_MODIFY, 0, TRUE);
            DirectSaveResult nativeSave = InvokeReleaseProjectSave(
                base + 0xF2750, project);
            if (nativeSave.exceptionCode != 0)
                return Error("Release CPmProject::Save raised exception 0x" +
                    std::to_string(nativeSave.exceptionCode));
            const int saveResult = nativeSave.result;
        return {
            {"success", true},
            {"native", true},
            {"action", "graph_create_native"},
            {"graph_name", registeredName},
            {"registered", true},
            {"registered_filename", registeredInfo->filename},
            {"rg_open_message", static_cast<long long>(openResult)},
            {"project_save_result", static_cast<long long>(saveResult)},
            {"projectmgr_build", "Release-2026-08-05"},
            {"note", "Created via Release ProjectMgr graph helper with native 11-argument ABI"}
        };
#endif
    }

    json RemoveTestGraph(const json& payload, const std::string& filename, std::string& error) {
        const std::string name = payload.value("name", std::string());
        if (name.empty()) return Error("graph removal requires non-empty name");
        CVsModule* module = reinterpret_cast<CVsModule*>(FxPluginMgr_GetData(NULL, "Ptr_MainVsModule"));
        if (!module) return Error("PE main module is unavailable");
        GraphInfo* info = module->FindGraphInfo(name.c_str());
        if (!info) return Error("graph registration was not found");
        if (!info->filename || filename != info->filename)
            return Error("refusing removal: registered graph path does not match request");
        module->RemoveGraphInfo(name.c_str());
        int projectSaveResult = 0;
#ifdef _DEBUG
        FxSendMessage(PEM_MAIN_SET_MODIFY, 0, TRUE);
        projectSaveResult = static_cast<int>(FxSendMessage(PEM_MAIN_MSG, MAINMSG_ProjectSave, 0));
#else
        HMODULE projectMgr = GetModuleHandleA("ProjectMgr.plu");
        if (!projectMgr) return Error("Release ProjectMgr module is not loaded");
        const uintptr_t projectBase = reinterpret_cast<uintptr_t>(projectMgr);
        void* project = *reinterpret_cast<void**>(projectBase + 0x1BF698);
        if (!project || *reinterpret_cast<uintptr_t*>(project) != projectBase + 0x15D3D0)
            return Error("Release ProjectMgr singleton/vtable mismatch");
        FxSendMessage(PEM_MAIN_SET_MODIFY, 0, TRUE);
        DirectSaveResult nativeSave = InvokeReleaseProjectSave(
            projectBase + 0xF2750, project);
        if (nativeSave.exceptionCode != 0)
            return Error("Release CPmProject::Save raised exception 0x" +
                std::to_string(nativeSave.exceptionCode));
        projectSaveResult = nativeSave.result;
#endif
        if (module->FindGraphInfo(name.c_str())) return Error("PE did not remove graph registration");
        if (!fs::remove(filename)) return Error("registration removed but graph file could not be deleted");
        return {{"success", true}, {"native", true}, {"removed_name", name},
            {"removed_file", filename}, {"project_save_result", static_cast<long long>(projectSaveResult)}};
    }

    json ProbeProjectMgr() const {
        const char* keys[] = {
            "Ptr_MainUIModule", "Ptr_CurrentUIModule", "Ptr_UIModule",
            "Ptr_ProjectMgr", "Ptr_Project", "Ptr_MainVsModule"
        };
        json values = json::object();
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
            const FX_PTR globalValue = FxPluginMgr_GetData(NULL, keys[i], NULL);
            const FX_PTR projectMgrValue = FxPluginMgr_GetData("ProjectMgr", keys[i], NULL);
            values[keys[i]] = {
                {"global", static_cast<unsigned long long>(globalValue)},
                {"project_mgr", static_cast<unsigned long long>(projectMgrValue)}
            };
        }
        return {
            {"success", true}, {"native", true}, {"action", "graph_projectmgr_probe"},
            {"project_mgr_loaded", FxPluginMgr_IsLoaded("ProjectMgr") != NULL},
            {"project_mgr_running", FxPluginMgr_IsRunning("ProjectMgr")},
            {"data", values}
        };
    }

    json CreateAndOpen(const json& payload, const std::string& filename, std::string& error) {
        const std::string name = payload.value("name", std::string());
        if (name.empty()) return Error("graph creation requires non-empty name");
        if (filename.empty()) return Error("graph creation requires filename");
        if (fs::exists(filename)) return Error("refusing to overwrite existing graph");
        const fs::path graphPath(filename);
        if (!graphPath.parent_path().empty()) fs::create_directories(graphPath.parent_path());

        Session graph;
        if (!graph.CreateEmpty(filename, FxGetMainAppHwnd(), error)) return Error(error);
        json saved = graph.SaveAndVerify(filename, "created", error);
        if (!saved.value("success", false)) return saved;

        CVsModule* module = reinterpret_cast<CVsModule*>(FxPluginMgr_GetData(NULL, "Ptr_MainVsModule"));
        if (!module) return Error("PE main module is unavailable");
        if (module->FindGraphInfo(name.c_str())) return Error("graph name already exists in PE module");
        module->AddGraphInfo(name.c_str(), filename.c_str(), 0);
        const bool opened = OpenNativeEditor(module, filename, error);
        FxSendMessage(PEM_MAIN_SET_MODIFY, 0, TRUE);
        const LRESULT saveResult = FxSendMessage(PEM_MAIN_MSG, MAINMSG_ProjectSave, 0);
        saved["graph_name"] = name;
        saved["registered"] = module->FindGraphInfo(name.c_str()) != NULL;
        saved["editor_open_requested"] = opened;
        saved["project_save_result"] = static_cast<long long>(saveResult);
        saved["editor_session"] = opened ? "native_projectmgr_editor_session_loaded" : "graph_initialized_editor_view_not_bound";
        if (!opened) saved["editor_error"] = error;
        return saved;
    }

    // This is the same internal chain used by the PE project tree when a
    // behavior graph is opened.  It is deliberately Debug-only until a
    // matching Release ProjectMgr PDB is available.
    static bool OpenNativeEditor(CVsModule* mainModule, const std::string& filename, std::string& error) {
#ifdef _DEBUG
        HMODULE projectMgr = GetModuleHandleA("ProjectMgrd.plu");
        if (!projectMgr) { error = "Debug ProjectMgr module is not loaded"; return false; }
        const uintptr_t base = reinterpret_cast<uintptr_t>(projectMgr);
        const uintptr_t projectSlot = base + 0x916a08; // ProjectMgrd.pdb: theProjectMgr
        void* project = *reinterpret_cast<void**>(projectSlot);
        if (!project) { error = "Debug ProjectMgr singleton is unavailable"; return false; }
        const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(project);
        if (vtable != base + 0x7389e8) { error = "Debug ProjectMgr singleton layout does not match ProjectMgrd.pdb"; return false; }
        typedef void* (__fastcall* GetUIModuleFn)(void*, CVsModule*);
        typedef void (__fastcall* LoadGraphFn)(const char*, void*);
        GetUIModuleFn getUIModule = reinterpret_cast<GetUIModuleFn>(base + 0x19f080);
        LoadGraphFn loadGraph = reinterpret_cast<LoadGraphFn>(base + 0x294270);
        void* uiModule = getUIModule(project, mainModule);
        if (!uiModule) { error = "Debug ProjectMgr did not resolve a UI module for the PE main module"; return false; }
        loadGraph(filename.c_str(), uiModule);
        return true;
#else
        (void)mainModule; (void)filename;
        error = "native behavior-graph editor sessions require a matching ProjectMgr symbol build";
        return false;
#endif
    }

    static void BindEditorToMainModule(const std::string& filename, json& result, std::string& error) {
        CVsModule* module = reinterpret_cast<CVsModule*>(FxPluginMgr_GetData(NULL, "Ptr_MainVsModule"));
        const bool opened = module && OpenNativeEditor(module, filename, error);
        result["editor_session"] = opened ? "native_projectmgr_editor_session_loaded" : "native_editor_reload_failed";
        if (!opened) result["editor_error"] = error;
    }

    struct ListNode { ListNode* next; ListNode* prev; };
    static const size_t kGraphBytes = 4144;
    typedef void(__fastcall* GraphCtor)(void*, const char*);
    typedef void(__fastcall* GraphDtor)(void*);
    typedef int(__fastcall* GraphRead)(void*, CBufferIO&, int);
    typedef int(__fastcall* GraphWrite)(void*, CBufferIO&);
    typedef int(__fastcall* GraphInitialize)(void*, HWND*);
    typedef void* (__fastcall* GraphRoot)(void*);
    typedef void* (__fastcall* GraphOperator)(void*);
    typedef void* (__fastcall* CreateObject)(void*, int, const wchar_t*, int, int);
    typedef void* (__fastcall* FindById)(void*, long);
    typedef void(__fastcall* RemoveObject)(void*, void*);

    static json Error(const std::string& message) { return {{"success", false}, {"error", message}, {"native", true}}; }
    static std::wstring Utf8ToWide(const std::string& value) {
        if (value.empty()) return std::wstring();
        const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), NULL, 0);
        std::wstring out(count, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), &out[0], count);
        return out;
    }
    static int OpType(const std::string& type) {
        if (type == "boot") return 11;
        if (type == "program") return 7;
        if (type == "script") return 16;
        if (type == "timer") return 8;
        if (type == "branch") return 10;
        if (type == "condition") return 5;
        if (type == "comment") return 13;
        if (type == "step") return 2;
        return 0;
    }

    class Session {
    public:
        Session() : m_module(NULL), m_graph(NULL), m_root(NULL), m_ctor(NULL), m_dtor(NULL), m_read(NULL), m_write(NULL), m_initialize(NULL), m_rootFn(NULL), m_operatorFn(NULL), m_create(NULL), m_find(NULL), m_remove(NULL) {}
        ~Session() { if (m_graph && m_dtor) m_dtor(m_graph); }

        bool Open(const std::string& filename, std::string& error) {
            std::ifstream in(filename.c_str(), std::ios::binary);
            if (!in) { error = "cannot open behavior graph"; return false; }
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (bytes.size() < 7 || bytes[4] != 'R' || bytes[5] != 'G') { error = "not a RelaGraph .rg file"; return false; }
            m_module = GetRelaGraphModule();
            if (!m_module) { error = "PE RelaGraph runtime is not loaded"; return false; }
            if (!Bind(error)) return false;
            m_storage.resize(kGraphBytes + 16); m_graph = reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(m_storage.data()) + 15) & ~uintptr_t(15));
            m_ctor(m_graph, filename.c_str());
            CBufferIO buffer; buffer.SetBuffer(bytes.data(), (int64_t)bytes.size());
            if (!m_read(m_graph, buffer, 0)) { error = "RelaGraph rejected graph data"; return false; }
            // Reading restores serialized state only.  PE creates the operator
            // and editor services during Initialize before allowing mutations.
            HWND editorHost = FxGetMainAppHwnd();
            if (!m_initialize(m_graph, &editorHost)) { error = "RelaGraph editor initialization failed"; return false; }
            m_root = m_rootFn(m_graph);
            if (!m_root) { error = "RelaGraph returned no root object"; return false; }
            return true;
        }

        bool CreateEmpty(const std::string& filename, HWND host, std::string& error) {
            m_module = GetRelaGraphModule();
            if (!m_module) { error = "PE RelaGraph runtime is not loaded"; return false; }
            if (!Bind(error)) return false;
            m_storage.resize(kGraphBytes + 16); m_graph = reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(m_storage.data()) + 15) & ~uintptr_t(15));
            m_ctor(m_graph, filename.c_str());
            HWND editorHost = host;
            if (!m_initialize(m_graph, &editorHost)) { error = "RelaGraph initialization failed"; return false; }
            m_root = m_rootFn(m_graph);
            if (!m_root) { error = "RelaGraph did not create a graph root"; return false; }
            return true;
        }

        json Describe(const std::string& filename) const {
            return {{"success", true}, {"native", true}, {"filename", filename},
                {"parse_verified", true},
                {"capabilities", {"read", "save", "project_registration"}},
                {"node_mutation", "requires_native_editor_session"}};
        }

        bool CreateNode(int op, const std::wstring& name, int x, int y, std::string& error) {
            void* oper = m_operatorFn(m_graph);
            if (!oper) { error = "RelaGraph returned no operator"; return false; }
            if (!m_create(oper, op, name.c_str(), x, y)) { error = "RelaGraph could not create node"; return false; }
            return true;
        }

        bool DeleteNode(int id, std::string& error) {
            void* object = m_find(m_root, id);
            if (!object) { error = "node id was not found"; return false; }
            m_remove(m_root, object); return true;
        }

        json SaveAndVerify(const std::string& filename, const std::string& operation, std::string& error) {
            CBufferIO out;
            if (!m_write(m_graph, out)) { error = "RelaGraph serialization failed"; return Error(error); }
            const int64_t size = out.GetBufferCount();
            if (size <= 0) { error = "RelaGraph serialization produced no data"; return Error(error); }
            std::vector<unsigned char> bytes((size_t)size); out.GetBuffer(bytes.data());
            std::ofstream file(filename.c_str(), std::ios::binary | std::ios::trunc);
            if (!file) { error = "cannot save behavior graph"; return Error(error); }
            file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); file.close();
            Session verifier;
            if (!verifier.Open(filename, error)) return Error("saved graph failed native reload: " + error);
            json result = verifier.Describe(filename); result["operation"] = operation; result["serialized_bytes"] = size; result["round_trip_verified"] = true; return result;
        }

    private:
        static HMODULE GetRelaGraphModule() {
#ifdef _DEBUG
            HMODULE module = GetModuleHandleA("RelaGraphd.dll");
            return module ? module : LoadLibraryA("RelaGraphd.dll");
#else
            HMODULE module = GetModuleHandleA("RelaGraph.dll");
            return module ? module : LoadLibraryA("RelaGraph.dll");
#endif
        }
        FARPROC Symbol(const char* name, std::string& error) { FARPROC p = GetProcAddress(m_module, name); if (!p) error = std::string("RelaGraph contract missing ") + name; return p; }
        bool Bind(std::string& error) {
            m_ctor = reinterpret_cast<GraphCtor>(Symbol("??0CRelationGraph@@QEAA@PEBD@Z", error)); if (!m_ctor) return false;
            m_dtor = reinterpret_cast<GraphDtor>(Symbol("??1CRelationGraph@@UEAA@XZ", error)); if (!m_dtor) return false;
            m_read = reinterpret_cast<GraphRead>(Symbol("?ReadFromBuffer@CRelationGraph@@QEAAHAEAVCBufferIO@@H@Z", error)); if (!m_read) return false;
            m_write = reinterpret_cast<GraphWrite>(Symbol("?WriteToBuffer@CRelationGraph@@QEAAHAEAVCBufferIO@@@Z", error)); if (!m_write) return false;
            m_initialize = reinterpret_cast<GraphInitialize>(Symbol("?Initialize@CRelationGraph@@QEAAHPEAUHWND__@@@Z", error)); if (!m_initialize) return false;
            m_rootFn = reinterpret_cast<GraphRoot>(Symbol("?Graph@CRelationGraph@@QEAAPEAVCRgComplexObject@@XZ", error)); if (!m_rootFn) return false;
            m_operatorFn = reinterpret_cast<GraphOperator>(Symbol("?Operator@CRelationGraph@@QEAAPEAVCRgOperator@@XZ", error)); if (!m_operatorFn) return false;
            m_create = reinterpret_cast<CreateObject>(Symbol("?CreateObject@CRgOperator@@QEAAPEAVCRgObject@@W4OpType@@PEB_WHH@Z", error)); if (!m_create) return false;
            m_find = reinterpret_cast<FindById>(Symbol("?FindByID@CRgComplexObject@@UEAAPEAVCRgObject@@J@Z", error)); if (!m_find) return false;
            m_remove = reinterpret_cast<RemoveObject>(Symbol("?RemoveObject@CRgComplexObject@@QEAAXPEAVCRgObject@@@Z", error)); return m_remove != NULL;
        }
        HMODULE m_module; void* m_graph; void* m_root; std::vector<unsigned char> m_storage;
        GraphCtor m_ctor; GraphDtor m_dtor; GraphRead m_read; GraphWrite m_write; GraphInitialize m_initialize; GraphRoot m_rootFn; GraphOperator m_operatorFn; CreateObject m_create; FindById m_find; RemoveObject m_remove;
    };
};

} // namespace graph
} // namespace hello_mcp
