// mcp.hpp — `interviewprep mcp`, the server a model connects to.
#pragma once

#include <string>

#include "../core/engine.hpp"

using namespace std;

namespace interviewprep {

/*
Serves the Model Context Protocol on stdin and stdout until stdin closes.
The profile folder sits beside `statePath`. A non-empty `fixedNow` pins the
clock, which is how the tests run.
*/
int runMcpServer(Engine& engine, const string& statePath, const string& fixedNow);

}  // namespace interviewprep
