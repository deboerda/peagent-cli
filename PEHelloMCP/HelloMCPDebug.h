#pragma once

#include "HelloMCPCommon.h"
#include <string>

namespace HelloMCPDebug {

/// Start the debug player for the current project.
/// Creates PEDATA shared memory from the current engine scene,
/// then launches start.exe /debug.
json StartCurrentProject(const std::string& requestId);

/// Arm one-shot capture of the exact native toolbar command route.
json BeginDebugCommandCapture(const std::string& requestId);

/// Stop the debug player.
/// Terminates the start.exe process and releases any held resources.
json StopDebugSession(const std::string& requestId);

}  // namespace HelloMCPDebug