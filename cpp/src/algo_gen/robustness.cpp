/**
 * AlgoForge — src/algo_gen/robustness.cpp
 * S1 Phase 1 — Parameter robustness sweep.
 *
 * Mirrors python/algoforge/algo_gen/robustness.py logic exactly:
 *   1. Collect numeric params from indicators + risk.
 *   2. Finite-difference Sharpe gradient (quick 50-bar replay).
 *   3. Top-3 by gradient.
 *   4. 27-cell Cartesian product (±20%, 0%).
 *   5. % of cells passing the gate.
 */

#include "algo_gen_internal.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace algoforge::algo_gen {

static const double PERTURB_FRAC = 0.20;
static const int    MAX_PARAMS   = 3;
static const int    QUICK_BARS   = 50;

/* =========================================================================
 * Collect numeric parameters as (path, value) pairs
 * ========================================================================= */

static std::vector<std::pair<std::string, double>>
collect_numeric_params(const AlgoManifest& m) {
    std::vector<std::pair<std::string, double>> params;

    // Indicator params
    for (const auto& ind : m.indicators) {
        for (const auto& [k, v] : ind.params) {
            params.push_back({"ind." + ind.id + "." + k, v});
        }
    }

    // Risk params
    if (m.risk.size == "atr") {
        params.push_back({"risk.atr_mult", m.risk.atr_mult});
    } else {
        params.push_back({"risk.fixed_lots", m.risk.fixed_lots});
    }
    if (m.risk.cool_down_bars > 0) {
        params.push_back({"risk.cool_down_bars", (double)m.risk.cool_down_bars});
    }

    return params;
}

/* =========================================================================
 * Apply a single parameter mutation
 * ========================================================================= */

static AlgoManifest apply_param(const AlgoManifest& m,
                                 const std::string& path,
                                 double value) {
    AlgoManifest nm = m;

    // Indicator param
    if (path.substr(0, 4) == "ind.") {
        // path = "ind.<id>.<param_name>"
        size_t p1 = 4;
        size_t p2 = path.find('.', p1);
        if (p2 == std::string::npos) return nm;
        std::string ind_id    = path.substr(p1, p2 - p1);
        std::string param_name = path.substr(p2 + 1);

        nm.indicators.clear();
        for (const auto& ind : m.indicators) {
            IndicatorDecl ni = ind;
            if (ni.id == ind_id && ni.params.count(param_name)) {
                double orig = ni.params[param_name];
                // If original was integer-like, round the perturbed value
                if (std::floor(orig) == orig) {
                    ni.params[param_name] = std::max(1.0, std::round(value));
                } else {
                    ni.params[param_name] = value;
                }
            }
            nm.indicators.push_back(ni);
        }
        return nm;
    }

    // Risk param
    if (path == "risk.atr_mult") {
        nm.risk.atr_mult = std::max(0.1, value);
    } else if (path == "risk.fixed_lots") {
        nm.risk.fixed_lots = std::max(0.001, value);
    } else if (path == "risk.cool_down_bars") {
        nm.risk.cool_down_bars = std::max(0, (int)std::round(value));
    }
    return nm;
}

/* =========================================================================
 * Public API: parameter_robustness
 * ========================================================================= */

double parameter_robustness(
    const AlgoManifest& m,
    const std::vector<Bar>& bars,
    uint64_t seed,
    const BacktestConfig& cfg
) {
    auto all_params = collect_numeric_params(m);

    if (all_params.empty()) {
        // No numeric params → perfect robustness
        return 100.0;
    }

    // Quick-replay bars
    int quick_n = std::max((int)bars.size() / 4,
                           std::min(QUICK_BARS + cfg.warmup_bars, (int)bars.size()));
    std::vector<Bar> quick_bars(bars.begin(), bars.begin() + quick_n);

    // Baseline sharpe on quick bars
    double baseline_sharpe = 0.0;
    try {
        auto r = run_manifest_backtest(m, quick_bars, cfg);
        baseline_sharpe = r.sharpe;
    } catch (...) {}

    // Finite-difference gradient for each param
    std::vector<std::tuple<double, std::string, double>> gradients;

    for (const auto& [path, val] : all_params) {
        double delta = (std::abs(val) > 1e-6) ? std::abs(val) * PERTURB_FRAC : 0.1;

        double s_plus  = baseline_sharpe;
        double s_minus = baseline_sharpe;

        try {
            auto mp = apply_param(m, path, val + delta);
            auto r  = run_manifest_backtest(mp, quick_bars, cfg);
            s_plus  = r.sharpe;
        } catch (...) {}

        try {
            auto mm = apply_param(m, path, val - delta);
            auto r  = run_manifest_backtest(mm, quick_bars, cfg);
            s_minus = r.sharpe;
        } catch (...) {}

        double grad = (delta > 1e-12) ?
            std::abs(s_plus - s_minus) / (2.0 * delta) : 0.0;
        gradients.push_back({grad, path, val});
    }

    // Sort descending by gradient
    std::sort(gradients.begin(), gradients.end(),
              [](const auto& a, const auto& b) {
                  return std::get<0>(a) > std::get<0>(b);
              });

    // Top-3
    int n_top = std::min(MAX_PARAMS, (int)gradients.size());
    std::vector<std::tuple<double, std::string, double>> top(
        gradients.begin(), gradients.begin() + n_top);

    if (top.empty()) return 100.0;

    // Cartesian product: -20%, 0%, +20% for each selected param
    std::vector<std::vector<double>> perturb_vals;
    for (const auto& [grad, path, val] : top) {
        double delta = (std::abs(val) > 1e-6) ? std::abs(val) * PERTURB_FRAC : 0.1;
        perturb_vals.push_back({val - delta, val, val + delta});
    }

    // Generate Cartesian product
    // For up to 3 params: max 27 combos
    int pass_count = 0;
    int total = 0;

    // Build index arrays for Cartesian product
    std::vector<int> idxs(n_top, 0);
    while (true) {
        // Build perturbed manifest
        AlgoManifest pm = m;
        for (int k = 0; k < n_top; ++k) {
            const auto& [grad, path, val] = top[k];
            pm = apply_param(pm, path, perturb_vals[k][idxs[k]]);
        }

        try {
            auto r = run_manifest_backtest(pm, bars, cfg);
            if (passes_gate(r)) pass_count++;
        } catch (...) {}
        total++;

        // Increment indices (Cartesian product)
        int carry = 1;
        for (int k = n_top - 1; k >= 0 && carry; --k) {
            idxs[k] += carry;
            if (idxs[k] >= 3) { idxs[k] = 0; carry = 1; }
            else               { carry = 0; }
        }
        if (carry) break;  // overflow → done
    }

    return total > 0 ? (double)pass_count / total * 100.0 : 0.0;
}

} /* namespace algoforge::algo_gen */
