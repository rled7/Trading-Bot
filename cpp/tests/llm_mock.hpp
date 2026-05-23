/**
 * AlgoForge — tests/llm_mock.hpp
 * S2 LLM Layer — MockTransport for fixture-based tests.
 *
 * Usage:
 *   auto transport = std::make_shared<MockTransport>("complete_basic");
 *   OllamaProvider p("http://localhost:11434", "llama3.1:8b",
 *                    "nomic-embed-text", transport);
 *   auto resp = p.complete(req);
 *
 * The MockTransport:
 *   - Loads tests/fixtures/llm/<name>.json on construction
 *   - On request(): verifies method + path + (optionally) body
 *   - Returns the fixture's response.body as JSON string
 *
 * Special fixtures:
 *   - status != 200  → returns the error status (for error tests)
 *   - name "mock_unreachable" → throws LLMError(unreachable)
 *   - name "mock_timeout"     → throws LLMError(timeout)
 */
#pragma once

#include "core/llm.hpp"

#include <memory>
#include <string>

/**
 * Load and serve a single fixture file.
 */
class MockTransport : public algoforge::llm::ITransport {
public:
    /**
     * @param fixture_name   Base name without .json extension.
     * @param fixture_dir    Directory containing the .json files.
     *                       Default: "tests/fixtures/llm" relative to cwd,
     *                       or override via constructor.
     */
    explicit MockTransport(const std::string& fixture_name,
                           const std::string& fixture_dir = "");

    algoforge::llm::TransportResult
    request(const std::string& method,
            const std::string& path,
            const std::string& body) override;

    /* Accessors for test assertions */
    const std::string& last_request_method() const { return last_method_; }
    const std::string& last_request_path()   const { return last_path_;   }
    const std::string& last_request_body()   const { return last_body_;   }

private:
    std::string fixture_name_;
    std::string fixture_dir_;

    /* Parsed from the fixture */
    std::string expected_method_;
    std::string expected_path_;
    std::string response_body_str_;
    int         response_status_ = 200;
    bool        is_unreachable_  = false;
    bool        is_timeout_      = false;

    /* Last call recorded */
    std::string last_method_;
    std::string last_path_;
    std::string last_body_;

    void load_fixture();
};

/**
 * Factory: create a MockTransport wrapped as shared_ptr<ITransport>.
 */
inline std::shared_ptr<algoforge::llm::ITransport>
make_mock(const std::string& fixture_name, const std::string& fixture_dir = "") {
    return std::make_shared<MockTransport>(fixture_name, fixture_dir);
}
