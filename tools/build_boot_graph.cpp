// Programmatically construct a minimal behavior graph (boot script + include.script)
// using RelaGraph.dll's internal API, serialized with the engine's own WriteToBuffer so the
// on-disk format is guaranteed correct.
//
// Note: CreateByType/SetText/LinkFrom are NOT exported (no __declspec(dllexport)), so we
// resolve them by absolute address (module base + RVA), the same technique used in
// HelloMCPGraph.h for ProjectMgr's CreateUIObjByID. The two RVA tables below are
// build-specific and are selected from the exact DLL basename.
//
// Exported entry points (ctor/dtor/Initialize/Graph/WriteToBuffer/AddObject) are resolved
// by name via GetProcAddress because they ARE in the export table.
#include <windows.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include "BufferIO.h"

constexpr size_t kRelationGraphSize = 4144;

enum RgType {
    RG_TYPE_CURVE   = 2,
    RG_TYPE_PROGRAM = 6,
    RG_TYPE_HUB    = 4,
    RG_TYPE_BOOT    = 11,
    RG_TYPE_SCRIPT  = 14,  // CRgScrDef - registers a .script include file
    RG_TYPE_BUTTON  = 19
};

// OpType is the palette/operator enum, distinct from RgType.  The toolbar
// item shown as a circle with an X is OP_TYPE_HUB=6.
constexpr int OP_TYPE_HUB = 6;

struct RelaGraphRvas {
    const char* variant;
    uintptr_t createByType;
    uintptr_t setText;
    uintptr_t linkFrom;
    uintptr_t translate;
    uintptr_t alignToGrid;
};

static bool IsNamed(const char* path, const char* name) {
    const char* slash = strrchr(path, '\\');
    slash = slash ? slash + 1 : path;
    return _stricmp(slash, name) == 0;
}

static bool SelectRvas(const char* path, RelaGraphRvas& out) {
    // Debug RelaGraphd.dll: matches the PDB-backed adapter used by the original
    // development build.
    if (IsNamed(path, "RelaGraphd.dll")) {
        out = {"Debug", 0x1429, 0x29e6, 0x1b36, 0, 0};
        return true;
    }
    // Release RelaGraph.dll: RVAs recovered from the matching Release export/
    // class layout dump. Do not silently apply these to an unknown binary.
    if (IsNamed(path, "RelaGraph.dll")) {
        // Current 2026-07-30 Release binary: CreateByType is the internal
        // all-RgType factory at RVA 0x2BA40 (the public CreateObject export
        // intentionally handles only the interactive operator subset).
        // Release CreateObject's post-construction calls in the analyzed
        // RelaGraph.dll are Translate/AlignToGrid at 0x29A90/0x29B70.
        // CRgObject::LinkFrom in the matching Release image.  It links the
        // animation/program chain below the boot object; it is not the same
        // as CRgComplexObject::AddObject (which is only for root children).
        out = {"Release", 0x2ba40, 0x299e0, 0x83f0, 0x29a90, 0x29b70};
        return true;
    }
    return false;
}

// ---- Non-exported internal functions, resolved by RVA from RelaGraph.pdb ----
// RVA 0x1429:  CRgObject::CreateByType(RgType, CRelationGraph*) __cdecl
// RVA 0x29e6:  CRgObject::SetText(wchar_t*) __thiscall
// RVA 0x1b36:  CRgObject::LinkFrom(CRgObject*) __thiscall   (just sets m_pPrev/m_pNext)
using CreateByTypeFn = void*(__fastcall*)(int, void*);
using SetTextFn       = void (__fastcall*)(void*, const wchar_t*);
using LinkFromFn      = void (__fastcall*)(void*, void*);
using TranslateFn     = void (__fastcall*)(void*, float, float);
using AlignToGridFn   = void (__fastcall*)(void*);
using AllocateFn       = void* (__fastcall*)(size_t);

