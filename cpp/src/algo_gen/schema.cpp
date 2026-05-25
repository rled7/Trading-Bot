/**
 * AlgoForge — src/algo_gen/schema.cpp
 * S1 Phase 1 — AlgoManifest JSON schema validator + parser.
 *
 * Mirrors python/algoforge/algo_gen/schema.py logic exactly.
 */

#include "algo_gen_internal.hpp"

#include <algorithm>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace algoforge::algo_gen {

/* =========================================================================
 * Schema constants (mirrors schema.py)
 * ========================================================================= */

static const std::set<std::string> SUPPORTED_SCHEMA_VERSIONS = {"1.0"};

static const std::set<std::string> VALID_TIMEFRAMES = {
    "M1","M5","M15","M30","H1","H4","D1","W1","S15"
};

static const std::set<std::string> INDICATOR_KINDS = {
    "sma","ema","rsi","atr","macd","bollinger","stochastic","obv","wma",
    "cci","williams_r","roc","mfi","vwap","keltner","adx","hma","dema","tema",
    "trix","momentum","true_range","wilder_ema","vwma","hist_vol","cmf",
    "acc_dist","force_index","vol_osc","donchian",
};

static const std::set<std::string> VALID_SIDES = {"long","short"};
static const std::set<std::string> VALID_SIZE_METHODS = {"atr","fixed"};

/* =========================================================================
 * Sub-validators
 * ========================================================================= */

static bool is_kebab_case(const std::string& s) {
    if (s.empty()) return false;
    // must start with lowercase letter, only lowercase letters/digits/hyphens
    if (!std::islower((unsigned char)s[0])) return false;
    for (char c : s) {
        if (!std::islower((unsigned char)c) && !std::isdigit((unsigned char)c) && c != '-')
            return false;
    }
    return true;
}

static IndicatorDecl parse_indicator(const json::JsonValue& v, int idx) {
    if (!v.is_object()) {
        throw std::invalid_argument(
            "indicators[" + std::to_string(idx) + "]: must be an object");
    }
    if (!v.has("id") || !v.get("id").is_string())
        throw std::invalid_argument("indicators[" + std::to_string(idx) + "]: missing 'id'");
    if (!v.has("kind") || !v.get("kind").is_string())
        throw std::invalid_argument("indicators[" + std::to_string(idx) + "]: missing 'kind'");

    std::string id   = v.get("id").as_str();
    std::string kind = v.get("kind").as_str();

    // id must be a valid identifier
    if (id.empty() || !(std::isalpha((unsigned char)id[0]) || id[0] == '_')) {
        throw std::invalid_argument(
            "indicators[" + std::to_string(idx) + "]: 'id' must be a valid identifier");
    }
    for (char c : id) {
        if (!std::isalnum((unsigned char)c) && c != '_') {
            throw std::invalid_argument(
                "indicators[" + std::to_string(idx) + "]: 'id' must be a valid identifier");
        }
    }

    if (INDICATOR_KINDS.find(kind) == INDICATOR_KINDS.end()) {
        throw std::invalid_argument(
            "indicators[" + std::to_string(idx) + "]: unknown indicator kind '" + kind + "'");
    }

    std::map<std::string, double> params;
    if (v.has("params")) {
        const auto& pr = v.get("params");
        if (!pr.is_object()) {
            throw std::invalid_argument(
                "indicators[" + std::to_string(idx) + "]: 'params' must be an object");
        }
        for (const auto& [k, pv] : pr.obj) {
            if (!pv.is_number() && !pv.is_bool()) {
                throw std::invalid_argument(
                    "indicators[" + std::to_string(idx) + "].params." + k +
                    ": must be number or bool");
            }
            if (pv.is_bool()) {
                params[k] = pv.as_bool() ? 1.0 : 0.0;
            } else {
                params[k] = pv.as_double();
            }
        }
    }

    return IndicatorDecl{id, kind, params};
}

static EntryRule parse_entry_rule(const json::JsonValue& v, int idx) {
    if (!v.is_object())
        throw std::invalid_argument("entries[" + std::to_string(idx) + "]: must be an object");
    if (!v.has("side") || !v.get("side").is_string())
        throw std::invalid_argument("entries[" + std::to_string(idx) + "]: missing 'side'");
    if (!v.has("when") || !v.get("when").is_string())
        throw std::invalid_argument("entries[" + std::to_string(idx) + "]: missing 'when'");

    std::string side = v.get("side").as_str();
    if (VALID_SIDES.find(side) == VALID_SIDES.end())
        throw std::invalid_argument(
            "entries[" + std::to_string(idx) + "]: 'side' must be 'long' or 'short'");

    return EntryRule{side, v.get("when").as_str()};
}

