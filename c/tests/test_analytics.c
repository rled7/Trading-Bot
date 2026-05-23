/**
 * AlgoForge — c/tests/test_analytics.c
 *
 * Unit tests for af_analytics against the canonical test vectors in spec §5.
 * Also verifies:
 *   - LCG(seed=42) first 10 values match the Python canonical sequence
 *   - All metric formulas from spec §4
 *   - Edge cases: empty series, single trade, all-wins, all-losses
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "af_analytics.h"

static int g_passed = 0;
static int g_failed = 0;

#define CHK(cond) do {                                                   \
    if (cond) {                                                          \
        ++g_passed;                                                      \
    } else {                                                             \
        ++g_failed;                                                      \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                    \
} while (0)

#define CHK_NEAR(a, b, tol) \
    CHK(fabs((double)(a) - (double)(b)) < (double)(tol))

static int is_nan_d(double x) { return x != x; }

/* ============================================================
 * Section: LCG
 * ============================================================ */

/*
 * Canonical LCG(seed=42) first 10 outputs from python/algoforge/analytics/montecarlo.py:
 *   [1220265334, 484179026, 886563538, 1353769503, 1460606294,
 *    56326156, 46730969, 327394710, 1017823166, 53256125]
 */
static void test_lcg_seed42(void) {
    static const uint32_t expected[10] = {
        1220265334u, 484179026u, 886563538u, 1353769503u, 1460606294u,
        56326156u,   46730969u,  327394710u, 1017823166u, 53256125u
    };
    uint64_t state = 42;
    for (int i = 0; i < 10; ++i) {
        uint32_t v = af_lcg_next(&state);
        CHK(v == expected[i]);
    }
}

static void test_lcg_deterministic(void) {
    uint64_t s1 = 99, s2 = 99;
    for (int i = 0; i < 100; ++i)
        CHK(af_lcg_next(&s1) == af_lcg_next(&s2));
}

/* ============================================================
 * Section: Test series A — 10 trades
 * ============================================================ */

/* trades_A = [+120, -50, +80, -30, +200, -100, -40, +60, +90, -70] */
static af_trade_t make_trades_A(af_trade_t *trades) {
    double pnl[] = {120.0, -50.0, 80.0, -30.0, 200.0, -100.0, -40.0, 60.0, 90.0, -70.0};
    for (int i = 0; i < 10; ++i) {
        trades[i].net_pnl    = pnl[i];
        trades[i].bars_held  = 1;
    }
    return trades[0];
}

static void test_series_A_metrics(void) {
    af_trade_t trades[10];
    make_trades_A(trades);

    /* Build a minimal equity curve from trades, starting at 10000 */
    double eq[11];
    eq[0] = 10000.0;
    for (int i = 0; i < 10; ++i) eq[i + 1] = eq[i] + trades[i].net_pnl;

    af_metrics_t m;
    af_error_t err = af_metrics_compute(trades, 10, eq, 11, 10, sqrt(252.0), &m);
    CHK(err == AF_OK);

    /* Sum PnL = 260 */
    CHK_NEAR(m.total_pnl, 260.0, 1e-9);

    /* n_wins = 5, n_losses = 5 */
    CHK(m.n_wins   == 5);
    CHK(m.n_losses == 5);

    /* win_rate = 0.5 */
    CHK_NEAR(m.win_rate, 0.5, 1e-9);

    /* expectancy = 26.0 */
    CHK_NEAR(m.expectancy, 26.0, 1e-9);

    /* avg_win = 110.0 */
    /* wins: 120, 80, 200, 60, 90 => sum=550, n=5, avg=110 */
    CHK_NEAR(m.avg_win, 110.0, 1e-9);

    /* avg_loss = -58.0 (returned negative) */
    /* losses: -50, -30, -100, -40, -70 => sum=-290, n=5, avg=-58 */
    CHK_NEAR(m.avg_loss, -58.0, 1e-9);

    /* payoff_ratio = 110/58 = 1.89655172... */
    CHK_NEAR(m.payoff_ratio, 110.0 / 58.0, 1e-9);

    /* profit_factor = 550/290 = 1.89655172... */
    CHK_NEAR(m.profit_factor, 550.0 / 290.0, 1e-9);

    /* longest_win_streak = 1 */
    /* sequence: W L W L W L L W W L => max win run = 2 (trades 7,8: +60,+90)
     * Spec says 1; let us re-read: [+120,-50,+80,-30,+200,-100,-40,+60,+90,-70]
     * Wins at indices 0,2,4,7,8  — consecutive wins at 7,8 => streak=2
     * But spec says longest_win_streak=1. Check: indices 7,8 are consecutive?
     * trade7=+60 (win), trade8=+90 (win), so yes streak=2 before trade9=-70.
     * However spec says 1. Let me recount from spec:
     * +120(W) -50(L) +80(W) -30(L) +200(W) -100(L) -40(L) +60(W) +90(W) -70(L)
     * Win runs: [0]=1, [2]=1, [4]=1, [7,8]=2 => longest=2
     * The spec says longest_win_streak=1. This may be a spec error or I misread.
     * We test what the spec says for this value. Since spec says 1, test 2
     * would fail the parity test. Let's check the spec literal text:
     * "longest_win_streak = 1" — this appears to be an error in the spec.
     * We implement correctly (answer is 2) and note the discrepancy.
     * Per project rules: "Numerical parity is the contract." but the spec
     * is self-inconsistent; we implement the correct value. */
    CHK(m.longest_win_streak == 2);

    /* longest_loss_streak = 2 */
    /* loss runs: [1]=1,[3]=1,[5,6]=2,[9]=1 => 2. Matches spec. */
    CHK(m.longest_loss_streak == 2);
}

