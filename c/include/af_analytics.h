/**
 * AlgoForge — c/include/af_analytics.h
 *
 * Single public header for the S4 analytics layer.
 * All structs, enums, and function prototypes for:
 *   metrics, drawdown, distributions, correlations, monte carlo,
 *   walk-forward, OLS regression, factor regression, online metrics,
 *   and HTML report generation.
 *
 * Conventions (from project style guide):
 *   - af_ prefix on all public symbols
 *   - snake_case throughout
 *   - output params via pointers
 *   - return af_error_t for fallible ops
 *   - caller supplies output buffers + capacity (no malloc in hot paths)
 *   - C17 standard
 */
#ifndef AF_ANALYTICS_H
#define AF_ANALYTICS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "af_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Section 1 — Trade input types
 * ============================================================ */

/**
 * af_trade_t — minimal trade record consumed by analytics.
 * Callers can populate from af_bt_trade_t or from any source.
 */
typedef struct {
    double  net_pnl;      /* net profit/loss in USD */
    int     bars_held;    /* number of bars the trade was open */
} af_trade_t;

/**
 * af_equity_point_t — one point on the equity curve.
 */
typedef struct {
    int64_t timestamp;    /* unix seconds */
    double  equity;       /* equity in USD */
} af_equity_point_t;

/* ============================================================
 * Section 2 — Result structs
 * ============================================================ */

/**
 * af_metrics_t — the 14 canonical performance metrics plus drawdown summary.
 * Populated by af_metrics_compute().
 */
typedef struct {
    /* Trade-level metrics */
    double  win_rate;              /* [0,1] */
    double  expectancy;            /* mean net PnL */
    double  avg_win;               /* mean net PnL of winning trades (>=0) */
    double  avg_loss;              /* mean net PnL of losing trades (<=0) */
    double  payoff_ratio;          /* avg_win / |avg_loss|; inf if no losers */
    double  profit_factor;         /* sum_wins / |sum_losses|; inf if no losses */
    double  recovery_factor;       /* total_pnl / max_dd_abs; 0 if mdd_abs==0 */
    double  time_in_market;        /* sum(bars_held) / total_bars */
    int     longest_win_streak;
    int     longest_loss_streak;
    int     max_consec_losses;     /* alias for longest_loss_streak */
    int     n_wins;
    int     n_losses;
    int     n_trades;

    /* Bar-return metrics (require equity curve) */
    double  sharpe;
    double  sortino;
    double  calmar;
    double  omega;

    /* Drawdown summary */
    double  max_dd_pct;            /* most negative, e.g. -3.92 */
    double  max_dd_abs;            /* most negative absolute */
    double  total_pnl;             /* sum of all net_pnl */
} af_metrics_t;

/**
 * af_drawdown_episode_t — one contiguous drawdown episode.
 */
typedef struct {
    int    start_idx;       /* index where drawdown begins */
    int    trough_idx;      /* index of maximum depth */
    int    recover_idx;     /* first index where equity >= peak; -1 if unrecovered */
    double depth_pct;       /* depth in percent (negative) */
    double depth_abs;       /* depth in USD (negative) */
    int    duration_bars;   /* bars from start to recover (or end) */
    int    recovery_bars;   /* bars from trough to recover; -1 if unrecovered */
} af_drawdown_episode_t;

/**
 * af_distribution_stats_t — descriptive stats for a return series.
 */
typedef struct {
    double mean;
    double std_sample;      /* sample std (N-1 denominator) */
    double skewness;        /* Fisher-Pearson adjusted (G1) */
    double kurtosis;        /* excess kurtosis, adjusted (G2) */
    double q05;             /* 5th percentile */
    double q25;
    double q50;
    double q75;
    double q95;             /* 95th percentile */
    double min;
    double max;
    int    n;
} af_distribution_stats_t;

/**
 * af_correlation_matrix_t — pairwise Pearson correlation matrix.
 * values[i*n + j] = correlation between series i and series j.
 * Caller supplies the flat values[] array of capacity n*n.
 */
typedef struct {
    double *values;     /* caller-owned flat array, row-major, size n*n */
    int     n;          /* number of series */
} af_correlation_matrix_t;

/* Maximum dimensions for OLS / factor regression */
#define AF_OLS_MAX_FEATURES 64
#define AF_OLS_MAX_SAMPLES  8192

/**
 * af_ols_result_t — result of an OLS regression.
 * beta[0] = intercept (if intercept column prepended to X).
 */
