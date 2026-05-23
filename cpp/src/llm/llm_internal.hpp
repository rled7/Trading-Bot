/**
 * AlgoForge — src/llm/llm_internal.hpp
 * Internal implementation header — NOT installed.
 *
 * Defines JsonValue and the json:: helpers used by ollama.cpp,
 * use_cases.cpp, and json.cpp.
 */
#pragma once

#include "core/llm.hpp"

#include <map>
#include <string>
#include <vector>

namespace algoforge::llm::json {

/* ── Minimal JSON value type ── */

enum class JsonType { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    bool     boolean = false;
    double   num     = 0.0;
    std::string str;
    std::vector<JsonValue>            arr;
    std::map<std::string, JsonValue>  obj;

    /* Convenience accessors — throw std::runtime_error on type mismatch */
    bool        as_bool()   const { return boolean; }
    double      as_double() const { return num; }
    long long   as_int()    const { return static_cast<long long>(num); }
    const std::string& as_str() const { return str; }

    bool has(const std::string& key) const {
        return type == JsonType::Object && obj.count(key) > 0;
    }
    const JsonValue& get(const std::string& key) const {
        return obj.at(key);
    }
};

/* ── Parser ── */
JsonValue parse(const std::string& s);

/* ── Emitters ── */
std::string emit_object(const std::map<std::string, std::string>& kv);
std::string emit_double(double v);
std::string emit_string_val(const std::string& s);
std::string emit_bool(bool b);
std::string emit_indicators(const std::vector<std::pair<std::string,double>>& ind);

/* ── §9 balanced-brace extractor ── */
std::string extract_first_json_object(const std::string& text);

} /* namespace algoforge::llm::json */

/* ── Prompt string constants (defined in prompts.cpp) ── */
namespace algoforge::llm {

extern const char* TRADE_RATIONALE_SYSTEM;
extern const char* TRADE_RATIONALE_USER_TMPL;
extern const char* BACKTEST_COMMENTARY_SYSTEM;
extern const char* BACKTEST_COMMENTARY_USER_TMPL;
extern const char* NEWS_SENTIMENT_SYSTEM;
extern const char* NEWS_SENTIMENT_USER_TMPL;
extern const char* STRATEGY_DESCRIBE_SYSTEM;
extern const char* STRATEGY_DESCRIBE_USER_TMPL;

} /* namespace algoforge::llm */
