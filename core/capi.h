/* capi.h — the core as plain C, for Swift and WebAssembly.
 *
 * Five functions that pass strings, plus two that let JavaScript allocate
 * memory for them. Returned strings belong to the core and stay valid until
 * the next call, so copy them if you need to keep them.
 */
#ifndef INTERVIEWPREP_CAPI_H
#define INTERVIEWPREP_CAPI_H

#include <stddef.h>

#ifdef __wasm__
#define INTERVIEWPREP_EXPORT(name) __attribute__((export_name(#name)))
#else
#define INTERVIEWPREP_EXPORT(name)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Loads the resources: every problem, card and drill. Returns the question count, or -1. */
INTERVIEWPREP_EXPORT(interviewprep_load_resources) int interviewprep_load_resources(const char* json);

/* Loads saved progress. Returns 0 on success, -1 otherwise. */
INTERVIEWPREP_EXPORT(interviewprep_load_state) int interviewprep_load_state(const char* json);

/* Returns the progress as JSON, ready to write to storage. */
INTERVIEWPREP_EXPORT(interviewprep_save_state) const char* interviewprep_save_state(void);

/* Runs one JSON command and returns the JSON response. */
INTERVIEWPREP_EXPORT(interviewprep_call) const char* interviewprep_call(const char* request);

/* The last error from interviewprep_load_resources or interviewprep_load_state. */
INTERVIEWPREP_EXPORT(interviewprep_last_error) const char* interviewprep_last_error(void);

/* Memory for passing strings in from JavaScript. */
INTERVIEWPREP_EXPORT(interviewprep_alloc) void* interviewprep_alloc(size_t size);
INTERVIEWPREP_EXPORT(interviewprep_free) void interviewprep_free(void* pointer);

#ifdef __cplusplus
}
#endif

#endif
