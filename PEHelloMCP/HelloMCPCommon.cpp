#include "stdafx.h"
#include "HelloMCPCommon.h"


json g_pe_scene_cache = {
	{"probefactor", json{{"factor", 1.0f}}},
	{"airparticledensity", json{{"density", 0.0f}}}
};

std::queue<std::shared_ptr<EngineCommand>> g_cmd_queue;
std::mutex g_cmd_mutex;
std::atomic<bool> g_mcp_server_running{ false };
std::atomic<bool> g_engine_view_attached{ false };
std::atomic<bool> g_engine_accepting_commands{ false };
std::atomic<unsigned long long> g_last_engine_tick_ms{ 0 };
std::atomic<unsigned long long> g_engine_service_started_ms{ 0 };

namespace {
	const size_t kMaxPendingEngineCommands = 64;
	std::atomic<unsigned long long> g_request_sequence{ 0 };
	std::atomic<unsigned long long> g_commands_accepted{ 0 };
	std::atomic<unsigned long long> g_commands_completed{ 0 };
	std::atomic<unsigned long long> g_commands_failed{ 0 };
	std::atomic<unsigned long long> g_commands_timed_out{ 0 };
	std::atomic<unsigned long long> g_commands_rejected{ 0 };
	std::atomic<unsigned long long> g_commands_cancelled{ 0 };
	std::atomic<unsigned long long> g_late_results{ 0 };
	std::mutex g_telemetry_mutex;
	std::string g_last_request_id;
	std::string g_last_action;
	std::string g_last_error_code;
	unsigned long long g_last_queue_wait_ms = 0;
	unsigned long long g_last_execution_ms = 0;
	unsigned long long g_last_total_ms = 0;
	bool g_has_last_command = false;
	bool g_last_success = false;

	json MakeTransportError(EngineAction action, const std::string& requestId,
		const char* errorCode, const char* error, bool rejected) {
		if (rejected) {
			g_commands_rejected.fetch_add(1);
		}
		return {
			{"success", false},
			{"error", error},
			{"error_code", errorCode},
			{"request_id", requestId},
			{"action", EngineActionName(action)},
			{"retryable", strcmp(errorCode, "engine_timeout") != 0}
		};
	}
}


const char* EngineActionName(EngineAction action) {
	switch (action) {
	case EngineAction::ObjectCreate: return "obj_create";
	case EngineAction::ObjectQuery: return "obj_query";
	case EngineAction::ObjectModify: return "obj_modify";
	case EngineAction::OutputQuery: return "output_query";
	case EngineAction::EffectQuery: return "effect_query";
	case EngineAction::EffectApply: return "effect_apply";
	case EngineAction::FrameCapture: return "frame_capture";
	case EngineAction::LightProbeCreate: return "pe_light_probe_create";
	case EngineAction::LightProbeQuery: return "pe_light_probe_query";
	case EngineAction::LightProbeModify: return "pe_light_probe_modify";
	case EngineAction::LightProbeDelete: return "pe_light_probe_delete";
	case EngineAction::LightProbeRender: return "pe_light_probe_render";
	case EngineAction::ScriptExecute: return "script_execute";
	case EngineAction::ScriptExecuteFile: return "script_execute_file";
	case EngineAction::ScriptValidate: return "script_validate";
	case EngineAction::GraphCreate: return "graph_create";
	case EngineAction::GraphRead: return "graph_read";
	case EngineAction::GraphNodeCreate: return "graph_node_create";
	case EngineAction::GraphNodeDelete: return "graph_node_delete";
	case EngineAction::GraphSave: return "graph_save";
	case EngineAction::GraphRegister: return "graph_register";
	case EngineAction::GraphRemove: return "graph_remove";
	case EngineAction::GraphCreateNative: return "graph_create_native";
	case EngineAction::GraphBuildBoot: return "graph_build_boot";
	case EngineAction::DebugStart: return "debug_start";
	case EngineAction::DebugStop: return "debug_stop";
	default: return "unknown";
	}
}


std::string GbkToUtf8(const std::string& gbkStr) {
	if (gbkStr.empty()) return "";

	int unicodeLen = MultiByteToWideChar(CP_ACP, 0, gbkStr.c_str(), (int)gbkStr.size(), nullptr, 0);
	if (unicodeLen == 0) return "";

	std::vector<wchar_t> wideBuf(unicodeLen);
	MultiByteToWideChar(CP_ACP, 0, gbkStr.c_str(), (int)gbkStr.size(), wideBuf.data(), unicodeLen);

	int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wideBuf.data(), unicodeLen, nullptr, 0, nullptr, nullptr);
	if (utf8Len == 0) return "";

	std::vector<char> utf8Buf(utf8Len);
	WideCharToMultiByte(CP_UTF8, 0, wideBuf.data(), unicodeLen, utf8Buf.data(), utf8Len, nullptr, nullptr);
	return std::string(utf8Buf.data(), utf8Len);
}


