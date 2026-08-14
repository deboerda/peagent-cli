#pragma once

#include "HelloMCPCommon.h"
#include "Plugin.h"
#include "VsModule.h"

namespace hello_mcp { namespace detail {

// ── GetCurrentView helper ─────────────────────────────────
// Duplicated from HelloMCPService.cpp anonymous namespace.
// Gets the active CVsView* from the PE engine plugin manager.

inline CVsView* GetCurrentViewForScript() {
    CVsModule* mainModule = reinterpret_cast<CVsModule*>(FxPluginMgr_GetData(NULL, "Ptr_MainVsModule"));
    return mainModule ? mainModule->VsView() : nullptr;
}

// ── ScriptExecuteService ──────────────────────────────────
// Executes PE scripts via CVsView::ReadScriptFromString / ReadScriptFromFile.
// CVsView methods return int (non-zero = success).

class ScriptExecuteService {
public:
    // ── helper ────────────────────────────────────────────

    json make_result(bool success, const std::string& action,
                     const std::string& message = "") {
        json r;
        r["success"] = success;
        r["action"] = action;
        if (!message.empty()) r["message"] = message;
        return r;
    }

    // ── execute inline script ─────────────────────────────

    json ProcessScriptExecute(const std::string& action, const json& payload) {
        if (!payload.contains("script") || !payload["script"].is_string()) {
            json response = make_result(false, action, "missing 'script' field");
            response["error"] = "request must contain 'script' string field";
            response["error_code"] = "missing_script";
            return response;
        }

        std::string script = payload["script"].get<std::string>();
        if (script.empty()) {
            json response = make_result(false, action, "empty script");
            response["error"] = "script string is empty";
            response["error_code"] = "empty_script";
            return response;
        }

        CVsView* view = GetCurrentViewForScript();
        if (!view) {
            json response = make_result(false, action, "no active view");
            response["error"] = "PE engine has no active CVsView";
            response["error_code"] = "no_view";
            return response;
        }

        CVsModule* mainModule = reinterpret_cast<CVsModule*>(FxPluginMgr_GetData(NULL, "Ptr_MainVsModule"));
        const std::string executionMode = payload.value("execution_mode", "load");
        std::string scriptName = payload.value("script_name", "inline_script");

        // ReadScriptFromString loads/registers a PE project script.  It does not
        // run Program{} statements.  The explicit program mode uses the SDK's
        // ReadProgramFromString + CVsProgram::Execute runtime path instead.
        if (executionMode == "program") {
            CVsProgram* program = view->ReadProgramFromString(script.c_str(), mainModule);
            if (!program) {
                json response = make_result(false, action, "program parse failed");
                response["script_name"] = scriptName;
                response["script_length"] = static_cast<int>(script.size());
                response["execution_mode"] = executionMode;
                response["error"] = "CVsView::ReadProgramFromString returned null";
                response["error_code"] = "program_parse_failed";
                return response;
            }
            ProgramStack callingStack;
            const BOOL executed = program->Execute(callingStack);
            json response = make_result(executed != FALSE, action,
                executed != FALSE ? "program executed successfully" : "program execution failed");
            response["script_name"] = scriptName;
            response["script_length"] = static_cast<int>(script.size());
            response["execution_mode"] = executionMode;
            response["engine_return_code"] = executed != FALSE ? 1 : 0;
            if (executed == FALSE) {
                response["error"] = "CVsProgram::Execute returned FALSE";
                response["error_code"] = "program_execute_failed";
            }
            return response;
        }
        if (executionMode != "load") {
            json response = make_result(false, action, "unsupported execution mode");
            response["error"] = "execution_mode must be 'load' or 'program'";
            response["error_code"] = "invalid_execution_mode";
            return response;
        }

        int ret = view->ReadScriptFromString(script.c_str(), mainModule);
        bool ok = (ret != 0);
        json response = make_result(ok, action,
            ok ? "script loaded successfully" : "script load failed");
        response["script_name"] = scriptName;
        response["script_length"] = static_cast<int>(script.size());
        response["execution_mode"] = executionMode;
        response["engine_return_code"] = ret;
        if (!ok) {
            response["error"] = "CVsView::ReadScriptFromString returned 0";
            response["error_code"] = "engine_load_failed";
        }
        return response;
    }

