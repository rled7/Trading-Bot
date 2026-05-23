/**
 * AlgoForge — c/src/analytics.c
 *
 * Implementation of the S4 analytics layer.
 * Covers: metrics, drawdown, distributions, correlations, rolling beta,
 * monte carlo, walk-forward, OLS regression, factor regression,
 * online metrics (Welford), and HTML report generation.
 *
 * No external dependencies beyond libc.
 * C17 standard (-std=c17).
 * All arithmetic in double precision.
 * No malloc in hot paths — caller supplies output buffers.
 */

#include "af_analytics.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

/* ============================================================
 * Internal math helpers
 * ============================================================ */

static double an_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double g = (x > 1.0) ? x * 0.5 : 1.0;
    for (int i = 0; i < 64; ++i) {
        double ng = 0.5 * (g + x / g);
        if (ng == g) break;
        g = ng;
    }
    return g;
}

static double an_fabs(double x) { return x < 0.0 ? -x : x; }


static double an_nan(void) {
    static const double _nan = 0.0 / 0.0;
    return _nan;
}

/* Compute sample mean of array a of length n. */
static double mean_d(const double *a, int n) {
    if (n <= 0) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += a[i];
    return s / (double)n;
}

/* Compute sample standard deviation (N-1 denominator). Returns 0 if n < 2. */
static double std_sample(const double *a, int n) {
    if (n < 2) return 0.0;
    double m = mean_d(a, n);
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        double d = a[i] - m;
        s += d * d;
    }
    return an_sqrt(s / (double)(n - 1));
}

/* Biased (population) std for use in skew/kurtosis formulas. */
static double std_biased(const double *a, int n, double m) {
    if (n < 1) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        double d = a[i] - m;
        s += d * d;
    }
    return an_sqrt(s / (double)n);
}

/* Comparison function for qsort (doubles). */
static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

/* Comparison function for sorting episodes by depth_pct (ascending = deepest first). */
static int cmp_episode_depth(const void *a, const void *b) {
    const af_drawdown_episode_t *ea = (const af_drawdown_episode_t *)a;
    const af_drawdown_episode_t *eb = (const af_drawdown_episode_t *)b;
    if (ea->depth_pct < eb->depth_pct) return -1;
    if (ea->depth_pct > eb->depth_pct) return  1;
    return 0;
}

/* Linear interpolation percentile (numpy 'linear' / R-7).
 * sorted_data must be sorted ascending. pct in [0, 100]. */
static double percentile_sorted(const double *sorted_data, int n, double pct) {
    if (n <= 0) return 0.0;
    if (n == 1) return sorted_data[0];
    double idx = (pct / 100.0) * (double)(n - 1);
    int lo = (int)idx;
    int hi = lo + 1;
    if (hi >= n) return sorted_data[n - 1];
    double frac = idx - (double)lo;
    return sorted_data[lo] + frac * (sorted_data[hi] - sorted_data[lo]);
}

/* ============================================================
 * LCG
 * ============================================================ */

uint32_t af_lcg_next(uint64_t *state) {
    *state = (*state) * UINT64_C(6364136223846793005)
             + UINT64_C(1442695040888963407);
    return (uint32_t)((*state) >> 33);
}

/* ============================================================
 * Metrics
 * ============================================================ */

double af_metrics_sharpe(const double *bar_returns, int n, double ann_factor) {
    if (!bar_returns || n < 2) return 0.0;
    double m = mean_d(bar_returns, n);
    double s = std_sample(bar_returns, n);
    if (s == 0.0) return 0.0;
    return ann_factor * m / s;
}

double af_metrics_sortino(const double *bar_returns, int n, double ann_factor) {
    if (!bar_returns || n < 2) return 0.0;
    double m = mean_d(bar_returns, n);
    /* Downside std: sqrt(sum(min(r,0)^2) / (n-1)) using full sample size n */
    double sum_sq = 0.0;
    for (int i = 0; i < n; ++i) {
        double r = bar_returns[i] < 0.0 ? bar_returns[i] : 0.0;
        sum_sq += r * r;
    }
    double downside_std = an_sqrt(sum_sq / (double)(n - 1));
    if (downside_std == 0.0) return 0.0;
    return ann_factor * m / downside_std;
}

double af_metrics_calmar(double total_return_pct, double max_dd_pct) {
    if (max_dd_pct == 0.0) return 0.0;
    /* max_dd_pct is negative; divide and negate */
    return total_return_pct / an_fabs(max_dd_pct);
}

double af_metrics_omega(const double *bar_returns, int n) {
    if (!bar_returns || n <= 0) return 0.0;
    double gains = 0.0, losses = 0.0;
    for (int i = 0; i < n; ++i) {
        if (bar_returns[i] > 0.0)      gains  += bar_returns[i];
        else if (bar_returns[i] < 0.0) losses += -bar_returns[i];
    }
    if (losses == 0.0) return (gains > 0.0) ? 1.0 / 0.0 : 0.0;
    return gains / losses;
}

