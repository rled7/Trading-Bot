/**
 * AlgoForge — c/tests/test_risk.c
 * Round 10: Unit tests for risk / position-sizing / drawdown guard.
 *
 * Compile (from repo root):
 *   cc -std=c11 -Wall -Wextra -lm \
 *      c/tests/test_risk.c -o test_risk && ./test_risk
 *
 * Signal processor is NOT tested here because it requires a live broker.
 * Drawdown-guard and position-sizer are fully standalone.
 */

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "../include/af_risk.h"
#include "../src/risk.c"
#include "../src/drawdown_guard.c"

/* ──────────────────────────────────────────────────────────────────────────
 * Helpers
 * ────────────────────────────────────────────────────────────────────────── */
static int g_tests  = 0;
static int g_passed = 0;
static int g_failed = 0;

#define ASSERT_NEAR(label, got, expected, tol)                              \
    do {                                                                     \
        g_tests++;                                                           \
        double _g = (got), _e = (expected), _t = (tol);                    \
        if (fabs(_g - _e) <= _t) {                                          \
            g_passed++;                                                      \
            printf("  PASS  %-42s  got=%.6f\n", (label), _g);              \
        } else {                                                             \
            g_failed++;                                                      \
            printf("  FAIL  %-42s  got=%.6f  expected≈%.6f (tol=%.6f)\n", \
                   (label), _g, _e, _t);                                    \
        }                                                                    \
    } while (0)

#define ASSERT_INT(label, got, expected)                                     \
    do {                                                                     \
        g_tests++;                                                           \
        int _g = (got), _e = (expected);                                    \
        if (_g == _e) {                                                      \
            g_passed++;                                                      \
            printf("  PASS  %-42s  got=%d\n", (label), _g);                \
        } else {                                                             \
            g_failed++;                                                      \
            printf("  FAIL  %-42s  got=%d  expected=%d\n",                 \
                   (label), _g, _e);                                         \
        }                                                                    \
    } while (0)

/* ──────────────────────────────────────────────────────────────────────────
 * Test 1 — af_kelly_fraction
 * ────────────────────────────────────────────────────────────────────────── */
static void test_kelly(void)
{
    printf("\n=== af_kelly_fraction ===\n");

    /*
     * win_rate=0.55, rr=2.0
     * edge = 0.55*2.0 - 0.45 = 1.10 - 0.45 = 0.65
     * kelly = 0.65 / 2.0 = 0.325   (full Kelly)
     * half  = 0.1625
     * max_risk=0.10  → capped to 0.10
     */
    double k1 = af_kelly_fraction(0.55, 2.0, 0.10);
    ASSERT_NEAR("WR=0.55 RR=2.0 cap=0.10 → 0.10", k1, 0.10, 1e-9);

    /* Uncapped: max_risk=0.20 → half-Kelly = 0.1625 */
    double k2 = af_kelly_fraction(0.55, 2.0, 0.20);
    ASSERT_NEAR("WR=0.55 RR=2.0 cap=0.20 → 0.1625", k2, 0.1625, 1e-9);

    /* Negative edge: WR=0.40, RR=1.0 → edge=-0.20 → kelly=-0.20 → half=-0.10
     * floored to 0.005 */
    double k3 = af_kelly_fraction(0.40, 1.0, 0.10);
    ASSERT_NEAR("WR=0.40 RR=1.0 (neg edge) → floor 0.005", k3, 0.005, 1e-9);

    /* RR=0 guard: result should be 0.005 (floored) */
    double k4 = af_kelly_fraction(0.60, 0.0, 0.05);
    ASSERT_NEAR("RR=0 guard → 0.005", k4, 0.005, 1e-9);
}

/* ──────────────────────────────────────────────────────────────────────────
 * Test 2 — af_normalize_lots
 * ────────────────────────────────────────────────────────────────────────── */
static void test_normalize(void)
{
    printf("\n=== af_normalize_lots ===\n");

    /* Basic rounding: 0.123 with step 0.01 → 0.12 */
    ASSERT_NEAR("0.123 step=0.01 → 0.12",
                af_normalize_lots(0.123, 0.01, 0.01, 100.0), 0.12, 1e-9);

    /* Rounding up: 0.126 with step 0.01 → 0.13 */
    ASSERT_NEAR("0.126 step=0.01 → 0.13",
                af_normalize_lots(0.126, 0.01, 0.01, 100.0), 0.13, 1e-9);

    /* Clamped to min: 0.001 with min=0.01 → 0.01 */
    ASSERT_NEAR("0.001 clamped to min=0.01",
                af_normalize_lots(0.001, 0.01, 0.01, 100.0), 0.01, 1e-9);

    /* Clamped to max: 200 with max=100 → 100 */
    ASSERT_NEAR("200.0 clamped to max=100.0",
                af_normalize_lots(200.0, 0.01, 0.01, 100.0), 100.0, 1e-9);

    /* Step=0 defaults to 0.01: 0.555 → 0.56 */
    ASSERT_NEAR("step=0 defaults to 0.01 → 0.56",
                af_normalize_lots(0.555, 0.0, 0.01, 100.0), 0.56, 1e-9);

    /* Step=0.1: 0.74 → 0.7 */
    ASSERT_NEAR("0.74 step=0.1 → 0.70",
                af_normalize_lots(0.74, 0.1, 0.01, 100.0), 0.7, 1e-9);
}

