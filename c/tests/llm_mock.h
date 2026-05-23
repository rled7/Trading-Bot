/**
 * AlgoForge — c/tests/llm_mock.h
 *
 * Mock HTTP transport for LLM tests.
 * Loads a fixture file, validates outgoing requests, and returns canned
 * responses — no live Ollama required.
 */
#ifndef AF_LLM_MOCK_H
#define AF_LLM_MOCK_H

#include "af_llm.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * af_llm_mock_transport_create — create a mock transport from a fixture name.
 *
 * fixture_name: base name without path or extension (e.g. "chat_basic").
 *   The file is resolved from AF_FIXTURE_DIR env var, or defaults to
 *   "tests/fixtures/llm/<fixture_name>.json" relative to the build's working
 *   directory.
 *
 * On first request():
 *   1. Validates method, path match the fixture's request.method/path.
 *   2. Validates the body is JSON-equal (key-sort normalised) to the fixture's
 *      request.body (skipped if fixture request.body is null/absent).
 *   3. Returns the fixture's response.body serialised as a string, and
 *      response.status.
 *
 * For streaming fixtures (response.body is a JSON array), returns the
 * NDJSON-joined lines.
 *
 * Returns a transport struct.  Caller must call af_llm_mock_transport_free()
 * when done.
 *
 * On fixture load failure, the returned transport's request() will immediately
 * return an error.
 */
af_llm_transport_t af_llm_mock_transport_create(const char *fixture_name);

/**
 * af_llm_mock_transport_free — release memory held by a mock transport.
 */
void af_llm_mock_transport_free(af_llm_transport_t *t);

/**
 * af_llm_mock_transport_create_503 — returns a transport that always
 * replies with HTTP 503 and an error body.
 */
af_llm_transport_t af_llm_mock_transport_create_503(void);

/**
 * af_llm_mock_transport_create_unreachable — returns a transport that always
 * fails with kind="unreachable".
 */
af_llm_transport_t af_llm_mock_transport_create_unreachable(void);

/**
 * af_llm_mock_transport_create_bad_json — returns a transport that replies
 * 200 with invalid JSON (for decode-error testing).
 */
af_llm_transport_t af_llm_mock_transport_create_bad_json(void);

#ifdef __cplusplus
}
#endif

#endif /* AF_LLM_MOCK_H */