af_error_t af_metrics_compute(
    const af_trade_t  *trades,
    int                n_trades,
    const double      *eq,
    int                n_eq,
    int                total_bars,
    double             ann_factor,
    af_metrics_t      *out)
{
    if (!out) return AF_ERR_INVALID_PARAM;
    memset(out, 0, sizeof(*out));

    /* ---- Trade-level metrics ---- */
    if (trades && n_trades > 0) {
        int n_wins = 0, n_losses = 0;
        double sum_pnl = 0.0;
        double sum_winning = 0.0;
        double sum_losing_abs = 0.0;
        int sum_bars = 0;
        int cur_win = 0, cur_loss = 0;
        int max_win = 0, max_loss = 0;

        for (int i = 0; i < n_trades; ++i) {
            double p = trades[i].net_pnl;
            sum_pnl += p;
            sum_bars += trades[i].bars_held;
            if (p > 0.0) {
                n_wins++;
                sum_winning += p;
                cur_win++;
                if (cur_win > max_win) max_win = cur_win;
                cur_loss = 0;
            } else {
                n_losses++;
                sum_losing_abs += -p;
                cur_loss++;
                if (cur_loss > max_loss) max_loss = cur_loss;
                cur_win = 0;
            }
        }

        out->n_trades      = n_trades;
        out->n_wins        = n_wins;
        out->n_losses      = n_losses;
        out->total_pnl     = sum_pnl;
        out->win_rate      = (double)n_wins / (double)n_trades;
        out->expectancy    = sum_pnl / (double)n_trades;
        out->avg_win       = (n_wins > 0)   ? sum_winning     / (double)n_wins   : 0.0;
        out->avg_loss      = (n_losses > 0) ? -(sum_losing_abs / (double)n_losses) : 0.0;
        out->profit_factor = (sum_losing_abs > 0.0)
                             ? sum_winning / sum_losing_abs
                             : ((sum_winning > 0.0) ? 1.0 / 0.0 : 0.0);
        if (n_losses > 0 && an_fabs(out->avg_loss) > 0.0)
            out->payoff_ratio = out->avg_win / an_fabs(out->avg_loss);
        else if (n_losses == 0)
            out->payoff_ratio = (out->avg_win > 0.0) ? 1.0 / 0.0 : 0.0;
        else
            out->payoff_ratio = 0.0;
        out->longest_win_streak  = max_win;
        out->longest_loss_streak = max_loss;
        out->max_consec_losses   = max_loss;
        out->time_in_market      = (total_bars > 0)
                                   ? (double)sum_bars / (double)total_bars
                                   : 0.0;
    }

    /* ---- Equity-curve metrics ---- */
    if (eq && n_eq >= 2) {
        /* Build bar returns */
        int n_ret = n_eq - 1;
        /* We need a stack allocation; limit to a reasonable size.
         * For large equity curves the caller should use the standalone fns. */
        if (n_ret > AF_OLS_MAX_SAMPLES) n_ret = AF_OLS_MAX_SAMPLES;
        double bar_ret[AF_OLS_MAX_SAMPLES];
        for (int i = 0; i < n_ret; ++i) {
            if (eq[i] != 0.0)
                bar_ret[i] = (eq[i + 1] - eq[i]) / eq[i];
            else
                bar_ret[i] = 0.0;
        }

        out->sharpe  = af_metrics_sharpe(bar_ret, n_ret, ann_factor);
        out->sortino = af_metrics_sortino(bar_ret, n_ret, ann_factor);
        out->omega   = af_metrics_omega(bar_ret, n_ret);

        /* Drawdown for Calmar and recovery factor */
        double max_dd_pct = 0.0, max_dd_abs = 0.0;
        double peak = eq[0];
        for (int i = 0; i < n_eq; ++i) {
            if (eq[i] > peak) peak = eq[i];
            double dpct = (peak > 0.0) ? (eq[i] - peak) / peak * 100.0 : 0.0;
            double dabs = eq[i] - peak;
            if (dpct < max_dd_pct) max_dd_pct = dpct;
            if (dabs < max_dd_abs) max_dd_abs = dabs;
        }
        out->max_dd_pct = max_dd_pct;
        out->max_dd_abs = max_dd_abs;

        double total_return_pct = (eq[0] > 0.0)
                                  ? (eq[n_eq - 1] - eq[0]) / eq[0] * 100.0
                                  : 0.0;
        out->calmar = af_metrics_calmar(total_return_pct, max_dd_pct);
        out->recovery_factor = (max_dd_abs < 0.0)
                               ? out->total_pnl / an_fabs(max_dd_abs)
                               : 0.0;
    }

    return AF_OK;
}

/* ============================================================
 * Drawdown
 * ============================================================ */

af_error_t af_dd_series(
    const double *eq,
    int           n,
    double       *peak,
    double       *dd_pct,
    double       *dd_abs,
    double       *max_dd_pct_out,
    double       *max_dd_abs_out)
{
    if (!eq || n < 1) return AF_ERR_INVALID_PARAM;

    double cur_peak = eq[0];
    double mdd_pct = 0.0, mdd_abs = 0.0;

    for (int i = 0; i < n; ++i) {
        if (eq[i] > cur_peak) cur_peak = eq[i];
        double dpct = (cur_peak > 0.0) ? (eq[i] - cur_peak) / cur_peak * 100.0 : 0.0;
        double dabs = eq[i] - cur_peak;
        if (peak)   peak[i]   = cur_peak;
        if (dd_pct) dd_pct[i] = dpct;
        if (dd_abs) dd_abs[i] = dabs;
        if (dpct < mdd_pct) mdd_pct = dpct;
        if (dabs < mdd_abs) mdd_abs = dabs;
    }
    if (max_dd_pct_out) *max_dd_pct_out = mdd_pct;
    if (max_dd_abs_out) *max_dd_abs_out = mdd_abs;
    return AF_OK;
}

af_error_t af_dd_episodes(
    const double          *eq,
    int                    n,
    af_drawdown_episode_t *out,
    int                    cap_out,
    int                   *n_out)
{
    if (!eq || !out || !n_out || cap_out < 1) return AF_ERR_INVALID_PARAM;
    *n_out = 0;

    /* Build peak and dd_pct arrays on stack (limited to 8192 bars) */
    if (n > AF_OLS_MAX_SAMPLES) n = AF_OLS_MAX_SAMPLES;
    double peaks[AF_OLS_MAX_SAMPLES];
    double dd_pct[AF_OLS_MAX_SAMPLES];
    af_dd_series(eq, n, peaks, dd_pct, NULL, NULL, NULL);

    int i = 0;
    while (i < n && *n_out < cap_out) {
        /* Find start of next episode (dd_pct < 0) */
        while (i < n && dd_pct[i] >= 0.0) ++i;
        if (i >= n) break;

        int start = i;
        int trough = i;
        double trough_depth = dd_pct[i];
        double trough_abs   = eq[i] - peaks[i];

        /* Walk to end of episode */
        while (i < n && dd_pct[i] < 0.0) {
            if (dd_pct[i] < trough_depth) {
                trough_depth = dd_pct[i];
                trough_abs   = eq[i] - peaks[i];
                trough = i;
            }
            ++i;
        }
        /* i now points to first bar where dd_pct >= 0 (recovery or end) */
        int recover = (i < n) ? i : -1;

        af_drawdown_episode_t ep;
        ep.start_idx     = start;
        ep.trough_idx    = trough;
        ep.recover_idx   = recover;
        ep.depth_pct     = trough_depth;
        ep.depth_abs     = trough_abs;
        ep.duration_bars = (recover >= 0) ? (recover - start + 1) : (n - start);
        ep.recovery_bars = (recover >= 0) ? (recover - trough)    : -1;

        out[(*n_out)++] = ep;
    }
    return AF_OK;
}