/* ──────────────────────────────────────────────────────────────────────────
 * Test 3 — af_size_position
 * ────────────────────────────────────────────────────────────────────────── */
static void test_size_position(void)
{
    printf("\n=== af_size_position ===\n");

    af_symbol_info_t sym;
    memset(&sym, 0, sizeof(sym));
    snprintf(sym.name, sizeof(sym.name), "EURUSD");
    sym.digits        = 5;
    sym.point         = 0.00001;
    sym.contract_size = 100000.0;
    sym.volume_min    = 0.01;
    sym.volume_max    = 100.0;
    sym.volume_step   = 0.01;

    af_size_result_t res;
    af_error_t rc;

    /* ── Case 1: FIXED_RISK, 1 % of $10 000, entry=1.10000, SL=1.09000
     * stop_dist = |1.10000 - 1.09000| = 0.01000
     * stop_in_pts = 0.01 / 0.00001 = 1000
     * val_per_point = 100000 * 0.00001 = 1.0
     * risk_amount = 10000 * 0.01 = 100
     * raw_lots = 100 / (1000 * 1.0) = 0.10
     */
    rc = af_size_position(AF_SIZE_FIXED_RISK,
                          10000.0, 1.10000, 1.09000,
                          &sym, 0.0, 0.01,
                          100.0, 0.01,
                          0.5, 2.0, &res);
    ASSERT_INT("FIXED_RISK rc=AF_OK", rc, AF_OK);
    ASSERT_NEAR("FIXED_RISK lots=0.10",        res.lots,          0.10, 0.005);
    ASSERT_NEAR("FIXED_RISK risk_amount≈100",  res.risk_amount,  100.0, 1.0);
    ASSERT_NEAR("FIXED_RISK risk_pct≈0.01",    res.risk_pct,     0.01,  0.001);
    ASSERT_NEAR("FIXED_RISK stop_dist=0.01",   res.stop_distance, 0.01, 1e-9);

    /* ── Case 2: ATR-based stop (stop_loss=0, atr provided)
     * stop_dist = 0.00200 * 1.5 = 0.00300
     * stop_in_pts = 300
     * risk_amount = 10000 * 0.01 = 100
     * raw_lots = 100 / (300 * 1.0) = 0.3333
     * normalised → 0.33
     */
    rc = af_size_position(AF_SIZE_ATR_RISK,
                          10000.0, 1.10000, 0.0,
                          &sym, 0.00200, 0.01,
                          100.0, 0.01,
                          0.5, 2.0, &res);
    ASSERT_INT("ATR_RISK rc=AF_OK", rc, AF_OK);
    ASSERT_NEAR("ATR_RISK stop_dist=0.003",    res.stop_distance, 0.003,  1e-9);
    ASSERT_NEAR("ATR_RISK lots≈0.33",          res.lots,          0.33,   0.02);

    /* ── Case 3: KELLY method
     * WR=0.55, RR=2.0 → half-Kelly=0.1625, cap=0.02 → effective risk=0.02
     * risk_amount = 200; same stop_dist=0.01 → lots=0.20
     */
    rc = af_size_position(AF_SIZE_KELLY,
                          10000.0, 1.10000, 1.09000,
                          &sym, 0.0, 0.02,   /* max_risk=0.02 */
                          100.0, 0.01,
                          0.55, 2.0, &res);
    ASSERT_INT("KELLY rc=AF_OK", rc, AF_OK);
    ASSERT_NEAR("KELLY lots=0.20", res.lots, 0.20, 0.01);

    /* ── Case 4: invalid param (NULL sym) */
    rc = af_size_position(AF_SIZE_FIXED_RISK,
                          10000.0, 1.10000, 1.09000,
                          NULL, 0.0, 0.01,
                          100.0, 0.01,
                          0.5, 2.0, &res);
    ASSERT_INT("NULL sym → AF_ERR_INVALID_PARAM", rc, AF_ERR_INVALID_PARAM);

    /* ── Case 5: negative balance */
    rc = af_size_position(AF_SIZE_FIXED_RISK,
                          -1000.0, 1.10000, 1.09000,
                          &sym, 0.0, 0.01,
                          100.0, 0.01,
                          0.5, 2.0, &res);
    ASSERT_INT("balance<0 → AF_ERR_INVALID_PARAM", rc, AF_ERR_INVALID_PARAM);
}

