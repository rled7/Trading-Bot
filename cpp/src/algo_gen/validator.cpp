/**
 * AlgoForge — src/algo_gen/validator.cpp
 * S1 Phase 1 — 5-stage validation pipeline orchestrator.
 *
 * Mirrors python/algoforge/algo_gen/validator.py logic.
 * Escape-hatch manifests (code != nullopt) return a deterministic skip record.
 */

#include "algo_gen_internal.hpp"
#include "core/analytics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace algoforge::algo_gen {

/* Forward declaration of build_tier_report (defined in tier.cpp) */
TierReport build_tier_report(
    double wf, double mc, double robustness,
    const std::string& manifest_name,
    const std::vector<StageResult>& stages);

/* Declaration of robustness function */
double parameter_robustness(
    const AlgoManifest& m,
    const std::vector<Bar>& bars,
    uint64_t seed,
    const BacktestConfig& cfg);

/* =========================================================================
 * Gate constants
 * ========================================================================= */

static constexpr int    MIN_TRADES   = 20;
static constexpr double SHARPE_GATE  = 0.5;
static constexpr double MAX_DD_GATE  = 25.0;
static constexpr int    WF_N_WINDOWS = 5;
static constexpr int    MC_N_RUNS    = 1000;

/* =========================================================================
 * Stage 1 — Schema + DSL lint
 * ========================================================================= */

static StageResult stage1_schema_lint(const AlgoManifest& m) {
    StageResult r;
    r.stage  = 1;
    r.name   = "schema_lint";

    // Already parsed (parse_manifest would have thrown). Check DSL compilation.
    if (!m.code) {
        std::vector<std::string> ind_ids;
        for (const auto& ind : m.indicators) ind_ids.push_back(ind.id);

        for (const auto& entry : m.entries) {
            try {
                dsl::compile_expr(entry.when, ind_ids);
            } catch (const std::exception& e) {
                r.passed = false;
                r.reason = std::string("entry rule DSL error: ") + e.what();
                return r;
            }
        }
        for (const auto& ex : m.exits) {
            if (ex.when) {
                try {
                    dsl::compile_expr(*ex.when, ind_ids);
                } catch (const std::exception& e) {
                    r.passed = false;
                    r.reason = std::string("exit rule DSL error: ") + e.what();
                    return r;
                }
            }
        }
    }

    r.passed = true;
    r.reason = "OK";
    return r;
}

/* =========================================================================
 * Stage 2 — Sandbox backtest
 * ========================================================================= */

static std::pair<SimpleBacktestResult, StageResult>
stage2_sandbox_backtest(const AlgoManifest& m,
                         const std::vector<Bar>& bars,
                         const BacktestConfig& cfg) {
    StageResult r;
    r.stage = 2;
    r.name  = "sandbox_backtest";

    SimpleBacktestResult bt;
    try {
        bt = run_manifest_backtest(m, bars, cfg);
    } catch (const std::exception& e) {
        r.passed = false;
        r.reason = std::string("backtest error: ") + e.what();
        return {bt, r};
    }

    if (bt.has_nan_inf) {
        r.passed = false;
        r.reason = "NaN or inf detected in equity curve";
        r.metrics["trades"] = std::to_string(bt.total_trades);
        return {bt, r};
    }
    if (bt.total_trades < MIN_TRADES) {
        r.passed = false;
        r.reason = "too few trades: " + std::to_string(bt.total_trades) +
                   " < " + std::to_string(MIN_TRADES);
        r.metrics["trades"] = std::to_string(bt.total_trades);
        return {bt, r};
    }

    r.passed = true;
    r.reason = "OK";
    r.metrics["trades"] = std::to_string(bt.total_trades);
    return {bt, r};
}

/* =========================================================================
 * Stage 3 — Walk-forward
 * ========================================================================= */

static std::pair<double, StageResult>
stage3_walk_forward(const AlgoManifest& m,
                     const std::vector<Bar>& bars,
                     const BacktestConfig& cfg,
                     int n_windows = WF_N_WINDOWS) {
    StageResult r;
    r.stage = 3;
    r.name  = "walk_forward";

    int n = (int)bars.size();
    int train_min = std::max(cfg.warmup_bars + 50, n / 3);

    // Use analytics walkforward_anchored
    int passing = 0;
    int n_folds_run = 0;

    auto bt_fn = [&](int /*ts*/, int te, int ss, int se)
        -> std::pair<analytics::Metrics, analytics::Metrics>
    {
        analytics::Metrics train_m, test_m;

        // Run backtest on test slice
        std::vector<Bar> test_slice(bars.begin() + ss, bars.begin() + se);
        if ((int)test_slice.size() < cfg.warmup_bars + 5) {
            return {train_m, test_m};
        }

        try {
            auto res = run_manifest_backtest(m, test_slice, cfg);
            test_m.sharpe  = res.sharpe;
            // Pack trades count into sharpe denominator-position so we can check
            // We use win_rate field to carry trade count (workaround)
            test_m.win_rate    = res.sharpe;
            test_m.expectancy  = res.max_dd_pct;
            test_m.profit_factor = (double)res.total_trades;
        } catch (...) {
            test_m.sharpe = 0.0;
        }
        return {train_m, test_m};
    };

    auto folds = analytics::walkforward_anchored(n, n_windows, train_min, bt_fn);

    if (folds.empty()) {
        r.passed = false;
        r.reason = "no folds produced (bars too few for walk-forward split)";
        return {0.0, r};
    }

    n_folds_run = (int)folds.size();
    for (const auto& fold : folds) {
        double sharpe   = fold.test_metrics.win_rate;
        double max_dd   = fold.test_metrics.expectancy;
        int    trades   = (int)fold.test_metrics.profit_factor;
        bool fold_passed = (sharpe >= SHARPE_GATE &&
                            max_dd <= MAX_DD_GATE &&
                            trades >= MIN_TRADES);
        if (fold_passed) passing++;
    }

    double score = n_folds_run > 0 ? (double)passing / n_folds_run * 100.0 : 0.0;
    bool   all_passed = (passing == n_folds_run);

    r.passed = all_passed;
    r.reason = std::to_string(passing) + "/" + std::to_string(n_folds_run) +
               " windows passed" + (all_passed ? "" : " (stage failed)");
    r.metrics["score"] = [&]{
        char buf[32]; snprintf(buf, sizeof(buf), "%.1f", score); return std::string(buf);
    }();

    return {score, r};
}