af_error_t af_dd_top5(
    const double          *eq,
    int                    n,
    af_drawdown_episode_t  out[5],
    int                   *n_out)
{
    if (!eq || !out || !n_out) return AF_ERR_INVALID_PARAM;

    af_drawdown_episode_t buf[256];
    int cnt = 0;
    af_dd_episodes(eq, n, buf, 256, &cnt);

    /* Sort by depth_pct ascending (deepest first) */
    qsort(buf, (size_t)cnt, sizeof(af_drawdown_episode_t), cmp_episode_depth);

    *n_out = (cnt < 5) ? cnt : 5;
    for (int i = 0; i < *n_out; ++i) out[i] = buf[i];
    return AF_OK;
}

/* ============================================================
 * Distributions
 * ============================================================ */

af_error_t af_dist_stats(
    const double             *data,
    int                       n,
    double                   *scratch,
    af_distribution_stats_t  *out)
{
    if (!data || !out || !scratch || n < 1) return AF_ERR_INVALID_PARAM;
    memset(out, 0, sizeof(*out));
    out->n = n;

    /* Copy and sort for percentiles */
    for (int i = 0; i < n; ++i) scratch[i] = data[i];
    qsort(scratch, (size_t)n, sizeof(double), cmp_double);

    out->min = scratch[0];
    out->max = scratch[n - 1];
    out->q05 = percentile_sorted(scratch, n, 5.0);
    out->q25 = percentile_sorted(scratch, n, 25.0);
    out->q50 = percentile_sorted(scratch, n, 50.0);
    out->q75 = percentile_sorted(scratch, n, 75.0);
    out->q95 = percentile_sorted(scratch, n, 95.0);

    double m = mean_d(data, n);
    out->mean = m;
    out->std_sample = std_sample(data, n);

    if (n < 4) {
        out->skewness = 0.0;
        out->kurtosis = 0.0;
        return AF_OK;
    }

    double sb = std_biased(data, n, m);
    if (sb == 0.0) {
        out->skewness = 0.0;
        out->kurtosis = 0.0;
        return AF_OK;
    }

    /* g1 = (1/n) * sum((x-mean)^3) / sb^3 */
    double sum3 = 0.0, sum4 = 0.0;
    for (int i = 0; i < n; ++i) {
        double d = data[i] - m;
        double d2 = d * d;
        sum3 += d2 * d;
        sum4 += d2 * d2;
    }
    double g1 = (sum3 / (double)n) / (sb * sb * sb);
    double dn = (double)n;
    /* G1 = sqrt(n*(n-1)) / (n-2) * g1 */
    double G1 = an_sqrt(dn * (dn - 1.0)) / (dn - 2.0) * g1;
    out->skewness = G1;

    /* g2 = (1/n) * sum((x-mean)^4) / sb^4 - 3 */
    double g2 = (sum4 / (double)n) / (sb * sb * sb * sb) - 3.0;
    /* G2 = ((n-1)/((n-2)(n-3))) * ((n+1)*g2 + 6) */
    double G2 = ((dn - 1.0) / ((dn - 2.0) * (dn - 3.0))) * ((dn + 1.0) * g2 + 6.0);
    out->kurtosis = G2;

    return AF_OK;
}

/* ============================================================
 * Correlations
 * ============================================================ */

af_error_t af_pearson(
    const double *x,
    const double *y,
    int           n,
    double       *r_out)
{
    if (!x || !y || !r_out || n < 2) return AF_ERR_INVALID_PARAM;
    double mx = mean_d(x, n);
    double my = mean_d(y, n);
    double num = 0.0, denom_x = 0.0, denom_y = 0.0;
    for (int i = 0; i < n; ++i) {
        double dx = x[i] - mx;
        double dy = y[i] - my;
        num     += dx * dy;
        denom_x += dx * dx;
        denom_y += dy * dy;
    }
    double denom = an_sqrt(denom_x * denom_y);
    *r_out = (denom == 0.0) ? 0.0 : num / denom;
    return AF_OK;
}

af_error_t af_correlation_matrix_compute(
    const double **series,
    int            n,
    int            len,
    double        *flat_vals,
    af_correlation_matrix_t *mat)
{
    if (!series || !flat_vals || !mat || n < 1 || len < 2)
        return AF_ERR_INVALID_PARAM;

    mat->values = flat_vals;
    mat->n      = n;

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (i == j) {
                flat_vals[i * n + j] = 1.0;
            } else if (j < i) {
                /* Copy symmetric value */
                flat_vals[i * n + j] = flat_vals[j * n + i];
            } else {
                double r = 0.0;
                af_pearson(series[i], series[j], len, &r);
                flat_vals[i * n + j] = r;
            }
        }
    }
    return AF_OK;
}

af_error_t af_rolling_beta(
    const double *target,
    const double *baseline,
    int           n,
    int           window,
    double       *out)
{
    if (!target || !baseline || !out || n < 1 || window < 2)
        return AF_ERR_INVALID_PARAM;

    double nan = an_nan();
    for (int i = 0; i < n; ++i) out[i] = nan;

    for (int i = window - 1; i < n; ++i) {
        const double *tx = target   + (i - window + 1);
        const double *bx = baseline + (i - window + 1);
        double mt = mean_d(tx, window);
        double mb = mean_d(bx, window);
        double cov = 0.0, var_b = 0.0;
        for (int k = 0; k < window; ++k) {
            double dt = tx[k] - mt;
            double db = bx[k] - mb;
            cov   += dt * db;
            var_b += db * db;
        }
        /* cov and var_b are unnormalised sums; ratio is the same */
        out[i] = (var_b == 0.0) ? 0.0 : cov / var_b;
    }
    return AF_OK;
}

/* ============================================================
 * Monte Carlo
 * ============================================================ */

/*
 * Pointwise percentile: for each time step t, collect final_eq values
 * across all runs and return the pct-th percentile.
 * paths is a flat array [run0_step0, run0_step1, ..., run1_step0, ...],
 * dimensions: n_runs * path_len.
 */
static void pointwise_percentile(
    const double *paths,
    int           n_runs,
    int           path_len,
    double        pct,
    double       *out)
{
    double col[AF_MC_MAX_RUNS];
    for (int t = 0; t < path_len; ++t) {
        int nr = (n_runs < AF_MC_MAX_RUNS) ? n_runs : AF_MC_MAX_RUNS;
        for (int r = 0; r < nr; ++r)
            col[r] = paths[r * path_len + t];
        /* sort */
        qsort(col, (size_t)nr, sizeof(double), cmp_double);
        out[t] = percentile_sorted(col, nr, pct);
    }
}

