#include "capi.h"

#include <cstdlib>
#include <string>

#include "engine.hpp"

using namespace std;

namespace {

interviewprep::Engine& engine() {
  static interviewprep::Engine instance;
  return instance;
}

string& lastResult() {
  static string value;
  return value;
}

string& lastError() {
  static string value;
  return value;
}

}  // namespace

extern "C" {

int interviewprep_load_resources(const char* json) {
  lastError().clear();
  return engine().loadResources(json, &lastError());
}

int interviewprep_load_state(const char* json) {
  lastError().clear();
  return engine().loadState(json, &lastError()) ? 0 : -1;
}

const char* interviewprep_save_state(void) {
  lastResult() = engine().saveState();
  return lastResult().c_str();
}

const char* interviewprep_call(const char* request) {
  lastResult() = engine().call(request);
  return lastResult().c_str();
}

const char* interviewprep_last_error(void) { return lastError().c_str(); }

void* interviewprep_alloc(size_t size) { return malloc(size); }

void interviewprep_free(void* pointer) { free(pointer); }

}  // extern "C"
