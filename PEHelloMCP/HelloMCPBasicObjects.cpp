#include "stdafx.h"
#include "HelloMCPBasicObjects.h"


using hello_mcp::detail::BasicObjectService;
using hello_mcp::detail::ChartSupport;
using hello_mcp::detail::ObjectAssetService;


namespace {
	bool ReadRequiredName(const json& config, const char* objectType,
		std::string& engineName, std::string& errorMessage) {
		engineName.clear();
		errorMessage.clear();
		try {
			if (!config.is_object()) {
				errorMessage = "config must be an object";
				return false;
			}
			engineName = _G(config.at("name").get<std::string>());
		}
		catch (const json::exception& e) {
			errorMessage = std::string(objectType) + " config JSON error: " + e.what();
			return false;
		}
		if (engineName.empty()) {
			errorMessage = std::string(objectType) + " name cannot be empty";
			return false;
		}
		return true;
	}

	CVsModule* GetMainModule(std::string& errorMessage) {
		CVsModule* mainModule = (CVsModule*)FxPluginMgr_GetData(NULL, "Ptr_MainVsModule");
		if (!mainModule) {
			errorMessage = "main module not found";
		}
		return mainModule;
	}

	bool ReadOptionalNodeKey(const json& config, VS_KEY& key, bool& hasKey,
		std::string& errorMessage) {
		hasKey = false;
		key = -1;
		try {
			if (!config.contains("node_path")) {
				return true;
			}
			const std::string utf8Path = config["node_path"].get<std::string>();
			if (utf8Path.empty()) {
				return true;
			}
			const std::string enginePath = _G(utf8Path);
			key = VsFieldExists(enginePath.c_str());
			if (key == -1) {
				errorMessage = "node path does not exist: " + utf8Path;
				return false;
			}
			hasKey = true;
			return true;
		}
		catch (const json::exception& e) {
			errorMessage = std::string("node_path JSON error: ") + e.what();
			return false;
		}
	}

	int ResolveVariableType(const std::string& typeName) {
		if (typeName == "string") return VAR_STRING;
		if (typeName == "int") return VAR_INT;
		if (typeName == "float") return VAR_FLOAT;
		if (typeName == "long") return VAR_LONG;
		if (typeName == "double") return VAR_DOUBLE;
		if (typeName == "bool") return VAR_BOOL;
		if (typeName == "mutex") return VAR_MUTEX;
		return -1;
	}

	bool ValidateBitmapField(const json& data, const char* fieldName,
		const char* label, std::string& errorMessage) {
		if (!data.contains(fieldName)) {
			return true;
		}
		try {
			const std::string path = _G(data[fieldName].get<std::string>());
			return path.empty() || ObjectAssetService::ValidateBitmapPath(path, label, errorMessage);
		}
		catch (const json::exception& e) {
			errorMessage = std::string(fieldName) + " JSON error: " + e.what();
			return false;
		}
	}

	bool ApplyButtonBitmap(CVsButton* button, const json& data,
		const char* imageField, const char* maskField, int state,
		std::string& errorMessage) {
		if (!data.contains(imageField)) {
			return true;
		}
		const std::string imagePath = _G(data[imageField].get<std::string>());
		if (imagePath.empty()) {
			return true;
		}
		const std::string relativeImage = ObjectAssetService::ImportImage(imagePath, errorMessage);
		if (relativeImage.empty()) {
			return false;
		}

		const std::string maskPath = data.contains(maskField)
			? _G(data[maskField].get<std::string>()) : std::string();
		if (maskPath.empty()) {
			button->SetFile(relativeImage.c_str(), "", state, TRUE);
			return true;
		}
		const std::string relativeMask = ObjectAssetService::ImportImage(maskPath, errorMessage);
		if (relativeMask.empty()) {
			return false;
		}
		button->SetFile(relativeImage.c_str(), relativeMask.c_str(), state, TRUE);
		return true;
	}
}