af_error_t af_mc_run(
    const double    *pnl,
    int              n_trades,
    int              n_runs,
    uint64_t         seed,
    double           initial_capital,
    double          *scratch,
    int              scratch_cap,
    af_mc_result_t  *result)
{
    if (!pnl || !result || n_trades < 1 || n_runs < 1)
        return AF_ERR_INVALID_PARAM;
    if (n_runs > AF_MC_MAX_RUNS) n_runs = AF_MC_MAX_RUNS;

    int path_len = n_trades + 1;
    if (scratch_cap < n_runs * path_len) return AF_ERR_OUT_OF_MEMORY;

    uint64_t state = seed;
    double final_eq[AF_MC_MAX_RUNS];
    int ruin_count = 0;

    for (int r = 0; r < n_runs; ++r) {
        double *path = scratch + r * path_len;
        double eq = initial_capital;
        path[0]   = eq;
        int ruined = (eq <= 0.0);
        for (int t = 0; t < n_trades; ++t) {
            int idx = (int)(af_lcg_next(&state) % (uint32_t)n_trades);
            eq += pnl[idx];
            path[t + 1] = eq;
            if (eq <= 0.0) ruined = 1;
        }
        final_eq[r] = eq;
        if (ruined) ruin_count++;
    }

    /* Sort final_eq for percentile computation */
    double sorted_final[AF_MC_MAX_RUNS];
    for (int r = 0; r < n_runs; ++r) sorted_final[r] = final_eq[r];
    qsort(sorted_final, (size_t)n_runs, sizeof(double), cmp_double);

    result->p5_final  = percentile_sorted(sorted_final, n_runs, 5.0);
    result->p50_final = percentile_sorted(sorted_final, n_runs, 50.0);
    result->p95_final = percentile_sorted(sorted_final, n_runs, 95.0);
    result->prob_of_ruin = (double)ruin_count / (double)n_runs;
    result->path_len  = path_len;

    /* Pointwise path percentiles */
    if (result->p5_path)
        pointwise_percentile(scratch, n_runs, path_len, 5.0,  result->p5_path);
    if (result->p50_path)
        pointwise_percentile(scratch, n_runs, path_len, 50.0, result->p50_path);
    if (result->p95_path)
        pointwise_percentile(scratch, n_runs, path_len, 95.0, result->p95_path);

    return AF_OK;
}

/* ============================================================
 * Walk-forward
 * ============================================================ */

af_error_t af_wf_anchored(
    int               n_bars,
    int               n_folds,
    int               train_min_bars,
    af_wf_backtest_fn backtest_fn,
    void             *ctx,
    af_wf_result_t   *result)
{
    if (!result || !backtest_fn || n_folds < 1 || n_bars < 1)
        return AF_ERR_INVALID_PARAM;

    int fold_test_len = (n_bars - train_min_bars) / n_folds;
    if (fold_test_len < 1) return AF_ERR_INVALID_PARAM;

    result->n_folds = 0;
    for (int k = 0; k < n_folds && result->n_folds < AF_WF_MAX_FOLDS; ++k) {
        int train_end  = train_min_bars + k * fold_test_len;
        int test_start = train_end;
        int test_end   = test_start + fold_test_len;
        if (test_end > n_bars) test_end = n_bars;
        if (test_start >= test_end) continue;

        af_wf_fold_t *f = &result->folds[result->n_folds];
        f->fold_idx    = k;
        f->train_start = 0;
        f->train_end   = train_end;
        f->test_start  = test_start;
        f->test_end    = test_end;

        af_error_t err = backtest_fn(0, train_end, test_start, test_end,
                                     ctx, &f->train_metrics, &f->test_metrics);
        if (err != AF_OK) return err;
        result->n_folds++;
    }
    return AF_OK;
}

af_error_t af_wf_rolling(
    int               n_bars,
    int               train_window,
    int               test_window,
    int               step,
    af_wf_backtest_fn backtest_fn,
    void             *ctx,
    af_wf_result_t   *result)
{
    if (!result || !backtest_fn || n_bars < 1 || train_window < 1
        || test_window < 1 || step < 1)
        return AF_ERR_INVALID_PARAM;

    result->n_folds = 0;
    int k = 0;
    for (int i = 0;
         i + train_window + test_window <= n_bars && result->n_folds < AF_WF_MAX_FOLDS;
         i += step, k++)
    {
        int train_start = i;
        int train_end   = i + train_window;
        int test_start  = train_end;
        int test_end    = test_start + test_window;

        af_wf_fold_t *f = &result->folds[result->n_folds];
        f->fold_idx    = k;
        f->train_start = train_start;
        f->train_end   = train_end;
        f->test_start  = test_start;
        f->test_end    = test_end;

        af_error_t err = backtest_fn(train_start, train_end, test_start, test_end,
                                     ctx, &f->train_metrics, &f->test_metrics);
        if (err != AF_OK) return err;
        result->n_folds++;
    }
    return AF_OK;
}

/* ============================================================
 * OLS — Gauss-Jordan with partial pivoting
 * ============================================================ */

/*
 * Invert a k×k matrix in place using Gauss-Jordan elimination with
 * partial pivoting.  The augmented matrix [A | I] is reduced to [I | A^-1].
 * mat_in:  k×k input, row-major (will be overwritten)
 * mat_out: k×k output (A^-1), row-major
 * Returns 0 on success, -1 if singular.
 */
