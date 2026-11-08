#include "capi.h"

#include <cstdlib>
#include <string>

#include "engine.hpp"

namespace {

interviewprep::Engine& engine() {
  static interviewprep::Engine instance;
  return instance;
}

std::string& lastResult() {
  static std::string value;
  return value;
}

std::string& lastError() {
  static std::string value;
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

void* interviewprep_alloc(size_t size) { return std::malloc(size); }

void interviewprep_free(void* pointer) { std::free(pointer); }

}  // extern "C"
