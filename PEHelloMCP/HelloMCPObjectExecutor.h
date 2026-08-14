#pragma once

#include "HelloMCPCommon.h"


class ObjectExecutor {
protected:
	json ProcessObjectCreate(const std::string& buffer);
	json ProcessObjectQuery(const std::string& buffer);
	json ProcessObjectModify(const std::string& buffer);
};
