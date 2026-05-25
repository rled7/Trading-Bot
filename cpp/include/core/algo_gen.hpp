/**
 * AlgoForge — include/core/algo_gen.hpp
 * S1 Phase 1 — Algorithm Generator: validator + tier scorer.
 *
 * Cross-language parity with python/algoforge/algo_gen/ (commit 99e2d0f).
 * LLM generation and escape-hatch sandbox remain Python-only (§10).
 * Escape-hatch manifests return a deterministic "skip" TierReport.
 *
 * Design rules:
 *   - Namespace algoforge::algo_gen
 *   - C++17: std::variant, std::optional, std::filesystem
 *   - snake_case fields, hand-rolled JSON (no external deps)
 *   - Reuses algoforge::analytics LCG / mc_bootstrap / walkforward_anchored
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace algoforge::algo_gen {

/* =========================================================================
 * §6  Tier enum
 * ========================================================================= */

enum class Tier {
    Red,     ///< min_score < 70 — not promoted
    Orange,  ///< 70 <= min_score < 80
    Yellow,  ///< 80 <= min_score < 90
    Green,   ///< 90 <= min_score < 95
    White,   ///< min_score >= 95
};

const char* tier_name(Tier t) noexcept;   ///< "red" / "orange" / ...
const char* tier_color(Tier t) noexcept;  ///< hex color string

/* =========================================================================
 * §3  Manifest types
 * ========================================================================= */

struct IndicatorDecl {
    std::string                      id;
    std::string                      kind;
    std::map<std::string, double>    params;   ///< numeric/bool params
};

struct EntryRule {
    std::string side;   ///< "long" | "short"
    std::string when;   ///< DSL expression
};

struct ExitRule {
    std::string                side;
    std::optional<std::string> when;    ///< DSL expression (may be absent)
    std::optional<double>      sl_atr;
    std::optional<double>      tp_atr;
};

struct RiskSpec {
    std::string size          = "atr";   ///< "atr" | "fixed"
    double      atr_mult      = 1.5;
    double      fixed_lots    = 0.01;
    int         max_concurrent = 1;
    bool        hedge          = false;
    int         cool_down_bars = 0;
};

/** symbols is either the string "any" or a list of symbol strings. */
using SymbolsSpec = std::variant<std::string, std::vector<std::string>>;

struct AlgoManifest {
    std::string                   schema_version;
    std::string                   name;
    std::string                   description;
    std::string                   rationale;
    std::vector<std::string>      timeframes;
    SymbolsSpec                   symbols;
    std::vector<IndicatorDecl>    indicators;
    std::vector<EntryRule>        entries;
    std::vector<ExitRule>         exits;
    RiskSpec                      risk;
    std::optional<std::string>    code;       ///< escape-hatch (DSL path if absent)
};

/* =========================================================================
 * §5  Validation pipeline result types
 * ========================================================================= */

struct StageResult {
    int         stage  = 0;
    std::string name;
    bool        passed = false;
    std::string reason;
    std::map<std::string, std::string> metrics;  ///< serialised as JSON strings
};

struct TierReport {
    bool         passed       = false;
    std::string  manifest_name;
    Tier         tier         = Tier::Red;
    bool         has_tier     = false;   ///< false when validation failed early
    double       min_score    = 0.0;
    double       walk_forward = 0.0;
    double       mc_bootstrap = 0.0;
    double       robustness   = 0.0;
    double       size_mult    = 0.0;
    bool         experimental = true;
    bool         paper_only   = true;
    std::vector<StageResult> stages;
};

/* =========================================================================
 * §3 / §1  Parse + validate a manifest JSON string
 * ========================================================================= */

/**
 * Parse a JSON string into an AlgoManifest.
 * Throws std::invalid_argument with a descriptive message on any schema error.
 */
AlgoManifest parse_manifest(const std::string& json);

/* =========================================================================
 * §5  Validate — runs all 5 stages, halts on first failure
 * ========================================================================= */

/**
 * A simple C-struct bar compatible with the C++ backtest engine.
 */
struct Bar {
    int64_t timestamp = 0;
    double  open      = 0.0;
    double  high      = 0.0;
    double  low       = 0.0;
    double  close     = 0.0;
    double  volume    = 0.0;
    double  spread    = 0.0;
};

/**
 * Run the 5-stage validation pipeline.
 *
 * @param m     Parsed manifest (from parse_manifest).
 * @param bars  Historical bar data.
 * @param seed  Seed for Monte Carlo bootstrap (default 42).
 *
 * Escape-hatch manifests (m.code != nullopt) return a deterministic skip
 * record (passed=false, reason="escape_hatch_skip_cpp").
 */
TierReport validate(const AlgoManifest& m,
                    const std::vector<Bar>& bars,
                    uint64_t seed = 42);

/* =========================================================================
 * §6  Tier scoring
 * ========================================================================= */

/**
 * Assign a Tier from three component scores (each in [0, 100]).
 * Tier = function of min(walk_forward, mc_bootstrap, robustness).
 */
Tier score_to_tier(double wf, double mc, double robustness) noexcept;

/* =========================================================================
 * §7  JSON serialisation round-trip
 * ========================================================================= */

std::string   tier_report_to_json(const TierReport& r);
TierReport    tier_report_from_json(const std::string& json);

/* =========================================================================
 * §7  Registry promotion
 * ========================================================================= */

/**
 * Promote a passing manifest into registry_dir.
 *
 * Writes registry_dir/<name>.json with embedded _metadata.
 * Throws std::runtime_error if tier == Red or if registry_dir cannot be created.
 *
 * @param confirm  Required for Orange/Yellow tier (manual review gate).
 */
void promote(const TierReport& report,
             const AlgoManifest& manifest,
             const std::filesystem::path& registry_dir,
             bool confirm = false);

} /* namespace algoforge::algo_gen */