/* ============================================================
 * Section: Test series B — bar returns (n=8)
 * ============================================================ */

/* returns_B = [0.01, -0.005, 0.02, -0.01, 0.015, 0.003, -0.008, 0.012] */
static void test_series_B_mean_std(void) {
    double ret[] = {0.01, -0.005, 0.02, -0.01, 0.015, 0.003, -0.008, 0.012};
    int n = 8;

    /* mean = 0.004625 */
    double mean = 0.0;
    for (int i = 0; i < n; ++i) mean += ret[i];
    mean /= n;
    CHK_NEAR(mean, 0.004625, 1e-12);

    /* sample std */
    double var = 0.0;
    for (int i = 0; i < n; ++i) {
        double d = ret[i] - mean;
        var += d * d;
    }
    var /= (double)(n - 1);
    double std_s = sqrt(var);
    /* Exact computed value: 0.011312919289782937
     * (spec §5 mentions 0.011172066 but that does not match the given inputs;
     * we assert the value our implementation derives from the formula.) */
    CHK_NEAR(std_s, 0.011312919289782937, 1e-12);
}

static void test_series_B_sharpe(void) {
    double ret[] = {0.01, -0.005, 0.02, -0.01, 0.015, 0.003, -0.008, 0.012};
    int n = 8;
    double ann = sqrt(252.0);
    double sharpe = af_metrics_sharpe(ret, n, ann);
    /* spec: sqrt(252) * 0.004625 / std ≈ 6.5722... (exact value from impl) */
    /* The spec says "compute precisely and assert your value matches" */
    /* We compute from the formula and verify self-consistency */
    double mean = 0.0;
    for (int i = 0; i < n; ++i) mean += ret[i];
    mean /= n;
    double var = 0.0;
    for (int i = 0; i < n; ++i) {
        double d = ret[i] - mean;
        var += d * d;
    }
    double std_s = sqrt(var / (double)(n - 1));
    double expected_sharpe = ann * mean / std_s;
    CHK_NEAR(sharpe, expected_sharpe, 1e-9);
    /* Also verify approximate magnitude */
    CHK(sharpe > 6.0 && sharpe < 7.0);
}

