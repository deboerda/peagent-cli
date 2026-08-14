#include "stdafx.h"
#include "HelloMCPChartSupport.h"


using hello_mcp::detail::ChartSupport;


bool ChartSupport::ReadVec3(const json& data, const char* fieldName, float out[3],
	bool& hasValue, std::string& errorMessage) {
	hasValue = false;
	if (!data.contains(fieldName)) {
		return true;
	}
	if (!data[fieldName].is_array() || data[fieldName].size() != 3) {
		errorMessage = std::string(fieldName) +
			(data[fieldName].is_array() ? " must contain exactly 3 numbers" : " must be an array of 3 numbers");
		return false;
	}

	try {
		for (int index = 0; index < 3; ++index) {
			out[index] = data[fieldName][index].get<float>();
		}
	}
	catch (const json::exception& e) {
		errorMessage = std::string(fieldName) + " JSON error: " + e.what();
		return false;
	}
	hasValue = true;
	return true;
}


bool ChartSupport::ReadVec2(const json& data, const char* fieldName, float out[2],
	bool& hasValue, std::string& errorMessage) {
	hasValue = false;
	if (!data.contains(fieldName)) {
		return true;
	}
	if (!data[fieldName].is_array() || data[fieldName].size() != 2) {
		errorMessage = std::string(fieldName) +
			(data[fieldName].is_array() ? " must contain exactly 2 numbers" : " must be an array of 2 numbers");
		return false;
	}

	try {
		for (int index = 0; index < 2; ++index) {
			out[index] = data[fieldName][index].get<float>();
		}
	}
	catch (const json::exception& e) {
		errorMessage = std::string(fieldName) + " JSON error: " + e.what();
		return false;
	}
	hasValue = true;
	return true;
}


bool ChartSupport::ReadSize(const json& data, float& width, float& height,
	bool& hasValue, std::string& errorMessage) {
	hasValue = false;
	const bool hasWidth = data.contains("width");
	const bool hasHeight = data.contains("height");
	if (!hasWidth && !hasHeight) {
		return true;
	}
	if (hasWidth != hasHeight) {
		errorMessage = "width and height must be provided together";
		return false;
	}

	try {
		width = data["width"].get<float>();
		height = data["height"].get<float>();
	}
	catch (const json::exception& e) {
		errorMessage = std::string("size JSON error: ") + e.what();
		return false;
	}
	hasValue = true;
	return true;
}


bool ChartSupport::ReadFont(const json& data, LOGFONT& font,
	bool& hasValue, std::string& errorMessage) {
	hasValue = false;
	if (!data.contains("font")) {
		return true;
	}
	if (!data["font"].is_object()) {
		errorMessage = "font must be an object";
		return false;
	}

	try {
		const json& input = data["font"];
		ZeroMemory(&font, sizeof(LOGFONT));
		font.lfHeight = -abs(input.value("size", 20));
		font.lfWeight = input.value("bold", false) ? FW_BOLD : FW_NORMAL;
		font.lfItalic = input.value("italic", false) ? TRUE : FALSE;
		font.lfCharSet = static_cast<BYTE>(input.value("charset", static_cast<int>(GB2312_CHARSET)));
	}
	catch (const json::exception& e) {
		errorMessage = std::string("font JSON error: ") + e.what();
		return false;
	}
	hasValue = true;
	return true;
}


CVsVariable* ChartSupport::FindVariable(CVsModule* mainModule, const std::string& variableName,
	std::string& errorMessage) {
	errorMessage.clear();
	if (!mainModule) {
		errorMessage = "main module not found";
		return nullptr;
	}
	if (variableName.empty()) {
		errorMessage = "variable name cannot be empty";
		return nullptr;
	}

	CVsVariable* variable = (CVsVariable*)mainModule->FindObjectLocal(MVO_OBJ_TYPE_VAR, variableName.c_str());
	if (!variable) {
		errorMessage = "variable not found: " + variableName;
	}
	return variable;
}


bool ChartSupport::BindTableVariables(CTable* chart, CVsModule* mainModule,
	const json& data, std::string& errorMessage) {
	errorMessage.clear();
	if (!data.contains("value_variables")) {
		return true;
	}
	if (!data["value_variables"].is_array()) {
		errorMessage = "value_variables must be an array";
		return false;
	}

	try {
		const json& values = data["value_variables"];
		for (size_t index = 0; index < values.size(); ++index) {
			const std::string variableName = _G(values[index].get<std::string>());
			if (variableName.empty()) {
				continue;
			}
			CVsVariable* variable = FindVariable(mainModule, variableName, errorMessage);
			if (!variable) {
				return false;
			}
			chart->SetValueVariable((int)index, variable);
		}
	}
	catch (const json::exception& e) {
		errorMessage = std::string("value_variables JSON error: ") + e.what();
		return false;
	}
	return true;
}


bool ChartSupport::BindGridVariables(CGrid* chart, CVsModule* mainModule,
	const json& data, std::string& errorMessage) {
	errorMessage.clear();
	if (!data.contains("value_variables")) {
		return true;
	}
	if (!data["value_variables"].is_array()) {
		errorMessage = "value_variables must be a 2d array";
		return false;
	}

	try {
		const json& rows = data["value_variables"];
		for (size_t row = 0; row < rows.size(); ++row) {
			if (!rows[row].is_array()) {
				errorMessage = "value_variables row must be an array";
				return false;
			}
			for (size_t column = 0; column < rows[row].size(); ++column) {
				const std::string variableName = _G(rows[row][column].get<std::string>());
				if (variableName.empty()) {
					continue;
				}
				CVsVariable* variable = FindVariable(mainModule, variableName, errorMessage);
				if (!variable) {
					return false;
				}
				chart->SetValueVariable((int)row, (int)column, variable);
			}
		}
	}
	catch (const json::exception& e) {
		errorMessage = std::string("value_variables JSON error: ") + e.what();
		return false;
	}
	return true;
}