static ExitRule parse_exit_rule(const json::JsonValue& v, int idx) {
    if (!v.is_object())
        throw std::invalid_argument("exits[" + std::to_string(idx) + "]: must be an object");
    if (!v.has("side") || !v.get("side").is_string())
        throw std::invalid_argument("exits[" + std::to_string(idx) + "]: missing 'side'");

    std::string side = v.get("side").as_str();
    if (VALID_SIDES.find(side) == VALID_SIDES.end())
        throw std::invalid_argument(
            "exits[" + std::to_string(idx) + "]: 'side' must be 'long' or 'short'");

    std::optional<std::string> when;
    std::optional<double>      sl_atr;
    std::optional<double>      tp_atr;

    if (v.has("when")) {
        const auto& wv = v.get("when");
        if (!wv.is_string())
            throw std::invalid_argument("exits[" + std::to_string(idx) + "]: 'when' must be a string");
        when = wv.as_str();
    }
    if (v.has("sl_atr")) {
        const auto& sv = v.get("sl_atr");
        if (!sv.is_number())
            throw std::invalid_argument("exits[" + std::to_string(idx) + "]: 'sl_atr' must be a number");
        sl_atr = sv.as_double();
    }
    if (v.has("tp_atr")) {
        const auto& tv = v.get("tp_atr");
        if (!tv.is_number())
            throw std::invalid_argument("exits[" + std::to_string(idx) + "]: 'tp_atr' must be a number");
        tp_atr = tv.as_double();
    }
    if (!when && !sl_atr && !tp_atr) {
        throw std::invalid_argument(
            "exits[" + std::to_string(idx) + "]: must have 'when', 'sl_atr', or 'tp_atr'");
    }

    return ExitRule{side, when, sl_atr, tp_atr};
}

static RiskSpec parse_risk(const json::JsonValue& v) {
    if (!v.is_object())
        throw std::invalid_argument("risk: must be an object");

    RiskSpec r;

    if (v.has("size")) {
        if (!v.get("size").is_string())
            throw std::invalid_argument("risk: 'size' must be a string");
        r.size = v.get("size").as_str();
        if (VALID_SIZE_METHODS.find(r.size) == VALID_SIZE_METHODS.end())
            throw std::invalid_argument("risk: 'size' must be 'atr' or 'fixed'");
    }
    if (v.has("atr_mult")) {
        const auto& av = v.get("atr_mult");
        if (!av.is_number())
            throw std::invalid_argument("risk: 'atr_mult' must be a number");
        r.atr_mult = av.as_double();
    }
    if (v.has("fixed_lots")) {
        const auto& fv = v.get("fixed_lots");
        if (!fv.is_number())
            throw std::invalid_argument("risk: 'fixed_lots' must be a number");
        r.fixed_lots = fv.as_double();
    }
    if (v.has("max_concurrent")) {
        const auto& mv = v.get("max_concurrent");
        if (!mv.is_number())
            throw std::invalid_argument("risk: 'max_concurrent' must be an integer");
        r.max_concurrent = static_cast<int>(mv.as_int());
    }
    if (v.has("hedge")) {
        const auto& hv = v.get("hedge");
        if (!hv.is_bool())
            throw std::invalid_argument("risk: 'hedge' must be boolean");
        r.hedge = hv.as_bool();
    }
    if (v.has("cool_down_bars")) {
        const auto& cv = v.get("cool_down_bars");
        if (!cv.is_number())
            throw std::invalid_argument("risk: 'cool_down_bars' must be an integer");
        int cd = static_cast<int>(cv.as_int());
        if (cd < 0)
            throw std::invalid_argument("risk: 'cool_down_bars' must be >= 0");
        r.cool_down_bars = cd;
    }

    return r;
}

/* =========================================================================
 * Public API: parse_manifest
 * ========================================================================= */

