#include "stdafx.h"
#include "HelloMCPDebug.h"
#include "VsInterface.h"
#include "VsApp.h"

#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <fstream>
#include <sstream>
#include <vector>

namespace HelloMCPDebug {

// ══════════════════════════════════════════════════════════════════════
// Internal helpers
// ══════════════════════════════════════════════════════════════════════

static std::string WcharToUtf8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, nullptr, nullptr);
    return result;
}

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
    return result;
}

static std::string ReadBinaryFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return "";
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string data(static_cast<size_t>(size), '\0');
    if (!file.read(&data[0], size)) return "";
    return data;
}

static bool WriteTextFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << content;
    return true;
}

/// Kill a process by image name (case-insensitive).
static bool KillProcess(const wchar_t* imageName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = { sizeof(pe) };
    bool any = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, imageName) == 0) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) {
                    TerminateProcess(h, 0);
                    CloseHandle(h);
                    any = true;
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return any;
}

/// Find .peproj from PostEngineer window title.
static std::string FindProjectFile() {
    struct Ctx { std::string result; };
    Ctx ctx;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        wchar_t title[512];
        if (!GetWindowTextW(h, title, 512)) return TRUE;
        std::wstring wt(title);
        size_t pos = wt.find(L"PostEngineer - ");
        if (pos == std::wstring::npos) return TRUE;
        std::wstring part = wt.substr(pos + 16);
        // Trim trailing spaces / ellipsis
        while (!part.empty() && (part.back() == L' ' || part.back() == L'.'))
            part.pop_back();
        c->result = WcharToUtf8(part.c_str());
        return FALSE;  // stop enumeration
    }, reinterpret_cast<LPARAM>(&ctx));

    if (!ctx.result.empty()) return ctx.result;

    // Fallback: project.ini
    std::ifstream ini("project.ini");
    if (ini.is_open()) {
        std::string line;
        while (std::getline(ini, line)) {
            if (line.compare(0, 8, "project=") == 0) {
                std::string val = line.substr(8);
                while (!val.empty() && (val.back() == '\r' || val.back() == '\n'))
                    val.pop_back();
                return val;
            }
        }
    }
    return "";
}

static std::string PeoFromProj(const std::string& projPath) {
    if (projPath.empty()) return "";
    size_t dot = projPath.rfind(".peproj");
    if (dot != std::string::npos)
        return projPath.substr(0, dot) + ".peo";
    return projPath + ".peo";
}

/// Get the Release-x64 directory (parent of Plugins/).
static std::string GetBinDir() {
    HMODULE hMod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GetBinDir), &hMod);

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(hMod, path, MAX_PATH);
    std::wstring ws(path);
    // Navigate up from Plugins\PEHelloMCP.plu to Release-x64
    size_t pos = ws.rfind(L'\\');
    if (pos != std::wstring::npos) ws.resize(pos);  // remove filename
    pos = ws.rfind(L'\\');
    if (pos != std::wstring::npos) ws.resize(pos);  // remove Plugins
    return WcharToUtf8(ws.c_str()) + "\\";
}

// ══════════════════════════════════════════════════════════════════════
// PEDATA — named shared memory
// ══════════════════════════════════════════════════════════════════════

struct PedataState {
    HANDLE mapping = nullptr;
    void*  view    = nullptr;
    size_t size    = 0;

    void Release() {
        if (view)    { UnmapViewOfFile(view); view = nullptr; }
        if (mapping) { CloseHandle(mapping); mapping = nullptr; }
        size = 0;
    }
};

static PedataState g_pedata;

/// Create PEDATA named file mapping and copy |data| into it.
static bool CreatePedata(const std::string& data) {
    g_pedata.Release();

    size_t mapSize = data.size() + 4096;
    HANDLE hMap = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, static_cast<DWORD>(mapSize), L"PEDATA");

    if (!hMap) return false;

    void* v = MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, mapSize);
    if (!v) {
        CloseHandle(hMap);
        return false;
    }

    memcpy(v, data.data(), data.size());
    memset(static_cast<char*>(v) + data.size(), 0, mapSize - data.size());

    g_pedata.mapping = hMap;
    g_pedata.view    = v;
    g_pedata.size    = mapSize;
    return true;
}

