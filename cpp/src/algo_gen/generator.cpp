/**
 * AlgoForge — src/algo_gen/generator.cpp
 *
 * LLM-driven manifest generation (S1) — generate_fast.
 * Python oracle: python/algoforge/algo_gen/generator.py
 *
 * Sandbox/escape-hatch is intentionally NOT here: it is a Python-subprocess
 * executor with no honest C++ analog (the engine skips escape-hatch manifests
 * in validate()). Balanced/Max modes are follow-on increments.
 */
#include "algo_gen_internal.hpp"
#include "core/algo_gen.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace algoforge::algo_gen {
namespace generator {

using algoforge::llm::ChatMessage;
using algoforge::llm::ChatRequest;
using algoforge::llm::ChatResponse;
using algoforge::llm::LLMProvider;

namespace {

const char* FIX_JSON_PROMPT =
    "The JSON you produced is malformed or schema-invalid. "
    "Please output ONLY the corrected manifest JSON in a ```json ... ``` fence. "
    "No prose, no explanation — just the fixed JSON.";

void emit(const json::JsonValue& v, std::string& out) {
    using JT = json::JsonType;
    switch (v.type) {
        case JT::Null:   out += "null"; break;
        case JT::Bool:   out += json::emit_bool(v.boolean); break;
        case JT::Number: out += json::emit_double(v.num); break;
        case JT::String: out += json::emit_string_val(v.str); break;
        case JT::Array:  out += "["; for (size_t i = 0; i < v.arr.size(); ++i) { if (i) out += ","; emit(v.arr[i], out); } out += "]"; break;
        case JT::Object: {
            out += "{"; bool f = true;
            for (const auto& [k, val] : v.obj) { if (!f) out += ","; f = false; out += json::emit_string_val(k); out += ":"; emit(val, out); }
            out += "}"; break;
        }
    }
}
std::string dumps(const json::JsonValue& v) { std::string s; emit(v, s); return s; }

std::string chat(LLMProvider& p, const std::vector<ChatMessage>& msgs, const std::string& model, int seed) {
    ChatRequest req;
    req.model = model; req.messages = msgs; req.temperature = 0.5f;
    req.max_tokens = 2048; req.seed = seed;
    ChatResponse resp = p.chat(req);
    return resp.message.content;
}

/* extract JSON + schema-validate; throws std::exception on failure. */
std::pair<json::JsonValue, AlgoManifest> extract_validate(const std::string& raw) {
    json::JsonValue d = prompts::extract_json_block(raw);  /* throws if no JSON */
    AlgoManifest m = parse_manifest(dumps(d));             /* throws on schema error */
    return {d, m};
}

/* One LLM call + parse, with a single fix-JSON retry. Records turns +
 * parse_failures into trace. Throws GenerationError after two failures. */
std::pair<json::JsonValue, AlgoManifest> generate_with_retry(
        LLMProvider& provider, std::vector<ChatMessage> messages,
        const std::string& model, int seed, GenerationTrace& trace) {
    std::string raw = chat(provider, messages, model, seed);
    trace.turns.push_back({"assistant", raw});
    try { return extract_validate(raw); }
    catch (const std::exception& e) { trace.parse_failures.push_back({raw, e.what()}); }

    std::vector<ChatMessage> retry = messages;
    retry.push_back({"assistant", raw});
    retry.push_back({"user", FIX_JSON_PROMPT});
    trace.turns.push_back({"user", FIX_JSON_PROMPT});

    std::string raw2 = chat(provider, retry, model, seed);
    trace.turns.push_back({"assistant", raw2});
    try { return extract_validate(raw2); }
    catch (const std::exception& e2) {
        trace.parse_failures.push_back({raw2, e2.what()});
        throw GenerationError(std::string("Failed to produce a valid manifest after 2 attempts. Last error: ") + e2.what());
    }
}

} /* anonymous namespace */

GenResult generate_fast(const std::string& brief, LLMProvider& provider,
                        int seed, const std::string& model) {
    GenerationTrace trace;
    trace.mode = "fast"; trace.model = model; trace.seed = seed;

    std::string sys = prompts::FAST_SYSTEM;
    std::string usr = prompts::render_fast_user(brief);
    std::vector<ChatMessage> messages = {{"system", sys}, {"user", usr}};
    trace.turns.push_back({"system", sys});
    trace.turns.push_back({"user",   usr});

    auto [d, m] = generate_with_retry(provider, messages, model, seed, trace);
    (void)d;
    return {m, trace};
}

GenResult generate_balanced(const std::string& brief, LLMProvider& provider,
                            int seed, const std::string& model) {
    GenerationTrace trace;
    trace.mode = "balanced"; trace.model = model; trace.seed = seed;

    std::string sys = prompts::BALANCED_SYSTEM;
    std::string usr = prompts::render_balanced_user(brief);
    std::vector<ChatMessage> messages = {{"system", sys}, {"user", usr}};
    trace.turns.push_back({"system", sys});
    trace.turns.push_back({"user",   usr});

    /* Turn 1: initial manifest */
    auto [initial_d, initial_m] = generate_with_retry(provider, messages, model, seed, trace);
    (void)initial_m;

    /* Turn 2: critique + revision */
    std::string critique = prompts::render_balanced_critique(initial_d);
    messages.push_back({"assistant", dumps(initial_d)});
    messages.push_back({"user", critique});
    trace.turns.push_back({"user", critique});

    auto [revised_d, revised_m] = generate_with_retry(provider, messages, model, seed, trace);
    (void)revised_d;
    return {revised_m, trace};
}

} /* namespace generator */
} /* namespace algoforge::algo_gen */