static int gauss_jordan_invert(double *A, double *inv, int k) {
    /* Build augmented matrix [A | I] stored as a k × 2k array */
    double aug[AF_OLS_MAX_FEATURES][AF_OLS_MAX_FEATURES * 2];
    for (int i = 0; i < k; ++i) {
        for (int j = 0; j < k; ++j)
            aug[i][j] = A[i * k + j];
        for (int j = 0; j < k; ++j)
            aug[i][k + j] = (i == j) ? 1.0 : 0.0;
    }

    for (int col = 0; col < k; ++col) {
        /* Partial pivot */
        int pivot_row = col;
        double pivot_val = an_fabs(aug[col][col]);
        for (int row = col + 1; row < k; ++row) {
            if (an_fabs(aug[row][col]) > pivot_val) {
                pivot_val = an_fabs(aug[row][col]);
                pivot_row = row;
            }
        }
        if (pivot_val < 1e-15) return -1; /* singular */

        /* Swap rows */
        if (pivot_row != col) {
            for (int j = 0; j < 2 * k; ++j) {
                double tmp = aug[col][j];
                aug[col][j] = aug[pivot_row][j];
                aug[pivot_row][j] = tmp;
            }
        }

        /* Scale pivot row */
        double scale = aug[col][col];
        for (int j = 0; j < 2 * k; ++j)
            aug[col][j] /= scale;

        /* Eliminate column in all other rows */
        for (int row = 0; row < k; ++row) {
            if (row == col) continue;
            double factor = aug[row][col];
            for (int j = 0; j < 2 * k; ++j)
                aug[row][j] -= factor * aug[col][j];
        }
    }

    /* Extract result */
    for (int i = 0; i < k; ++i)
        for (int j = 0; j < k; ++j)
            inv[i * k + j] = aug[i][k + j];

    return 0;
}

af_error_t af_ols(
    const double    *X_flat,
    const double    *y,
    int              n,
    int              k,
    af_ols_result_t *out)
{
    if (!X_flat || !y || !out || n < k || k < 1
        || n > AF_OLS_MAX_SAMPLES || k > AF_OLS_MAX_FEATURES)
        return AF_ERR_INVALID_PARAM;

    memset(out, 0, sizeof(*out));
    out->n = n;
    out->k = k;

    /* Compute XtX (k×k) and Xty (k×1) */
    double XtX[AF_OLS_MAX_FEATURES * AF_OLS_MAX_FEATURES] = {0};
    double Xty[AF_OLS_MAX_FEATURES] = {0};

    for (int i = 0; i < n; ++i) {
        const double *xi = X_flat + i * k;
        for (int a = 0; a < k; ++a) {
            Xty[a] += xi[a] * y[i];
            for (int b = 0; b < k; ++b)
                XtX[a * k + b] += xi[a] * xi[b];
        }
    }

    /* Invert XtX */
    double XtX_inv[AF_OLS_MAX_FEATURES * AF_OLS_MAX_FEATURES];
    if (gauss_jordan_invert(XtX, XtX_inv, k) != 0) return AF_ERR_UNKNOWN;

    /* beta = XtX_inv @ Xty */
    for (int a = 0; a < k; ++a) {
        double s = 0.0;
        for (int b = 0; b < k; ++b)
            s += XtX_inv[a * k + b] * Xty[b];
        out->beta[a] = s;
    }

    /* Residuals and SS */
    double y_mean = mean_d(y, n);
    double ss_res = 0.0, ss_tot = 0.0;
    for (int i = 0; i < n; ++i) {
        const double *xi = X_flat + i * k;
        double y_hat = 0.0;
        for (int a = 0; a < k; ++a) y_hat += xi[a] * out->beta[a];
        double resid = y[i] - y_hat;
        out->residuals[i] = resid;
        ss_res += resid * resid;
        double dy = y[i] - y_mean;
        ss_tot += dy * dy;
    }

    out->r2    = (ss_tot == 0.0) ? 0.0 : 1.0 - ss_res / ss_tot;
    out->adj_r2 = (n > k) ? 1.0 - (1.0 - out->r2) * (double)(n - 1) / (double)(n - k)
                           : out->r2;

    /* sigma^2 = ss_res / (n-k) */
    double sigma_sq = (n > k) ? ss_res / (double)(n - k) : 0.0;

    /* var_beta = sigma^2 * XtX_inv; std_err[j] = sqrt(var_beta[j,j]) */
    for (int a = 0; a < k; ++a) {
        double var_j = sigma_sq * XtX_inv[a * k + a];
        out->std_errors[a] = (var_j >= 0.0) ? an_sqrt(var_j) : 0.0;
        out->t_stats[a]    = (out->std_errors[a] > 0.0)
                             ? out->beta[a] / out->std_errors[a]
                             : 0.0;
    }

    return AF_OK;
}

/* ============================================================
 * Factor regression
 * ============================================================ */

af_error_t af_factors_ols(
    const double       *strategy_returns,
    const double       *factor_returns,
    int                 n,
    int                 n_factors,
    af_factor_result_t *out)
{
    if (!strategy_returns || !out || n < 2 || n_factors < 0
        || n_factors >= AF_OLS_MAX_FEATURES || n > AF_OLS_MAX_SAMPLES)
        return AF_ERR_INVALID_PARAM;

    int k = n_factors + 1; /* +1 for intercept */

    /* Build X: n × k, first column = 1 (intercept), then factor columns */
    double X_flat[AF_OLS_MAX_SAMPLES * AF_OLS_MAX_FEATURES];
    for (int i = 0; i < n; ++i) {
        X_flat[i * k + 0] = 1.0; /* intercept */
        for (int f = 0; f < n_factors; ++f)
            X_flat[i * k + (f + 1)] = factor_returns[f * n + i];
    }

    af_ols_result_t ols;
    af_error_t err = af_ols(X_flat, strategy_returns, n, k, &ols);
    if (err != AF_OK) return err;

    out->alpha         = ols.beta[0];
    out->alpha_t_stat  = ols.t_stats[0];
    out->r2            = ols.r2;
    out->adj_r2        = ols.adj_r2;
    out->n_factors     = n_factors;
    for (int f = 0; f < n_factors; ++f) {
        out->betas[f]   = ols.beta[f + 1];
        out->t_stats[f] = ols.t_stats[f + 1];
    }
    return AF_OK;
}

/* ============================================================
 * Online / streaming metrics (Welford)
 * ============================================================ */

void af_online_init(af_online_metrics_t *om, double initial_capital) {
    memset(om, 0, sizeof(*om));
    om->initial_capital = initial_capital;
    om->running_equity  = initial_capital;
    om->peak_equity     = initial_capital;
}