/* =========================================================================
 * Stage 4 — Monte Carlo bootstrap
 * ========================================================================= */

static std::pair<double, StageResult>
stage4_monte_carlo(const SimpleBacktestResult& bt, uint64_t seed) {
    StageResult r;
    r.stage = 4;
    r.name  = "mc_bootstrap";

    if (bt.trade_pnls.empty()) {
        r.passed = false;
        r.reason = "no trades to resample";
        return {0.0, r};
    }

    // Convert trade PnLs to analytics::Trade
    std::vector<analytics::Trade> trades;
    trades.reserve(bt.trade_pnls.size());
    for (double pnl : bt.trade_pnls) {
        trades.push_back({pnl, 1});
    }

    auto mc_res = analytics::mc_bootstrap(
        std::span<const analytics::Trade>(trades.data(), trades.size()),
        MC_N_RUNS,
        seed,
        bt.initial_capital
    );

    double initial     = bt.initial_capital;
    double p5_final    = mc_res.p5_final;
    double prob_ruin   = mc_res.prob_of_ruin;

    // Score heuristic (mirrors Python validator.py stage 4)
    double score;
    if (initial > 0 && p5_final < 0) {
        score = 0.0;
    } else if (prob_ruin > 0.5) {
        score = std::max(0.0, (1.0 - prob_ruin) * 100.0);
    } else {
        double ret_p5 = (initial > 1e-6) ? (p5_final - initial) / initial : 0.0;
        if (ret_p5 >= 0)
            score = std::min(100.0, 70.0 + ret_p5 * 60.0);
        else
            score = std::max(0.0, 70.0 + ret_p5 * 140.0);
        score = score * (1.0 - prob_ruin);
    }

    bool passed = (score >= 70.0);

    char score_buf[32];
    snprintf(score_buf, sizeof(score_buf), "%.1f", score);

    r.passed = passed;
    r.reason = std::string("MC score=") + score_buf + "%" +
               (passed ? "" : " (stage failed)");
    r.metrics["score"] = score_buf;

    return {score, r};
}

/* =========================================================================
 * Stage 5 — Parameter robustness
 * ========================================================================= */

static std::pair<double, StageResult>
stage5_robustness(const AlgoManifest& m,
                   const std::vector<Bar>& bars,
                   uint64_t seed,
                   const BacktestConfig& cfg) {
    StageResult r;
    r.stage = 5;
    r.name  = "parameter_robustness";

    double score = 0.0;
    try {
        score = parameter_robustness(m, bars, seed, cfg);
    } catch (const std::exception& e) {
        r.passed = false;
        r.reason = std::string("robustness error: ") + e.what();
        return {0.0, r};
    }

    bool passed = (score >= 70.0);
    char score_buf[32];
    snprintf(score_buf, sizeof(score_buf), "%.1f", score);

    r.passed = passed;
    r.reason = std::string("Robustness score=") + score_buf + "%" +
               (passed ? "" : " (stage failed)");
    r.metrics["score"] = score_buf;

    return {score, r};
}

/* =========================================================================
 * Public API: validate
 * ========================================================================= */

TierReport validate(const AlgoManifest& m,
                    const std::vector<Bar>& bars,
                    uint64_t seed) {
    TierReport report;
    report.manifest_name = m.name;
    report.passed        = false;
    report.has_tier      = false;

    // Escape-hatch: return deterministic skip
    if (m.code) {
        StageResult s;
        s.stage  = 1;
        s.name   = "schema_lint";
        s.passed = false;
        s.reason = "escape_hatch_skip_cpp";
        report.stages.push_back(s);
        return report;
    }

    BacktestConfig cfg;
    cfg.warmup_bars = std::min(200, std::max(20, (int)bars.size() / 4));

    // Stage 1
    StageResult s1 = stage1_schema_lint(m);
    report.stages.push_back(s1);
    if (!s1.passed) return report;

    // Stage 2
    auto [bt, s2] = stage2_sandbox_backtest(m, bars, cfg);
    report.stages.push_back(s2);
    if (!s2.passed) return report;

    // Stage 3
    auto [wf_score, s3] = stage3_walk_forward(m, bars, cfg);
    report.stages.push_back(s3);
    if (!s3.passed) return report;

    // Stage 4
    auto [mc_score, s4] = stage4_monte_carlo(bt, seed);
    report.stages.push_back(s4);
    if (!s4.passed) return report;

    // Stage 5
    auto [rob_score, s5] = stage5_robustness(m, bars, seed, cfg);
    report.stages.push_back(s5);
    if (!s5.passed) return report;

    // All passed — build tier report
    report = build_tier_report(wf_score, mc_score, rob_score, m.name, report.stages);
    return report;
}

} /* namespace algoforge::algo_gen */
