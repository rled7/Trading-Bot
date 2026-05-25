/**
 * AlgoForge — src/algo_gen/tier.cpp
 * S1 Phase 1 — Composite tier scorer.
 *
 * Mirrors python/algoforge/algo_gen/tier.py:
 *   tier = f(min(walk_forward, mc, robustness))
 *   thresholds: white>=95, green>=90, yellow>=80, orange>=70, else red
 */

#include "core/algo_gen.hpp"

namespace algoforge::algo_gen {

/* =========================================================================
 * Tier enum helpers
 * ========================================================================= */

const char* tier_name(Tier t) noexcept {
    switch (t) {
        case Tier::Red:    return "red";
        case Tier::Orange: return "orange";
        case Tier::Yellow: return "yellow";
        case Tier::Green:  return "green";
        case Tier::White:  return "white";
    }
    return "red";
}

const char* tier_color(Tier t) noexcept {
    switch (t) {
        case Tier::Red:    return "#ff4444";
        case Tier::Orange: return "#ff8800";
        case Tier::Yellow: return "#ffdd00";
        case Tier::Green:  return "#00ff88";
        case Tier::White:  return "#ffffff";
    }
    return "#ff4444";
}

/* =========================================================================
 * score_to_tier — same thresholds as spec §6 defaults
 * ========================================================================= */

Tier score_to_tier(double wf, double mc, double robustness) noexcept {
    double min_score = wf;
    if (mc         < min_score) min_score = mc;
    if (robustness < min_score) min_score = robustness;

    if (min_score >= 95.0) return Tier::White;
    if (min_score >= 90.0) return Tier::Green;
    if (min_score >= 80.0) return Tier::Yellow;
    if (min_score >= 70.0) return Tier::Orange;
    return Tier::Red;
}

/* =========================================================================
 * Size multiplier and flags per tier (§6 defaults)
 * ========================================================================= */

static double size_mult_for_tier(Tier t) noexcept {
    switch (t) {
        case Tier::Red:    return 0.0;
        case Tier::Orange: return 0.25;
        case Tier::Yellow: return 0.50;
        case Tier::Green:  return 1.00;
        case Tier::White:  return 1.50;
    }
    return 0.0;
}

static bool is_experimental(Tier t) noexcept {
    // orange and yellow are experimental
    return t == Tier::Orange || t == Tier::Yellow || t == Tier::Red;
}

static bool is_paper_only(Tier t) noexcept {
    // paper_only_below = yellow (spec default): orange and red
    return t == Tier::Orange || t == Tier::Red;
}

/* =========================================================================
 * build_tier_report — helper used by validator
 * ========================================================================= */

TierReport build_tier_report(
    double wf, double mc, double robustness,
    const std::string& manifest_name,
    const std::vector<StageResult>& stages
) {
    TierReport r;
    r.passed        = true;
    r.has_tier      = true;
    r.manifest_name = manifest_name;
    r.walk_forward  = wf;
    r.mc_bootstrap  = mc;
    r.robustness    = robustness;
    r.min_score     = std::min({wf, mc, robustness});
    r.tier          = score_to_tier(wf, mc, robustness);
    r.size_mult     = size_mult_for_tier(r.tier);
    r.experimental  = is_experimental(r.tier);
    r.paper_only    = is_paper_only(r.tier);
    r.stages        = stages;
    return r;
}

} /* namespace algoforge::algo_gen */
