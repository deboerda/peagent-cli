#include "stdafx.h"
#include "HelloMCPService.h"

#include "HelloMCPCommon.h"
#include "HelloMCPEngineExecutor.h"
#include "HelloMCPHandlers.h"
#include "HelloMCPScript.h"
#include "HelloMCPDebug.h"


namespace {
	EngineExecutor g_engine_executor;
	CVsView* g_attached_view = nullptr;

	ActionHandler g_obj_create_handler(EngineAction::ObjectCreate);
	ActionHandler g_obj_query_handler(EngineAction::ObjectQuery);
	ActionHandler g_obj_modify_handler(EngineAction::ObjectModify);
	ActionHandler g_output_query_handler(EngineAction::OutputQuery);
	ActionHandler g_effect_query_handler(EngineAction::EffectQuery);
	ActionHandler g_effect_apply_handler(EngineAction::EffectApply);
	ActionHandler g_frame_capture_handler(EngineAction::FrameCapture);
	ActionHandler g_light_probe_create_handler(EngineAction::LightProbeCreate);
	ActionHandler g_light_probe_query_handler(EngineAction::LightProbeQuery);
	ActionHandler g_light_probe_modify_handler(EngineAction::LightProbeModify);
	ActionHandler g_light_probe_delete_handler(EngineAction::LightProbeDelete);
	ActionHandler g_light_probe_render_handler(EngineAction::LightProbeRender);
	ActionHandler g_script_execute_handler(EngineAction::ScriptExecute);
	ActionHandler g_script_execute_file_handler(EngineAction::ScriptExecuteFile);
	ActionHandler g_script_validate_handler(EngineAction::ScriptValidate);
	ActionHandler g_graph_create_handler(EngineAction::GraphCreate);
	ActionHandler g_graph_read_handler(EngineAction::GraphRead);
	ActionHandler g_graph_node_create_handler(EngineAction::GraphNodeCreate);
	ActionHandler g_graph_node_delete_handler(EngineAction::GraphNodeDelete);
	ActionHandler g_graph_save_handler(EngineAction::GraphSave);
	ActionHandler g_graph_register_handler(EngineAction::GraphRegister);
	ActionHandler g_graph_remove_handler(EngineAction::GraphRemove);
	ActionHandler g_graph_create_native_handler(EngineAction::GraphCreateNative);
	ActionHandler g_graph_build_boot_handler(EngineAction::GraphBuildBoot);
	DebugStartHandler g_debug_start_handler;
	DebugCalibrateHandler g_debug_calibrate_handler;
	DebugStopHandler g_debug_stop_handler;
	HealthHandler g_health_handler;
	EngineStatusHandler g_engine_status_handler;
	std::unique_ptr<CivetServer> g_server;

	CVsView* GetCurrentView() {
		CVsModule* mainModule = reinterpret_cast<CVsModule*>(FxPluginMgr_GetData(NULL, "Ptr_MainVsModule"));
		return mainModule ? mainModule->VsView() : nullptr;
	}