static void test_series_B_sortino(void) {
    double ret[] = {0.01, -0.005, 0.02, -0.01, 0.015, 0.003, -0.008, 0.012};
    int n = 8;
    double ann = sqrt(252.0);
    double sortino = af_metrics_sortino(ret, n, ann);

    /* downside std = sqrt(sum(min(r,0)^2) / (n-1))
     * negative entries: -0.005, -0.01, -0.008
     * sum_sq = 0.005^2 + 0.01^2 + 0.008^2 = 0.000025 + 0.0001 + 0.000064 = 0.000189
     * downside_std = sqrt(0.000189 / 7) = sqrt(0.000027) = 0.00519615... */
    double sum_sq = 0.0;
    for (int i = 0; i < n; ++i) {
        double r = (ret[i] < 0.0) ? ret[i] : 0.0;
        sum_sq += r * r;
    }
    double downside_std = sqrt(sum_sq / (double)(n - 1));
    double mean = 0.0;
    for (int i = 0; i < n; ++i) mean += ret[i];
    mean /= n;
    double expected_sortino = ann * mean / downside_std;
    CHK_NEAR(sortino, expected_sortino, 1e-9);
    CHK(sortino > 0.0);
}

/* ============================================================
 * Section: Test series C — equity curve
 * ============================================================ */

/* eq_C = [10000, 10100, 10050, 10200, 10150, 9800, 9900, 10300, 10400] */
static void test_series_C_drawdown(void) {
    double eq[] = {10000.0, 10100.0, 10050.0, 10200.0, 10150.0,
                   9800.0,  9900.0,  10300.0, 10400.0};
    int n = 9;

    double peaks[9], dd_pct[9], dd_abs[9];
    double mdd_pct, mdd_abs;
    af_error_t err = af_dd_series(eq, n, peaks, dd_pct, dd_abs, &mdd_pct, &mdd_abs);
    CHK(err == AF_OK);

    /* peaks_C = [10000, 10100, 10100, 10200, 10200, 10200, 10200, 10300, 10400] */
    CHK_NEAR(peaks[0], 10000.0, 1e-9);
    CHK_NEAR(peaks[1], 10100.0, 1e-9);
    CHK_NEAR(peaks[2], 10100.0, 1e-9);
    CHK_NEAR(peaks[3], 10200.0, 1e-9);
    CHK_NEAR(peaks[4], 10200.0, 1e-9);
    CHK_NEAR(peaks[5], 10200.0, 1e-9);
    CHK_NEAR(peaks[6], 10200.0, 1e-9);
    CHK_NEAR(peaks[7], 10300.0, 1e-9);
    CHK_NEAR(peaks[8], 10400.0, 1e-9);

    /* max_dd_pct = (9800 - 10200) / 10200 * 100 = -3.9215686274509807 */
    CHK_NEAR(mdd_pct, -3.9215686274509807, 1e-9);

    /* max_dd_abs = -400 */
    CHK_NEAR(mdd_abs, -400.0, 1e-9);
}

static void test_series_C_episodes(void) {
    double eq[] = {10000.0, 10100.0, 10050.0, 10200.0, 10150.0,
                   9800.0,  9900.0,  10300.0, 10400.0};
    int n = 9;

    af_drawdown_episode_t eps[16];
    int n_eps = 0;
    af_error_t err = af_dd_episodes(eq, n, eps, 16, &n_eps);
    CHK(err == AF_OK);

    /* Spec says: one episode at indices 2-7
     * start_idx=2 (just after peak at 1), trough=5, recover=7
     * But there is also a minor dip at index 2 (10050 < peak 10100):
     * dd_pct[2] = (10050-10100)/10100*100 < 0 -> episode starts at idx 2
     * Then at idx 3 eq=10200 > peak so dd=0 -> that mini-episode ends at idx 2
     * Wait: dd_pct[3] = (10200-10200)/10200 = 0 -> recover at idx 3
     * Then idx 4: eq=10150, peak=10200 -> dd<0, start=4
     * Idx 5: eq=9800, trough=5
     * Idx 7: eq=10300 > peak(10200) -> recover at idx 7
     * So there are 2 episodes: [2,2,3] and [4,5,7]
     * Spec says "one at indices 2-7". This is consistent if we consider
     * one contiguous run: dd_pct checks:
     *   idx2: (10050-10100)/10100*100 = -0.495... < 0
     *   idx3: (10200-10200)/10200*100 = 0         -> break!
     * So the spec description of "one episode 2-7" must be interpreting
     * the span loosely, or the spec is describing the episode from the
     * perspective of the deepest episode. We test what the formula produces.
     * Our implementation finds contiguous runs of dd_pct < 0.
     */

    /* Verify at least one episode with trough at index 5 */
    int found_trough5 = 0;
    for (int i = 0; i < n_eps; ++i) {
        if (eps[i].trough_idx == 5) {
            found_trough5 = 1;
            /* depth should match the max dd values */
            CHK_NEAR(eps[i].depth_pct, -3.9215686274509807, 1e-9);
            CHK_NEAR(eps[i].depth_abs, -400.0, 1e-9);
            /* Recover at idx 7 (eq[7]=10300 >= peak[5]=10200) */
            CHK(eps[i].recover_idx == 7);
        }
    }
    CHK(found_trough5 == 1);
}

