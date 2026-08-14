#include "stdafx.h"
#include "HelloMCPObjectExecutor.h"
#include "HelloMCPObject.h"
#include "HelloMCPBasicObjects.h"
#include "HelloMCPChartObjects.h"


using hello_mcp::detail::BasicObjectService;
using hello_mcp::detail::ChartObjectService;
using hello_mcp::detail::ObjectService;


namespace {
	using CreateHandler = json (*)(const json& config);
	using ExportHandler = json (*)(std::string& errorMessage);
	using ModifyHandler = bool (*)(CMvoObject* object, CVsModule* mainModule,
		const json& changes, std::string& errorMessage);

	struct ObjectTypeDescriptor {
		const char* name;
		int typeId;
		bool checkDuplicateName;
		CreateHandler create;
		ExportHandler exportObjects;
		ModifyHandler modify;
	};

	CVsModule* GetMainModule(std::string& errorMessage) {
		errorMessage.clear();
		CVsModule* mainModule = (CVsModule*)FxPluginMgr_GetData(NULL, "Ptr_MainVsModule");
		if (!mainModule) {
			errorMessage = "main module not found";
		}
		return mainModule;
	}

	json CreateVis(const json& config) {
		std::string visPath;
		try {
			visPath = _G(config.at("path").get<std::string>());
		}
		catch (const json::exception& e) {
			return make_result(false, "vis", "", std::string("vis config JSON error: ") + e.what());
		}
		if (visPath.empty()) {
			return make_result(false, "vis", "", "vis path cannot be empty");
		}

		const fs::path filePath(visPath);
		if (filePath.extension() != ".vis") {
			return make_result(false, "vis", _U(visPath), "file extension must be .vis");
		}
		try {
			if (!fs::exists(filePath)) {
				return make_result(false, "vis", _U(visPath), "vis file does not exist");
			}
			if (!fs::is_regular_file(filePath)) {
				return make_result(false, "vis", _U(visPath), "path is not a regular file");
			}
		}
		catch (const fs::filesystem_error&) {
			return make_result(false, "vis", _U(visPath), "invalid file path");
		}

		std::string errorMessage;
		CVsModule* mainModule = GetMainModule(errorMessage);
		if (!mainModule) {
			return make_result(false, "vis", _U(visPath), errorMessage);
		}
		FX_CMD("ProjectMgr", "CreateUIObject", (FX_PTR)mainModule,
			(FX_PTR)visPath.c_str(), (FX_PTR)"model");
		return make_result(true, "vis", _U(visPath), "vis created");
	}

	json ExportVisUnavailable(std::string& errorMessage) {
		errorMessage = "vis imports are scene resources and cannot be enumerated as module objects";
		return json();
	}

	json ExportImages(std::string& errorMessage) {
		errorMessage.clear();
		CVsModule* mainModule = GetMainModule(errorMessage);
		if (!mainModule) {
			return json();
		}

		json objectsJson = json::array();
		MvoObjectList objects;
		mainModule->FindObjects(MVO_OBJ_TYPE_IMAGE, objects, false);
		for (CMvoObject* object : objects) {
			CVsImage* image = (CVsImage*)object;
			float position[3] = { 0.0f, 0.0f, 0.0f };
			float width = 0.0f;
			float height = 0.0f;
			const char* name = image->Name();
			const char* pathName = image->PathName();
			const char* fileName = image->FileName();
			const char* maskName = image->MaskFileName();
			const std::string nodePath = std::string(pathName ? pathName : "") + "/" +
				std::string(name ? name : "");
			image->GetPosition(position);
			image->GetSize(width, height);
			objectsJson.push_back({
				{"obj_type", "image"},
				{"name", _U(name ? name : "")},
				{"node_path", _U(nodePath)},
				{"img_path", _U(fileName ? fileName : "")},
				{"mask_path", _U(maskName ? maskName : "")},
				{"width", width},
				{"height", height},
				{"nodePath", _U(nodePath)},
				{"imagePath", _U(fileName ? fileName : "")},
				{"maskPath", _U(maskName ? maskName : "")},
				{"position", {position[0], position[1], position[2]}}
			});
		}
		return objectsJson;
	}