std::string Utf8ToGbk(const std::string& utf8Str) {
	if (utf8Str.empty()) return "";

	int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), (int)utf8Str.size(), nullptr, 0);
	std::wstring wstr(wlen, 0);
	MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), (int)utf8Str.size(), &wstr[0], wlen);

	int glen = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wlen, nullptr, 0, nullptr, nullptr);
	std::string gbk(glen, 0);
	WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wlen, &gbk[0], glen, nullptr, nullptr);
	return gbk;
}


json make_result(bool success, const std::string& objType,
	const std::string& name, const std::string& message) {
	json result;
	result["success"] = success;
	result["obj_type"] = objType;
	result["name"] = name;
	result["message"] = message;
	return result;
}


std::string ReadHttpRequest(struct mg_connection *conn) {
	std::string buffer;
	char buf[4096];
	int dataLength;
	while ((dataLength = mg_read(conn, buf, sizeof(buf))) > 0) {
		buffer.append(buf, dataLength);
	}
	return buffer;
}


void SendJsonResponse(struct mg_connection *conn, const json& response) {
	std::string body = response.dump();
	const std::string requestId = response.is_object()
		? response.value("request_id", std::string()) : std::string();
	mg_printf(conn, "HTTP/1.1 200 OK\r\n"
		"Content-Type: application/json\r\n"
		"X-Request-ID: %s\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n\r\n",
		requestId.c_str(),
		(int)body.size());
	mg_printf(conn, "%s", body.c_str());
}


unsigned long long EngineMonotonicMs() {
	return static_cast<unsigned long long>(GetTickCount64());
}


std::string GetOrCreateRequestId(struct mg_connection *conn) {
	const char* header = conn ? mg_get_header(conn, "X-Request-ID") : nullptr;
	std::string requestId = header ? header : "";
	if (requestId.size() > 128) {
		requestId.resize(128);
	}
	for (char& character : requestId) {
		const unsigned char value = static_cast<unsigned char>(character);
		if (!std::isalnum(value) && character != '-' && character != '_' && character != '.') {
			character = '_';
		}
	}
	if (!requestId.empty()) {
		return requestId;
	}
	return "pe-" + std::to_string(EngineMonotonicMs()) + "-" +
		std::to_string(g_request_sequence.fetch_add(1) + 1);
}


json EnqueueAndWait(EngineAction action, const std::string& payload,
	const std::string& requestId) {
	if (!g_engine_accepting_commands.load()) {
		return MakeTransportError(action, requestId, "engine_not_ready",
			"engine view unavailable", true);
	}

	auto cmd = std::make_shared<EngineCommand>();
	cmd->action = action;
	cmd->payload = payload;
	cmd->request_id = requestId;
	cmd->created_at_ms = EngineMonotonicMs();
	auto future = cmd->result_promise.get_future();

	{
		std::lock_guard<std::mutex> lock(g_cmd_mutex);
		if (!g_engine_accepting_commands.load()) {
			return MakeTransportError(action, requestId, "engine_not_ready",
				"engine view unavailable", true);
		}
		if (g_cmd_queue.size() >= kMaxPendingEngineCommands) {
			return MakeTransportError(action, requestId, "engine_queue_full",
				"engine command queue is full", true);
		}
		cmd->enqueued_at_ms = EngineMonotonicMs();
		g_cmd_queue.push(cmd);
		g_commands_accepted.fetch_add(1);
	}

	auto status = future.wait_for(std::chrono::seconds(30));
	if (status == std::future_status::timeout) {
		cmd->cancelled.store(true);
		cmd->client_timed_out.store(true);
		g_commands_timed_out.fetch_add(1);
		json response = MakeTransportError(action, requestId, "engine_timeout",
			"engine timeout 30s; command outcome is unknown", false);
		response["timing_ms"] = {
			{"total", EngineMonotonicMs() - cmd->created_at_ms}
		};
		return response;
	}
	return future.get();
}