void af_online_update(af_online_metrics_t *om, af_trade_t trade, int total_bars) {
    double p = trade.net_pnl;

    /* Welford's online algorithm for mean and variance of PnL */
    om->n_trades++;
    double delta = p - om->welford_mean;
    om->welford_mean += delta / (double)om->n_trades;
    double delta2 = p - om->welford_mean;
    om->welford_m2 += delta * delta2;

    om->sum_pnl += p;
    om->sum_bars_held += trade.bars_held;
    if (total_bars > 0) om->total_bars = total_bars;

    /* Win/loss tracking */
    if (p > 0.0) {
        om->n_wins++;
        om->sum_winning_pnl += p;
        if (om->current_streak >= 0) {
            om->current_streak++;
        } else {
            om->current_streak = 1;
        }
        if (om->current_streak > om->longest_win_streak)
            om->longest_win_streak = om->current_streak;
    } else {
        om->n_losses++;
        om->sum_losing_pnl_abs += -p;
        if (om->current_streak <= 0) {
            om->current_streak--;
        } else {
            om->current_streak = -1;
        }
        if (-om->current_streak > om->longest_loss_streak)
            om->longest_loss_streak = -om->current_streak;
    }

    /* Equity and drawdown */
    om->running_equity += p;
    if (om->running_equity > om->peak_equity)
        om->peak_equity = om->running_equity;

    double dd_abs = om->running_equity - om->peak_equity;
    double dd_pct = (om->peak_equity > 0.0)
                    ? (om->running_equity - om->peak_equity) / om->peak_equity * 100.0
                    : 0.0;
    if (dd_abs < om->max_dd_abs) om->max_dd_abs = dd_abs;
    if (dd_pct < om->max_dd_pct) om->max_dd_pct = dd_pct;
}

void af_online_snapshot(const af_online_metrics_t *om, af_snapshot_t *out) {
    memset(out, 0, sizeof(*out));
    out->running_equity = om->running_equity;

    af_metrics_t *m = &out->metrics;
    m->n_trades       = om->n_trades;
    m->n_wins         = om->n_wins;
    m->n_losses       = om->n_losses;
    m->total_pnl      = om->sum_pnl;
    m->max_dd_abs     = om->max_dd_abs;
    m->max_dd_pct     = om->max_dd_pct;
    m->longest_win_streak  = om->longest_win_streak;
    m->longest_loss_streak = om->longest_loss_streak;
    m->max_consec_losses   = om->longest_loss_streak;

    if (om->n_trades > 0) {
        m->win_rate   = (double)om->n_wins / (double)om->n_trades;
        m->expectancy = om->welford_mean;
        m->avg_win    = (om->n_wins > 0)
                        ? om->sum_winning_pnl / (double)om->n_wins
                        : 0.0;
        m->avg_loss   = (om->n_losses > 0)
                        ? -(om->sum_losing_pnl_abs / (double)om->n_losses)
                        : 0.0;
        m->profit_factor = (om->sum_losing_pnl_abs > 0.0)
                           ? om->sum_winning_pnl / om->sum_losing_pnl_abs
                           : (om->sum_winning_pnl > 0.0 ? 1.0 / 0.0 : 0.0);
        if (om->n_losses > 0 && om->sum_losing_pnl_abs > 0.0) {
            double avg_loss_abs = om->sum_losing_pnl_abs / (double)om->n_losses;
            m->payoff_ratio = (m->avg_win / avg_loss_abs);
        } else {
            m->payoff_ratio = (om->n_losses == 0 && m->avg_win > 0.0)
                              ? 1.0 / 0.0 : 0.0;
        }
        m->time_in_market = (om->total_bars > 0)
                            ? (double)om->sum_bars_held / (double)om->total_bars
                            : 0.0;
        m->recovery_factor = (om->max_dd_abs < 0.0)
                             ? om->sum_pnl / an_fabs(om->max_dd_abs)
                             : 0.0;
    }
}

/* ============================================================
 * HTML report
 * ============================================================ */

/* Embedded CSS — dark monospace theme */
static const char *s_css =
"body{margin:0;padding:16px;background:#0d1117;color:#c9d1d9;"
"font-family:'Courier New',Courier,monospace;font-size:14px}"
"h1{color:#58a6ff;border-bottom:1px solid #30363d;padding-bottom:8px}"
"h2{color:#8b949e;font-size:14px;text-transform:uppercase;letter-spacing:1px;"
"margin-top:24px;border-top:1px solid #21262d;padding-top:8px}"
"section{margin-bottom:32px}"
"table{border-collapse:collapse;width:100%;max-width:700px}"
"th,td{padding:6px 10px;text-align:left;border:1px solid #30363d}"
"th{background:#161b22;color:#8b949e}"
"tr:nth-child(even){background:#161b22}"
"canvas{max-width:100%;height:300px!important;background:#161b22;"
"border:1px solid #30363d;border-radius:4px;margin-top:8px}"
".pos{color:#3fb950}.neg{color:#f85149}.neutral{color:#8b949e}"
".metric-label{color:#8b949e;min-width:220px;display:inline-block}"
".metric-value{color:#c9d1d9}"
"pre{background:#161b22;padding:8px;border:1px solid #30363d;overflow-x:auto}";


/* Helper: write a JSON array of doubles */
static void json_double_array(FILE *fp, const double *a, int n) {
    fputs("[", fp);
    for (int i = 0; i < n; ++i) {
        if (i) fputs(",", fp);
        fprintf(fp, "%.10g", a[i]);
    }
    fputs("]", fp);
}

/* Helper: format a double for display, infinity as a string */
static void fmt_double(FILE *fp, double v) {
    if (v == 1.0 / 0.0)       fputs("&#8734;", fp);  /* inf */
    else if (v == -1.0 / 0.0) fputs("-&#8734;", fp);
    else                       fprintf(fp, "%.6g", v);
}

static void write_metric_row(FILE *fp, const char *label, double v) {
    fprintf(fp, "<tr><td class='metric-label'>%s</td><td class='metric-value'>",
            label);
    fmt_double(fp, v);
    fputs("</td></tr>\n", fp);
}

static void write_metric_row_int(FILE *fp, const char *label, int v) {
    fprintf(fp, "<tr><td class='metric-label'>%s</td>"
                "<td class='metric-value'>%d</td></tr>\n", label, v);
}