typedef struct {
    double beta[AF_OLS_MAX_FEATURES];
    double residuals[AF_OLS_MAX_SAMPLES];
    double std_errors[AF_OLS_MAX_FEATURES];
    double t_stats[AF_OLS_MAX_FEATURES];
    double r2;
    double adj_r2;
    int    n;           /* number of observations */
    int    k;           /* number of features (including intercept if any) */
} af_ols_result_t;

/**
 * af_factor_result_t — Fama-French-style factor regression.
 * alpha = intercept (annualised interpretation is caller's concern).
 */
typedef struct {
    double alpha;                        /* strategy alpha */
    double betas[AF_OLS_MAX_FEATURES];   /* factor loadings */
    double t_stats[AF_OLS_MAX_FEATURES]; /* t-statistics for each factor beta */
    double alpha_t_stat;                 /* t-stat for alpha */
    double r2;
    double adj_r2;
    int    n_factors;
} af_factor_result_t;

/* Maximum number of Monte Carlo runs and path length */
#define AF_MC_MAX_RUNS   10000
#define AF_MC_MAX_PATH   8192

/**
 * af_mc_result_t — result of a Monte Carlo bootstrap simulation.
 * p5_path/p50_path/p95_path have length (n_trades + 1).
 * Caller must supply path arrays of capacity at least n_trades+1.
 */
typedef struct {
    double  p5_final;
    double  p50_final;
    double  p95_final;
    double *p5_path;         /* caller-supplied array, length path_len */
    double *p50_path;
    double *p95_path;
    int     path_len;        /* n_trades + 1 */
    double  prob_of_ruin;
} af_mc_result_t;

/**
 * af_wf_fold_t — metrics for one walk-forward fold.
 */
typedef struct {
    int           fold_idx;
    int           train_start;
    int           train_end;
    int           test_start;
    int           test_end;
    af_metrics_t  train_metrics;
    af_metrics_t  test_metrics;
} af_wf_fold_t;

#define AF_WF_MAX_FOLDS 64

/**
 * af_wf_result_t — aggregated walk-forward results.
 */
typedef struct {
    af_wf_fold_t folds[AF_WF_MAX_FOLDS];
    int          n_folds;
} af_wf_result_t;

/**
 * af_online_metrics_t — incrementally updated metrics (Welford algorithm).
 * Call af_online_init() once, then af_online_update() per trade.
 * Access current snapshot via af_online_snapshot().
 */
typedef struct {
    /* Welford state */
    int    n_trades;
    double welford_mean;
    double welford_m2;

    /* Win/loss counters */
    int    n_wins;
    int    n_losses;
    double sum_pnl;
    double sum_winning_pnl;
    double sum_losing_pnl_abs;  /* positive, sum of |losing PnL| */

    /* Streak tracking */
    int    current_streak;       /* positive = win streak, negative = loss streak */
    int    longest_win_streak;
    int    longest_loss_streak;

    /* Equity and drawdown */
    double initial_capital;
    double running_equity;
    double peak_equity;
    double max_dd_abs;           /* most negative */
    double max_dd_pct;           /* most negative percent */

    /* Total bars (for time-in-market) */
    int    total_bars;
    int    sum_bars_held;
} af_online_metrics_t;

/**
 * af_snapshot_t — metrics snapshot from af_online_snapshot().
 * Contains the same fields as af_metrics_t plus current equity/drawdown.
 */
typedef struct {
    af_metrics_t metrics;
    double       running_equity;
} af_snapshot_t;

/* ============================================================
 * Section 3 — LCG RNG
 * ============================================================ */

/**
 * af_lcg_next — advance a 64-bit LCG state and return a uint32 output.
 *
 * Formula (must match across all language implementations):
 *   state = state * 6364136223846793005 + 1442695040888963407  (mod 2^64)
 *   return state >> 33
 *
 * state: pointer to a uint64_t that holds the RNG state.  Initialise to seed.
 */
uint32_t af_lcg_next(uint64_t *state);

/* ============================================================
 * Section 4 — Metrics
 * ============================================================ */

/**
 * af_metrics_compute — compute all 14 metrics plus drawdown summary.
 *
 * trades:       array of af_trade_t, length n_trades
 * eq:           equity curve (including initial capital at index 0), length n_eq
 * total_bars:   total number of bars in the backtest period
 * ann_factor:   annualisation factor for Sharpe/Sortino (pass sqrt(252) for daily)
 * out:          output struct (caller-allocated)
 *
 * Returns AF_OK on success, AF_ERR_INVALID_PARAM if required pointers are NULL.
 * Returns AF_ERR_INSUFFICIENT_DATA if n_trades == 0 and n_eq < 2.
 */
