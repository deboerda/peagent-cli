#include "stdafx.h"
#include "HelloMCPSceneEffectExecutor.h"
#include "HelloMCPSceneEffects.h"


json SceneEffectExecutor::ProcessFrameCapture(const std::string& action, const json& payload) {
	hello_mcp::detail::SceneEffectService service;
	return service.ProcessFrameCapture(action, payload);
}


json SceneEffectExecutor::ProcessSceneEffectQuery(const std::string& action,
	const std::string& effectName, const std::string& target) {
	hello_mcp::detail::SceneEffectService service;
	return service.ProcessSceneEffectQuery(action, effectName, target);
}


json SceneEffectExecutor::ProcessSceneEffectApply(const std::string& action,
	const std::string& effectName, const std::string& target, const json& payload) {
	hello_mcp::detail::SceneEffectService service;
	return service.ProcessSceneEffectApply(action, effectName, target, payload);
}
