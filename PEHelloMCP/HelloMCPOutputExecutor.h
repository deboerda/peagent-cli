#pragma once

#include "HelloMCPCommon.h"


class OutputExecutor {
protected:
	json ProcessOutputQuery(const std::string& buffer);
};