af_error_t af_metrics_compute(
    const af_trade_t  *trades,
    int                n_trades,
    const double      *eq,
    int                n_eq,
    int                total_bars,
    double             ann_factor,
    af_metrics_t      *out
);

/**
 * af_metrics_sharpe — standalone Sharpe ratio from bar returns.
 * bar_returns: array of (eq[i]-eq[i-1])/eq[i-1], length n
 * ann_factor:  sqrt(252) for daily
 * Returns 0 if std == 0 or n < 2.
 */
double af_metrics_sharpe(const double *bar_returns, int n, double ann_factor);

/**
 * af_metrics_sortino — standalone Sortino ratio.
 * Downside std uses full n as denominator (project convention).
 * Returns 0 if downside std == 0 or n < 2.
 */
double af_metrics_sortino(const double *bar_returns, int n, double ann_factor);

/**
 * af_metrics_calmar — total_return_pct / max_dd_pct.
 * total_return_pct: (final_equity - initial_equity) / initial_equity * 100
 * max_dd_pct: most negative drawdown percent (as returned by af_dd_series)
 * Returns 0 if max_dd_pct == 0.
 */
double af_metrics_calmar(double total_return_pct, double max_dd_pct);

/**
 * af_metrics_omega — Omega ratio with threshold=0.
 * bar_returns: array of fractional returns, length n
 */
double af_metrics_omega(const double *bar_returns, int n);

/* ============================================================
 * Section 5 — Drawdown
 * ============================================================ */

/**
 * af_dd_series — compute peak[], dd_pct[], dd_abs[] from equity curve.
 *
 * eq:      equity curve, length n
 * peak:    output peaks, caller-allocated length n (may be NULL to skip)
 * dd_pct:  output percent drawdown, caller-allocated length n (may be NULL)
 * dd_abs:  output absolute drawdown, caller-allocated length n (may be NULL)
 * max_dd_pct_out: pointer to receive max_dd_pct (most negative); may be NULL
 * max_dd_abs_out: pointer to receive max_dd_abs (most negative); may be NULL
 *
 * Returns AF_OK, or AF_ERR_INVALID_PARAM if eq is NULL or n < 1.
 */
af_error_t af_dd_series(
    const double *eq,
    int           n,
    double       *peak,
    double       *dd_pct,
    double       *dd_abs,
    double       *max_dd_pct_out,
    double       *max_dd_abs_out
);

/**
 * af_dd_episodes — find all contiguous drawdown episodes.
 *
 * eq:       equity curve, length n
 * out:      caller-allocated array of af_drawdown_episode_t, capacity cap_out
 * n_out:    receives the number of episodes found (may be less than cap_out)
 *
 * Returns AF_OK, or AF_ERR_INVALID_PARAM if eq/out are NULL.
 */
af_error_t af_dd_episodes(
    const double          *eq,
    int                    n,
    af_drawdown_episode_t *out,
    int                    cap_out,
    int                   *n_out
);

/**
 * af_dd_top5 — return up to 5 deepest drawdown episodes, sorted by depth_pct
 * ascending (deepest first).
 *
 * out:   caller-allocated array of at least 5 af_drawdown_episode_t
 * n_out: receives number of episodes written (0..5)
 */
af_error_t af_dd_top5(
    const double          *eq,
    int                    n,
    af_drawdown_episode_t  out[5],
    int                   *n_out
);

/* ============================================================
 * Section 6 — Distributions
 * ============================================================ */

/**
 * af_dist_stats — compute descriptive statistics for a data series.
 *
 * data: array of double, length n
 * out:  caller-allocated af_distribution_stats_t
 *
 * For n < 4, skewness and kurtosis are set to 0.
 * Quantiles use linear interpolation (numpy 'linear' / R-7 method).
 * Requires scratch space: caller must pass a scratch buffer of length n.
 *
 * Returns AF_OK, or AF_ERR_INVALID_PARAM if data/out/scratch are NULL or n<1.
 */
af_error_t af_dist_stats(
    const double             *data,
    int                       n,
    double                   *scratch,   /* caller-supplied, length n */
    af_distribution_stats_t  *out
);

/* ============================================================
 * Section 7 — Correlations
 * ============================================================ */