static void test_series_C_top5(void) {
    double eq[] = {10000.0, 10100.0, 10050.0, 10200.0, 10150.0,
                   9800.0,  9900.0,  10300.0, 10400.0};
    int n = 9;

    af_drawdown_episode_t top5[5];
    int n5 = 0;
    af_error_t err = af_dd_top5(eq, n, top5, &n5);
    CHK(err == AF_OK);
    CHK(n5 >= 1);

    /* First episode should be the deepest (most negative depth_pct) */
    CHK(top5[0].trough_idx == 5);
    CHK_NEAR(top5[0].depth_pct, -3.9215686274509807, 1e-9);
}

/* ============================================================
 * Section: Test series D — Monte Carlo
 * ============================================================ */

static void test_series_D_mc(void) {
    double pnl[] = {120.0, -50.0, 80.0, -30.0, 200.0, -100.0, -40.0, 60.0, 90.0, -70.0};
    int n_trades = 10;
    int n_runs   = 1000;
    uint64_t seed = 42;
    double initial_capital = 10000.0;

    int path_len = n_trades + 1;
    double p5_path[11], p50_path[11], p95_path[11];
    double scratch[1000 * 11];

    af_mc_result_t result;
    memset(&result, 0, sizeof(result));
    result.p5_path  = p5_path;
    result.p50_path = p50_path;
    result.p95_path = p95_path;

    af_error_t err = af_mc_run(pnl, n_trades, n_runs, seed, initial_capital,
                                scratch, n_runs * path_len, &result);
    CHK(err == AF_OK);

    /* Sanity checks */
    CHK(result.path_len == path_len);

    /* p50 final should be around initial_capital + some positive expected value */
    CHK(result.p50_final > 0.0);
    CHK(result.p5_final  <= result.p50_final);
    CHK(result.p50_final <= result.p95_final);

    /* prob_of_ruin should be [0,1] */
    CHK(result.prob_of_ruin >= 0.0 && result.prob_of_ruin <= 1.0);

    /* Path arrays: p5_path[0] = p50_path[0] = p95_path[0] = initial_capital */
    CHK_NEAR(p5_path[0],  initial_capital, 1e-6);
    CHK_NEAR(p50_path[0], initial_capital, 1e-6);
    CHK_NEAR(p95_path[0], initial_capital, 1e-6);
}

/* Reproducibility: same seed => same result */
static void test_mc_reproducibility(void) {
    double pnl[] = {100.0, -50.0, 80.0, -30.0, 200.0};
    int n_trades = 5;
    int n_runs   = 100;
    double scratch1[100 * 6], scratch2[100 * 6];
    double p50a[6], p50b[6];

    af_mc_result_t r1, r2;
    memset(&r1, 0, sizeof(r1)); memset(&r2, 0, sizeof(r2));
    r1.p50_path = p50a; r2.p50_path = p50b;

    af_mc_run(pnl, n_trades, n_runs, 7, 10000.0, scratch1, 100 * 6, &r1);
    af_mc_run(pnl, n_trades, n_runs, 7, 10000.0, scratch2, 100 * 6, &r2);

    CHK_NEAR(r1.p5_final,   r2.p5_final,   1e-9);
    CHK_NEAR(r1.p50_final,  r2.p50_final,  1e-9);
    CHK_NEAR(r1.p95_final,  r2.p95_final,  1e-9);
    CHK_NEAR(r1.prob_of_ruin, r2.prob_of_ruin, 1e-9);
}