bool BasicObjectService::ApplyButton(CVsButton* button, const json& data, bool allowRename,
	std::string& errorMessage) {
	errorMessage.clear();
	if (!button) {
		errorMessage = "button object is null";
		return false;
	}

	try {
		if (data.contains("name")) {
			if (!allowRename) {
				errorMessage = "button name cannot be modified";
				return false;
			}
			const std::string name = _G(data["name"].get<std::string>());
			if (name.empty()) {
				errorMessage = "button name cannot be empty";
				return false;
			}
			button->SetName(name.c_str());
		}

		if (!ValidateBitmapField(data, "path_normal", "image", errorMessage) ||
			!ValidateBitmapField(data, "path_normal_mask", "image", errorMessage) ||
			!ValidateBitmapField(data, "path_hover", "image", errorMessage) ||
			!ValidateBitmapField(data, "path_hover_mask", "image", errorMessage) ||
			!ValidateBitmapField(data, "path_pressed", "image", errorMessage) ||
			!ValidateBitmapField(data, "path_pressed_mask", "image", errorMessage)) {
			return false;
		}
		if (!ApplyButtonBitmap(button, data, "path_normal", "path_normal_mask", BUT_NORMAL, errorMessage) ||
			!ApplyButtonBitmap(button, data, "path_hover", "path_hover_mask", BUT_HOVER, errorMessage) ||
			!ApplyButtonBitmap(button, data, "path_pressed", "path_pressed_mask", BUT_PUSH, errorMessage)) {
			return false;
		}

		if (data.contains("text")) {
			const std::string text = _G(data["text"].get<std::string>());
			button->SetCaption(text.c_str());
		}
		float position[3] = { 0.0f, 0.0f, 0.0f };
		bool hasPosition = false;
		if (!ChartSupport::ReadVec3(data, "position", position, hasPosition, errorMessage)) return false;
		if (hasPosition) button->SetPosition(position[0], position[1], position[2], BUT_ALL);

		float width = 0.0f;
		float height = 0.0f;
		bool hasSize = false;
		if (!ChartSupport::ReadSize(data, width, height, hasSize, errorMessage)) return false;
		if (hasSize) {
			button->SetSize(width, height, BUT_ALL);
		}
		else {
			button->GetSize(width, height, BUT_ALL);
			button->SetSize(width, height, BUT_ALL);
		}
	}
	catch (const json::exception& e) {
		errorMessage = std::string("button config JSON error: ") + e.what();
		return false;
	}
	button->Show(TRUE);
	return true;
}


bool BasicObjectService::ApplyImage(CVsImage* image, const json& data, bool allowRename,
	std::string& errorMessage) {
	errorMessage.clear();
	if (!image) {
		errorMessage = "image object is null";
		return false;
	}

	try {
		if (data.contains("name")) {
			if (!allowRename) {
				errorMessage = "image name cannot be modified";
				return false;
			}
			const std::string name = _G(data["name"].get<std::string>());
			if (name.empty()) {
				errorMessage = "image name cannot be empty";
				return false;
			}
			image->SetName(name.c_str());
		}

		if (!ValidateBitmapField(data, "img_path", "image", errorMessage) ||
			!ValidateBitmapField(data, "mask_path", "mask", errorMessage)) {
			return false;
		}
		if (data.contains("img_path")) {
			const std::string sourcePath = _G(data["img_path"].get<std::string>());
			if (!sourcePath.empty()) {
				const std::string relativePath = ObjectAssetService::ImportImage(sourcePath, errorMessage);
				if (relativePath.empty()) return false;
				image->SetFileName(relativePath.c_str());
			}
		}
		if (data.contains("mask_path")) {
			const std::string sourcePath = _G(data["mask_path"].get<std::string>());
			if (!sourcePath.empty()) {
				const std::string relativePath = ObjectAssetService::ImportImage(sourcePath, errorMessage);
				if (relativePath.empty()) return false;
				image->SetMaskFileName(relativePath.c_str());
			}
		}

		float position[3] = { 0.0f, 0.0f, 0.0f };
		bool hasPosition = false;
		if (!ChartSupport::ReadVec3(data, "position", position, hasPosition, errorMessage)) return false;
		if (hasPosition) image->SetPosition(position);
		float width = 0.0f;
		float height = 0.0f;
		bool hasSize = false;
		if (!ChartSupport::ReadSize(data, width, height, hasSize, errorMessage)) return false;
		if (hasSize) image->SetSize(width, height);
	}
	catch (const json::exception& e) {
		errorMessage = std::string("image config JSON error: ") + e.what();
		return false;
	}
	return true;
}


