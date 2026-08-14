#include "stdafx.h"
#include "Plugin.h"
#include "resource.h"
#include "HelloMCPService.h"


FXDLL_MAIN_DECLARE;

FXSDK_API_C void funcMain(FX_PTR dwData = NULL);
FXSDK_API_C void funcAbout(FX_PTR dwData = NULL);
LRESULT messageProc(UINT message, WPARAM wParam, LPARAM lParam);


namespace {
	void EnsureMCPServiceAttached() {
		if (HelloMCPService::IsRunning()) {
			HelloMCPService::AttachCurrentView();
		}
		else {
			HelloMCPService::Start();
		}
	}
}


FXSDK_API int FxPluginDLL_Init(void* context) {
	PluginInfo* dll = static_cast<PluginInfo*>(context);
	dll->Init();

	dll->SetProperty("Author", "PESoft");
	dll->SetProperty("Summary", "PEHelloMCP");
	dll->SetProperty("Description", "");
	dll->SetProperty("Version", 100);
	dll->SetProperty("ClassID", (DWORD_PTR)&PLUGIN_CLASS_ID);
	dll->SetProperty("dllMessageProc", (DWORD_PTR)messageProc);

	dll->AddCmd(helloMCP, STRINGIZE(helloMCP));
	FXDLL_ADDABOUT;
	HelloMCPService::Start();
	return 1;
}


void funcMain(FX_PTR dwData) {
	FxPluginShowAboutDlg("Hello MCP...");
}


void funcAbout(FX_PTR dwData) {
	FxPluginShowAboutDlg("PEHelloMCP");
}


LRESULT messageProc(UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == PEM_PLUGIN_FUNC) {
		if (wParam == PluginFunc_Destroy) {
			HelloMCPService::Stop();
		}
		else if (wParam == PluginFunc_ViewInit) {
			EnsureMCPServiceAttached();
		}
	}
	else if (message == PEO_MAIN_MSG || message == PEM_MAIN_MSG) {
		if (wParam == MAINMSG_ViewDestroy) {
			HelloMCPService::DetachView();
		}
		else if (wParam == MAINMSG_ViewCreate) {
			EnsureMCPServiceAttached();
		}
		else if (wParam == MAINMSG_ProjectClose) {
			HelloMCPService::DetachView();
		}
		else if (wParam == MAINMSG_ProjectOpenEnd) {
			EnsureMCPServiceAttached();
		}
	}
	return 0;
}
