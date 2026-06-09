/**
 * AlgoForge — src/algo_gen/prompts.cpp
 *
 * S1 generator prompt templates + robust JSON extraction.
 * Python oracle: python/algoforge/algo_gen/prompts.py
 */
#include "algo_gen_internal.hpp"

#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace algoforge::algo_gen {
namespace prompts {

static const char* INDICATOR_KINDS =
    "sma, ema, rsi, atr, macd, bollinger, stochastic, obv, wma, cci, "
    "williams_r, roc, mfi, vwap, keltner, adx, hma, dema, tema";

static const std::string SCHEMA_SUMMARY =
R"(The manifest must be a JSON object with these exact fields:
{
  "schema_version": "1.0",
  "name": "<kebab-case, <=48 chars, starts with a letter>",
  "description": "<=280 chars plain prose",
  "rationale": "<=1024 chars LLM reasoning trace",
  "timeframes": ["H1"],
  "symbols": ["EURUSD"] or "any",
  "indicators": [
    {"id": "<valid_python_identifier>", "kind": "<indicator_kind>", "params": {"period": <int>}}
  ],
  "entries": [{"side": "long"|"short", "when": "<DSL expression>"}],
  "exits": [{"side": "long"|"short", "sl_atr": <float>, "tp_atr": <float>}],
  "risk": {"size": "atr"|"fixed", "atr_mult": <float>, "fixed_lots": <float>,
           "max_concurrent": <int>, "hedge": false, "cool_down_bars": <int>=0},
  "code": null
}
DSL expressions use indicator ids, bar fields (close, open, high, low, volume), and
pattern names (pattern.hammer, pattern.doji, pattern.engulfing, pattern.pin_bar,
pattern.morning_star, pattern.evening_star).
Operators: <, >, <=, >=, ==, !=, and, or, not, +, -, *, /.)";

const char* FAST_SYSTEM =
    "You are an expert algorithmic trading strategy designer. "
    "Given a brief description, produce a complete, valid AlgoForge manifest JSON. "
    "Respond with ONLY a JSON code block — no prose before or after.";
const char* BALANCED_SYSTEM =
    "You are an expert algorithmic trading strategy designer and self-critic. "
    "You generate manifests and then rigorously critique them to identify weaknesses. "
    "All output is JSON-only in ```json ... ``` fences — no prose outside fences.";
const char* MAX_SYSTEM =
    "You are an expert algorithmic trading strategy designer using step-by-step reasoning. "
    "Given a brief and context, generate a single complete, novel, schema-valid AlgoForge manifest. "
    "Respond with ONLY a JSON code block — no prose before or after.";

/* Compact JSON emit for embedding a manifest into critique prompts. */
static void emit(const json::JsonValue& v, std::string& out) {
    using JT = json::JsonType;
    switch (v.type) {
        case JT::Null:   out += "null"; break;
        case JT::Bool:   out += json::emit_bool(v.boolean); break;
        case JT::Number: out += json::emit_double(v.num); break;
        case JT::String: out += json::emit_string_val(v.str); break;
        case JT::Array: {
            out += "[";
            for (size_t i = 0; i < v.arr.size(); ++i) { if (i) out += ","; emit(v.arr[i], out); }
            out += "]"; break;
        }
        case JT::Object: {
            out += "{";
            bool first = true;
            for (const auto& [k, val] : v.obj) { if (!first) out += ","; first = false;
                out += json::emit_string_val(k); out += ":"; emit(val, out); }
            out += "}"; break;
        }
    }
}
static std::string dumps(const json::JsonValue& v) { std::string s; emit(v, s); return s; }

json::JsonValue extract_json_block(const std::string& text) {
    /* Strategy 1: ```json ... ``` (or bare ```) fence containing an object. */
    static const std::regex fence(R"(```(?:json)?\s*(\{[\s\S]*?\})\s*```)");
    std::smatch m;
    if (std::regex_search(text, m, fence)) {
        try { return json::parse(m[1].str()); } catch (...) { /* fall through */ }
    }
    /* Strategy 2: scan for the first balanced { ... } object. */
    int depth = 0; bool in_str = false, esc = false; long start = -1;
    for (size_t i = 0; i < text.size(); ++i) {
        char ch = text[i];
        if (esc) { esc = false; continue; }
        if (ch == '\\' && in_str) { esc = true; continue; }
        if (ch == '"') { in_str = !in_str; continue; }
        if (in_str) continue;
        if (ch == '{') { if (depth == 0) start = static_cast<long>(i); ++depth; }
        else if (ch == '}') { if (depth > 0 && --depth == 0 && start >= 0) {
            try { return json::parse(text.substr(start, i - start + 1)); } catch (...) {}
            start = -1;
        } }
    }
    throw std::runtime_error("No valid JSON object found in LLM response (length="
                             + std::to_string(text.size()) + ")");
}

std::string render_fast_user(const std::string& brief) {
    return "Brief: " + brief + "\n\nSchema reference:\n" + SCHEMA_SUMMARY +
           "\n\nGenerate a complete manifest for this strategy. "
           "Respond with exactly:\n```json\n<manifest JSON>\n```";
}
std::string render_balanced_user(const std::string& brief) {
    return "Brief: " + brief + "\n\nSchema reference:\n" + SCHEMA_SUMMARY +
           "\n\nStep 1: Generate a complete manifest for this strategy.\n"
           "Respond with exactly:\n```json\n<manifest JSON>\n```";
}
std::string render_balanced_critique(const json::JsonValue& manifest) {
    return "You produced this candidate manifest:\n```json\n" + dumps(manifest) +
           "\n```\n\nStep 2: Identify exactly 3 weaknesses, then emit a revised manifest "
           "that addresses all three.\n\nRespond with exactly:\n```json\n<revised manifest JSON>\n```";
}
std::string render_max_user(const std::string& brief, int candidate_num, int n_candidates,
                            const std::string& context_hint) {
    std::ostringstream os;
    os << "Brief: " << brief << "\n\nContext: candidate " << candidate_num << " of " << n_candidates
       << ". Make this candidate distinct from typical EMA-crossover approaches. Consider: "
       << context_hint << "\n\nSchema reference:\n" << SCHEMA_SUMMARY
       << "\n\nGenerate a complete, distinct manifest. Respond with exactly:\n```json\n<manifest JSON>\n```";
    return os.str();
}
std::string render_max_critique(const json::JsonValue& manifest, int trades, double sharpe, double max_dd) {
    std::ostringstream os;
    os << "Candidate manifest:\n```json\n" << dumps(manifest) << "\n```\n\nQuick backtest summary:\n"
       << "  trades=" << trades << "  sharpe=" << sharpe << "  max_dd=" << max_dd
       << "%\n\nIdentify 3 specific weaknesses, then emit a revised manifest that addresses all three.\n\n"
       << "Respond with exactly:\n```json\n<revised manifest JSON>\n```";
    return os.str();
}

} /* namespace prompts */
} /* namespace algoforge::algo_gen */
