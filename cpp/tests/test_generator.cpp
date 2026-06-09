/**
 * AlgoForge — tests/test_generator.cpp
 *
 * Parity tests for algo_gen generate_fast (+ retry mechanics).
 * Python oracle: python/tests/algo_gen/test_generator.py
 *   :: TestGenerateFast, TestRetryMechanics
 */
#include "test_helpers.hpp"
#include "algo_gen_internal.hpp"
#include "core/llm.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using namespace algoforge::algo_gen;

/* Mock LLM provider that plays back a queue of canned chat responses. */
class MockProvider : public algoforge::llm::LLMProvider {
public:
    std::vector<std::string> responses;
    size_t idx = 0;
    std::vector<algoforge::llm::ChatRequest> requests;

    explicit MockProvider(std::vector<std::string> r) : responses(std::move(r)) {}

    algoforge::llm::ChatResponse chat(const algoforge::llm::ChatRequest& req) override {
        requests.push_back(req);
        algoforge::llm::ChatResponse r;
        r.message.role = "assistant";
        if (idx < responses.size()) r.message.content = responses[idx++];
        else throw std::runtime_error("MockProvider: responses exhausted");
        return r;
    }
    algoforge::llm::HealthStatus           health() override { return {}; }
    std::vector<algoforge::llm::ModelInfo> list_models() override { return {}; }
    algoforge::llm::CompletionResponse     complete(const algoforge::llm::CompletionRequest&) override { return {}; }
    std::vector<algoforge::llm::ChatChunk>  chat_stream(const algoforge::llm::ChatRequest&) override { return {}; }
    algoforge::llm::EmbedResponse          embed(const algoforge::llm::EmbedRequest&) override { return {}; }
};

static const char* VALID_MANIFEST = R"(```json
{
  "schema_version": "1.0",
  "name": "test-algo",
  "description": "Test strategy.",
  "rationale": "Simple test.",
  "timeframes": ["H1"],
  "symbols": ["EURUSD"],
  "indicators": [{"id": "ema9", "kind": "ema", "params": {"period": 9}}],
  "entries": [{"side": "long", "when": "ema9 > 1.0"}],
  "exits": [{"side": "long", "sl_atr": 1.5, "tp_atr": 3.0}],
  "risk": {"size": "atr", "atr_mult": 1.5, "fixed_lots": 0.01, "max_concurrent": 1, "hedge": false, "cool_down_bars": 0}
}
```)";

/* valid JSON, but fails schema validation (missing required fields) */
static const char* SCHEMA_INVALID = "```json\n{\"foo\": 1}\n```";
/* not JSON at all */
static const char* MALFORMED = "Sorry, I cannot produce that.";

static bool turns_have(const generator::GenerationTrace& t, const std::string& content) {
    for (auto& [role, c] : t.turns) if (c == content) return true;
    return false;
}

void test_generator(TestRunner T) {

    section("generate_fast — happy path");
    T("returns_manifest_and_fast_trace", []{
        MockProvider p({VALID_MANIFEST});
        auto r = generator::generate_fast("buy dips", p);
        CHK(r.manifest.name == "test-algo");
        CHK(r.trace.mode == "fast");
        CHK_EQ(r.trace.seed, 42);
        CHK((int)r.trace.turns.size() >= 3);   /* system, user, assistant */
        CHK(r.trace.parse_failures.empty());
    });
    T("manifest_has_indicators_and_entries", []{
        MockProvider p({VALID_MANIFEST});
        auto r = generator::generate_fast("x", p);
        CHK((int)r.manifest.indicators.size() >= 1);
        CHK((int)r.manifest.entries.size() >= 1);
    });
    T("seed_recorded", []{
        MockProvider p({VALID_MANIFEST});
        auto r = generator::generate_fast("x", p, 7);
        CHK_EQ(r.trace.seed, 7);
    });

    section("generate_fast — retry mechanics");
    T("malformed_json_retries_once", []{
        MockProvider p({MALFORMED, VALID_MANIFEST});
        auto r = generator::generate_fast("x", p);
        CHK(r.manifest.name == "test-algo");
        CHK_EQ((int)r.trace.parse_failures.size(), 1);
        CHK_EQ((int)p.idx, 2);   /* two LLM calls made */
    });
    T("retry_trace_has_fix_prompt", []{
        MockProvider p({MALFORMED, VALID_MANIFEST});
        auto r = generator::generate_fast("x", p);
        CHK(turns_have(r.trace, "The JSON you produced is malformed or schema-invalid. "
                                "Please output ONLY the corrected manifest JSON in a ```json ... ``` fence. "
                                "No prose, no explanation — just the fixed JSON."));
    });
    T("schema_invalid_retries_once", []{
        MockProvider p({SCHEMA_INVALID, VALID_MANIFEST});
        auto r = generator::generate_fast("x", p);
        CHK(r.manifest.name == "test-algo");
        CHK_EQ((int)r.trace.parse_failures.size(), 1);
    });
    T("two_failures_raise_generation_error", []{
        MockProvider p({MALFORMED, MALFORMED});
        bool threw = false;
        try { generator::generate_fast("x", p); }
        catch (const generator::GenerationError&) { threw = true; }
        CHK(threw);
    });
}