// ---- Exported entry points, resolved by name ----
using Ctor          = void*(__fastcall*)(void*, const char*);
using Dtor           = void (__fastcall*)(void*);
using WriteToBuffer  = int  (__fastcall*)(void*, CBufferIO&);
using Initialize     = int  (__fastcall*)(void*, HWND*);
using GraphFn        = void*(__fastcall*)(void*);
using AddObjectFn   = void (__fastcall*)(void*, void*);
using OperatorFn    = void*(__fastcall*)(void*);
using CreateObjectFn = void*(__fastcall*)(void*, int, const wchar_t*, int, int);
using BuildFromIdFn  = int  (__fastcall*)(void*);

// CRgObject layout is confirmed by the matching RelaGraph PDB:
//   +0x0c m_ID, +0x80 m_prevID, +0x84 m_nextID.
// LinkFrom/LinkTo update the in-memory pointers, but the graph writer emits
// these numeric IDs.  Keep this small layout bridge local to the adapter.
static int ObjectId(void* object) {
    return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(object) + 0x0c);
}

static void SetObjectId(void* object, int id) {
    *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(object) + 0x0c) = id;
}

static void SetPrevNextIds(void* object, int previousId, int nextId) {
    auto* bytes = reinterpret_cast<unsigned char*>(object);
    *reinterpret_cast<int*>(bytes + 0x80) = previousId;
    *reinterpret_cast<int*>(bytes + 0x84) = nextId;
}

// The palette creation path initializes the visual bounds.  The internal
// factory leaves these at zero, which renders a plain circle and hides the
// script label even though the text/path is serialized correctly.
static void SetDimensions(void* object, float width, float height) {
    auto* bytes = reinterpret_cast<unsigned char*>(object);
    *reinterpret_cast<float*>(bytes + 0x3c) = width;
    *reinterpret_cast<float*>(bytes + 0x40) = height;
}

static void SetHubLinkState(void* object, int previousId, int nextId, AllocateFn allocate) {
    // Release CRgHub keeps the endpoint IDs in two small native arrays.  The
    // raw factory allocates the arrays but leaves both counts at zero; the
    // interactive circle-X item has one previous and one next endpoint.
    auto* bytes = reinterpret_cast<unsigned char*>(object);
    auto* previous = *reinterpret_cast<unsigned char**>(bytes + 0x100);
    auto* next = *reinterpret_cast<unsigned char**>(bytes + 0x110);
    if (!previous && allocate) {
        previous = reinterpret_cast<unsigned char*>(allocate(4));
        *reinterpret_cast<unsigned char**>(bytes + 0x100) = previous;
    }
    if (!next && allocate) {
        next = reinterpret_cast<unsigned char*>(allocate(4));
        *reinterpret_cast<unsigned char**>(bytes + 0x110) = next;
    }
    if (previous) *reinterpret_cast<int*>(previous) = previousId;
    if (next) *reinterpret_cast<int*>(next) = nextId;
    *reinterpret_cast<int*>(bytes + 0x108) = previous ? 1 : 0;
    *reinterpret_cast<int*>(bytes + 0x118) = next ? 1 : 0;
    std::cerr << "hub cache prev=" << static_cast<void*>(previous)
              << " next=" << static_cast<void*>(next)
              << " ids=" << (previous ? *reinterpret_cast<int*>(previous) : 0)
              << "," << (next ? *reinterpret_cast<int*>(next) : 0)
              << " counts=" << *reinterpret_cast<int*>(bytes + 0x108)
              << "," << *reinterpret_cast<int*>(bytes + 0x118) << "\n";
}

static void SetScriptIncludeState(void* object) {
    // CRgScrDef's final field is initialized to zero by CreateByType.  A
    // dragged script palette item stores one here, which makes the path text
    // render as the script element and marks it as an include definition.
    *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(object) + 0x4f8) = 1;
}