    // ── execute script file ───────────────────────────────

    json ProcessScriptExecuteFile(const std::string& action, const json& payload) {
        if (!payload.contains("filename") || !payload["filename"].is_string()) {
            json response = make_result(false, action, "missing 'filename' field");
            response["error"] = "request must contain 'filename' string field";
            response["error_code"] = "missing_filename";
            return response;
        }

        std::string filename = payload["filename"].get<std::string>();
        if (filename.empty()) {
            json response = make_result(false, action, "empty filename");
            response["error"] = "filename string is empty";
            response["error_code"] = "empty_filename";
            return response;
        }

        CVsView* view = GetCurrentViewForScript();
        if (!view) {
            json response = make_result(false, action, "no active view");
            response["error"] = "PE engine has no active CVsView";
            response["error_code"] = "no_view";
            return response;
        }

        CVsModule* mainModule = reinterpret_cast<CVsModule*>(FxPluginMgr_GetData(NULL, "Ptr_MainVsModule"));
        int ret = view->ReadScriptFromFile(filename.c_str(), mainModule);

        bool ok = (ret != 0);

        json response = make_result(ok, action,
            ok ? "script file executed successfully" : "script file execution failed");
        response["filename"] = filename;
        response["engine_return_code"] = ret;

        if (!ok) {
            response["error"] = "CVsView::ReadScriptFromFile returned 0";
            response["error_code"] = "engine_execute_file_failed";
        }

        return response;
    }

    // ── validate script syntax ────────────────────────────
    // Writes script to temp file, then calls ReadScriptFromFile.
    // If it parses without error, the script is valid.

    json ProcessScriptValidate(const std::string& action, const json& payload) {
        if (!payload.contains("script") || !payload["script"].is_string()) {
            json response = make_result(false, action, "missing 'script' field");
            response["error"] = "request must contain 'script' string field";
            response["error_code"] = "missing_script";
            return response;
        }

        std::string script = payload["script"].get<std::string>();
        if (script.empty()) {
            json response = make_result(false, action, "empty script");
            response["error"] = "script string is empty";
            response["error_code"] = "empty_script";
            return response;
        }

        CVsView* view = GetCurrentViewForScript();
        if (!view) {
            json response = make_result(false, action, "no active view");
            response["error"] = "PE engine has no active CVsView";
            response["error_code"] = "no_view";
            return response;
        }

        std::string tempDir = fs::temp_directory_path().string();
        std::string tempFile = tempDir + "/_pe_script_validate_" +
            std::to_string(GetTickCount64()) + ".scr";

        {
            std::ofstream ofs(tempFile, std::ios::binary);
            if (!ofs) {
                json response = make_result(false, action, "cannot create temp file");
                response["error"] = "failed to write temp script file for validation";
                response["error_code"] = "temp_file_error";
                return response;
            }
            ofs.write(script.data(), script.size());
            ofs.close();
        }

        CVsModule* mainModule = reinterpret_cast<CVsModule*>(FxPluginMgr_GetData(NULL, "Ptr_MainVsModule"));
        int ret = view->ReadScriptFromFile(tempFile.c_str(), mainModule);
        std::remove(tempFile.c_str());

        bool ok = (ret != 0);

        json response = make_result(ok, action,
            ok ? "script is valid" : "script has syntax errors");
        response["script_length"] = static_cast<int>(script.size());
        response["engine_return_code"] = ret;

        if (!ok) {
            response["error"] = "CVsView::ReadScriptFromFile returned 0 during validation";
            response["error_code"] = "script_validation_failed";
        }

        return response;
    }

    // ── main router ───────────────────────────────────────

    json ProcessScriptAction(const std::string& action, const json& payload) {
        if (action == "script_execute")
            return ProcessScriptExecute(action, payload);
        if (action == "script_execute_file")
            return ProcessScriptExecuteFile(action, payload);
        if (action == "script_validate")
            return ProcessScriptValidate(action, payload);

        json response = make_result(false, action, "unknown script action");
        response["error"] = "Unknown script action: " + action;
        return response;
    }
};

} } // namespace hello_mcp::detail
