#include "stdafx.h"
#include "HelloMCPChartObjects.h"


using hello_mcp::detail::ChartObjectService;
using hello_mcp::detail::ChartSupport;


namespace {
	template <typename TChart>
	using ApplyChartConfig = bool (*)(TChart*, CVsModule*, const json&, std::string&);

	template <typename TChart>
	json CreateChart(const json& config, const char* objectType, int typeId,
		ApplyChartConfig<TChart> applyConfig, bool applyDefaultRange = false) {
		std::string name;
		try {
			if (!config.is_object()) {
				return make_result(false, objectType, "", "config must be an object");
			}
			name = _G(config.at("name").get<std::string>());
		}
		catch (const json::exception& e) {
			return make_result(false, objectType, "",
				std::string(objectType) + " config JSON error: " + e.what());
		}
		if (name.empty()) {
			return make_result(false, objectType, "",
				std::string(objectType) + " name cannot be empty");
		}

		CVsModule* mainModule = (CVsModule*)FxPluginMgr_GetData(NULL, "Ptr_MainVsModule");
		if (!mainModule) {
			return make_result(false, objectType, _U(name), "main module not found");
		}

		FX_CMD("ProjectMgr", "CreateUIObject", (FX_PTR)mainModule,
			(FX_PTR)name.c_str(), (FX_PTR)"chart", typeId);
		TChart* chart = (TChart*)mainModule->FindObjectLocal(typeId, name.c_str());
		if (!chart) {
			return make_result(false, objectType, _U(name),
				std::string("failed to locate created ") + objectType);
		}

		json settings = config;
		settings.erase("name");
		if (applyDefaultRange && !settings.contains("range_min") && !settings.contains("range_max")) {
			settings["range_min"] = 0.0f;
			settings["range_max"] = 100.0f;
		}

		std::string errorMessage;
		if (!applyConfig(chart, mainModule, settings, errorMessage)) {
			return make_result(false, objectType, _U(name), errorMessage);
		}
		return make_result(true, objectType, _U(name),
			std::string(objectType) + " created");
	}

	template <typename TChart>
	bool ApplyRangedChart(TChart* chart, CVsModule* mainModule, const json& data,
		const char* objectType, std::string& errorMessage) {
		errorMessage.clear();
		if (!chart) {
			errorMessage = std::string(objectType) + " object is null";
			return false;
		}
		if (data.contains("name")) {
			errorMessage = std::string(objectType) + " name cannot be modified";
			return false;
		}
		if (!ChartSupport::ApplyPositionSizeColor(chart, data, errorMessage)) {
			return false;
		}

		try {
			const bool hasRangeMin = data.contains("range_min");
			const bool hasRangeMax = data.contains("range_max");
			if (hasRangeMin != hasRangeMax) {
				errorMessage = "range_min and range_max must be provided together";
				return false;
			}
			if (hasRangeMin) {
				chart->SetRange(data["range_min"].get<float>(), data["range_max"].get<float>());
			}
			if (data.contains("decimal_num")) chart->SetDecimalNum(data["decimal_num"].get<int>());
			if (data.contains("style")) chart->SetStyle(data["style"].get<int>());
		}
		catch (const json::exception& e) {
			errorMessage = std::string(objectType) + " config JSON error: " + e.what();
			return false;
		}
		return ChartSupport::BindSingleVariable(chart, mainModule, data, errorMessage);
	}
}


json ChartObjectService::CreateRemark(const json& config) {
	return CreateChart<CRemark>(config, "remark", MVO_OBJ_TYPE_REMARK, &ApplyRemark);
}


json ChartObjectService::CreateCurve(const json& config) {
	return CreateChart<CCurve>(config, "curve", MVO_OBJ_TYPE_CURVE, &ApplyCurve);
}


json ChartObjectService::CreateNumber(const json& config) {
	return CreateChart<CNumber>(config, "number", MVO_OBJ_TYPE_NUMBER, &ApplyNumber);
}


json ChartObjectService::CreateProgress(const json& config) {
	return CreateChart<CProgress>(config, "progress", MVO_OBJ_TYPE_PROGRESS,
		&ApplyProgress, true);
}