	json ExportButtons(std::string& errorMessage) {
		errorMessage.clear();
		CVsModule* mainModule = GetMainModule(errorMessage);
		if (!mainModule) return json();

		json objectsJson = json::array();
		MvoObjectList objects;
		mainModule->FindObjects(MVO_OBJ_TYPE_BUT, objects, false);
		for (CMvoObject* object : objects) {
			CVsButton* button = (CVsButton*)object;
			float x = 0.0f;
			float y = 0.0f;
			float z = 0.0f;
			float width = 0.0f;
			float height = 0.0f;
			button->GetPosition(x, y, z, BUT_ALL);
			button->GetSize(width, height, BUT_ALL);
			const char* caption = button->Caption();
			objectsJson.push_back({
				{"obj_type", "button"},
				{"name", _U(button->Name() ? button->Name() : "")},
				{"text", _U(caption ? caption : "")},
				{"position", {x, y, z}},
				{"width", width},
				{"height", height}
			});
		}
		return objectsJson;
	}

	const char* VariableTypeName(DWORD type) {
		switch (type) {
		case VAR_STRING: return "string";
		case VAR_INT: return "int";
		case VAR_BOOL: return "bool";
		case VAR_FLOAT: return "float";
		case VAR_LONG: return "long";
		case VAR_DOUBLE: return "double";
		case VAR_MUTEX: return "mutex";
		default: return "unknown";
		}
	}

	json ExportVariables(std::string& errorMessage) {
		errorMessage.clear();
		CVsModule* mainModule = GetMainModule(errorMessage);
		if (!mainModule) return json();

		json objectsJson = json::array();
		MvoObjectList objects;
		mainModule->FindObjects(MVO_OBJ_TYPE_VAR, objects, false);
		for (CMvoObject* object : objects) {
			CVsVariable* variable = (CVsVariable*)object;
			const DWORD variableType = variable->VarType();
			objectsJson.push_back({
				{"obj_type", "variable"},
				{"name", _U(variable->Name() ? variable->Name() : "")},
				{"type", VariableTypeName(variableType)},
				{"type_code", static_cast<unsigned long>(variableType)},
				{"is_array", variable->IsArray() != FALSE},
				{"size", variable->GetSize()}
			});
		}
		return objectsJson;
	}

	json ExportBodies(std::string& errorMessage) {
		errorMessage.clear();
		CVsModule* mainModule = GetMainModule(errorMessage);
		if (!mainModule) return json();

		json objectsJson = json::array();
		MvoObjectList objects;
		mainModule->FindObjects(MVO_OBJ_TYPE_BODY, objects, false);
		for (CMvoObject* object : objects) {
			CVsBody* body = (CVsBody*)object;
			objectsJson.push_back({
				{"obj_type", "body"},
				{"name", _U(body->Name() ? body->Name() : "")},
				{"node_key", static_cast<long long>(body->Key())}
			});
		}
		return objectsJson;
	}

	json ExportControlPoints(std::string& errorMessage) {
		errorMessage.clear();
		CVsModule* mainModule = GetMainModule(errorMessage);
		if (!mainModule) return json();

		json objectsJson = json::array();
		MvoObjectList objects;
		mainModule->FindObjects(MVO_OBJ_TYPE_CTRLPT, objects, false);
		for (CMvoObject* object : objects) {
			COpCtrlPoint* controlPoint = (COpCtrlPoint*)object;
			objectsJson.push_back({
				{"obj_type", "control_point"},
				{"name", _U(controlPoint->Name() ? controlPoint->Name() : "")},
				{"node_key", static_cast<long long>(controlPoint->Key())}
			});
		}
		return objectsJson;
	}