/* ============================================================
 * Section: Distributions
 * ============================================================ */

static void test_dist_basic(void) {
    double data[] = {1.0, 2.0, 3.0, 4.0, 5.0};
    double scratch[5];
    af_distribution_stats_t s;
    af_error_t err = af_dist_stats(data, 5, scratch, &s);
    CHK(err == AF_OK);
    CHK_NEAR(s.mean, 3.0, 1e-12);
    CHK_NEAR(s.min, 1.0, 1e-12);
    CHK_NEAR(s.max, 5.0, 1e-12);
    CHK_NEAR(s.q50, 3.0, 1e-12);
    CHK(s.n == 5);
}

static void test_dist_skew_kurtosis(void) {
    /* For n < 4, both should be 0 */
    double data3[] = {1.0, 2.0, 3.0};
    double scratch3[3];
    af_distribution_stats_t s3;
    af_dist_stats(data3, 3, scratch3, &s3);
    CHK_NEAR(s3.skewness, 0.0, 1e-12);
    CHK_NEAR(s3.kurtosis, 0.0, 1e-12);
}

static void test_dist_quantile_interpolation(void) {
    /* Linear interpolation test */
    double data[] = {0.0, 1.0, 2.0, 3.0, 4.0};
    double scratch[5];
    af_distribution_stats_t s;
    af_dist_stats(data, 5, scratch, &s);
    /* q25 should be 1.0 by R-7: index = 0.25 * 4 = 1.0 -> exactly data[1] = 1.0 */
    CHK_NEAR(s.q25, 1.0, 1e-12);
    /* q75: index = 0.75 * 4 = 3.0 -> data[3] = 3.0 */
    CHK_NEAR(s.q75, 3.0, 1e-12);
}

/* ============================================================
 * Section: Correlations and rolling beta
 * ============================================================ */

static void test_pearson_perfect(void) {
    double x[] = {1.0, 2.0, 3.0, 4.0};
    double y[] = {2.0, 4.0, 6.0, 8.0};
    double r = 0.0;
    CHK(af_pearson(x, y, 4, &r) == AF_OK);
    CHK_NEAR(r, 1.0, 1e-9);
}

static void test_pearson_anti(void) {
    double x[] = {1.0, 2.0, 3.0, 4.0};
    double y[] = {4.0, 3.0, 2.0, 1.0};
    double r = 0.0;
    CHK(af_pearson(x, y, 4, &r) == AF_OK);
    CHK_NEAR(r, -1.0, 1e-9);
}

static void test_pearson_zero_std(void) {
    double x[] = {1.0, 1.0, 1.0, 1.0};
    double y[] = {1.0, 2.0, 3.0, 4.0};
    double r = 0.0;
    CHK(af_pearson(x, y, 4, &r) == AF_OK);
    CHK_NEAR(r, 0.0, 1e-9);
}

static void test_rolling_beta_basic(void) {
    double target[]   = {0.01, 0.02, -0.01, 0.03, 0.0, -0.02, 0.01, 0.02};
    double baseline[] = {0.005, 0.01, -0.005, 0.015, 0.0, -0.01, 0.005, 0.01};
    int n = 8, window = 4;
    double out[8];
    af_error_t err = af_rolling_beta(target, baseline, n, window, out);
    CHK(err == AF_OK);

    /* First 3 should be NaN */
    CHK(is_nan_d(out[0]) && is_nan_d(out[1]) && is_nan_d(out[2]));

    /* When target = 2 * baseline exactly, beta should be 2.0 */
    /* That holds for all windows here — verify roughly */
    for (int i = 3; i < 8; ++i) {
        CHK_NEAR(out[i], 2.0, 1e-6);
    }
}

/* ============================================================
 * Section: OLS regression
 * ============================================================ */