json ChartObjectService::CreateHistogram(const json& config) {
	return CreateChart<CHistogram>(config, "histogram", MVO_OBJ_TYPE_HISTOGRAM,
		&ApplyHistogram, true);
}


json ChartObjectService::CreateTable(const json& config) {
	return CreateChart<CTable>(config, "table", MVO_OBJ_TYPE_TABLE, &ApplyTable);
}


json ChartObjectService::CreateGrid(const json& config) {
	return CreateChart<CGrid>(config, "grid", MVO_OBJ_TYPE_GRID, &ApplyGrid);
}


json ChartObjectService::CreateDashBoard(const json& config) {
	return CreateChart<CDashBoard>(config, "dashBoard", MVO_OBJ_TYPE_DASHBOARD,
		&ApplyDashBoard, true);
}


bool ChartObjectService::RejectRename(const json& data, const std::string& objectType,
	std::string& errorMessage) {
	if (data.contains("name")) {
		errorMessage = objectType + " name cannot be modified";
		return false;
	}
	return true;
}


bool ChartObjectService::ApplyRemark(CRemark* chart, CVsModule* mainModule,
	const json& data, std::string& errorMessage) {
	errorMessage.clear();
	if (!chart) {
		errorMessage = "remark object is null";
		return false;
	}
	if (!RejectRename(data, "remark", errorMessage)) {
		return false;
	}

	try {
		if (data.contains("text")) {
			const std::string text = _G(data["text"]);
			chart->SetString(text.c_str());
		}
	}
	catch (const json::exception& e) {
		errorMessage = std::string("remark config JSON error: ") + e.what();
		return false;
	}
	return ChartSupport::ApplyPositionSizeColor(chart, data, errorMessage) &&
		ChartSupport::ApplyFont(chart, data, errorMessage) &&
		ChartSupport::BindSingleVariable(chart, mainModule, data, errorMessage);
}


bool ChartObjectService::ApplyCurve(CCurve* chart, CVsModule* mainModule,
	const json& data, std::string& errorMessage) {
	errorMessage.clear();
	if (!chart) {
		errorMessage = "curve object is null";
		return false;
	}
	if (!RejectRename(data, "curve", errorMessage) ||
		!ChartSupport::ApplyPositionSizeColor(chart, data, errorMessage) ||
		!ChartSupport::ApplyFont(chart, data, errorMessage)) {
		return false;
	}

	try {
		if (data.contains("line_width")) chart->SetLineWidth(data["line_width"].get<int>());
		if (data.contains("line_type")) chart->SetLineType((MVOLineType)data["line_type"].get<int>());
		if (data.contains("decimal_num")) chart->SetDecimalNum(data["decimal_num"].get<int>());
		if (data.contains("curve_type")) chart->SetCurveType(data["curve_type"].get<int>());
		if (data.contains("style")) chart->SetStyle(data["style"].get<int>());

		float range[2] = { 0.0f, 0.0f };
		bool hasRange = false;
		if (!ChartSupport::ReadVec2(data, "x_range", range, hasRange, errorMessage)) return false;
		if (hasRange) chart->SetXRange(range[0], range[1]);
		if (!ChartSupport::ReadVec2(data, "y_range", range, hasRange, errorMessage)) return false;
		if (hasRange) chart->SetYRange(range[0], range[1]);
		if (!ChartSupport::ReadVec2(data, "y_range2", range, hasRange, errorMessage)) return false;
		if (hasRange) chart->SetYRange2(range[0], range[1]);
		if (!ChartSupport::ReadVec2(data, "min_unit", range, hasRange, errorMessage)) return false;
		if (hasRange) chart->SetMinUnit(range[0], range[1]);
	}
	catch (const json::exception& e) {
		errorMessage = std::string("curve config JSON error: ") + e.what();
		return false;
	}
	return ChartSupport::BindSingleVariable(chart, mainModule, data, errorMessage);
}


