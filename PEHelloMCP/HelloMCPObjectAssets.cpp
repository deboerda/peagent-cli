#include "stdafx.h"
#include "HelloMCPObjectAssets.h"


bool hello_mcp::detail::ObjectAssetService::ValidateBitmapPath(const std::string& assetPath,
	const std::string& assetLabel, std::string& errorMessage) {
	fs::path filePath(assetPath);
	const std::string extension = filePath.extension().string();
	if (extension != ".jpg" && extension != ".png" && extension != ".bmp") {
		errorMessage = "unsupported " + assetLabel + " extension";
		return false;
	}

	try {
		if (!fs::exists(filePath)) {
			errorMessage = assetLabel + " file does not exist";
			return false;
		}
		if (!fs::is_regular_file(filePath)) {
			errorMessage = assetLabel + " path is not a regular file";
			return false;
		}
	}
	catch (const fs::filesystem_error&) {
		errorMessage = "invalid " + assetLabel + " path";
		return false;
	}
	return true;
}


std::string hello_mcp::detail::ObjectAssetService::ImportImage(const std::string& sourcePath,
	std::string& errorMessage) {
	errorMessage.clear();
	char workDir[MAX_PATH] = { 0 };
	VsGetWorkDirectory(workDir);
	if (strlen(workDir) == 0) {
		errorMessage = "project work directory not available";
		return "";
	}

	const fs::path imagesDir = fs::path(workDir) / "main/image";
	try {
		if (!fs::exists(imagesDir)) {
			fs::create_directories(imagesDir);
		}
	}
	catch (const fs::filesystem_error& e) {
		errorMessage = "failed to create images directory: " + std::string(e.what());
		return "";
	}

	const fs::path sourceFile(sourcePath);
	std::string baseName = sourceFile.filename().string();
	fs::path destination = imagesDir / baseName;
	try {
		if (fs::exists(destination) && fs::file_size(sourceFile) != fs::file_size(destination)) {
			const std::string stem = sourceFile.stem().string();
			const std::string extension = sourceFile.extension().string();
			int counter = 1;
			do {
				baseName = stem + "_" + std::to_string(counter++) + extension;
				destination = imagesDir / baseName;
			} while (fs::exists(destination));
		}
		fs::copy_file(sourceFile, destination, fs::copy_options::skip_existing);
	}
	catch (const fs::filesystem_error& e) {
		errorMessage = "failed to copy image: " + std::string(e.what());
		return "";
	}

	return "image/" + baseName;
}
