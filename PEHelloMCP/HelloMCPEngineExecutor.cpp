#include "stdafx.h"
#include "HelloMCPEngineExecutor.h"
#include "HelloMCPScript.h"
#include "HelloMCPDebug.h"
#include "HelloMCPGraph.h"


void EngineExecutor::OnTick(VsDWord t) {
	g_last_engine_tick_ms.store(static_cast<unsigned long long>(GetTickCount64()));
	ProcessEngineCommands();
}


json EngineExecutor::ProcessObjectAction(EngineAction action, const std::string& buffer) {
	try {
		json payload = json::parse(buffer);
		const std::string objType = payload.value("obj_type", std::string());
		switch (action) {
		case EngineAction::ObjectCreate:
			return ProcessObjectCreate(buffer);
		case EngineAction::ObjectQuery:
			return ProcessObjectQuery(buffer);
		case EngineAction::ObjectModify:
			return ProcessObjectModify(buffer);
		default: {
			json response = MakeDispatchResult(false, action, objType, "");
			response["error"] = std::string("object action not implemented: ") + EngineActionName(action);
			response["state"] = payload;
			return response;
		}
		}
	}
	catch (const json::exception& e) {
		json response = MakeDispatchResult(false, action, "", "");
		response["error"] = std::string("JSON error: ") + e.what();
		return response;
	}
}


json EngineExecutor::ProcessSceneAction(EngineAction action, const std::string& buffer) {
	try {
		json payload = json::parse(buffer);
		const std::string effectName = payload.value("effect_name", std::string());
		const std::string target = payload.value("target", std::string());

		switch (action) {
		case EngineAction::FrameCapture:
			return ProcessFrameCapture(EngineActionName(action), payload);
		case EngineAction::EffectQuery:
			return ProcessSceneEffectQuery(EngineActionName(action), effectName, target);
		case EngineAction::EffectApply:
			return ProcessSceneEffectApply(EngineActionName(action), effectName, target, payload);
		default: {
			json response = MakeDispatchResult(false, action, effectName, target);
			response["error"] = std::string("scene action not implemented: ") + EngineActionName(action);
			response["state"] = payload;
			return response;
		}
		}
	}
	catch (const json::exception& e) {
		json response = MakeDispatchResult(false, action, "", "");
		response["error"] = std::string("PE JSON error: ") + e.what();
		return response;
	}
}


json EngineExecutor::ProcessOutputAction(EngineAction action, const std::string& buffer) {
	if (action == EngineAction::OutputQuery) {
		return ProcessOutputQuery(buffer);
	}

	return {
		{"success", false},
		{"available", false},
		{"peplayer_running", false},
		{"line_count", 0},
		{"returned_count", 0},
		{"lines", json::array()},
		{"text", ""},
		{"error", std::string("output action not implemented: ") + EngineActionName(action)}
	};
}


json EngineExecutor::MakeDispatchResult(bool success, EngineAction action,
	const std::string& subject, const std::string& target) {
	return {
		{"success", success},
		{"action", EngineActionName(action)},
		{"effect_name", subject},
		{"target", target},
		{"state", nullptr}
	};
}


void EngineExecutor::ProcessEngineCommands() {
	while (true) {
		std::shared_ptr<EngineCommand> cmd;
		{
			std::lock_guard<std::mutex> lock(g_cmd_mutex);
			if (g_cmd_queue.empty()) {
				break;
			}
			cmd = g_cmd_queue.front();
			g_cmd_queue.pop();
		}

		if (cmd->cancelled.load()) {
			const unsigned long long startedAtMs = EngineMonotonicMs();
			json response = FinalizeEngineCommand(cmd, {
				{"success", false},
				{"error", "engine command cancelled"},
				{"error_code", "engine_command_cancelled"}
			}, startedAtMs);
			cmd->TrySetResult(response);
			continue;
		}

		const unsigned long long startedAtMs = EngineMonotonicMs();
		json result;
		try {
			switch (cmd->action) {
			case EngineAction::ObjectCreate:
			case EngineAction::ObjectQuery:
			case EngineAction::ObjectModify:
				result = ProcessObjectAction(cmd->action, cmd->payload);
				break;
			case EngineAction::OutputQuery:
				result = ProcessOutputAction(cmd->action, cmd->payload);
				break;
			case EngineAction::EffectQuery:
			case EngineAction::EffectApply:
			case EngineAction::FrameCapture:
			case EngineAction::LightProbeCreate:
			case EngineAction::LightProbeQuery:
			case EngineAction::LightProbeModify:
			case EngineAction::LightProbeDelete:
			case EngineAction::LightProbeRender:
				result = ProcessSceneAction(cmd->action, cmd->payload);
				break;            case EngineAction::ScriptExecute:
            case EngineAction::ScriptExecuteFile:
            case EngineAction::ScriptValidate: {
                hello_mcp::detail::ScriptExecuteService service;
                result = service.ProcessScriptAction(EngineActionName(cmd->action), json::parse(cmd->payload));
                break;
            }
            case EngineAction::GraphCreate:
            case EngineAction::GraphRead:
            case EngineAction::GraphNodeCreate:
            case EngineAction::GraphNodeDelete:
            case EngineAction::GraphRegister:
            case EngineAction::GraphSave:
            case EngineAction::GraphCreateNative:
            case EngineAction::GraphBuildBoot:
            case EngineAction::GraphRemove: {
                hello_mcp::graph::NativeGraphService service;
                result = service.Process(EngineActionName(cmd->action), json::parse(cmd->payload));
                break;
            }
            case EngineAction::DebugStart:
                result = HelloMCPDebug::StartCurrentProject(cmd->request_id);
                break;
            case EngineAction::DebugStop:
                result = HelloMCPDebug::StopDebugSession(cmd->request_id);
                break;
			default:
				result = {
					{"success", false},
					{"error", std::string("unknown action: ") + EngineActionName(cmd->action)},
					{"error_code", "unknown_action"}
				};
				break;
			}
		}
		catch (const std::exception& e) {
			result = {
				{"success", false},
				{"error", std::string("engine command exception: ") + e.what()},
				{"error_code", "engine_exception"}
			};
		}
		catch (...) {
			result = {
				{"success", false},
				{"error", "unknown engine command exception"},
				{"error_code", "engine_exception"}
			};
		}

		result = FinalizeEngineCommand(cmd, result, startedAtMs);
		cmd->TrySetResult(result);
	}
}