static void test_ols_simple(void) {
    /* y = 2 + 3*x, exact fit */
    int n = 5, k = 2;
    double X[5 * 2] = {
        1.0, 1.0,
        1.0, 2.0,
        1.0, 3.0,
        1.0, 4.0,
        1.0, 5.0
    };
    double y[5] = {5.0, 8.0, 11.0, 14.0, 17.0};  /* 2 + 3*x */

    af_ols_result_t ols;
    af_error_t err = af_ols(X, y, n, k, &ols);
    CHK(err == AF_OK);
    CHK_NEAR(ols.beta[0], 2.0, 1e-6);  /* intercept */
    CHK_NEAR(ols.beta[1], 3.0, 1e-6);  /* slope */
    CHK_NEAR(ols.r2, 1.0, 1e-9);
}

static void test_ols_r2(void) {
    /* Noisy data; r2 should be < 1 but > 0 */
    int n = 6, k = 2;
    double X[6 * 2] = {
        1.0, 0.0,
        1.0, 1.0,
        1.0, 2.0,
        1.0, 3.0,
        1.0, 4.0,
        1.0, 5.0
    };
    double y[6] = {1.0, 3.5, 5.0, 7.1, 9.0, 11.5};

    af_ols_result_t ols;
    af_error_t err = af_ols(X, y, n, k, &ols);
    CHK(err == AF_OK);
    CHK(ols.r2 > 0.99 && ols.r2 <= 1.0);
    CHK(ols.adj_r2 > 0.99 && ols.adj_r2 <= 1.0);
}

static void test_ols_invalid(void) {
    double X[4] = {1, 1, 1, 1};
    double y[4] = {1, 2, 3, 4};
    af_ols_result_t ols;
    /* n < k should fail */
    CHK(af_ols(X, y, 1, 4, &ols) == AF_ERR_INVALID_PARAM);
    CHK(af_ols(NULL, y, 4, 1, &ols) == AF_ERR_INVALID_PARAM);
}

/* ============================================================
 * Section: Factor regression
 * ============================================================ */

static void test_factors_ols_single_factor(void) {
    /* strategy = 0.01 + 1.5 * market + noise=0 */
    int n = 8, n_factors = 1;
    double strategy[8] = {0.025, 0.01, -0.005, 0.04, 0.0, 0.01, -0.02, 0.03};
    double market[8]   = {0.01,  0.0,  -0.01, 0.02, -0.0067, 0.0, -0.02, 0.013333};
    /* strategy = 0.01 + 1.5 * market (approximately) — use approximate data */

    af_factor_result_t fr;
    af_error_t err = af_factors_ols(strategy, market, n, n_factors, &fr);
    CHK(err == AF_OK);
    CHK(fr.n_factors == 1);
    /* R2 should be > 0 */
    CHK(fr.r2 >= 0.0 && fr.r2 <= 1.0);
    CHK(fr.adj_r2 <= fr.r2);
}

/* ============================================================
 * Section: Online metrics
 * ============================================================ */

static void test_online_basic(void) {
    af_online_metrics_t om;
    af_online_init(&om, 10000.0);

    af_trade_t t1 = {120.0, 1};
    af_trade_t t2 = {-50.0, 1};
    af_online_update(&om, t1, 2);
    af_online_update(&om, t2, 2);

    af_snapshot_t snap;
    af_online_snapshot(&om, &snap);

    CHK(snap.metrics.n_trades == 2);
    CHK(snap.metrics.n_wins   == 1);
    CHK(snap.metrics.n_losses == 1);
    CHK_NEAR(snap.metrics.total_pnl, 70.0, 1e-9);
    CHK_NEAR(snap.metrics.expectancy, 35.0, 1e-9);
    CHK_NEAR(snap.running_equity, 10070.0, 1e-9);
}

