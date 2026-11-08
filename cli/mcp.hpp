// mcp.hpp — `interviewprep mcp`, the server a model connects to.
#pragma once

#include <string>

#include "../core/engine.hpp"

namespace interviewprep {

/*
Serves the Model Context Protocol on stdin and stdout until stdin closes.
The profile folder sits beside `statePath`. A non-empty `fixedNow` pins the
clock, which is how the tests run.
*/
int runMcpServer(Engine& engine, const std::string& statePath, const std::string& fixedNow);

}  // namespace interviewprep
