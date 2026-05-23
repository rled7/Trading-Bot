/**
 * AlgoForge — include/core/analytics.hpp
 * S4 Analytics Layer — C++20 header for namespace algoforge::analytics.
 *
 * All result structs, free functions, OnlineMetrics class, and LCG RNG are
 * declared here.  Implementations live in cpp/src/analytics/ (one .cpp per module).
 *
 * Design rules (from spec §6 C++ conventions):
 *   - Namespace algoforge::analytics
 *   - std::span<const double> for input ranges
 *   - Return struct aggregates by value
 *   - noexcept on free functions where applicable
 *   - No external deps — stdlib only
 *   - C++20
 */
#pragma once

#include "core/types.h"

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace algoforge::analytics {

/* =========================================================================
 * §3  Input data shapes
 * ========================================================================= */

/**
 * A single trade result.  net_pnl is the dollar PnL; bars_held is the number
 * of bars the position was open (used for time_in_market).
 */
struct Trade {
    double  net_pnl   = 0.0;
    int     bars_held = 0;
};

/**
 * One point on an equity curve.
 */
struct EquityPoint {
    int64_t timestamp = 0;   /* unix seconds */
    double  equity    = 0.0;
};

/* =========================================================================
 * §4  Result structs
 * ========================================================================= */

/** All 14 performance metrics. */
struct Metrics {
    double sharpe              = 0.0;
    double sortino             = 0.0;
    double calmar              = 0.0;
    double omega               = 0.0;
    double profit_factor       = 0.0;
    double win_rate            = 0.0;
    double expectancy          = 0.0;
    double avg_win             = 0.0;
    double avg_loss            = 0.0;   /* negative or 0 */
    double payoff_ratio        = 0.0;
    double recovery_factor     = 0.0;
    double time_in_market      = 0.0;
    int    longest_win_streak  = 0;
    int    longest_loss_streak = 0;
    /* alias */
    int    max_consec_losses() const noexcept { return longest_loss_streak; }
};

/** One contiguous drawdown episode. */
struct DrawdownEpisode {
    int    start_idx     = 0;
    int    trough_idx    = 0;
    int    recover_idx   = -1;   /* -1 = not recovered */
    double depth_pct     = 0.0;  /* negative */
    double depth_abs     = 0.0;  /* negative */
    int    duration_bars = 0;
    int    recovery_bars = -1;   /* -1 = not recovered */
};

/** Drawdown summary from an equity curve. */
struct DrawdownSummary {
    double              max_dd_pct  = 0.0;   /* negative */
    double              max_dd_abs  = 0.0;   /* negative */
    std::vector<double> dd_pct_series;
    std::vector<double> dd_abs_series;
    std::vector<DrawdownEpisode> episodes;
};

/** Descriptive statistics for a numeric series. */
struct DistributionStats {
    int    n               = 0;
    double mean            = 0.0;
    double std_dev         = 0.0;   /* sample std dev */
    double min_val         = 0.0;
    double max_val         = 0.0;
    double skewness        = 0.0;   /* Fisher-Pearson adjusted G1 */
    double excess_kurtosis = 0.0;   /* Fisher adjusted G2 */
    double p25             = 0.0;
    double p50             = 0.0;
    double p75             = 0.0;
};

/** Monte Carlo bootstrap result. */
struct MonteCarloResult {
    double p5_final        = 0.0;
    double p50_final       = 0.0;
    double p95_final       = 0.0;
    std::vector<double> p5_path;
    std::vector<double> p50_path;
    std::vector<double> p95_path;
    double prob_of_ruin    = 0.0;
};

/** OLS regression result. */
struct OLSResult {
    std::vector<double> beta;         /* n_features+1 coefficients (intercept first) */
    std::vector<double> residuals;
    double r2              = 0.0;
    double adj_r2          = 0.0;
    std::vector<double> t_stats;
    std::vector<double> std_errors;
};

/** Fama-French-style factor regression result. */
struct FactorResult {
    double alpha           = 0.0;
    std::vector<std::string> factor_names;
    std::vector<double> betas;
    std::vector<double> t_stats;
    double r2              = 0.0;
    double adj_r2          = 0.0;
};

/** Symmetric Pearson correlation matrix. */
struct CorrelationMatrix {
    std::vector<std::string>         symbols;
    std::vector<std::vector<double>> matrix;   /* [i][j] = r(i,j) */
};

/* =========================================================================
 * §4  Seeded LCG (must match across all languages)
 *
 *   state = seed  (uint64, unsigned wrap)
 *   next(): state = state * 6364136223846793005 + 1442695040888963407  (mod 2^64)
 *           return state >> 33   (uint32)
 * ========================================================================= */

constexpr uint64_t LCG_MUL = 6364136223846793005ULL;
constexpr uint64_t LCG_ADD = 1442695040888963407ULL;

inline uint32_t lcg_next(uint64_t& state) noexcept {
    state = state * LCG_MUL + LCG_ADD;
    return static_cast<uint32_t>(state >> 33);
}

/* =========================================================================
 * §4  Free function declarations — batch computations
 * ========================================================================= */

/* ── metrics.cpp ── */

/**
 * Compute all 14 metrics from trades + equity curve.
 *
 * @param trades          Slice of trades (each has net_pnl, bars_held).
 * @param bar_returns     Fractional per-bar returns = (eq[i]-eq[i-1])/eq[i-1].
 * @param max_dd_pct      Pre-computed max drawdown % (negative, from drawdown module).
 * @param max_dd_abs      Pre-computed max drawdown abs (negative).
 * @param total_bars      Total bars in backtest (for time_in_market).
 * @param annualisation   sqrt(252) by default.
 */
Metrics compute_metrics(
    std::span<const Trade>  trades,
    std::span<const double> bar_returns,
    double                  max_dd_pct,
    double                  max_dd_abs,
    int                     total_bars,
    double                  annualisation = 15.874507866387544  /* sqrt(252) */
) noexcept;

/* ── drawdown.cpp ── */

/**
 * Full drawdown analysis from an equity curve.
 */
DrawdownSummary compute_drawdown(std::span<const double> equity) noexcept;

/**
 * Top-5 drawdown episodes sorted by depth_pct ascending (deepest first).
 */
std::vector<DrawdownEpisode> top5_drawdowns(std::span<const double> equity) noexcept;

/* ── distributions.cpp ── */

/**
 * Compute descriptive statistics including adjusted skewness and kurtosis.
 */
DistributionStats compute_distribution(std::span<const double> values) noexcept;

/**
 * Linear-interpolation percentile (numpy 'linear' / R-7 method).
 * pct is in [0, 100].
 */
double percentile_linear(std::span<const double> sorted_values, double pct) noexcept;

/* ── correlations.cpp ── */

/**
 * Pearson correlation between two equal-length series.
 * Returns 0.0 if std of either series is 0 or n < 2.
 */
double pearson_correlation(
    std::span<const double> x,
    std::span<const double> y
) noexcept;

/**
 * Pearson correlation matrix over multiple return series.
 * Truncates all series to the length of the shortest one.
 */
CorrelationMatrix correlation_matrix(
    const std::unordered_map<std::string, std::vector<double>>& returns_per_symbol
) noexcept;

/**
 * Rolling beta = cov(target, baseline) / var(baseline) on a sliding window.
 * Returns empty vector if n < window.
 */
std::vector<double> rolling_beta(
    std::span<const double> target,
    std::span<const double> baseline,
    int                     window
) noexcept;

/* ── montecarlo.cpp ── */

/**
 * Bootstrap Monte Carlo simulation over trade PnL.
 */
MonteCarloResult mc_bootstrap(
    std::span<const Trade> trades,
    int                    n_runs,
    uint64_t               seed,
    double                 initial_capital = 10000.0
) noexcept;

/* ── walkforward.cpp ── */

/** Per-fold result from walk-forward analysis. */
struct WalkForwardFold {
    int     fold_index  = 0;
    int     train_start = 0;
    int     train_end   = 0;   /* exclusive */
    int     test_start  = 0;
    int     test_end    = 0;   /* exclusive */
    Metrics train_metrics;
    Metrics test_metrics;
};

/**
 * Backtest callback type: given train [0,train_end) and test [test_start,test_end),
 * returns {train_metrics, test_metrics}.
 * Caller supplies an std::function wrapping their backtest logic.
 */
using BacktestFn = std::function<std::pair<Metrics, Metrics>(int train_start, int train_end, int test_start, int test_end)>;

/**
 * Anchored walk-forward: training window always starts from bar 0.
 *
 * @param n_bars          Total number of bars.
 * @param n_folds         Number of folds.
 * @param train_min_bars  Minimum training bars in the first fold.
 * @param backtest_fn     Callback that runs the backtest.
 */
std::vector<WalkForwardFold> walkforward_anchored(
    int           n_bars,
    int           n_folds,
    int           train_min_bars,
    BacktestFn    backtest_fn
);

/**
 * Rolling walk-forward: fixed-size sliding training window.
 */
std::vector<WalkForwardFold> walkforward_rolling(
    int           n_bars,
    int           train_window,
    int           test_window,
    int           step,
    BacktestFn    backtest_fn
);

/* ── ml_attribution.cpp ── */

/**
 * OLS linear regression.
 *
 * @param X   n x k matrix stored row-major (with intercept column prepended by caller).
 * @param y   n-vector of observations.
 * @param n   Number of observations.
 * @param k   Number of columns (including intercept).
 */
OLSResult ols_regression(
    const std::vector<std::vector<double>>& X,
    std::span<const double>                  y
) noexcept;

/* ── factors.cpp ── */

/**
 * Fama-French-style multi-factor regression.
 *
 * @param strategy_returns  Per-bar strategy returns (length n).
 * @param factor_returns    Map of factor name -> return series (each length n).
 */
FactorResult factor_regression(
    std::span<const double>                                       strategy_returns,
    const std::unordered_map<std::string, std::vector<double>>&   factor_returns
) noexcept;

/* ── html_report.cpp ── */

/** All data needed to build an HTML report. */
struct ReportData {
    std::string                      symbol        = "UNKNOWN";
    Metrics                          metrics;
    DrawdownSummary                  drawdown;
    std::vector<double>              equity_curve;
    DistributionStats                dist_stats;
    MonteCarloResult                 mc_result;
    std::vector<WalkForwardFold>     walkforward_folds;
    OLSResult                        attribution;     /* optional — empty beta = omit section */
    FactorResult                     factor_result;   /* optional — empty betas = omit section */
};

/**
 * Emit a self-contained HTML report string.
 */
std::string generate_html_report(const ReportData& data) noexcept;

/* =========================================================================
 * §4  OnlineMetrics — streaming incremental updates
 * ========================================================================= */

/**
 * OnlineMetrics maintains running state and accepts trades one at a time.
 * Internally uses Welford's algorithm for mean/variance.
 *
 * Thread-safety: none. Single-threaded use only.
 */
class OnlineMetrics {
public:
    OnlineMetrics() noexcept = default;

    /** Process one new trade. */
    void update(const Trade& trade) noexcept;

    /** Return snapshot of all current metrics. */
    Metrics snapshot() const noexcept;

    /** Reset all accumulated state. */
    void reset() noexcept;

    /* ── Accessors for individual running values ── */
    int    n_trades()      const noexcept { return n_trades_; }
    int    n_wins()        const noexcept { return n_wins_; }
    int    n_losses()      const noexcept { return n_losses_; }
    double running_equity()const noexcept { return running_equity_; }
    double peak_equity()   const noexcept { return peak_equity_; }
    double max_dd_abs()    const noexcept { return max_dd_abs_; }
    double max_dd_pct()    const noexcept { return max_dd_pct_; }

private:
    /* Trade counts */
    int    n_trades_       = 0;
    int    n_wins_         = 0;
    int    n_losses_       = 0;
    int    total_bars_held_= 0;

    /* Welford running mean/variance for PnL */
    double welford_mean_   = 0.0;
    double welford_m2_     = 0.0;   /* sum of squared deltas */

    /* Win/loss PnL accumulators */
    double sum_winning_pnl_    = 0.0;
    double sum_losing_pnl_abs_ = 0.0;

    /* Streaks */
    int    current_streak_     = 0;   /* + = win streak, - = loss streak */
    int    longest_win_streak_ = 0;
    int    longest_loss_streak_= 0;

    /* Drawdown */
    double initial_equity_  = 10000.0;
    double running_equity_  = 10000.0;
    double peak_equity_     = 10000.0;
    double max_dd_abs_      = 0.0;   /* most negative seen */
    double max_dd_pct_      = 0.0;   /* most negative % seen */
};

} /* namespace algoforge::analytics */