static FARPROC require(HMODULE module, const char* name) {
    FARPROC proc = GetProcAddress(module, name);
    if (!proc) std::cerr << "missing RelaGraph export: " << name << "\n";
    return proc;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::cerr << "usage: build_boot_graph <output.rg> <RelaGraphd.dll|RelaGraph.dll path> [include.script path]\n";
        return 2;
    }
    const char* outPath = argv[1];
    const char* includePath = argc >= 4 ? argv[3] : "main\\Script\\include.script";
    HMODULE module = LoadLibraryA(argv[2]);
    if (!module) { std::cerr << "LoadLibrary failed: " << GetLastError() << "\n"; return 4; }
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    RelaGraphRvas rvas = {};
    if (!SelectRvas(argv[2], rvas)) {
        std::cerr << "unsupported RelaGraph filename; expected RelaGraphd.dll or RelaGraph.dll\n";
        FreeLibrary(module);
        return 5;
    }
    std::cout << "RelaGraph variant=" << rvas.variant << "\n";

    auto ctor          = reinterpret_cast<Ctor>(require(module, "??0CRelationGraph@@QEAA@PEBD@Z"));
    auto dtor          = reinterpret_cast<Dtor>(require(module, "??1CRelationGraph@@UEAA@XZ"));
    auto write         = reinterpret_cast<WriteToBuffer>(require(module, "?WriteToBuffer@CRelationGraph@@QEAAHAEAVCBufferIO@@@Z"));
    auto initialize    = reinterpret_cast<Initialize>(require(module, "?Initialize@CRelationGraph@@QEAAHPEAUHWND__@@@Z"));
    auto graphFn       = reinterpret_cast<GraphFn>(require(module, "?Graph@CRelationGraph@@QEAAPEAVCRgComplexObject@@XZ"));
    auto addObject     = reinterpret_cast<AddObjectFn>(require(module, "?AddObject@CRgComplexObject@@QEAAXPEAVCRgObject@@@Z"));
    auto operatorFn    = reinterpret_cast<OperatorFn>(require(module, "?Operator@CRelationGraph@@QEAAPEAVCRgOperator@@XZ"));
    auto createObject  = reinterpret_cast<CreateObjectFn>(require(module, "?CreateObject@CRgOperator@@QEAAPEAVCRgObject@@W4OpType@@PEB_WHH@Z"));
    auto buildFromId   = reinterpret_cast<BuildFromIdFn>(require(module, "?BuildFromID@CRelationGraph@@QEAAHXZ"));
    if (!ctor || !dtor || !write || !initialize || !graphFn || !addObject || !operatorFn || !createObject || !buildFromId) { FreeLibrary(module); return 6; }

    auto createByType = reinterpret_cast<CreateByTypeFn>(base + rvas.createByType);
    auto setText      = reinterpret_cast<SetTextFn>(base + rvas.setText);
    auto linkFrom     = reinterpret_cast<LinkFromFn>(base + rvas.linkFrom);
    auto translate     = rvas.translate ? reinterpret_cast<TranslateFn>(base + rvas.translate) : nullptr;
    auto alignToGrid   = rvas.alignToGrid ? reinterpret_cast<AlignToGridFn>(base + rvas.alignToGrid) : nullptr;
    // Release's array allocator used by CRgComplexObject copy/read paths.
    auto allocate       = reinterpret_cast<AllocateFn>(base + 0x383e0);

    alignas(16) unsigned char graphMem[kRelationGraphSize] = {};
    void* graph = ctor(graphMem, outPath);

    HWND host = nullptr;
    int initResult = initialize(graph, &host);
    std::cout << "Initialize -> " << initResult << "\n";

    void* root = graphFn(graph);
    if (!root) { std::cerr << "Graph() returned NULL root\n"; dtor(graph); return 7; }

    // boot node carries the executable script directly (like secondtry one.rg's
    // pluginCall pattern). No separate program node, no link.
    const wchar_t* bootText = L"outputMessage(\"hello world from agent script\");";
    int includeLength = MultiByteToWideChar(CP_UTF8, 0, includePath, -1, nullptr, 0);
    std::vector<wchar_t> includeWide(static_cast<size_t>(includeLength > 0 ? includeLength : 1));
    if (includeLength > 0) {
        MultiByteToWideChar(CP_UTF8, 0, includePath, -1, includeWide.data(), includeLength);
    } else {
        includeWide[0] = L'\0';
    }

    void* bootNode = nullptr;
    void* hubNode = nullptr;
    void* curveStart = nullptr;
    void* programNode = nullptr;
    void* scriptNode = nullptr;
    void* buttonNode = nullptr;
    void* curveEnd = nullptr;
    std::cerr << "creating nodes variant=" << rvas.variant << "\n";
    if (_stricmp(rvas.variant, "Release") == 0) {
        // The Release public operator factory only accepts curve/step/condition
        // types. Boot/script use the internal all-RgType factory instead.
        bootNode = createByType(RG_TYPE_BOOT, graph);
        std::cerr << "boot factory returned " << bootNode << "\n";
        // Use the same operator path as dragging the circle-with-X palette
        // item.  Direct CreateByType(RG_TYPE_HUB) creates the plain empty
        // circle seen in the first diagnostic screenshot and is not the
        // executable hub element.
        void* op = operatorFn(graph);
        hubNode = op ? createObject(op, OP_TYPE_HUB, L"", 300, 120) : nullptr;
        if (!hubNode) {
            std::cerr << "operator hub creation failed; falling back to raw hub\n";
            hubNode = createByType(RG_TYPE_HUB, graph);
        }
        std::cerr << "hub factory returned " << hubNode << "\n";
        curveStart = createByType(RG_TYPE_CURVE, graph);
        std::cerr << "start curve factory returned " << curveStart << "\n";
        programNode = createByType(RG_TYPE_PROGRAM, graph);
        std::cerr << "program factory returned " << programNode << "\n";
        scriptNode = createByType(RG_TYPE_SCRIPT, graph);
        std::cerr << "script factory returned " << scriptNode << "\n";
        buttonNode = createByType(RG_TYPE_BUTTON, graph);
        std::cerr << "button factory returned " << buttonNode << "\n";
        curveEnd = createByType(RG_TYPE_CURVE, graph);
        std::cerr << "end curve factory returned " << curveEnd << "\n";
    } else {
        bootNode = createByType(RG_TYPE_BOOT, graph);
        hubNode = createByType(RG_TYPE_HUB, graph);
        curveStart = createByType(RG_TYPE_CURVE, graph);
        programNode = createByType(RG_TYPE_PROGRAM, graph);
        scriptNode = createByType(RG_TYPE_SCRIPT, graph);
        buttonNode = createByType(RG_TYPE_BUTTON, graph);
        curveEnd = createByType(RG_TYPE_CURVE, graph);
    }
    std::cout << "boot=" << bootNode << " hub=" << hubNode
              << " curveStart=" << curveStart << " program=" << programNode
              << " script=" << scriptNode << " button=" << buttonNode
              << " curveEnd=" << curveEnd << "\n";
    if (!bootNode || !hubNode || !curveStart || !programNode || !scriptNode || !buttonNode || !curveEnd) {
        dtor(graph); FreeLibrary(module); return 9;
    }

    // Match the manually verified model.rg structure:
    //   root -> boot -> hub -> program(helloworld();) ; root -> script include
    setText(bootNode, L"boot");
    setText(programNode, L"helloworld();");
    setText(scriptNode, includeWide.data());
    SetDimensions(hubNode, 0.0f, 30.375f);
    SetDimensions(curveStart, 0.0f, 30.375f);
    SetDimensions(scriptNode, 151.875f, 30.375f);
    SetScriptIncludeState(scriptNode);
    SetDimensions(programNode, 114.069f, 31.875f);
    SetDimensions(curveEnd, 0.0f, 30.375f);

    // The PE editor's CreateObject path translates every new object from the
    // default origin before serializing it. Without this, Release RelaGraph
    // accepts the objects but the editor canvas draws them outside the visible
    // workspace at the zero matrix origin.
    if (translate) {
        translate(bootNode, 120.0f, 120.0f);
        translate(hubNode, 300.0f, 120.0f);
        translate(curveStart, 210.0f, 120.0f);
        translate(programNode, 420.0f, 120.0f);
        // Keep the script palette item inside the visible canvas; y=20 is
        // hidden behind PE's palette toolbar when the graph is opened.
        translate(scriptNode, 420.0f, 220.0f);
        translate(buttonNode, 420.0f, 360.0f);
        translate(curveEnd, 360.0f, 120.0f);
        if (alignToGrid) {
            alignToGrid(bootNode);
            alignToGrid(hubNode);
            alignToGrid(curveStart);
            alignToGrid(programNode);
            alignToGrid(scriptNode);
            alignToGrid(buttonNode);
            alignToGrid(curveEnd);
        }
    }

    if (linkFrom) {
        std::cerr << "linking program from hub\n";
        linkFrom(programNode, hubNode);
    }
    std::cerr << "adding boot\n";
    addObject(root, bootNode);
    std::cerr << "adding hub\n";
    addObject(root, hubNode);
    std::cerr << "adding start curve\n";
    addObject(root, curveStart);
    std::cerr << "adding script\n";
    addObject(root, scriptNode);
    std::cerr << "adding program\n";
    addObject(root, programNode);
    std::cerr << "adding button\n";
    addObject(root, buttonNode);
    std::cerr << "adding end curve\n";
    addObject(root, curveEnd);
    std::cerr << "objects added\n";

    // LinkFrom establishes m_pPrev/m_pNext only.  The on-disk graph stores
    // m_prevID/m_nextID instead, so mirror the same chain in the confirmed
    // CRgObject fields and let CRelationGraph rebuild pointer links from IDs.
    // Keep the same stable IDs as the manually saved MVP.  ID 5 belongs to
    // an internal hub-side allocation and is intentionally not a root item.
    SetObjectId(bootNode, 2);
    SetObjectId(hubNode, 3);
    SetObjectId(curveStart, 4);
    SetObjectId(scriptNode, 6);
    SetObjectId(programNode, 7);
    SetObjectId(buttonNode, 8);
    SetObjectId(curveEnd, 9);
    const int bootId = ObjectId(bootNode);
    const int hubId = ObjectId(hubNode);
    const int curveStartId = ObjectId(curveStart);
    const int programId = ObjectId(programNode);
    const int buttonId = ObjectId(buttonNode);
    const int curveEndId = ObjectId(curveEnd);
    std::cerr << "ids boot=" << bootId << " hub=" << hubId << " program=" << programId << "\n";
    // Match the manually saved graph: curves carry the two endpoint IDs,
    // while the executable chain remains boot -> hub -> program.
    SetPrevNextIds(bootNode, 0, hubId);
    SetPrevNextIds(hubNode, 0, programId);
    SetPrevNextIds(curveStart, bootId, hubId);
    SetPrevNextIds(programNode, hubId, 0);
    SetPrevNextIds(curveEnd, hubId, programId);
    SetPrevNextIds(buttonNode, 0, 0);
    SetPrevNextIds(scriptNode, 0, 0);
    for (void* object : {bootNode, hubNode, curveStart, programNode, scriptNode, buttonNode, curveEnd}) {
        *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(object) + 0x88) = 1; // fatherID=root
    }
    std::cerr << "ids curveStart=" << curveStartId << " button=" << buttonId
              << " curveEnd=" << curveEndId << "\n";
    SetHubLinkState(hubNode, bootId, programId, allocate);
    const int buildResult = buildFromId(graph);
    std::cerr << "BuildFromID -> " << buildResult << "\n";
    // BuildFromID allocates the Release hub endpoint arrays.  Populate them
    // after that bookkeeping pass so WriteToBuffer persists the same state as
    // the manually saved circle-X graph.
    SetHubLinkState(hubNode, bootId, programId, allocate);

    CBufferIO out;
    std::cerr << "writing graph\n";
    int writeResult = write(graph, out);
    std::cerr << "write returned " << writeResult << "\n";
    const auto bufferCount = out.GetBufferCount();
    std::cerr << "buffer count " << bufferCount << "\n";
    std::vector<unsigned char> bytes(static_cast<size_t>(bufferCount));
    std::cerr << "buffer allocated\n";
    out.GetBuffer(bytes.data());
    std::cerr << "buffer copied\n";
    std::cerr << "destroying graph\n";
    dtor(graph);
    std::cerr << "graph destroyed\n";
    FreeLibrary(module);
    std::cerr << "module freed\n";

    std::ofstream file(outPath, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    file.close();

    std::cout << "write=" << writeResult << " bytes=" << bytes.size() << " -> " << outPath << "\n";
    // RelaGraph.dll and the PE SDK ship different CRT ownership domains in
    // some Release distributions. The native graph and buffer are already
    // finalized above; terminate this short-lived adapter after closing the
    // output so an implicit CBufferIO destructor cannot cross CRT boundaries
    // and hang the MCP request.
    ExitProcess(writeResult ? 0 : 9);
    return 9;
}