bool ChartObjectService::ApplyNumber(CNumber* chart, CVsModule* mainModule,
	const json& data, std::string& errorMessage) {
	errorMessage.clear();
	if (!chart) {
		errorMessage = "number object is null";
		return false;
	}
	return RejectRename(data, "number", errorMessage) &&
		ChartSupport::ApplyPositionSizeColor(chart, data, errorMessage) &&
		ChartSupport::ApplyFont(chart, data, errorMessage) &&
		ChartSupport::BindSingleVariable(chart, mainModule, data, errorMessage);
}


bool ChartObjectService::ApplyProgress(CProgress* chart, CVsModule* mainModule,
	const json& data, std::string& errorMessage) {
	return ApplyRangedChart(chart, mainModule, data, "progress", errorMessage);
}


bool ChartObjectService::ApplyHistogram(CHistogram* chart, CVsModule* mainModule,
	const json& data, std::string& errorMessage) {
	errorMessage.clear();
	if (!chart) {
		errorMessage = "histogram object is null";
		return false;
	}
	if (!RejectRename(data, "histogram", errorMessage) ||
		!ChartSupport::ApplyPositionSizeColor(chart, data, errorMessage)) {
		return false;
	}

	try {
		const bool hasRangeMin = data.contains("range_min");
		const bool hasRangeMax = data.contains("range_max");
		if (hasRangeMin != hasRangeMax) {
			errorMessage = "range_min and range_max must be provided together";
			return false;
		}
		if (hasRangeMin) {
			chart->SetRange(data["range_min"].get<float>(), data["range_max"].get<float>());
		}
		if (data.contains("orientation")) {
			chart->SetOrientation((ChartOrient)data["orientation"].get<int>());
		}
	}
	catch (const json::exception& e) {
		errorMessage = std::string("histogram config JSON error: ") + e.what();
		return false;
	}
	return ChartSupport::BindSingleVariable(chart, mainModule, data, errorMessage);
}


bool ChartObjectService::ApplyTable(CTable* chart, CVsModule* mainModule,
	const json& data, std::string& errorMessage) {
	errorMessage.clear();
	if (!chart) {
		errorMessage = "table object is null";
		return false;
	}
	if (!RejectRename(data, "table", errorMessage)) {
		return false;
	}

	float position[3] = { 0.0f, 0.0f, 0.0f };
	bool hasPosition = false;
	if (!ChartSupport::ReadVec3(data, "position", position, hasPosition, errorMessage)) return false;
	if (hasPosition) chart->SetPosition(position);
	float width = 0.0f;
	float height = 0.0f;
	bool hasSize = false;
	if (!ChartSupport::ReadSize(data, width, height, hasSize, errorMessage)) return false;
	if (hasSize) chart->SetSize(width, height);

	try {
		if (data.contains("title")) {
			const std::string title = _G(data["title"]);
			chart->SetTitle(title.c_str());
		}
		float color[3] = { 0.0f, 0.0f, 0.0f };
		bool hasColor = false;
		if (!ChartSupport::ReadVec3(data, "title_color", color, hasColor, errorMessage)) return false;
		if (hasColor) chart->SetTitleColor(color);
		if (!ChartSupport::ReadVec3(data, "back_color", color, hasColor, errorMessage)) return false;
		if (hasColor) chart->SetBackColor(color);
		if (!ChartSupport::ReadVec3(data, "client_color", color, hasColor, errorMessage)) return false;
		if (hasColor) chart->SetClientColor(color);
		if (!ChartSupport::ReadVec3(data, "item_color", color, hasColor, errorMessage)) return false;
		if (hasColor) chart->SetItemColor(color);
		if (!ChartSupport::ReadVec3(data, "value_color", color, hasColor, errorMessage)) return false;
		if (hasColor) chart->SetValueColor(color);

		if (data.contains("line_num") || data.contains("item_strings") || data.contains("value_variables")) {
			int lineCount = data.value("line_num", 0);
			if (lineCount == 0 && data.contains("item_strings") && data["item_strings"].is_array()) {
				lineCount = (int)data["item_strings"].size();
			}
			if (lineCount == 0 && data.contains("value_variables") && data["value_variables"].is_array()) {
				lineCount = (int)data["value_variables"].size();
			}
			if (lineCount > 0) chart->SetLineNum(lineCount);
		}
		if (data.contains("item_strings")) {
			if (!data["item_strings"].is_array()) {
				errorMessage = "item_strings must be an array";
				return false;
			}
			for (size_t index = 0; index < data["item_strings"].size(); ++index) {
				const std::string item = _G(data["item_strings"][index]);
				chart->SetItemString((int)index, item.c_str());
			}
		}
		if (data.contains("decimal_num")) chart->SetDecimalNum(data["decimal_num"].get<int>());
	}
	catch (const json::exception& e) {
		errorMessage = std::string("table config JSON error: ") + e.what();
		return false;
	}
	return ChartSupport::ApplyFont(chart, data, errorMessage) &&
		ChartSupport::BindTableVariables(chart, mainModule, data, errorMessage);
}