af_error_t af_report_write(const af_report_context_t *ctx, FILE *fp) {
    if (!ctx || !fp) return AF_ERR_INVALID_PARAM;
    if (!ctx->metrics) return AF_ERR_INVALID_PARAM;

    const af_metrics_t *m = ctx->metrics;
    const char *sym = ctx->symbol ? ctx->symbol : "Unknown";

    /* DOCTYPE / head */
    if (fputs("<!DOCTYPE html>\n<html><head>\n", fp) < 0) return AF_ERR_IO;
    fprintf(fp, "<meta charset=\"utf-8\">\n"
                "<title>AlgoForge Backtest Report &mdash; %s</title>\n", sym);
    fputs("<script src=\"https://cdn.jsdelivr.net/npm/chart.js@4\"></script>\n", fp);
    fprintf(fp, "<style>%s</style>\n", s_css);
    fputs("</head><body>\n", fp);
    fprintf(fp, "<h1>AlgoForge Backtest Report</h1>\n<p>Symbol: <strong>%s</strong></p>\n",
            sym);

    /* Section: summary metrics */
    fputs("<section id=\"summary\">\n<h2>Performance Summary</h2>\n"
          "<table>\n<tr><th>Metric</th><th>Value</th></tr>\n", fp);
    write_metric_row(fp, "Sharpe Ratio",         m->sharpe);
    write_metric_row(fp, "Sortino Ratio",        m->sortino);
    write_metric_row(fp, "Calmar Ratio",         m->calmar);
    write_metric_row(fp, "Omega Ratio",          m->omega);
    write_metric_row(fp, "Profit Factor",        m->profit_factor);
    write_metric_row(fp, "Win Rate",             m->win_rate);
    write_metric_row(fp, "Expectancy (USD)",     m->expectancy);
    write_metric_row(fp, "Avg Win (USD)",        m->avg_win);
    write_metric_row(fp, "Avg Loss (USD)",       m->avg_loss);
    write_metric_row(fp, "Payoff Ratio",         m->payoff_ratio);
    write_metric_row(fp, "Recovery Factor",      m->recovery_factor);
    write_metric_row(fp, "Time in Market",       m->time_in_market);
    write_metric_row_int(fp, "Longest Win Streak",  m->longest_win_streak);
    write_metric_row_int(fp, "Longest Loss Streak", m->longest_loss_streak);
    write_metric_row(fp, "Max DD %",             m->max_dd_pct);
    write_metric_row(fp, "Max DD (USD)",         m->max_dd_abs);
    write_metric_row(fp, "Total PnL (USD)",      m->total_pnl);
    write_metric_row_int(fp, "Total Trades",        m->n_trades);
    fputs("</table>\n</section>\n", fp);

    /* Section: equity curve */
    if (ctx->equity_curve && ctx->eq_len > 0) {
        fputs("<section id=\"equity-curve\">\n<h2>Equity Curve</h2>\n"
              "<canvas id=\"eq\"></canvas>\n</section>\n", fp);
    }

    /* Section: drawdown */
    if (ctx->dd_pct && ctx->eq_len > 0) {
        fputs("<section id=\"drawdown\">\n<h2>Drawdown (%)</h2>\n"
              "<canvas id=\"dd\"></canvas>\n</section>\n", fp);
    }

    /* Section: trade distribution */
    if (ctx->trades && ctx->n_trades > 0) {
        fputs("<section id=\"trade-distribution\">\n<h2>Trade PnL Distribution</h2>\n"
              "<canvas id=\"pnl-hist\"></canvas>\n</section>\n", fp);
    }

    /* Section: monte carlo */
    if (ctx->mc) {
        fputs("<section id=\"monte-carlo\">\n<h2>Monte Carlo</h2>\n"
              "<canvas id=\"mc\"></canvas>\n"
              "<table>\n<tr><th>Metric</th><th>Value</th></tr>\n", fp);
        fprintf(fp, "<tr><td>P5 Final Equity</td><td>%.2f</td></tr>\n",
                ctx->mc->p5_final);
        fprintf(fp, "<tr><td>P50 Final Equity</td><td>%.2f</td></tr>\n",
                ctx->mc->p50_final);
        fprintf(fp, "<tr><td>P95 Final Equity</td><td>%.2f</td></tr>\n",
                ctx->mc->p95_final);
        fprintf(fp, "<tr><td>Probability of Ruin</td><td>%.4f</td></tr>\n",
                ctx->mc->prob_of_ruin);
        fputs("</table>\n</section>\n", fp);
    }

    /* Section: top 5 drawdowns */
    if (ctx->top5_dd && ctx->n_top5_dd > 0) {
        fputs("<section id=\"top-drawdowns\">\n<h2>Top 5 Drawdown Episodes</h2>\n"
              "<table>\n<tr><th>#</th><th>Start</th><th>Trough</th>"
              "<th>Recover</th><th>Depth%</th><th>Depth(USD)</th>"
              "<th>Duration</th><th>Recovery Bars</th></tr>\n", fp);
        for (int i = 0; i < ctx->n_top5_dd; ++i) {
            const af_drawdown_episode_t *ep = &ctx->top5_dd[i];
            fprintf(fp, "<tr><td>%d</td><td>%d</td><td>%d</td><td>%d</td>"
                        "<td>%.4f</td><td>%.2f</td><td>%d</td><td>%d</td></tr>\n",
                    i + 1, ep->start_idx, ep->trough_idx,
                    ep->recover_idx, ep->depth_pct, ep->depth_abs,
                    ep->duration_bars, ep->recovery_bars);
        }
        fputs("</table>\n</section>\n", fp);
    }

    /* Section: walk-forward */
    if (ctx->wf && ctx->wf->n_folds > 0) {
        fputs("<section id=\"walkforward\">\n<h2>Walk-Forward Results</h2>\n"
              "<table>\n<tr><th>Fold</th><th>Train Sharpe</th>"
              "<th>Test Sharpe</th><th>Train WR</th><th>Test WR</th></tr>\n", fp);
        for (int i = 0; i < ctx->wf->n_folds; ++i) {
            const af_wf_fold_t *f = &ctx->wf->folds[i];
            fprintf(fp, "<tr><td>%d</td><td>%.4f</td><td>%.4f</td>"
                        "<td>%.4f</td><td>%.4f</td></tr>\n",
                    f->fold_idx,
                    f->train_metrics.sharpe, f->test_metrics.sharpe,
                    f->train_metrics.win_rate, f->test_metrics.win_rate);
        }
        fputs("</table>\n</section>\n", fp);
    }

    /* Section: OLS attribution */
    if (ctx->ols) {
        fputs("<section id=\"attribution\">\n<h2>ML Attribution (OLS)</h2>\n"
              "<table>\n<tr><th>Feature</th><th>Beta</th><th>Std Err</th>"
              "<th>t-stat</th></tr>\n", fp);
        for (int j = 0; j < ctx->ols->k; ++j) {
            const char *name = (ctx->ols_feature_names && ctx->ols_feature_names[j])
                               ? ctx->ols_feature_names[j] : "?";
            fprintf(fp, "<tr><td>%s</td><td>%.6g</td><td>%.6g</td><td>%.6g</td></tr>\n",
                    name, ctx->ols->beta[j], ctx->ols->std_errors[j],
                    ctx->ols->t_stats[j]);
        }
        fprintf(fp, "</table>\n<p>R&sup2; = %.6f &nbsp; Adj R&sup2; = %.6f</p>\n"
                    "</section>\n",
                ctx->ols->r2, ctx->ols->adj_r2);
    }

    /* Section: factor regression */
    if (ctx->factors) {
        fputs("<section id=\"factors\">\n<h2>Factor Regression</h2>\n"
              "<table>\n<tr><th>Factor</th><th>Beta</th><th>t-stat</th></tr>\n", fp);
        fprintf(fp, "<tr><td>Alpha (intercept)</td><td>%.6g</td><td>%.6g</td></tr>\n",
                ctx->factors->alpha, ctx->factors->alpha_t_stat);
        for (int f = 0; f < ctx->factors->n_factors; ++f) {
            const char *nm = (ctx->factor_names && ctx->factor_names[f])
                             ? ctx->factor_names[f] : "?";
            fprintf(fp, "<tr><td>%s</td><td>%.6g</td><td>%.6g</td></tr>\n",
                    nm, ctx->factors->betas[f], ctx->factors->t_stats[f]);
        }
        fprintf(fp, "</table>\n<p>R&sup2; = %.6f &nbsp; Adj R&sup2; = %.6f</p>\n"
                    "</section>\n",
                ctx->factors->r2, ctx->factors->adj_r2);
    }

    /* Inline JSON data + Chart.js init */
    fputs("<script>\n(function(){\n", fp);

    /* Equity curve data */
    if (ctx->equity_curve && ctx->eq_len > 0) {
        fputs("var eqData=", fp);
        json_double_array(fp, ctx->equity_curve, ctx->eq_len);
        fputs(";\n", fp);
    }

    /* Drawdown data */
    if (ctx->dd_pct && ctx->eq_len > 0) {
        fputs("var ddData=", fp);
        json_double_array(fp, ctx->dd_pct, ctx->eq_len);
        fputs(";\n", fp);
    }

    /* Trade PnL data */
    if (ctx->trades && ctx->n_trades > 0) {
        fputs("var pnlData=[", fp);
        for (int i = 0; i < ctx->n_trades; ++i) {
            if (i) fputc(',', fp);
            fprintf(fp, "%.6g", ctx->trades[i].net_pnl);
        }
        fputs("];\n", fp);
    }

    /* MC path data */
    if (ctx->mc && ctx->mc->p50_path && ctx->mc->path_len > 0) {
        fputs("var mcP5=",  fp);
        if (ctx->mc->p5_path)
            json_double_array(fp, ctx->mc->p5_path,  ctx->mc->path_len);
        else fputs("[]", fp);
        fputs(";\nvar mcP50=", fp);
        json_double_array(fp, ctx->mc->p50_path, ctx->mc->path_len);
        fputs(";\nvar mcP95=", fp);
        if (ctx->mc->p95_path)
            json_double_array(fp, ctx->mc->p95_path, ctx->mc->path_len);
        else fputs("[]", fp);
        fputs(";\n", fp);
    }

    /* Chart.js initialisations */
    if (ctx->equity_curve && ctx->eq_len > 0) {
        fputs("var eqCtx=document.getElementById('eq');\n"
              "if(eqCtx){new Chart(eqCtx,{type:'line',data:{labels:"
              "eqData.map((_,i)=>i),datasets:[{label:'Equity',data:eqData,"
              "borderColor:'#58a6ff',borderWidth:1.5,pointRadius:0,"
              "fill:false,tension:0}]},options:{animation:false,"
              "scales:{x:{display:false},y:{grid:{color:'#21262d'}}},"
              "plugins:{legend:{display:false}}}});}\n", fp);
    }
    if (ctx->dd_pct && ctx->eq_len > 0) {
        fputs("var ddCtx=document.getElementById('dd');\n"
              "if(ddCtx){new Chart(ddCtx,{type:'line',data:{labels:"
              "ddData.map((_,i)=>i),datasets:[{label:'DD%',data:ddData,"
              "borderColor:'#f85149',borderWidth:1.5,pointRadius:0,"
              "fill:'origin',backgroundColor:'rgba(248,81,73,0.15)',"
              "tension:0}]},options:{animation:false,"
              "scales:{x:{display:false},y:{grid:{color:'#21262d'}}},"
              "plugins:{legend:{display:false}}}});}\n", fp);
    }
    if (ctx->trades && ctx->n_trades > 0) {
        fputs("var pnlCtx=document.getElementById('pnl-hist');\n"
              "if(pnlCtx){new Chart(pnlCtx,{type:'bar',data:{labels:"
              "pnlData.map((_,i)=>i),datasets:[{label:'Trade PnL',"
              "data:pnlData,backgroundColor:pnlData.map(v=>v>=0?"
              "'#3fb950':'#f85149')}]},options:{animation:false,"
              "scales:{x:{display:false},y:{grid:{color:'#21262d'}}},"
              "plugins:{legend:{display:false}}}});}\n", fp);
    }
    if (ctx->mc && ctx->mc->p50_path && ctx->mc->path_len > 0) {
        fputs("var mcCtx=document.getElementById('mc');\n"
              "if(mcCtx){new Chart(mcCtx,{type:'line',data:{labels:"
              "mcP50.map((_,i)=>i),datasets:["
              "{label:'P50',data:mcP50,borderColor:'#58a6ff',"
              "borderWidth:2,pointRadius:0,fill:false},"
              "{label:'P5',data:mcP5,borderColor:'#f85149',"
              "borderWidth:1,pointRadius:0,fill:false,borderDash:[4,2]},"
              "{label:'P95',data:mcP95,borderColor:'#3fb950',"
              "borderWidth:1,pointRadius:0,fill:false,borderDash:[4,2]}"
              "]},options:{animation:false,"
              "scales:{x:{display:false},y:{grid:{color:'#21262d'}}},"
              "plugins:{legend:{labels:{color:'#8b949e'}}}}});}\n", fp);
    }

    fputs("})();\n</script>\n</body></html>\n", fp);
    return AF_OK;
}
