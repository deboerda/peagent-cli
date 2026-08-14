#pragma once

#include "HelloMCPCommon.h"


class SceneEffectExecutor {
protected:
	json ProcessFrameCapture(const std::string& action, const json& payload);
	json ProcessSceneEffectQuery(const std::string& action, const std::string& effectName,
		const std::string& target);
	json ProcessSceneEffectApply(const std::string& action, const std::string& effectName,
		const std::string& target, const json& payload);
};