bool ChartObjectService::ApplyGrid(CGrid* chart, CVsModule* mainModule,
	const json& data, std::string& errorMessage) {
	errorMessage.clear();
	if (!chart) {
		errorMessage = "grid object is null";
		return false;
	}
	if (!RejectRename(data, "grid", errorMessage)) {
		return false;
	}

	float position[3] = { 0.0f, 0.0f, 0.0f };
	bool hasPosition = false;
	if (!ChartSupport::ReadVec3(data, "position", position, hasPosition, errorMessage)) return false;
	if (hasPosition) chart->SetPosition(position);
	float width = 0.0f;
	float height = 0.0f;
	bool hasSize = false;
	if (!ChartSupport::ReadSize(data, width, height, hasSize, errorMessage)) return false;
	if (hasSize) chart->SetSize(width, height);

	try {
		if (data.contains("style")) chart->SetStyle(data["style"].get<int>());
		float color[3] = { 0.0f, 0.0f, 0.0f };
		bool hasColor = false;
		if (!ChartSupport::ReadVec3(data, "value_color", color, hasColor, errorMessage)) return false;
		if (hasColor) chart->SetValueColor(color);
		if (!ChartSupport::ReadVec3(data, "back_color", color, hasColor, errorMessage)) return false;
		if (hasColor) chart->SetBackColor(color);
		if (!ChartSupport::ReadVec3(data, "back_color2", color, hasColor, errorMessage)) return false;
		if (hasColor) chart->SetBackColor2(color);

		int rows = data.value("rows", 0);
		int columns = data.value("cols", 0);
		if (data.contains("value_variables")) {
			if (!data["value_variables"].is_array()) {
				errorMessage = "value_variables must be a 2d array";
				return false;
			}
			if (rows == 0) rows = (int)data["value_variables"].size();
		}

		if (data.contains("col_widths")) {
			if (!data["col_widths"].is_array()) {
				errorMessage = "col_widths must be an array";
				return false;
			}
			if (columns == 0) columns = (int)data["col_widths"].size();
			for (size_t index = 0; index < data["col_widths"].size(); ++index) {
				chart->SetColWidth((int)index, data["col_widths"][index].get<float>());
			}
		}
		if (columns == 0 && data.contains("value_variables") &&
			!data["value_variables"].empty() && data["value_variables"][0].is_array()) {
			columns = (int)data["value_variables"][0].size();
		}
		if (rows > 0 && columns > 0) chart->SetGridNum(rows, columns);
		if (data.contains("decimal_num")) chart->SetDecimalNum(data["decimal_num"].get<int>());
	}
	catch (const json::exception& e) {
		errorMessage = std::string("grid config JSON error: ") + e.what();
		return false;
	}
	return ChartSupport::ApplyFont(chart, data, errorMessage) &&
		ChartSupport::BindGridVariables(chart, mainModule, data, errorMessage);
}


bool ChartObjectService::ApplyDashBoard(CDashBoard* chart, CVsModule* mainModule,
	const json& data, std::string& errorMessage) {
	return ApplyRangedChart(chart, mainModule, data, "dashBoard", errorMessage);
}
