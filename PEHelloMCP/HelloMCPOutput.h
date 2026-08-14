#pragma once

#include "HelloMCPCommon.h"

namespace hello_mcp { namespace detail {

class OutputService {
public:
	std::string WideToUtf8(const std::wstring& value) {
		if (value.empty()) {
			return "";
		}

		int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (size <= 0) {
			return "";
		}

		std::vector<char> buffer(size);
		WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, buffer.data(), size, nullptr, nullptr);
		return std::string(buffer.data());
	}

	HWND FindTopLevelWindowContains(const wchar_t* keyword) {
		HWND hWnd = GetWindow(GetDesktopWindow(), GW_CHILD);
		while (hWnd) {
			wchar_t title[256] = { 0 };
			GetWindowTextW(hWnd, title, 255);
			if (wcsstr(title, keyword) && GetParent(hWnd) == NULL) {
				return hWnd;
			}
			hWnd = GetNextWindow(hWnd, GW_HWNDNEXT);
		}
		return NULL;
	}

	HWND FindDescendantWindowContains(HWND hParent, const wchar_t* keyword) {
		if (!hParent) {
			return NULL;
		}

		HWND hChild = GetWindow(hParent, GW_CHILD);
		while (hChild) {
			wchar_t title[256] = { 0 };
			GetWindowTextW(hChild, title, 255);
			if (wcsstr(title, keyword)) {
				return hChild;
			}

			HWND nested = FindDescendantWindowContains(hChild, keyword);
			if (nested) {
				return nested;
			}

			hChild = GetNextWindow(hChild, GW_HWNDNEXT);
		}
		return NULL;
	}

	void FindFirstListBox(HWND hParent, HWND& outHwnd) {
		if (!hParent || outHwnd) {
			return;
		}

		HWND hChild = GetWindow(hParent, GW_CHILD);
		while (hChild)
		{
			wchar_t szClass[256] = { 0 };
			GetClassNameW(hChild, szClass, 255);

			if (wcscmp(szClass, L"ListBox") == 0)
			{
				outHwnd = hChild;
			}

			FindFirstListBox(hChild, outHwnd);
			hChild = GetNextWindow(hChild, GW_HWNDNEXT);
		}
	}

	std::vector<std::wstring> ReadListBoxItems(HWND hListBox) {
		std::vector<std::wstring> items;
		if (!hListBox) {
			return items;
		}

		int count = static_cast<int>(SendMessageW(hListBox, LB_GETCOUNT, 0, 0));
		if (count <= 0) {
			return items;
		}

		items.reserve(count);
		for (int i = 0; i < count; ++i) {
			int len = static_cast<int>(SendMessageW(hListBox, LB_GETTEXTLEN, i, 0));
			if (len < 0) {
				continue;
			}

			std::vector<wchar_t> buffer(len + 1, 0);
			SendMessageW(hListBox, LB_GETTEXT, i, reinterpret_cast<LPARAM>(buffer.data()));
			items.emplace_back(buffer.data());
		}

		return items;
	}

	json BuildOutputUnavailable(const std::string& error, bool peplayerRunning) {
		json response;
		response["success"] = false;
		response["available"] = false;
		response["peplayer_running"] = peplayerRunning;
		response["line_count"] = 0;
		response["returned_count"] = 0;
		response["lines"] = json::array();
		response["text"] = "";
		response["error"] = error;
		return response;
	}

	json QueryEngineOutput(int limit) {
		HWND hPePlayer = FindTopLevelWindowContains(L"PEPlayer");
		if (hPePlayer) {
			return BuildOutputUnavailable("PEPlayer window is running; engine output is only available after it closes", true);
		}

		HWND hMain = FindTopLevelWindowContains(L"PostEngineer");
		if (!hMain) {
			return BuildOutputUnavailable("PostEngineer main window not found", false);
		}

		HWND hOutput = FindDescendantWindowContains(hMain, L"输出");
		if (!hOutput) {
			return BuildOutputUnavailable("PostEngineer output window not found", false);
		}

		HWND hListBox = NULL;
		FindFirstListBox(hOutput, hListBox);
		if (!hListBox) {
			return BuildOutputUnavailable("output ListBox not found", false);
		}

		std::vector<std::wstring> allItems = ReadListBoxItems(hListBox);
		int totalCount = static_cast<int>(allItems.size());
		int startIndex = 0;
		if (limit > 0 && limit < totalCount) {
			startIndex = totalCount - limit;
		}

		json lines = json::array();
		std::string joinedText;
		for (int i = startIndex; i < totalCount; ++i) {
			std::string line = WideToUtf8(allItems[i]);
			lines.push_back(line);
			if (!joinedText.empty()) {
				joinedText += "\n";
			}
			joinedText += line;
		}

		json response;
		response["success"] = true;
		response["available"] = true;
		response["peplayer_running"] = false;
		response["line_count"] = totalCount;
		response["returned_count"] = static_cast<int>(lines.size());
		response["lines"] = lines;
		response["text"] = joinedText;
		response["message"] = totalCount > 0 ? "engine output collected" : "engine output is empty";
		return response;
	}

	json ProcessOutputQuery(const std::string& buffer) {
		try {
			json payload = json::parse(buffer);
			int limit = payload.value("limit", 0);
			if (limit < 0) {
				return BuildOutputUnavailable("limit must be >= 0", false);
			}

			return QueryEngineOutput(limit);
		}
		catch (json::exception& e) {
			json response;
			response["success"] = false;
			response["available"] = false;
			response["peplayer_running"] = false;
			response["line_count"] = 0;
			response["returned_count"] = 0;
			response["lines"] = json::array();
			response["text"] = "";
			response["error"] = std::string("JSON error: ") + e.what();
			return response;
		}
	}
};

} }