// ══════════════════════════════════════════════════════════════════════
// Public functions
// ══════════════════════════════════════════════════════════════════════

// The PE toolbar owns the runtime-scene -> PEDATA serializer.  Invoke its
// command instead of attempting to manufacture PEDATA from the .peo file.
static bool IsStartDebugCaption(const std::wstring& caption) {
    return caption.find(L"\x542F\x52A8\x8C03\x8BD5") != std::wstring::npos ||
           caption.find(L"Start Debug") != std::wstring::npos ||
           caption.find(L"start debug") != std::wstring::npos;
}

static HWND FindPostEngineerWindow() {
    struct Ctx { HWND window = nullptr; DWORD pid = GetCurrentProcessId(); } ctx;
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        Ctx* ctx = reinterpret_cast<Ctx*>(value);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (pid != ctx->pid || !IsWindowVisible(window)) return TRUE;
        wchar_t title[512] = {};
        GetWindowTextW(window, title, _countof(title));
        if (wcsstr(title, L"PostEngineer") != nullptr) {
            ctx->window = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.window;
}

static UINT FindStartDebugMenuCommand(HMENU menu) {
    if (!menu) return 0;
    const int count = GetMenuItemCount(menu);
    for (int index = 0; index < count; ++index) {
        HMENU submenu = GetSubMenu(menu, index);
        if (submenu) {
            const UINT nested = FindStartDebugMenuCommand(submenu);
            if (nested) return nested;
        }
        wchar_t text[256] = {};
        GetMenuStringW(menu, index, text, _countof(text), MF_BYPOSITION);
        if (IsStartDebugCaption(text)) {
            const UINT id = GetMenuItemID(menu, index);
            if (id != static_cast<UINT>(-1)) return id;
        }
    }
    return 0;
}

static bool IsToolbarWindow(HWND window) {
    wchar_t className[64] = {};
    GetClassNameW(window, className, _countof(className));
    // PE uses MFC CToolBar windows (Afx:ToolBar:...), not only the common-control class.
    return wcscmp(className, TOOLBARCLASSNAMEW) == 0 ||
           wcsncmp(className, L"Afx:ToolBar", 11) == 0;
}
static UINT FindStartDebugToolbarCommand(HWND root) {
    struct Ctx { UINT command = 0; } ctx;
    EnumChildWindows(root, [](HWND child, LPARAM value) -> BOOL {
        Ctx* ctx = reinterpret_cast<Ctx*>(value);
        if (!IsToolbarWindow(child)) return TRUE;
        const int count = static_cast<int>(SendMessageW(child, TB_BUTTONCOUNT, 0, 0));
        for (int index = 0; index < count; ++index) {
            TBBUTTON button = {};
            if (!SendMessageW(child, TB_GETBUTTON, index, reinterpret_cast<LPARAM>(&button))) continue;
            wchar_t text[256] = {};
            const LRESULT length = SendMessageW(child, TB_GETBUTTONTEXTW,
                button.idCommand, reinterpret_cast<LPARAM>(text));
            if (length >= 0 && IsStartDebugCaption(text)) {
                ctx->command = static_cast<UINT>(button.idCommand);
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.command;
}

static UINT FindStartDebugTooltipCommand(HWND root) {
    struct Ctx { UINT command = 0; } ctx;
    EnumChildWindows(root, [](HWND child, LPARAM value) -> BOOL {
        Ctx* ctx = reinterpret_cast<Ctx*>(value);
        if (!IsToolbarWindow(child)) return TRUE;
        HWND tooltips = reinterpret_cast<HWND>(SendMessageW(child, TB_GETTOOLTIPS, 0, 0));
        if (!tooltips) return TRUE;
        const int count = static_cast<int>(SendMessageW(tooltips, TTM_GETTOOLCOUNT, 0, 0));
        for (int index = 0; index < count; ++index) {
            wchar_t text[256] = {};
            TOOLINFOW tool = {};
            tool.cbSize = sizeof(tool);
            tool.lpszText = text;
            if (!SendMessageW(tooltips, TTM_ENUMTOOLSW, index, reinterpret_cast<LPARAM>(&tool))) continue;
            if (IsStartDebugCaption(text) && tool.uId != 0) {
                ctx->command = static_cast<UINT>(tool.uId);
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.command;
}
static DWORD FindOwnedDebugProcess() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry = { sizeof(entry) };
    const DWORD owner = GetCurrentProcessId();
    DWORD result = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ParentProcessID == owner &&
                _wcsicmp(entry.szExeFile, L"start.exe") == 0) {
                result = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

static std::vector<DWORD> FindOwnedDebugProcessTree() {
    struct ProcessInfo { DWORD pid; DWORD parent; };
    std::vector<ProcessInfo> starts;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return std::vector<DWORD>();
    PROCESSENTRY32W entry = { sizeof(entry) };
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"start.exe") == 0)
                starts.push_back({ entry.th32ProcessID, entry.th32ParentProcessID });
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    const DWORD owner = GetCurrentProcessId();
    std::vector<DWORD> result;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const ProcessInfo& info : starts) {
            bool parentOwned = info.parent == owner;
            for (DWORD known : result) if (info.parent == known) { parentOwned = true; break; }
            bool alreadyKnown = false;
            for (DWORD known : result) if (info.pid == known) { alreadyKnown = true; break; }
            if (parentOwned && !alreadyKnown) { result.push_back(info.pid); changed = true; }
        }
    }
    return result;
}

static void AppendNewOwnedDebugDescendants(std::vector<DWORD>& pids) {
    struct ProcessInfo { DWORD pid; DWORD parent; };
    std::vector<ProcessInfo> starts;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W entry = { sizeof(entry) };
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"start.exe") == 0)
                starts.push_back({ entry.th32ProcessID, entry.th32ParentProcessID });
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    const DWORD owner = GetCurrentProcessId();
    bool changed = true;
    while (changed) {
        changed = false;
        for (const ProcessInfo& info : starts) {
            bool parentKnown = info.parent == owner;
            for (DWORD known : pids) if (info.parent == known) { parentKnown = true; break; }
            bool alreadyKnown = false;
            for (DWORD known : pids) if (info.pid == known) { alreadyKnown = true; break; }
            if (parentKnown && !alreadyKnown) { pids.push_back(info.pid); changed = true; }
        }
    }
}
static bool RequestProcessWindowClose(DWORD pid) {
    struct Ctx { DWORD pid; bool posted; } ctx = { pid, false };
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        Ctx* ctx = reinterpret_cast<Ctx*>(value);
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        if (owner == ctx->pid && IsWindowVisible(window)) {
            PostMessageW(window, WM_CLOSE, 0, 0);
            ctx->posted = true;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.posted;
}
static DWORD g_debugProcessId = 0;
static UINT g_capturedStartDebugCommand = 0;
static HWND g_debugCommandTarget = nullptr;
static HWND g_debugCommandSource = nullptr;
static WPARAM g_debugCommandWParam = 0;
static LPARAM g_debugCommandLParam = 0;
static HHOOK g_commandCaptureHook = nullptr;
static LRESULT CALLBACK CaptureUiCommand(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0) {
        const CWPSTRUCT* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (message && message->message == WM_COMMAND && HIWORD(message->wParam) == 0) {
            const UINT command = LOWORD(message->wParam);
            if (command != 0) {
                // Preserve the exact recipient and source captured from a real
                // toolbar click; the command ID alone is not enough for PE's route.
                g_capturedStartDebugCommand = command;
                g_debugCommandTarget = message->hwnd;
                g_debugCommandWParam = message->wParam;
                g_debugCommandLParam = message->lParam;
                HWND source = reinterpret_cast<HWND>(message->lParam);
                g_debugCommandSource = IsWindow(source) ? source : nullptr;
                WriteTextFile(GetBinDir() + "PEHelloMCP.debug-command", std::to_string(command));
                if (g_commandCaptureHook) {
                    UnhookWindowsHookEx(g_commandCaptureHook);
                    g_commandCaptureHook = nullptr;
                }
            }
        }
    }
    return CallNextHookEx(g_commandCaptureHook, code, wParam, lParam);
}

static UINT LoadCapturedStartDebugCommand() {
    std::ifstream file(GetBinDir() + "PEHelloMCP.debug-command");
    unsigned long value = 0;
    if (file >> value && value > 0 && value <= 0xFFFF) return static_cast<UINT>(value);
    return 0;
}

static bool ArmStartDebugCommandCapture(HWND mainWindow, bool force) {
    if (force) {
        g_debugCommandTarget = nullptr;
        g_debugCommandSource = nullptr;
        g_debugCommandWParam = 0;
        g_debugCommandLParam = 0;
        if (g_commandCaptureHook) {
            UnhookWindowsHookEx(g_commandCaptureHook);
            g_commandCaptureHook = nullptr;
        }
    } else {
        if (!g_capturedStartDebugCommand) g_capturedStartDebugCommand = LoadCapturedStartDebugCommand();
        if (g_capturedStartDebugCommand) return true;
        if (g_commandCaptureHook) return true;
    }
    const DWORD threadId = GetWindowThreadProcessId(mainWindow, nullptr);
    if (!threadId) return false;
    g_commandCaptureHook = SetWindowsHookExW(WH_CALLWNDPROC, CaptureUiCommand, nullptr, threadId);
    return g_commandCaptureHook != nullptr;
}

static HWND FindToolbarForCommand(HWND root, UINT command) {
    struct Ctx { UINT command; HWND toolbar; } ctx = { command, nullptr };
    EnumChildWindows(root, [](HWND child, LPARAM value) -> BOOL {
        Ctx* ctx = reinterpret_cast<Ctx*>(value);
        if (!IsToolbarWindow(child)) return TRUE;
        const int count = static_cast<int>(SendMessageW(child, TB_BUTTONCOUNT, 0, 0));
        for (int index = 0; index < count; ++index) {
            TBBUTTON button = {};
            if (SendMessageW(child, TB_GETBUTTON, index, reinterpret_cast<LPARAM>(&button)) &&
                static_cast<UINT>(button.idCommand) == ctx->command) {
                ctx->toolbar = child;
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.toolbar;
}
json BeginDebugCommandCapture(const std::string& requestId) {
    HWND mainWindow = FindPostEngineerWindow();
    if (!mainWindow) {
        return {{"success", false}, {"error", "PostEngineer main window not found"},
                {"error_code", "debug_main_window_not_found"}, {"request_id", requestId},
                {"action", "debug_calibrate"}};
    }
    const bool armed = ArmStartDebugCommandCapture(mainWindow, true);
    return {{"success", armed},
            {"message", armed ? "Click native Start Debug toolbar button once" : "Could not arm native command capture"},
            {"error_code", armed ? "" : "debug_command_capture_failed"},
            {"request_id", requestId}, {"action", "debug_calibrate"}};
}
json StartCurrentProject(const std::string& requestId) {
    // This function already runs from EngineExecutor::OnTick().  Dispatching
    // the native UI command preserves the exact toolbar implementation,
    // including its in-memory PEDATA serialization and player setup.
    HWND mainWindow = FindPostEngineerWindow();
    if (!mainWindow) {
        return {{"success", false}, {"error", "PostEngineer main window not found"},
                {"error_code", "debug_main_window_not_found"}, {"request_id", requestId},
                {"action", "debug_start"}};
    }

    if (!g_capturedStartDebugCommand) g_capturedStartDebugCommand = LoadCapturedStartDebugCommand();
    UINT command = g_capturedStartDebugCommand;
    if (!command) command = FindStartDebugMenuCommand(GetMenu(mainWindow));
    if (!command) command = FindStartDebugToolbarCommand(mainWindow);
    if (!command) command = FindStartDebugTooltipCommand(mainWindow);
    if (!command) {
        const bool armed = ArmStartDebugCommandCapture(mainWindow, false);
        return {{"success", false},
                {"error", armed ? "Click the native Start Debug toolbar button once to calibrate this PE session" : "Could not arm native Start Debug command capture"},
                {"error_code", armed ? "debug_command_capture_armed" : "debug_command_not_found"},
                {"request_id", requestId}, {"action", "debug_start"}};
    }

    HWND target = IsWindow(g_debugCommandTarget) ? g_debugCommandTarget : mainWindow;
    HWND source = IsWindow(g_debugCommandSource) ? g_debugCommandSource : FindToolbarForCommand(mainWindow, command);
    const WPARAM nativeWParam = g_debugCommandWParam ? g_debugCommandWParam : MAKEWPARAM(command, 0);
    const LPARAM nativeLParam = IsWindow(g_debugCommandSource) ? g_debugCommandLParam : reinterpret_cast<LPARAM>(source);
    // Async post avoids HTTP/GUI synchronous deadlock while preserving the
    // native recipient, command bits, and toolbar source when captured.
    if (!PostMessageW(target, WM_COMMAND, nativeWParam, nativeLParam)) {
        return {{"success", false}, {"error", "Could not post native Start Debug command"},
                {"error_code", "debug_command_post_failed"}, {"request_id", requestId},
                {"action", "debug_start"}};
    }
    return {{"success", true}, {"message", "Native Start Debug command posted"},
            {"command_id", command}, {"pid", 0},
            {"target_hwnd", reinterpret_cast<size_t>(target)},
            {"source_hwnd", reinterpret_cast<size_t>(source)},
            {"captured_route", IsWindow(g_debugCommandTarget)},
            {"request_id", requestId}, {"action", "debug_start"}};
}
json StopDebugSession(const std::string& requestId) {
    // start.exe may create a second start.exe.  Capture the complete descendant
    // tree before closing the root, then close/terminate leaves before parents.
    std::vector<DWORD> pids = FindOwnedDebugProcessTree();
    if (g_debugProcessId) {
        bool known = false;
        for (DWORD pid : pids) if (pid == g_debugProcessId) known = true;
        if (!known) pids.push_back(g_debugProcessId);
    }
    // Closing the bootstrap player can create a second start.exe during its
    // shutdown. Re-scan twice before force termination, retaining known parent
    // PIDs so descendants remain attributable after the root has exited.
    for (int pass = 0; pass < 2; ++pass) {
        AppendNewOwnedDebugDescendants(pids);
        for (DWORD pid : pids) RequestProcessWindowClose(pid);
        Sleep(600);
    }

    bool stopped = false;
    for (auto it = pids.rbegin(); it != pids.rend(); ++it) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, *it);
        if (!process) { stopped = true; continue; }
        if (WaitForSingleObject(process, 500) == WAIT_OBJECT_0) {
            stopped = true;
        } else if (TerminateProcess(process, 0) != FALSE) {
            stopped = true;
        }
        CloseHandle(process);
    }
    // One final scan catches a child spawned by a root process while it was
    // being terminated; only descendants of the already-owned PID set qualify.
    const size_t knownCount = pids.size();
    AppendNewOwnedDebugDescendants(pids);
    for (size_t index = pids.size(); index > knownCount; --index) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pids[index - 1]);
        if (process) { stopped = TerminateProcess(process, 0) != FALSE || stopped; CloseHandle(process); }
    }
    g_debugProcessId = 0;
    g_pedata.Release();
    return {
        {"success", true}, {"message", stopped ? "stopped" : "no session found"},
        {"process_killed", stopped}, {"process_count", pids.size()},
        {"request_id", requestId}, {"action", "debug_stop"}
    };
}

}  // namespace HelloMCPDebug