/* ──────────────────────────────────────────────────────────────────────────
 * Test 4 — af_drawdown_guard_check
 * ────────────────────────────────────────────────────────────────────────── */
static void test_drawdown_guard(void)
{
    printf("\n=== af_drawdown_guard_check ===\n");

    af_drawdown_guard_t g;
    af_guard_status_t   s;

    /* Init: balance=10000, equity=10000 */
    af_drawdown_guard_init(&g, 10000.0, 10000.0,
                           AF_DRAWDOWN_DAILY_LIMIT,
                           AF_DRAWDOWN_TOTAL_LIMIT);

    ASSERT_INT("init: state=GREEN", g.state, AF_GUARD_GREEN);
    ASSERT_NEAR("init: peak=10000", g.peak_equity, 10000.0, 1e-9);

    /* Check at equity=9900: -1 % daily → GREEN */
    s = af_drawdown_guard_check(&g, 9900.0, 10000.0);
    ASSERT_INT("9900 (1% loss) → GREEN", s.state, AF_GUARD_GREEN);
    ASSERT_INT("9900 → trading_allowed=1", s.trading_allowed, 1);

    /* Check at equity=9700: -3 % daily → still GREEN (< 75 % of 5 %) ?
     * warn_thresh = 0.75 * 0.05 = 0.0375 daily loss
     * 9700 daily_pct = (9700-10000)/10000 = -0.03 < 0.0375 → GREEN */
    s = af_drawdown_guard_check(&g, 9700.0, 10000.0);
    ASSERT_INT("9700 (3% loss) → GREEN", s.state, AF_GUARD_GREEN);

    /* Check at equity=9620: -3.8 % → daily_pct=-0.038 ≤ -0.0375 (75% of 5%) → YELLOW */
    s = af_drawdown_guard_check(&g, 9620.0, 10000.0);
    ASSERT_INT("9620 (3.8% loss) → YELLOW", s.state, AF_GUARD_YELLOW);
    ASSERT_INT("9620 → trading_allowed=1", s.trading_allowed, 1);

    /* Check at equity=9400: -6 % daily (> 5 % limit) → RED */
    s = af_drawdown_guard_check(&g, 9400.0, 10000.0);
    ASSERT_INT("9400 (6% loss) → RED", s.state, AF_GUARD_RED);
    ASSERT_INT("9400 → trading_allowed=0", s.trading_allowed, 0);
    ASSERT_NEAR("9400 daily_pnl=-600", s.daily_pnl, -600.0, 1.0);

    /* Daily total-drawdown check: equity=8500 from peak=10000 → 15 % → RED */
    af_drawdown_guard_t g2;
    af_drawdown_guard_init(&g2, 10000.0, 10000.0,
                           AF_DRAWDOWN_DAILY_LIMIT,
                           AF_DRAWDOWN_TOTAL_LIMIT);
    s = af_drawdown_guard_check(&g2, 8500.0, 10000.0);
    ASSERT_INT("8500 (15% total DD) → RED", s.state, AF_GUARD_RED);
    ASSERT_NEAR("8500 total_dd_pct≈0.15", s.total_dd_pct, 0.15, 0.001);

    /* Equity rises above peak → peak updated */
    af_drawdown_guard_t g3;
    af_drawdown_guard_init(&g3, 10000.0, 10000.0,
                           AF_DRAWDOWN_DAILY_LIMIT,
                           AF_DRAWDOWN_TOTAL_LIMIT);
    af_drawdown_guard_check(&g3, 10500.0, 10500.0);
    ASSERT_NEAR("peak updated to 10500", g3.peak_equity, 10500.0, 1e-9);

    /* Recover YELLOW → GREEN when metrics improve */
    af_drawdown_guard_t g4;
    af_drawdown_guard_init(&g4, 10000.0, 10000.0,
                           AF_DRAWDOWN_DAILY_LIMIT,
                           AF_DRAWDOWN_TOTAL_LIMIT);
    af_drawdown_guard_check(&g4, 9620.0, 10000.0); /* → YELLOW (3.8% loss > 3.75% warn) */
    ASSERT_INT("after 9620: YELLOW", g4.state, AF_GUARD_YELLOW);
    s = af_drawdown_guard_check(&g4, 9950.0, 10000.0); /* → back to GREEN */
    ASSERT_INT("after 9950: back to GREEN", s.state, AF_GUARD_GREEN);
}

/* ──────────────────────────────────────────────────────────────────────────
 * main
 * ────────────────────────────────────────────────────────────────────────── */
int main(void)
{
    printf("AlgoForge Round 10 — Risk Unit Tests\n");
    printf("=====================================\n");

    test_kelly();
    test_normalize();
    test_size_position();
    test_drawdown_guard();

    printf("\n=====================================\n");
    printf("Results: %d/%d passed", g_passed, g_tests);
    if (g_failed > 0) {
        printf("  *** %d FAILED ***\n", g_failed);
        return 1;
    }
    printf("  — all OK\n");
    return 0;
}
