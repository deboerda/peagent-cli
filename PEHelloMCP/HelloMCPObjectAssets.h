#pragma once

#include "HelloMCPCommon.h"


namespace hello_mcp { namespace detail {

class ObjectAssetService {
public:
	static bool ValidateBitmapPath(const std::string& assetPath, const std::string& assetLabel,
		std::string& errorMessage);
	static std::string ImportImage(const std::string& sourcePath, std::string& errorMessage);
};

} }
