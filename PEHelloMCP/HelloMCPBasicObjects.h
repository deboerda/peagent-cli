#pragma once

#include "HelloMCPChartSupport.h"
#include "HelloMCPObjectAssets.h"


namespace hello_mcp { namespace detail {

class BasicObjectService {
public:
	static json CreateVariable(const json& config);
	static json CreateButton(const json& config);
	static json CreateImage(const json& config);
	static json CreateBody(const json& config);
	static json CreateControlPoint(const json& config);
	static json CreateSpinner(const json& config);

	static bool ApplyButton(CVsButton* button, const json& data, bool allowRename,
		std::string& errorMessage);
	static bool ApplyImage(CVsImage* image, const json& data, bool allowRename,
		std::string& errorMessage);
};

} }
