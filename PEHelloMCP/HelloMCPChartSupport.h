#pragma once

#include "HelloMCPCommon.h"


namespace hello_mcp { namespace detail {

class ChartSupport {
public:
	static bool ReadVec3(const json& data, const char* fieldName, float out[3],
		bool& hasValue, std::string& errorMessage);
	static bool ReadVec2(const json& data, const char* fieldName, float out[2],
		bool& hasValue, std::string& errorMessage);
	static bool ReadSize(const json& data, float& width, float& height,
		bool& hasValue, std::string& errorMessage);
	static bool ReadFont(const json& data, LOGFONT& font,
		bool& hasValue, std::string& errorMessage);
	static CVsVariable* FindVariable(CVsModule* mainModule, const std::string& variableName,
		std::string& errorMessage);
	static bool BindTableVariables(CTable* chart, CVsModule* mainModule,
		const json& data, std::string& errorMessage);
	static bool BindGridVariables(CGrid* chart, CVsModule* mainModule,
		const json& data, std::string& errorMessage);

	template <typename TChart>
	static bool ApplyPositionSizeColor(TChart* chart, const json& data, std::string& errorMessage) {
		errorMessage.clear();
		if (!chart) {
			errorMessage = "chart object is null";
			return false;
		}

		float position[3] = { 0.0f, 0.0f, 0.0f };
		bool hasPosition = false;
		if (!ReadVec3(data, "position", position, hasPosition, errorMessage)) {
			return false;
		}
		if (hasPosition) {
			chart->SetPosition(position);
		}

		float width = 0.0f;
		float height = 0.0f;
		bool hasSize = false;
		if (!ReadSize(data, width, height, hasSize, errorMessage)) {
			return false;
		}
		if (hasSize) {
			chart->SetSize(width, height);
		}

		float color[3] = { 0.0f, 0.0f, 0.0f };
		bool hasColor = false;
		if (!ReadVec3(data, "color", color, hasColor, errorMessage)) {
			return false;
		}
		if (hasColor) {
			chart->SetColor(0, color);
		}
		return true;
	}

	template <typename TChart>
	static bool ApplyFont(TChart* chart, const json& data, std::string& errorMessage) {
		LOGFONT font;
		bool hasFont = false;
		if (!ReadFont(data, font, hasFont, errorMessage)) {
			return false;
		}
		if (hasFont) {
			chart->SetFont(&font);
		}
		return true;
	}

	template <typename TChart>
	static bool BindSingleVariable(TChart* chart, CVsModule* mainModule,
		const json& data, std::string& errorMessage) {
		errorMessage.clear();
		if (!data.contains("value_variable")) {
			return true;
		}

		try {
			const std::string variableName = _G(data["value_variable"]);
			if (variableName.empty()) {
				return true;
			}
			CVsVariable* variable = FindVariable(mainModule, variableName, errorMessage);
			if (!variable) {
				return false;
			}
			chart->SetValueVariable(variable);
		}
		catch (const json::exception& e) {
			errorMessage = std::string("value_variable JSON error: ") + e.what();
			return false;
		}
		return true;
	}
};

} }
