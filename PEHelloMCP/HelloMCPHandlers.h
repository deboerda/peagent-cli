#pragma once

#include "HelloMCPCommon.h"
#include "HelloMCPDebug.h"

class ActionHandler: public CivetHandler {
public:
	explicit ActionHandler(EngineAction action)
		: m_action(action) {
	}

	bool handlePost(CivetServer *server, struct mg_connection *conn) override {
		const std::string requestId = GetOrCreateRequestId(conn);
		std::string payload = ReadHttpRequest(conn);
		if (payload.empty()) {
			json response = {
				{"success", false},
				{"error", "empty request"},
				{"error_code", "empty_request"},
				{"request_id", requestId},
				{"action", EngineActionName(m_action)},
				{"retryable", false}
			};
			SendJsonResponse(conn, response);
			return true;
		}

		json response = EnqueueAndWait(m_action, payload, requestId);
		SendJsonResponse(conn, response);
		return true;
	}

private:
	EngineAction m_action;
};


class DebugStartHandler : public CivetHandler {
public:
    bool handlePost(CivetServer *server, struct mg_connection *conn) override {
        const std::string requestId = GetOrCreateRequestId(conn);
        // Start only posts a previously calibrated native UI command; it does
        // not call PE engine APIs, so it remains usable while debug pauses Tick.
        SendJsonResponse(conn, HelloMCPDebug::StartCurrentProject(requestId));
        return true;
    }
};
class DebugCalibrateHandler : public CivetHandler {
public:
    bool handlePost(CivetServer *server, struct mg_connection *conn) override {
        SendJsonResponse(conn, HelloMCPDebug::BeginDebugCommandCapture(GetOrCreateRequestId(conn)));
        return true;
    }
};
class DebugStopHandler : public CivetHandler {
public:
    bool handlePost(CivetServer *server, struct mg_connection *conn) override {
        const std::string requestId = GetOrCreateRequestId(conn);
        // This path must not wait for EngineExecutor: PE pauses its UI tick while
        // the native player is running, but terminating our owned child is safe.
        SendJsonResponse(conn, HelloMCPDebug::StopDebugSession(requestId));
        return true;
    }
};
class HealthHandler : public CivetHandler {
public:
	bool handleGet(CivetServer *server, struct mg_connection *conn) override {
		SendHealth(conn);
		return true;
	}

	bool handlePost(CivetServer *server, struct mg_connection *conn) override {
		SendHealth(conn);
		return true;
	}

private:
	void SendHealth(struct mg_connection *conn) {
		const std::string requestId = GetOrCreateRequestId(conn);
		SendJsonResponse(conn, {
			{"success", true},
			{"service", "PEHelloMCP"},
			{"server_running", g_mcp_server_running.load()},
			{"request_id", requestId}
		});
	}
};


class EngineStatusHandler : public CivetHandler {
public:
	bool handleGet(CivetServer *server, struct mg_connection *conn) override {
		SendStatus(conn);
		return true;
	}

	bool handlePost(CivetServer *server, struct mg_connection *conn) override {
		SendStatus(conn);
		return true;
	}

private:
	void SendStatus(struct mg_connection *conn) {
		json response = BuildEngineStatus();
		response["request_id"] = GetOrCreateRequestId(conn);
		SendJsonResponse(conn, response);
	}
};
