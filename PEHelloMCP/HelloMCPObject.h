#pragma once

#include "HelloMCPCommon.h"


namespace hello_mcp { namespace detail {

class ObjectService {
public:
	json ProcessObjectCreate(const std::string& buffer);
	json ProcessObjectQuery(const std::string& buffer);
	json ProcessObjectModify(const std::string& buffer);
};

} }