AlgoManifest parse_manifest(const std::string& json_str) {
    json::JsonValue root;
    try {
        root = json::parse(json_str);
    } catch (const std::exception& e) {
        throw std::invalid_argument(std::string("manifest JSON parse error: ") + e.what());
    }

    if (!root.is_object())
        throw std::invalid_argument("manifest: root must be an object");

    // schema_version
    if (!root.has("schema_version") || !root.get("schema_version").is_string())
        throw std::invalid_argument("manifest: missing required field 'schema_version'");
    std::string schema_ver = root.get("schema_version").as_str();
    if (SUPPORTED_SCHEMA_VERSIONS.find(schema_ver) == SUPPORTED_SCHEMA_VERSIONS.end())
        throw std::invalid_argument("unsupported_schema_version: '" + schema_ver + "'");

    // name
    if (!root.has("name") || !root.get("name").is_string())
        throw std::invalid_argument("manifest: missing required field 'name'");
    std::string name = root.get("name").as_str();
    if (name.size() > 48)
        throw std::invalid_argument("manifest: 'name' exceeds 48 characters");
    if (!is_kebab_case(name))
        throw std::invalid_argument("manifest: 'name' must be kebab-case, got '" + name + "'");

    // description
    if (!root.has("description") || !root.get("description").is_string())
        throw std::invalid_argument("manifest: missing required field 'description'");
    std::string description = root.get("description").as_str();
    if (description.size() > 280)
        throw std::invalid_argument("manifest: 'description' exceeds 280 characters");

    // rationale
    if (!root.has("rationale") || !root.get("rationale").is_string())
        throw std::invalid_argument("manifest: missing required field 'rationale'");
    std::string rationale = root.get("rationale").as_str();
    if (rationale.size() > 1024)
        throw std::invalid_argument("manifest: 'rationale' exceeds 1024 characters");

    // timeframes
    if (!root.has("timeframes") || !root.get("timeframes").is_array())
        throw std::invalid_argument("manifest: missing required field 'timeframes'");
    std::vector<std::string> timeframes;
    const auto& tf_arr = root.get("timeframes").arr;
    if (tf_arr.empty())
        throw std::invalid_argument("manifest: 'timeframes' must be a non-empty list");
    for (const auto& tv : tf_arr) {
        if (!tv.is_string())
            throw std::invalid_argument("manifest: timeframes must be strings");
        std::string tf = tv.as_str();
        if (VALID_TIMEFRAMES.find(tf) == VALID_TIMEFRAMES.end())
            throw std::invalid_argument("manifest: unknown timeframe '" + tf + "'");
        timeframes.push_back(tf);
    }

    // symbols
    if (!root.has("symbols"))
        throw std::invalid_argument("manifest: missing required field 'symbols'");
    SymbolsSpec symbols;
    const auto& syms = root.get("symbols");
    if (syms.is_string()) {
        if (syms.as_str() != "any")
            throw std::invalid_argument("manifest: 'symbols' string must be \"any\"");
        symbols = std::string("any");
    } else if (syms.is_array()) {
        std::vector<std::string> sym_list;
        for (const auto& sv : syms.arr) {
            if (!sv.is_string() || sv.as_str().empty())
                throw std::invalid_argument("manifest: 'symbols' list elements must be non-empty strings");
            sym_list.push_back(sv.as_str());
        }
        symbols = sym_list;
    } else {
        throw std::invalid_argument("manifest: 'symbols' must be a list or the string \"any\"");
    }

    // indicators
    std::vector<IndicatorDecl> indicators;
    if (root.has("indicators")) {
        const auto& inds = root.get("indicators");
        if (!inds.is_array())
            throw std::invalid_argument("manifest: 'indicators' must be a list");
        std::set<std::string> seen_ids;
        for (int i = 0; i < (int)inds.arr.size(); ++i) {
            auto ind = parse_indicator(inds.arr[i], i);
            if (seen_ids.count(ind.id))
                throw std::invalid_argument("manifest: duplicate indicator id '" + ind.id + "'");
            seen_ids.insert(ind.id);
            indicators.push_back(ind);
        }
    }

    // entries
    std::vector<EntryRule> entries;
    if (root.has("entries")) {
        const auto& ents = root.get("entries");
        if (!ents.is_array())
            throw std::invalid_argument("manifest: 'entries' must be a list");
        for (int i = 0; i < (int)ents.arr.size(); ++i)
            entries.push_back(parse_entry_rule(ents.arr[i], i));
    }

    // exits
    std::vector<ExitRule> exits;
    if (root.has("exits")) {
        const auto& exts = root.get("exits");
        if (!exts.is_array())
            throw std::invalid_argument("manifest: 'exits' must be a list");
        for (int i = 0; i < (int)exts.arr.size(); ++i)
            exits.push_back(parse_exit_rule(exts.arr[i], i));
    }

    // risk
    RiskSpec risk;
    if (root.has("risk")) {
        risk = parse_risk(root.get("risk"));
    }

    // code (escape hatch)
    std::optional<std::string> code;
    if (root.has("code") && !root.get("code").is_null()) {
        const auto& cv = root.get("code");
        if (!cv.is_string())
            throw std::invalid_argument("manifest: 'code' must be a string or null");
        code = cv.as_str();
    }

    return AlgoManifest{
        schema_ver,
        name,
        description,
        rationale,
        timeframes,
        symbols,
        indicators,
        entries,
        exits,
        risk,
        code
    };
}

} /* namespace algoforge::algo_gen */