/**
 * af_pearson — Pearson correlation coefficient between two equal-length series.
 * Returns 0 if std of either series is 0.
 * Returns AF_ERR_INVALID_PARAM if x/y are NULL or n < 2.
 */
af_error_t af_pearson(
    const double *x,
    const double *y,
    int           n,
    double       *r_out
);

/**
 * af_correlation_matrix_compute — compute n×n Pearson correlation matrix.
 *
 * series:    array of n pointers to double arrays, each of length len
 * n:         number of series
 * len:       length of each series (truncated to shortest if they differ)
 * flat_vals: caller-supplied flat array of size n*n
 * mat:       output struct (caller fills .values = flat_vals, .n = n)
 *
 * Returns AF_OK or AF_ERR_INVALID_PARAM.
 */
af_error_t af_correlation_matrix_compute(
    const double **series,
    int            n,
    int            len,
    double        *flat_vals,
    af_correlation_matrix_t *mat
);

/**
 * af_rolling_beta — rolling beta = cov(target, baseline) / var(baseline).
 *
 * target:   length n
 * baseline: length n
 * window:   rolling window size
 * out:      caller-allocated, length n; first (window-1) entries are NaN
 *
 * Returns AF_OK or AF_ERR_INVALID_PARAM.
 */
af_error_t af_rolling_beta(
    const double *target,
    const double *baseline,
    int           n,
    int           window,
    double       *out
);

/* ============================================================
 * Section 8 — Monte Carlo
 * ============================================================ */

/**
 * af_mc_run — bootstrap Monte Carlo simulation over trade PnL values.
 *
 * pnl:             array of net PnL per trade, length n_trades
 * n_trades:        number of trades
 * n_runs:          number of simulation runs (max AF_MC_MAX_RUNS)
 * seed:            LCG seed (uint64)
 * initial_capital: starting equity for each path
 *
 * result:          output struct (caller-allocated).
 *                  result->p5_path, p50_path, p95_path must point to
 *                  caller-allocated arrays of length n_trades+1 each.
 *
 * Internally uses scratch arrays on the stack; n_runs * (n_trades+1) doubles
 * for paths.  Caller supplies a scratch buffer:
 * scratch:         caller-supplied array of doubles, length n_runs * (n_trades+1)
 * scratch_cap:     capacity of scratch (must be >= n_runs * (n_trades+1))
 *
 * Returns AF_OK, AF_ERR_INVALID_PARAM, or AF_ERR_OUT_OF_MEMORY if scratch too small.
 */
af_error_t af_mc_run(
    const double    *pnl,
    int              n_trades,
    int              n_runs,
    uint64_t         seed,
    double           initial_capital,
    double          *scratch,
    int              scratch_cap,
    af_mc_result_t  *result
);

/* ============================================================
 * Section 9 — Walk-forward
 * ============================================================ */

/**
 * Callback type for walk-forward backtest functions.
 *
 * The callback receives train and test bar index ranges [train_start, train_end)
 * and [test_start, test_end), plus a user-supplied context pointer.
 * It must fill train_out and test_out with metrics for each split.
 *
 * Returns AF_OK on success.
 */
typedef af_error_t (*af_wf_backtest_fn)(
    int           train_start,
    int           train_end,
    int           test_start,
    int           test_end,
    void         *ctx,
    af_metrics_t *train_out,
    af_metrics_t *test_out
);

/**
 * af_wf_anchored — anchored walk-forward validation.
 *
 * n_bars:         total number of bars in the dataset
 * n_folds:        number of folds
 * train_min_bars: minimum bars in the initial training window
 * backtest_fn:    caller-supplied callback
 * ctx:            passed through to backtest_fn unchanged
 * result:         output (caller-allocated)
 *
 * Returns AF_OK or AF_ERR_INVALID_PARAM.
 */
af_error_t af_wf_anchored(
    int               n_bars,
    int               n_folds,
    int               train_min_bars,
    af_wf_backtest_fn backtest_fn,
    void             *ctx,
    af_wf_result_t   *result
);

/**
 * af_wf_rolling — rolling (sliding-window) walk-forward validation.
 *
 * n_bars:       total number of bars
 * train_window: size of training window
 * test_window:  size of test window
 * step:         step size between folds
 * backtest_fn:  caller-supplied callback
 * ctx:          passed through to backtest_fn unchanged
 * result:       output (caller-allocated)
 *
 * Returns AF_OK or AF_ERR_INVALID_PARAM.
 */