	void RegisterHandlers(CivetServer& server) {
		server.addHandler("/api/health", g_health_handler);
		server.addHandler("/api/engine/status", g_engine_status_handler);
		server.addHandler("/api/obj/data", g_obj_create_handler);
		server.addHandler("/api/obj/query", g_obj_query_handler);
		server.addHandler("/api/obj/modify", g_obj_modify_handler);
		server.addHandler("/api/output/query", g_output_query_handler);
		server.addHandler("/api/effect/query", g_effect_query_handler);
		server.addHandler("/api/effect/apply", g_effect_apply_handler);
		server.addHandler("/api/frame/capture", g_frame_capture_handler);
		server.addHandler("/api/pe/light_probe/create", g_light_probe_create_handler);
		server.addHandler("/api/pe/light_probe/query", g_light_probe_query_handler);
		server.addHandler("/api/pe/light_probe/modify", g_light_probe_modify_handler);
		server.addHandler("/api/pe/light_probe/delete", g_light_probe_delete_handler);
		server.addHandler("/api/pe/light_probe/render", g_light_probe_render_handler);
	server.addHandler("/api/script/execute", g_script_execute_handler);
	server.addHandler("/api/script/execute_file", g_script_execute_file_handler);
	server.addHandler("/api/script/validate", g_script_validate_handler);
	server.addHandler("/api/graph/create", g_graph_create_handler);
	server.addHandler("/api/graph/read", g_graph_read_handler);
	server.addHandler("/api/graph/node/create", g_graph_node_create_handler);
	server.addHandler("/api/graph/node/delete", g_graph_node_delete_handler);
	server.addHandler("/api/graph/save", g_graph_save_handler);
	server.addHandler("/api/graph/register", g_graph_register_handler);
	server.addHandler("/api/graph/remove", g_graph_remove_handler);
	server.addHandler("/api/graph/create_native", g_graph_create_native_handler);
	server.addHandler("/api/graph/build_boot", g_graph_build_boot_handler);
	server.addHandler("/api/debug/start", g_debug_start_handler);
	server.addHandler("/api/debug/calibrate", g_debug_calibrate_handler);
	server.addHandler("/api/debug/stop", g_debug_stop_handler);
	}
}


bool HelloMCPService::Start() {
	Stop();

	#ifdef _DEBUG
	const char* options[] = { "listening_ports", "18080", 0 };
#else
	const char* options[] = { "listening_ports", "8080", 0 };
#endif
	try {
		g_server = std::make_unique<CivetServer>(options);
		RegisterHandlers(*g_server);
		g_mcp_server_running.store(true);
		g_engine_service_started_ms.store(EngineMonotonicMs());
		AttachCurrentView();
		return true;
	}
	catch (const std::exception&) {
		g_engine_accepting_commands.store(false);
		g_engine_view_attached.store(false);
		g_mcp_server_running.store(false);
		g_engine_service_started_ms.store(0);
		g_server.reset();
		return false;
	}
}


void HelloMCPService::Stop() {
	g_engine_accepting_commands.store(false);
	FailPendingEngineCommands("MCP engine service stopped");
	DetachView();
	g_mcp_server_running.store(false);
	g_engine_service_started_ms.store(0);
	g_server.reset();
}


bool HelloMCPService::AttachCurrentView() {
	if (!g_server) {
		return false;
	}

	CVsView* currentView = GetCurrentView();
	if (currentView == g_attached_view && currentView != nullptr &&
		currentView->FindListener(&g_engine_executor)) {
		g_engine_view_attached.store(true);
		g_engine_accepting_commands.store(true);
		return true;
	}

	DetachView();
	if (!currentView) {
		return false;
	}

	currentView->AddListener(&g_engine_executor);
	g_attached_view = currentView;
	g_last_engine_tick_ms.store(0);
	g_engine_view_attached.store(true);
	g_engine_accepting_commands.store(true);
	return true;
}


void HelloMCPService::DetachView() {
	g_engine_accepting_commands.store(false);
	FailPendingEngineCommands("engine view detached");

	if (g_attached_view) {
		g_attached_view->RemoveListener(&g_engine_executor);
		g_attached_view = nullptr;
	}
	g_engine_view_attached.store(false);
	g_last_engine_tick_ms.store(0);
}


bool HelloMCPService::IsRunning() {
	return g_server != nullptr && g_mcp_server_running.load();
}


FX_PTR helloMCP() {
	if (!HelloMCPService::Start()) {
		FxPluginShowAboutDlg("server start failed");
		return 0;
	}

	if (g_engine_view_attached.load()) {
		FxPluginShowAboutDlg("server restarted and engine view attached");
	}
	else {
		FxPluginShowAboutDlg("server restarted but no active view found");
	}
	return 0;
}
