#pragma once

#include "HelloMCPCommon.h"
#include "HelloMCPObjectExecutor.h"
#include "HelloMCPOutputExecutor.h"
#include "HelloMCPSceneEffectExecutor.h"


class EngineExecutor : public ObjectExecutor, public OutputExecutor, public SceneEffectExecutor,
	public Vs::AppListener, public CVsView::Listener {
public:
	void OnTick(VsDWord t) override;

private:
	json ProcessObjectAction(EngineAction action, const std::string& buffer);
	json ProcessSceneAction(EngineAction action, const std::string& buffer);
	json ProcessOutputAction(EngineAction action, const std::string& buffer);
	json MakeDispatchResult(bool success, EngineAction action,
		const std::string& subject, const std::string& target);
	void ProcessEngineCommands();
};