json BasicObjectService::CreateVariable(const json& config) {
	std::string name;
	std::string errorMessage;
	if (!ReadRequiredName(config, "variable", name, errorMessage)) {
		return make_result(false, "variable", "", errorMessage);
	}
	CVsModule* mainModule = GetMainModule(errorMessage);
	if (!mainModule) return make_result(false, "variable", _U(name), errorMessage);
	if (mainModule->FindVariable(name.c_str())) {
		return make_result(false, "variable", _U(name), "variable already exists");
	}

	try {
		const std::string typeName = config.value("type", std::string("float"));
		const int variableType = ResolveVariableType(typeName);
		if (variableType < 0) {
			return make_result(false, "variable", _U(name), "unsupported variable type: " + typeName);
		}
		const int dimensionCount = config.value("arr_size", 0);
		const std::vector<int> dimensions = config.value("dims", std::vector<int>());
		if (dimensionCount < 0 || static_cast<size_t>(dimensionCount) > dimensions.size()) {
			return make_result(false, "variable", _U(name), "arr_size must not exceed dims length");
		}
		for (int index = 0; index < dimensionCount; ++index) {
			if (dimensions[index] <= 0) {
				return make_result(false, "variable", _U(name), "active dimensions must be greater than zero");
			}
		}

		std::unique_ptr<CVsVariable> variable(ScrCreateMultiArray(name.c_str(),
			dimensions.empty() ? NULL : dimensions.data(), dimensionCount, variableType, mainModule));
		if (!variable) {
			return make_result(false, "variable", _U(name), "failed to create variable");
		}
		mainModule->AddVariable(variable.get());
		FX_CMD("ProjectMgr", "CreateUIObject", (FX_PTR)mainModule, (FX_PTR)variable.get());
		variable.release();
		return make_result(true, "variable", _U(name), "variable created");
	}
	catch (const json::exception& e) {
		return make_result(false, "variable", _U(name),
			std::string("variable config JSON error: ") + e.what());
	}
}


json BasicObjectService::CreateButton(const json& config) {
	std::string name;
	std::string errorMessage;
	if (!ReadRequiredName(config, "button", name, errorMessage)) {
		return make_result(false, "button", "", errorMessage);
	}
	CVsModule* mainModule = GetMainModule(errorMessage);
	if (!mainModule) return make_result(false, "button", _U(name), errorMessage);
	if (mainModule->FindCtrlpt(name.c_str(), MVO_OBJ_TYPE_BUT)) {
		return make_result(false, "button", _U(name), "button already exists");
	}

	std::unique_ptr<CVsButton> button(new CVsButton(mainModule));
	if (!ApplyButton(button.get(), config, true, errorMessage)) {
		return make_result(false, "button", _U(name), errorMessage);
	}
	mainModule->OpController()->AddCtrlPoint(button.get());
	button->Create(FALSE);
	button->SetInitialEnableStatus(FALSE);
	button->Enable();
	button->SetTransparency(0.99, BUT_ALL);
	button->Show(TRUE);
	FX_CMD("ProjectMgr", "CreateUIObject", (FX_PTR)mainModule, (FX_PTR)button.get());
	button.release();
	return make_result(true, "button", _U(name), "button created");
}


json BasicObjectService::CreateImage(const json& config) {
	std::string name;
	std::string errorMessage;
	if (!ReadRequiredName(config, "image", name, errorMessage)) {
		return make_result(false, "image", "", errorMessage);
	}
	CVsModule* mainModule = GetMainModule(errorMessage);
	if (!mainModule) return make_result(false, "image", _U(name), errorMessage);
	if (mainModule->FindImage(name.c_str())) {
		return make_result(false, "image", _U(name), "image already exists");
	}

	std::unique_ptr<CVsImage> image(new CVsImage(mainModule));
	image->SetName(name.c_str());
	image->SetTransparency(0.99);
	if (!ApplyImage(image.get(), config, true, errorMessage)) {
		return make_result(false, "image", _U(name), errorMessage);
	}

	char modulePath[4096] = { 0 };
	mainModule->GetPath(modulePath, "/");
	const std::string imageContainer = std::string("/scene/modules/") + modulePath + "/images";
	VsOpenField(imageContainer.c_str());
	const VS_KEY key = VsOpenField(name.c_str());
	VsCloseField();
	VsCloseField();
	image->SetPathName(imageContainer.c_str());
	image->SetKey(key);

	mainModule->AddImage(image.get());
	image->Create();
	FX_CMD("ProjectMgr", "CreateUIObject", (FX_PTR)mainModule, (FX_PTR)image.get());
	image.release();
	return make_result(true, "image", _U(name), "image created");
}


