#include "stdafx.h"
#include "HelloMCPOutputExecutor.h"
#include "HelloMCPOutput.h"


json OutputExecutor::ProcessOutputQuery(const std::string& buffer) {
	hello_mcp::detail::OutputService service;
	return service.ProcessOutputQuery(buffer);
}