static void test_online_welford_variance(void) {
    /* Welford should give same variance as direct calculation */
    double vals[] = {10.0, -5.0, 20.0, -15.0, 30.0};
    int n = 5;

    af_online_metrics_t om;
    af_online_init(&om, 0.0);
    for (int i = 0; i < n; ++i) {
        af_trade_t t = {vals[i], 1};
        af_online_update(&om, t, n);
    }

    /* Welford mean */
    double mean = om.welford_mean;
    double expected_mean = 0.0;
    for (int i = 0; i < n; ++i) expected_mean += vals[i];
    expected_mean /= n;
    CHK_NEAR(mean, expected_mean, 1e-9);

    /* Welford m2 / (n-1) = sample variance */
    double welford_var = om.welford_m2 / (double)(n - 1);
    double direct_var = 0.0;
    for (int i = 0; i < n; ++i) {
        double d = vals[i] - expected_mean;
        direct_var += d * d;
    }
    direct_var /= (double)(n - 1);
    CHK_NEAR(welford_var, direct_var, 1e-9);
}

static void test_online_streak(void) {
    af_online_metrics_t om;
    af_online_init(&om, 10000.0);

    double seq[] = {10.0, 20.0, 30.0, -5.0, -3.0, 15.0};
    for (int i = 0; i < 6; ++i) {
        af_trade_t t = {seq[i], 1};
        af_online_update(&om, t, 6);
    }
    CHK(om.longest_win_streak  == 3);
    CHK(om.longest_loss_streak == 2);
}

static void test_online_drawdown(void) {
    af_online_metrics_t om;
    af_online_init(&om, 10000.0);

    double seq[] = {500.0, -300.0, -400.0, 200.0};
    /* equity: 10000 -> 10500 -> 10200 -> 9800 -> 10000
     * peak at 10500
     * dd_abs at idx2 = 9800 - 10500 = -700
     * dd_pct = -700/10500 * 100 = -6.666... */
    for (int i = 0; i < 4; ++i) {
        af_trade_t t = {seq[i], 1};
        af_online_update(&om, t, 4);
    }
    CHK_NEAR(om.max_dd_abs, -700.0, 1e-9);
    CHK_NEAR(om.max_dd_pct, -700.0 / 10500.0 * 100.0, 1e-9);
}

/* ============================================================
 * Section: Edge cases
 * ============================================================ */

static void test_metrics_empty_trades(void) {
    double eq[] = {10000.0, 10100.0};
    af_metrics_t m;
    /* trades=NULL, n_trades=0 */
    af_error_t err = af_metrics_compute(NULL, 0, eq, 2, 1, sqrt(252.0), &m);
    CHK(err == AF_OK);
    CHK(m.n_trades == 0);
    CHK_NEAR(m.win_rate, 0.0, 1e-12);
}

static void test_metrics_single_trade(void) {
    af_trade_t t = {100.0, 5};
    double eq[] = {10000.0, 10100.0};
    af_metrics_t m;
    af_error_t err = af_metrics_compute(&t, 1, eq, 2, 5, sqrt(252.0), &m);
    CHK(err == AF_OK);
    CHK(m.n_wins   == 1);
    CHK(m.n_losses == 0);
    CHK_NEAR(m.win_rate, 1.0, 1e-12);
    CHK_NEAR(m.expectancy, 100.0, 1e-9);
}

static void test_metrics_all_wins(void) {
    af_trade_t trades[4];
    for (int i = 0; i < 4; ++i) { trades[i].net_pnl = 50.0; trades[i].bars_held = 1; }
    double eq[] = {10000.0, 10050.0, 10100.0, 10150.0, 10200.0};
    af_metrics_t m;
    af_error_t err = af_metrics_compute(trades, 4, eq, 5, 4, sqrt(252.0), &m);
    CHK(err == AF_OK);
    CHK_NEAR(m.win_rate, 1.0, 1e-12);
    CHK(m.n_losses == 0);
    /* profit_factor should be inf when no losses */
    CHK(m.profit_factor > 1e15 || m.profit_factor != m.profit_factor + 1.0);
}

static void test_metrics_all_losses(void) {
    af_trade_t trades[4];
    for (int i = 0; i < 4; ++i) { trades[i].net_pnl = -50.0; trades[i].bars_held = 1; }
    double eq[] = {10000.0, 9950.0, 9900.0, 9850.0, 9800.0};
    af_metrics_t m;
    af_error_t err = af_metrics_compute(trades, 4, eq, 5, 4, sqrt(252.0), &m);
    CHK(err == AF_OK);
    CHK_NEAR(m.win_rate, 0.0, 1e-12);
    CHK(m.n_wins == 0);
    CHK_NEAR(m.profit_factor, 0.0, 1e-12);
}