json BasicObjectService::CreateBody(const json& config) {
	std::string name;
	std::string errorMessage;
	if (!ReadRequiredName(config, "body", name, errorMessage)) {
		return make_result(false, "body", "", errorMessage);
	}
	CVsModule* mainModule = GetMainModule(errorMessage);
	if (!mainModule) return make_result(false, "body", _U(name), errorMessage);
	if (mainModule->FindBody(name.c_str())) {
		return make_result(false, "body", _U(name), "body already exists");
	}
	VS_KEY key = -1;
	bool hasKey = false;
	if (!ReadOptionalNodeKey(config, key, hasKey, errorMessage)) {
		return make_result(false, "body", _U(name), errorMessage);
	}

	std::unique_ptr<CVsBody> body(new CVsBody(mainModule));
	body->SetName(name.c_str());
	if (hasKey) {
		body->SetKey(key);
		float matrix[16] = { 0.0f };
		VsShowTransformByKey(key, matrix);
		body->UpdateByMatrix(matrix);
	}
	mainModule->AddBody(body.get());
	FX_CMD("ProjectMgr", "CreateUIObject", (FX_PTR)mainModule, (FX_PTR)body.get());
	body.release();
	return make_result(true, "body", _U(name), "body created");
}


json BasicObjectService::CreateControlPoint(const json& config) {
	std::string name;
	std::string errorMessage;
	if (!ReadRequiredName(config, "control_point", name, errorMessage)) {
		return make_result(false, "control_point", "", errorMessage);
	}
	CVsModule* mainModule = GetMainModule(errorMessage);
	if (!mainModule) return make_result(false, "control_point", _U(name), errorMessage);
	if (mainModule->FindCtrlpt(name.c_str(), MVO_OBJ_TYPE_CTRLPT)) {
		return make_result(false, "control_point", _U(name), "control point already exists");
	}
	VS_KEY key = -1;
	bool hasKey = false;
	if (!ReadOptionalNodeKey(config, key, hasKey, errorMessage)) {
		return make_result(false, "control_point", _U(name), errorMessage);
	}

	std::unique_ptr<COpCtrlPoint> controlPoint(new COpCtrlPoint(mainModule));
	controlPoint->SetName(name.c_str());
	if (hasKey) controlPoint->AddKey(key);
	mainModule->OpController()->AddCtrlPoint(controlPoint.get());
	FX_CMD("ProjectMgr", "CreateUIObject", (FX_PTR)mainModule, (FX_PTR)controlPoint.get());
	controlPoint.release();
	return make_result(true, "control_point", _U(name), "control point created");
}


json BasicObjectService::CreateSpinner(const json& config) {
	std::string name;
	std::string errorMessage;
	if (!ReadRequiredName(config, "spinner", name, errorMessage)) {
		return make_result(false, "spinner", "", errorMessage);
	}
	CVsModule* mainModule = GetMainModule(errorMessage);
	if (!mainModule) return make_result(false, "spinner", _U(name), errorMessage);
	if (mainModule->FindVariable(name.c_str())) {
		return make_result(false, "spinner", _U(name), "spinner already exists");
	}
	VS_KEY key = -1;
	bool hasKey = false;
	if (!ReadOptionalNodeKey(config, key, hasKey, errorMessage)) {
		return make_result(false, "spinner", _U(name), errorMessage);
	}

	try {
		const std::vector<float> rotationAxis = config.value("rotation_axis",
			std::vector<float>{ 0.0f, 0.0f, 1.0f });
		if (rotationAxis.size() != 3) {
			return make_result(false, "spinner", _U(name),
				"rotation_axis must contain exactly 3 numbers");
		}
		std::unique_ptr<CVsScrew> spinner(new CVsScrew);
		spinner->SetName(name.c_str());
		if (hasKey) spinner->SetKey(key);
		const float axis[6] = { 0.0f, 0.0f, 0.0f,
			rotationAxis[0], rotationAxis[1], rotationAxis[2] };
		spinner->SetAxis(axis);
		mainModule->AddVariable(spinner.get());
		FX_CMD("ProjectMgr", "CreateUIObject", (FX_PTR)mainModule, (FX_PTR)spinner.get());
		spinner.release();
		return make_result(true, "spinner", _U(name), "spinner created");
	}
	catch (const json::exception& e) {
		return make_result(false, "spinner", _U(name),
			std::string("spinner config JSON error: ") + e.what());
	}
}