json FinalizeEngineCommand(const std::shared_ptr<EngineCommand>& command,
	json response, unsigned long long startedAtMs) {
	const unsigned long long completedAtMs = EngineMonotonicMs();
	const unsigned long long queueWaitMs = startedAtMs >= command->enqueued_at_ms
		? startedAtMs - command->enqueued_at_ms : 0;
	const unsigned long long executionMs = completedAtMs >= startedAtMs
		? completedAtMs - startedAtMs : 0;
	const unsigned long long totalMs = completedAtMs >= command->created_at_ms
		? completedAtMs - command->created_at_ms : 0;
	if (!response.is_object()) {
		response = {{"success", false}, {"data", response}, {"error", "invalid engine response"}};
	}
	response["request_id"] = command->request_id;
	response["action"] = EngineActionName(command->action);
	response["timing_ms"] = {
		{"queue_wait", queueWaitMs},
		{"engine_execution", executionMs},
		{"total", totalMs}
	};

	const bool success = response.value("success", false);
	g_commands_completed.fetch_add(1);
	if (!success) g_commands_failed.fetch_add(1);
	if (command->cancelled.load()) {
		g_commands_cancelled.fetch_add(1);
		response["client_cancelled"] = true;
	}
	if (command->client_timed_out.load()) {
		g_late_results.fetch_add(1);
		response["late_after_timeout"] = true;
	}
	{
		std::lock_guard<std::mutex> lock(g_telemetry_mutex);
		g_last_request_id = command->request_id;
		g_last_action = EngineActionName(command->action);
		g_last_error_code = response.value("error_code", std::string());
		g_last_queue_wait_ms = queueWaitMs;
		g_last_execution_ms = executionMs;
		g_last_total_ms = totalMs;
		g_has_last_command = true;
		g_last_success = success;
	}
	return response;
}


json BuildEngineStatus() {
	json response;
	response["success"] = true;
	response["server_running"] = g_mcp_server_running.load();
	response["view_attached"] = g_engine_view_attached.load();
	response["accepting_commands"] = g_engine_accepting_commands.load();
	response["queue_capacity"] = static_cast<unsigned long long>(kMaxPendingEngineCommands);

	{
		std::lock_guard<std::mutex> lock(g_cmd_mutex);
		response["pending_commands"] = static_cast<unsigned long long>(g_cmd_queue.size());
	}

	const unsigned long long lastTick = g_last_engine_tick_ms.load();
	response["last_tick_ms"] = lastTick;
	bool tickAlive = false;
	if (lastTick == 0) {
		response["last_tick_age_ms"] = nullptr;
	}
	else {
		const unsigned long long now = static_cast<unsigned long long>(GetTickCount64());
		const unsigned long long tickAge = now >= lastTick ? now - lastTick : 0;
		response["last_tick_age_ms"] = tickAge;
		tickAlive = tickAge <= 5000;
	}
	response["tick_alive"] = tickAlive;
	response["engine_ready"] = g_mcp_server_running.load() &&
		g_engine_view_attached.load() && g_engine_accepting_commands.load() && tickAlive;
	const unsigned long long startedAt = g_engine_service_started_ms.load();
	response["service_uptime_ms"] = startedAt > 0 ? EngineMonotonicMs() - startedAt : 0;
	response["metrics"] = {
		{"accepted", g_commands_accepted.load()},
		{"completed", g_commands_completed.load()},
		{"failed", g_commands_failed.load()},
		{"timed_out", g_commands_timed_out.load()},
		{"rejected", g_commands_rejected.load()},
		{"cancelled", g_commands_cancelled.load()},
		{"late_results", g_late_results.load()}
	};
	{
		std::lock_guard<std::mutex> lock(g_telemetry_mutex);
		if (g_has_last_command) {
			response["last_command"] = {
				{"request_id", g_last_request_id},
				{"action", g_last_action},
				{"success", g_last_success},
				{"error_code", g_last_error_code},
				{"queue_wait_ms", g_last_queue_wait_ms},
				{"engine_execution_ms", g_last_execution_ms},
				{"total_ms", g_last_total_ms}
			};
		}
		else {
			response["last_command"] = nullptr;
		}
	}
	return response;
}


void FailPendingEngineCommands(const std::string& error) {
	std::queue<std::shared_ptr<EngineCommand>> pending;
	{
		std::lock_guard<std::mutex> lock(g_cmd_mutex);
		pending.swap(g_cmd_queue);
	}

	while (!pending.empty()) {
		std::shared_ptr<EngineCommand> cmd = pending.front();
		pending.pop();
		cmd->cancelled.store(true);
		json response = FinalizeEngineCommand(cmd, {
			{"success", false},
			{"error", error},
			{"error_code", "engine_stopped"}
		}, EngineMonotonicMs());
		cmd->TrySetResult(response);
	}
}