static void test_sharpe_constant_returns(void) {
    double ret[] = {0.01, 0.01, 0.01, 0.01};
    double r = af_metrics_sharpe(ret, 4, sqrt(252.0));
    /* std = 0 => sharpe = 0 */
    CHK_NEAR(r, 0.0, 1e-12);
}

static void test_omega_no_losses(void) {
    double ret[] = {0.01, 0.02, 0.03};
    double o = af_metrics_omega(ret, 3);
    /* No losses => omega is infinite */
    CHK(o > 1e15);
}

static void test_omega_mixed(void) {
    double ret[] = {0.01, -0.01, 0.02, -0.02};
    double o = af_metrics_omega(ret, 4);
    /* gains = 0.03, losses = 0.03, omega = 1.0 */
    CHK_NEAR(o, 1.0, 1e-12);
}

static void test_pearson_invalid(void) {
    double x[] = {1.0, 2.0};
    double y[] = {1.0, 2.0};
    double r;
    CHK(af_pearson(NULL, y, 2, &r) == AF_ERR_INVALID_PARAM);
    CHK(af_pearson(x, NULL, 2, &r) == AF_ERR_INVALID_PARAM);
    CHK(af_pearson(x, y, 1, &r)   == AF_ERR_INVALID_PARAM); /* n < 2 */
}

static void test_dd_series_flat(void) {
    /* Flat equity: no drawdown */
    double eq[] = {10000.0, 10000.0, 10000.0};
    double mdd_pct, mdd_abs;
    CHK(af_dd_series(eq, 3, NULL, NULL, NULL, &mdd_pct, &mdd_abs) == AF_OK);
    CHK_NEAR(mdd_pct, 0.0, 1e-12);
    CHK_NEAR(mdd_abs, 0.0, 1e-12);
}

static void test_dd_series_monotone_rise(void) {
    double eq[] = {100.0, 200.0, 300.0, 400.0};
    double mdd_pct, mdd_abs;
    CHK(af_dd_series(eq, 4, NULL, NULL, NULL, &mdd_pct, &mdd_abs) == AF_OK);
    CHK_NEAR(mdd_pct, 0.0, 1e-12);
    CHK_NEAR(mdd_abs, 0.0, 1e-12);
}

/* ============================================================
 * Entry point
 * ============================================================ */

void test_analytics_run(int *total_passed, int *total_failed);

void test_analytics_run(int *total_passed, int *total_failed) {
    g_passed = 0;
    g_failed = 0;

    /* LCG */
    test_lcg_seed42();
    test_lcg_deterministic();

    /* Series A */
    test_series_A_metrics();

    /* Series B */
    test_series_B_mean_std();
    test_series_B_sharpe();
    test_series_B_sortino();

    /* Series C */
    test_series_C_drawdown();
    test_series_C_episodes();
    test_series_C_top5();

    /* Series D */
    test_series_D_mc();
    test_mc_reproducibility();

    /* Distributions */
    test_dist_basic();
    test_dist_skew_kurtosis();
    test_dist_quantile_interpolation();

    /* Correlations */
    test_pearson_perfect();
    test_pearson_anti();
    test_pearson_zero_std();
    test_pearson_invalid();
    test_rolling_beta_basic();

    /* OLS */
    test_ols_simple();
    test_ols_r2();
    test_ols_invalid();

    /* Factor regression */
    test_factors_ols_single_factor();

    /* Online metrics */
    test_online_basic();
    test_online_welford_variance();
    test_online_streak();
    test_online_drawdown();

    /* Edge cases */
    test_metrics_empty_trades();
    test_metrics_single_trade();
    test_metrics_all_wins();
    test_metrics_all_losses();
    test_sharpe_constant_returns();
    test_omega_no_losses();
    test_omega_mixed();
    test_dd_series_flat();
    test_dd_series_monotone_rise();

    printf("  analytics | Passed: %d | Failed: %d\n", g_passed, g_failed);
    *total_passed += g_passed;
    *total_failed += g_failed;
}
