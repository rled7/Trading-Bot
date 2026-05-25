/**
 * AlgoForge — src/algo_gen/algo_gen_internal.hpp
 * Internal implementation header — NOT installed.
 *
 * Declares JSON helpers, DSL node types, indicator dispatch, and the
 * backtest-runner used by the validator + robustness modules.
 */
#pragma once

#include "core/algo_gen.hpp"
#include "core/analytics.hpp"

#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace algoforge::algo_gen {

/* =========================================================================
 * JSON helpers (reuse the same pattern as llm/llm_internal.hpp)
 * ========================================================================= */

namespace json {

enum class JsonType { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType    type    = JsonType::Null;
    bool        boolean = false;
    double      num     = 0.0;
    std::string str;
    std::vector<JsonValue>            arr;
    std::map<std::string, JsonValue>  obj;

    bool has(const std::string& key) const {
        return type == JsonType::Object && obj.count(key) > 0;
    }
    const JsonValue& get(const std::string& key) const {
        return obj.at(key);
    }
    double    as_double() const { return num; }
    long long as_int()    const { return static_cast<long long>(num); }
    bool      as_bool()   const { return boolean; }
    const std::string& as_str() const { return str; }
    bool is_null()  const { return type == JsonType::Null; }
    bool is_number() const { return type == JsonType::Number; }
    bool is_string() const { return type == JsonType::String; }
    bool is_bool()   const { return type == JsonType::Bool; }
    bool is_array()  const { return type == JsonType::Array; }
    bool is_object() const { return type == JsonType::Object; }
};

JsonValue   parse(const std::string& s);
std::string emit_double(double v);
std::string emit_string_val(const std::string& s);
std::string emit_bool(bool b);

} /* namespace json */

/* =========================================================================
 * DSL: node types and evaluator
 * ========================================================================= */

namespace dsl {

enum class NodeKind {
    Lit,      // numeric literal
    Name,     // identifier (indicator id / bar field)
    Pattern,  // pattern.X
    BinOp,    // +  -  *  /
    CmpOp,    // <  <=  >  >=  ==  !=
    BoolAnd,
    BoolOr,
    Not,
    Neg,
};

struct Node {
    NodeKind kind;
    double   lit_val  = 0.0;        // Lit
    std::string name;               // Name / Pattern / BinOp/CmpOp sub-kind
    std::vector<std::shared_ptr<Node>> children;
};

using Expr = std::shared_ptr<Node>;

/**
 * Parse a DSL expression string.
 * @param indicator_ids  Set of declared indicator ids (for validation).
 * Throws std::invalid_argument on parse/validation errors.
 */
Expr compile_expr(const std::string& src,
                  const std::vector<std::string>& indicator_ids);

/**
 * Evaluation context: maps names → double values.
 * NaN represents "not yet available" (warmup).
 */
using Env = std::map<std::string, double>;

/**
 * Evaluate an expression to bool.
 * NaN / missing names → false (safe during warmup).
 */
bool eval_expr(const Expr& expr, const Env& env);

} /* namespace dsl */

/* =========================================================================
 * Indicator dispatch (mirrors Python runtime.py _call_indicator)
 * ========================================================================= */

/**
 * Compute the last value of a declared indicator over a bar slice.
 * Returns NaN if bars are insufficient.
 */
double compute_indicator(const IndicatorDecl& ind, const std::vector<Bar>& bars);

/* =========================================================================
 * Simple backtest runner used by validator + robustness
 * ========================================================================= */

struct SimpleBacktestResult {
    int    total_trades   = 0;
    double sharpe         = 0.0;
    double max_dd_pct     = 0.0;   // positive percentage
    double net_profit     = 0.0;
    double initial_capital= 10000.0;
    bool   has_nan_inf    = false;
    std::vector<double> equity_curve;
    std::vector<double> trade_pnls;   // for MC resampling
};

struct BacktestConfig {
    double initial_capital = 10000.0;
    double commission_usd  = 7.0;
    int    warmup_bars     = 200;
};

SimpleBacktestResult run_manifest_backtest(
    const AlgoManifest& m,
    const std::vector<Bar>& bars,
    const BacktestConfig& cfg = {}
);

/* =========================================================================
 * Gate check (§5: >=20 trades, Sharpe>=0.5, max_dd<=25%, no NaN)
 * ========================================================================= */

inline bool passes_gate(const SimpleBacktestResult& r) {
    if (r.total_trades < 20) return false;
    if (r.has_nan_inf)       return false;
    if (r.sharpe < 0.5)      return false;
    if (r.max_dd_pct > 25.0) return false;
    return true;
}

} /* namespace algoforge::algo_gen */
