#pragma once

#include "Plugin.h"


namespace HelloMCPService {
	bool Start();
	void Stop();
	bool AttachCurrentView();
	void DetachView();
	bool IsRunning();
}

FX_PTR helloMCP();
