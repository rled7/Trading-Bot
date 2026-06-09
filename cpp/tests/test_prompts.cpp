/**
 * AlgoForge — tests/test_prompts.cpp
 *
 * Parity tests for algo_gen prompt templates + extract_json_block.
 * Python oracle: python/tests/algo_gen/test_generator.py :: TestExtractJsonBlock
 */
#include "test_helpers.hpp"
#include "algo_gen_internal.hpp"

#include <string>

using namespace algoforge::algo_gen;
using algoforge::algo_gen::prompts::extract_json_block;

void test_prompts(TestRunner T) {

    /* ── extract_json_block — mirrors test_generator.py::TestExtractJsonBlock ── */
    section("extract_json_block");
    T("extracts_fenced_json", []{
        auto r = extract_json_block("Some prose\n```json\n{\"key\": \"value\"}\n```\nMore prose");
        CHK(r.has("key")); CHK(r.get("key").as_str() == "value");
    });
    T("extracts_unfenced_json", []{
        auto r = extract_json_block("Here: {\"name\": \"algo\"}");
        CHK(r.get("name").as_str() == "algo");
    });
    T("extracts_nested_json", []{
        auto r = extract_json_block("```json\n{\"outer\": {\"inner\": 42}}\n```");
        CHK_EQ((int)r.get("outer").get("inner").as_int(), 42);
    });
    T("raises_on_no_json", []{
        bool threw = false;
        try { extract_json_block("no json here at all"); }
        catch (const std::exception&) { threw = true; }
        CHK(threw);
    });
    T("strips_leading_prose", []{
        auto r = extract_json_block("Here is your manifest:\n```json\n{\"a\": 1}\n```");
        CHK_EQ((int)r.get("a").as_int(), 1);
    });
    T("fence_without_json_label", []{
        auto r = extract_json_block("```\n{\"x\": true}\n```");
        CHK(r.get("x").as_bool() == true);
    });

    /* ── render_* — functional checks (brief + schema injected) ── */
    section("render templates");
    T("fast_user_contains_brief_and_schema", []{
        auto s = prompts::render_fast_user("buy dips on RSI");
        CHK(s.find("buy dips on RSI") != std::string::npos);
        CHK(s.find("manifest must be a JSON object") != std::string::npos);
        CHK(s.find("```json") != std::string::npos);
    });
    T("balanced_user_contains_brief", []{
        auto s = prompts::render_balanced_user("mean reversion");
        CHK(s.find("mean reversion") != std::string::npos);
        CHK(s.find("Step 1") != std::string::npos);
    });
    T("max_user_has_candidate_context", []{
        auto s = prompts::render_max_user("breakout", 2, 5);
        CHK(s.find("breakout") != std::string::npos);
        CHK(s.find("candidate 2 of 5") != std::string::npos);
    });
    T("balanced_critique_embeds_manifest", []{
        auto m = json::parse("{\"name\":\"x\"}");
        auto s = prompts::render_balanced_critique(m);
        CHK(s.find("\"name\"") != std::string::npos);
        CHK(s.find("3 weaknesses") != std::string::npos);
    });
}
