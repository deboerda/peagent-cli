#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include "Plugin.h"
#include "VsInterface.h"
#include "VsButton.h"
#include "VsVariable.h"
#include "VsBody.h"
#include "VsScrew.h"
#include "VsImage.h"
#include "Remark.h"
#include "Curve.h"
#include "Number.h"
#include "Progress.h"
#include "Histogram.h"
#include "Table.h"
#include "Grid.h"
#include "DashBoard.h"
#include "ScriptUtilities.h"
#include "VsApp.h"
#include "VsView.h"

#include "CivetServer.h"
#include "json.hpp"

#include <string>
#include <ctime>
#include <vector>
#include <experimental/filesystem>
#include <mutex>
#include <queue>
#include <future>
#include <chrono>
#include <memory>
#include <atomic>
#include <cctype>
#include <fstream>

#pragma comment(lib, "gdiplus.lib")

namespace fs = std::experimental::filesystem;
using json = nlohmann::json;

std::string GbkToUtf8(const std::string& gbkStr);
std::string Utf8ToGbk(const std::string& utf8Str);

#define _U(str) GbkToUtf8(str)
#define _G(str) Utf8ToGbk(str)

json make_result(bool success, const std::string& objType,
	const std::string& name, const std::string& message);

enum class EngineAction {
	ObjectCreate,
	ObjectQuery,
	ObjectModify,
	OutputQuery,
	EffectQuery,
	EffectApply,
	FrameCapture,
	LightProbeCreate,
	LightProbeQuery,
	LightProbeModify,
	LightProbeDelete,
	LightProbeRender,
	ScriptExecute,
	ScriptExecuteFile,
	ScriptValidate,
	GraphCreate,
	GraphRead,
	GraphNodeCreate,
	GraphNodeDelete,
	GraphSave,
	GraphRegister,
	GraphRemove,
	GraphCreateNative,
	GraphBuildBoot,
	DebugStart,
	DebugStop
};

const char* EngineActionName(EngineAction action);

struct EngineCommand {
	EngineAction action;
	std::string payload;
	std::string request_id;
	unsigned long long created_at_ms{ 0 };
	unsigned long long enqueued_at_ms{ 0 };
	std::promise<json> result_promise;
	std::atomic<bool> cancelled{ false };
	std::atomic<bool> client_timed_out{ false };
	std::atomic<bool> completed{ false };

	bool TrySetResult(const json& result) {
		if (completed.exchange(true)) {
			return false;
		}
		result_promise.set_value(result);
		return true;
	}
};

extern json g_pe_scene_cache;
extern std::queue<std::shared_ptr<EngineCommand>> g_cmd_queue;
extern std::mutex g_cmd_mutex;
extern std::atomic<bool> g_mcp_server_running;
extern std::atomic<bool> g_engine_view_attached;
extern std::atomic<bool> g_engine_accepting_commands;
extern std::atomic<unsigned long long> g_last_engine_tick_ms;
extern std::atomic<unsigned long long> g_engine_service_started_ms;

std::string ReadHttpRequest(struct mg_connection *conn);
void SendJsonResponse(struct mg_connection *conn, const json& response);
std::string GetOrCreateRequestId(struct mg_connection *conn);
unsigned long long EngineMonotonicMs();
json EnqueueAndWait(EngineAction action, const std::string& payload,
	const std::string& requestId);
json FinalizeEngineCommand(const std::shared_ptr<EngineCommand>& command,
	json response, unsigned long long startedAtMs);
json BuildEngineStatus();
void FailPendingEngineCommands(const std::string& error);