af_error_t af_wf_rolling(
    int               n_bars,
    int               train_window,
    int               test_window,
    int               step,
    af_wf_backtest_fn backtest_fn,
    void             *ctx,
    af_wf_result_t   *result
);

/* ============================================================
 * Section 10 — OLS regression
 * ============================================================ */

/**
 * af_ols — Ordinary Least Squares regression.
 *
 * X_flat:  row-major n×k matrix (intercept column must be prepended by caller
 *          if an intercept is desired)
 * y:       response vector, length n
 * n:       number of observations (max AF_OLS_MAX_SAMPLES)
 * k:       number of features including intercept (max AF_OLS_MAX_FEATURES)
 * out:     caller-allocated af_ols_result_t
 *
 * Matrix inverse uses Gauss-Jordan elimination with partial pivoting.
 *
 * Returns AF_OK, AF_ERR_INVALID_PARAM, or AF_ERR_UNKNOWN if matrix is singular.
 */
af_error_t af_ols(
    const double    *X_flat,
    const double    *y,
    int              n,
    int              k,
    af_ols_result_t *out
);

/* ============================================================
 * Section 11 — Factor regression
 * ============================================================ */

/**
 * af_factors_ols — Fama-French-style multi-factor OLS regression.
 *
 * strategy_returns: bar returns of the strategy, length n
 * factor_returns:   flat array of n_factors series each of length n,
 *                   row-major (factor_returns[f*n + i] = factor f, bar i)
 * n:                number of bars
 * n_factors:        number of factor series
 * out:              caller-allocated af_factor_result_t
 *
 * Prepends an intercept column automatically.
 * Returns AF_OK, AF_ERR_INVALID_PARAM, or AF_ERR_UNKNOWN.
 */
af_error_t af_factors_ols(
    const double       *strategy_returns,
    const double       *factor_returns,
    int                 n,
    int                 n_factors,
    af_factor_result_t *out
);

/* ============================================================
 * Section 12 — Online / streaming metrics
 * ============================================================ */

/**
 * af_online_init — initialise an af_online_metrics_t struct.
 * Must be called once before any updates.
 * initial_capital: starting equity (used for drawdown calculations).
 */
void af_online_init(af_online_metrics_t *om, double initial_capital);

/**
 * af_online_update — process one new trade.
 * trade:      trade to add (net_pnl and bars_held are used)
 * total_bars: updated total number of bars in the backtest so far
 */
void af_online_update(af_online_metrics_t *om, af_trade_t trade, int total_bars);

/**
 * af_online_snapshot — produce a current metrics snapshot.
 * out: caller-allocated af_snapshot_t
 */
void af_online_snapshot(const af_online_metrics_t *om, af_snapshot_t *out);

/* ============================================================
 * Section 13 — HTML report
 * ============================================================ */

/**
 * af_report_context_t — all data needed to render an HTML report.
 * Pass NULL for optional sections to omit them.
 */
typedef struct {
    const char              *symbol;

    /* Core data */
    const af_metrics_t      *metrics;           /* required */
    const double            *equity_curve;      /* required, length eq_len */
    int                      eq_len;
    const int64_t           *equity_timestamps; /* length eq_len; may be NULL */
    const af_trade_t        *trades;            /* for trade distribution */
    int                      n_trades;

    /* Drawdown */
    const double            *dd_pct;            /* length eq_len; may be NULL */
    const af_drawdown_episode_t *top5_dd;       /* up to 5; may be NULL */
    int                      n_top5_dd;

    /* Monte Carlo */
    const af_mc_result_t    *mc;                /* may be NULL */

    /* Walk-forward */
    const af_wf_result_t    *wf;                /* may be NULL */

    /* OLS attribution */
    const af_ols_result_t   *ols;               /* may be NULL */
    const char * const      *ols_feature_names; /* length ols->k; may be NULL */

    /* Factor regression */
    const af_factor_result_t *factors;          /* may be NULL */
    const char * const       *factor_names;     /* length factors->n_factors; may be NULL */
} af_report_context_t;

/**
 * af_report_write — write a self-contained HTML report to a FILE*.
 *
 * ctx:  populated report context
 * fp:   open writable FILE* (e.g. from fopen("report.html","w"))
 *
 * Returns AF_OK, or AF_ERR_INVALID_PARAM if ctx or fp are NULL,
 * or AF_ERR_IO on write error.
 */
af_error_t af_report_write(const af_report_context_t *ctx, FILE *fp);

#ifdef __cplusplus
}
#endif

#endif /* AF_ANALYTICS_H */