	json ExportSpinners(std::string& errorMessage) {
		errorMessage.clear();
		CVsModule* mainModule = GetMainModule(errorMessage);
		if (!mainModule) return json();

		json objectsJson = json::array();
		MvoObjectList objects;
		mainModule->FindObjects(MVO_OBJ_TYPE_SCREW, objects, false);
		for (CMvoObject* object : objects) {
			CVsScrew* spinner = (CVsScrew*)object;
			float axis[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
			spinner->GetAxis(axis);
			objectsJson.push_back({
				{"obj_type", "spinner"},
				{"name", _U(spinner->Name() ? spinner->Name() : "")},
				{"node_key", static_cast<long long>(spinner->Key())},
				{"rotation_axis", {axis[3], axis[4], axis[5]}}
			});
		}
		return objectsJson;
	}

	bool ModifyButton(CMvoObject* object, CVsModule*, const json& changes,
		std::string& errorMessage) {
		return BasicObjectService::ApplyButton((CVsButton*)object, changes, false, errorMessage);
	}

	bool ModifyImage(CMvoObject* object, CVsModule*, const json& changes,
		std::string& errorMessage) {
		return BasicObjectService::ApplyImage((CVsImage*)object, changes, false, errorMessage);
	}

	bool ModifyRemark(CMvoObject* object, CVsModule* mainModule, const json& changes,
		std::string& errorMessage) {
		return ChartObjectService::ApplyRemark((CRemark*)object, mainModule, changes, errorMessage);
	}

	bool ModifyCurve(CMvoObject* object, CVsModule* mainModule, const json& changes,
		std::string& errorMessage) {
		return ChartObjectService::ApplyCurve((CCurve*)object, mainModule, changes, errorMessage);
	}

	bool ModifyNumber(CMvoObject* object, CVsModule* mainModule, const json& changes,
		std::string& errorMessage) {
		return ChartObjectService::ApplyNumber((CNumber*)object, mainModule, changes, errorMessage);
	}

	bool ModifyProgress(CMvoObject* object, CVsModule* mainModule, const json& changes,
		std::string& errorMessage) {
		return ChartObjectService::ApplyProgress((CProgress*)object, mainModule, changes, errorMessage);
	}

	bool ModifyHistogram(CMvoObject* object, CVsModule* mainModule, const json& changes,
		std::string& errorMessage) {
		return ChartObjectService::ApplyHistogram((CHistogram*)object, mainModule, changes, errorMessage);
	}

	bool ModifyTable(CMvoObject* object, CVsModule* mainModule, const json& changes,
		std::string& errorMessage) {
		return ChartObjectService::ApplyTable((CTable*)object, mainModule, changes, errorMessage);
	}

	bool ModifyGrid(CMvoObject* object, CVsModule* mainModule, const json& changes,
		std::string& errorMessage) {
		return ChartObjectService::ApplyGrid((CGrid*)object, mainModule, changes, errorMessage);
	}

	bool ModifyDashBoard(CMvoObject* object, CVsModule* mainModule, const json& changes,
		std::string& errorMessage) {
		return ChartObjectService::ApplyDashBoard((CDashBoard*)object, mainModule, changes, errorMessage);
	}

	const ObjectTypeDescriptor* FindObjectType(const std::string& objectType) {
		static const ObjectTypeDescriptor descriptors[] = {
			{"vis", -1, false, &CreateVis, &ExportVisUnavailable, nullptr},
			{"image", MVO_OBJ_TYPE_IMAGE, true, &BasicObjectService::CreateImage,
				&ExportImages, &ModifyImage},
			{"button", MVO_OBJ_TYPE_BUT, true, &BasicObjectService::CreateButton,
				&ExportButtons, &ModifyButton},
			{"variable", MVO_OBJ_TYPE_VAR, true, &BasicObjectService::CreateVariable,
				&ExportVariables, nullptr},
			{"body", MVO_OBJ_TYPE_BODY, true, &BasicObjectService::CreateBody,
				&ExportBodies, nullptr},
			{"spinner", MVO_OBJ_TYPE_SCREW, true, &BasicObjectService::CreateSpinner,
				&ExportSpinners, nullptr},
			{"control_point", MVO_OBJ_TYPE_CTRLPT, true, &BasicObjectService::CreateControlPoint,
				&ExportControlPoints, nullptr},
			{"remark", MVO_OBJ_TYPE_REMARK, true, &ChartObjectService::CreateRemark,
				nullptr, &ModifyRemark},
			{"curve", MVO_OBJ_TYPE_CURVE, true, &ChartObjectService::CreateCurve,
				nullptr, &ModifyCurve},
			{"number", MVO_OBJ_TYPE_NUMBER, true, &ChartObjectService::CreateNumber,
				nullptr, &ModifyNumber},
			{"progress", MVO_OBJ_TYPE_PROGRESS, true, &ChartObjectService::CreateProgress,
				nullptr, &ModifyProgress},
			{"histogram", MVO_OBJ_TYPE_HISTOGRAM, true, &ChartObjectService::CreateHistogram,
				nullptr, &ModifyHistogram},
			{"table", MVO_OBJ_TYPE_TABLE, true, &ChartObjectService::CreateTable,
				nullptr, &ModifyTable},
			{"grid", MVO_OBJ_TYPE_GRID, true, &ChartObjectService::CreateGrid,
				nullptr, &ModifyGrid},
			{"dashBoard", MVO_OBJ_TYPE_DASHBOARD, true, &ChartObjectService::CreateDashBoard,
				nullptr, &ModifyDashBoard}
		};
		for (const ObjectTypeDescriptor& descriptor : descriptors) {
			if (objectType == descriptor.name) {
				return &descriptor;
			}
		}
		return nullptr;
	}

	CMvoObject* FindObjectByName(CVsModule* mainModule, const ObjectTypeDescriptor& descriptor,
		const std::string& name, std::string& errorMessage) {
		errorMessage.clear();
		if (!mainModule) {
			errorMessage = "main module not found";
			return NULL;
		}
		if (name.empty()) {
			errorMessage = "object name cannot be empty";
			return NULL;
		}

		const std::string engineName = _G(name);
		if (strcmp(descriptor.name, "body") == 0) {
			return mainModule->FindBody(engineName.c_str());
		}
		if (strcmp(descriptor.name, "spinner") == 0) {
			CVsVariable* variable = mainModule->FindVariable(engineName.c_str());
			return variable && variable->Type() == MVO_OBJ_TYPE_SCREW ? variable : NULL;
		}
		if (descriptor.typeId < 0) {
			errorMessage = "object type id mapping not implemented";
			return NULL;
		}
		return mainModule->FindObject(descriptor.typeId, engineName.c_str());
	}

	json CheckDuplicateBeforeCreate(const ObjectTypeDescriptor& descriptor, const json& config) {
		if (!descriptor.checkDuplicateName) {
			return json(nullptr);
		}
		if (!config.is_object() || !config.contains("name") || !config["name"].is_string()) {
			return make_result(false, descriptor.name, "", "name is required before duplicate check");
		}
		const std::string name = config["name"].get<std::string>();
		if (name.empty()) {
			return make_result(false, descriptor.name, "", "name is required before duplicate check");
		}

		std::string errorMessage;
		CVsModule* mainModule = GetMainModule(errorMessage);
		if (!mainModule) {
			return make_result(false, descriptor.name, name, errorMessage);
		}
		if (FindObjectByName(mainModule, descriptor, name, errorMessage)) {
			json response = make_result(false, descriptor.name, name, "object already exists");
			response["error"] = "object already exists";
			return response;
		}
		if (!errorMessage.empty()) {
			return make_result(false, descriptor.name, name, errorMessage);
		}
		return json(nullptr);
	}

	std::string SanitizeObjectLabel(const std::string& label) {
		std::string sanitized;
		sanitized.reserve(label.size());
		for (unsigned char character : label) {
			sanitized.push_back(std::isalnum(character) || character == '_' || character == '-'
				? static_cast<char>(character) : '_');
		}
		return sanitized.empty() ? "objects" : sanitized;
	}

	json MakeObjectQueryError(const std::string& objectType, const std::string& error) {
		return {
			{"success", false},
			{"obj_type", objectType},
			{"file_path", ""},
			{"count", 0},
			{"error", error}
		};
	}

	bool WriteObjectsToFile(const std::string& objectType, const json& objects,
		std::string& filePath, int& count, std::string& errorMessage) {
		count = objects.is_array() ? static_cast<int>(objects.size()) : 0;
		if (!objects.is_array()) {
			errorMessage = "object exporter must return a JSON array";
			return false;
		}

		try {
			const fs::path outputDirectory = fs::temp_directory_path() / "hello_mcp_objects";
			if (!fs::exists(outputDirectory)) {
				fs::create_directories(outputDirectory);
			}
			const std::string fileName = SanitizeObjectLabel(objectType) + "_objects_" +
				std::to_string(static_cast<long long>(std::time(nullptr))) + "_" +
				std::to_string(static_cast<unsigned long long>(GetTickCount64())) + ".json";
			const fs::path outputPath = outputDirectory / fileName;

			std::ofstream output(outputPath.string(), std::ios::out | std::ios::trunc);
			if (!output.is_open()) {
				errorMessage = "failed to open output file";
				return false;
			}
			output << std::setw(4) << objects;
			output.close();
			filePath = _U(outputPath.string());
			return true;
		}
		catch (const fs::filesystem_error& e) {
			errorMessage = std::string("object export path error: ") + e.what();
			return false;
		}
	}

	json ExportNamedObjects(const ObjectTypeDescriptor& descriptor, std::string& errorMessage) {
		errorMessage.clear();
		CVsModule* mainModule = GetMainModule(errorMessage);
		if (!mainModule) {
			return json();
		}

		json objectsJson = json::array();
		MvoObjectList objects;
		mainModule->FindObjects(descriptor.typeId, objects, false);
		for (CMvoObject* object : objects) {
			objectsJson.push_back({
				{"obj_type", descriptor.name},
				{"name", _U(object && object->Name() ? object->Name() : "")}
			});
		}
		return objectsJson;
	}

	json QueryObjects(const std::string& objectType) {
		const ObjectTypeDescriptor* descriptor = FindObjectType(objectType);
		if (!descriptor) {
			return MakeObjectQueryError(objectType, "unknown object type");
		}

		std::string errorMessage;
		json objects = descriptor->exportObjects
			? descriptor->exportObjects(errorMessage)
			: ExportNamedObjects(*descriptor, errorMessage);
		if (!errorMessage.empty() && !objects.is_array()) {
			return MakeObjectQueryError(objectType, errorMessage);
		}

		std::string filePath;
		int count = 0;
		if (!WriteObjectsToFile(objectType, objects, filePath, count, errorMessage)) {
			return MakeObjectQueryError(objectType,
				errorMessage.empty() ? "object export failed" : errorMessage);
		}
		return {
			{"success", true},
			{"obj_type", objectType},
			{"file_path", filePath},
			{"count", count},
			{"message", "object query export ok"}
		};
	}

	json ModifyObject(const std::string& objectType, const std::string& name,
		const json& changes) {
		const ObjectTypeDescriptor* descriptor = FindObjectType(objectType);
		if (!descriptor) {
			return make_result(false, objectType, name, "unknown object type");
		}
		if (name.empty()) {
			return make_result(false, objectType, "", "object name cannot be empty");
		}
		if (!changes.is_object() || changes.empty()) {
			return make_result(false, objectType, name, "changes must be a non-empty object");
		}
		if (!descriptor->modify) {
			json response = make_result(false, objectType, name,
				"object modify not implemented for " + objectType);
			response["changes"] = changes;
			return response;
		}

		std::string errorMessage;
		CVsModule* mainModule = GetMainModule(errorMessage);
		CMvoObject* existing = mainModule
			? FindObjectByName(mainModule, *descriptor, name, errorMessage) : NULL;
		if (!existing) {
			json response = make_result(false, objectType, name,
				errorMessage.empty() ? "object not found" : errorMessage);
			response["changes"] = changes;
			return response;
		}
		if (!descriptor->modify(existing, mainModule, changes, errorMessage)) {
			json response = make_result(false, objectType, name,
				errorMessage.empty() ? "object modify failed" : errorMessage);
			response["changes"] = changes;
			return response;
		}
		return {
			{"success", true},
			{"obj_type", objectType},
			{"name", name},
			{"changes", changes},
			{"message", "object modify ok"}
		};
	}
}


json ObjectService::ProcessObjectCreate(const std::string& buffer) {
	try {
		const json data = json::parse(buffer);
		if (!data.contains("obj_type")) {
			return make_result(false, "", "", "lack of obj_type");
		}
		const std::string objectType = data["obj_type"].get<std::string>();
		const ObjectTypeDescriptor* descriptor = FindObjectType(objectType);
		if (!descriptor || !descriptor->create) {
			return make_result(false, objectType, "", "unknown object type");
		}
		if (!data.contains("config")) {
			return make_result(false, objectType, "", "lack of config");
		}
		const json& config = data["config"];
		const json duplicateResult = CheckDuplicateBeforeCreate(*descriptor, config);
		if (!duplicateResult.is_null()) {
			return duplicateResult;
		}
		return descriptor->create(config);
	}
	catch (const json::exception& e) {
		return make_result(false, "", "", std::string("JSON error: ") + e.what());
	}
}


json ObjectService::ProcessObjectQuery(const std::string& buffer) {
	try {
		const json data = json::parse(buffer);
		if (!data.contains("obj_type")) {
			return make_result(false, "", "", "lack of obj_type");
		}
		return QueryObjects(data["obj_type"].get<std::string>());
	}
	catch (const json::exception& e) {
		return make_result(false, "", "", std::string("JSON error: ") + e.what());
	}
}


json ObjectService::ProcessObjectModify(const std::string& buffer) {
	try {
		const json data = json::parse(buffer);
		if (!data.contains("obj_type")) {
			return make_result(false, "", "", "lack of obj_type");
		}
		const std::string objectType = data["obj_type"].get<std::string>();
		if (!data.contains("name")) {
			return make_result(false, objectType, "", "lack of name");
		}
		if (!data.contains("changes") || !data["changes"].is_object()) {
			return make_result(false, objectType, data.value("name", std::string()),
				"lack of changes object");
		}
		return ModifyObject(objectType, data["name"].get<std::string>(), data["changes"]);
	}
	catch (const json::exception& e) {
		return make_result(false, "", "", std::string("JSON error: ") + e.what());
	}
}


json ObjectExecutor::ProcessObjectCreate(const std::string& buffer) {
	ObjectService service;
	return service.ProcessObjectCreate(buffer);
}


json ObjectExecutor::ProcessObjectQuery(const std::string& buffer) {
	ObjectService service;
	return service.ProcessObjectQuery(buffer);
}


json ObjectExecutor::ProcessObjectModify(const std::string& buffer) {
	ObjectService service;
	return service.ProcessObjectModify(buffer);
}
