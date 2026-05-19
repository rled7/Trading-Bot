/**
 * AlgoForge — c/include/af_backtest.h
 * Bar-by-bar backtesting engine with full performance metrics.
 *
 * Mirrors the C++ BTTrade / BTConfig / BTResult / BacktestEngine types.
 * Uses fixed-size arrays (no dynamic allocation).
 *
 * Link with -lm.
 */
#ifndef AF_BACKTEST_H
#define AF_BACKTEST_H

#include <stddef.h>
#include <stdint.h>
#include "af_types.h"
#include "af_algorithm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Trade record ──────────────────────────────────────────────────────── */
typedef struct {
    int64_t entry_time;         /* bar index of entry */
    int64_t exit_time;          /* bar index of exit  */
    char    symbol[12];
    int     direction;          /* AF_DIR_LONG / AF_DIR_SHORT */
    double  lots;
    double  entry_price;
    double  exit_price;
    double  sl;
    double  tp;
    double  gross_pnl;          /* USD, before commission */
    double  commission;         /* USD */
    int     bars_held;
    char    close_reason[16];   /* "SL" | "TP" | "END" */
    char    algo[64];           /* algo name that triggered entry */
} af_bt_trade_t;

/** Net P&L (gross − commission). */
static inline double af_bt_trade_net_pnl(const af_bt_trade_t *t) {
    return t->gross_pnl - t->commission;
}

/** Returns 1 if net_pnl > 0, else 0. */
static inline int af_bt_trade_is_winner(const af_bt_trade_t *t) {
    return af_bt_trade_net_pnl(t) > 0.0;
}

/* ── Backtest configuration ─────────────────────────────────────────────── */
typedef struct {
    double initial_capital; /* starting equity, default 10 000 */
    double risk_pct;        /* fraction of equity risked per trade, default 0.01 */
    double commission_usd;  /* USD per lot (round-trip), default 7.0 */
    int    slippage_pts;    /* slippage in points (1 pt = 0.00001), default 2 */
    double atr_sl_mult;     /* ATR multiplier for stop-loss, default 1.5 */
    double rr_ratio;        /* reward-to-risk ratio (TP = atr_sl_mult * rr_ratio * ATR), default 2.0 */
    int    warmup_bars;     /* bars skipped before evaluation starts, default 200 */
} af_bt_config_t;

/** Fill *cfg with default values. */
void af_bt_config_default(af_bt_config_t *cfg);

/* ── Result ──────────────────────────────────────────────────────────────── */
#define AF_BT_MAX_TRADES 4096
#define AF_BT_MAX_EQUITY 65536

typedef struct {
    char   symbol[12];
    char   timeframe[8];
    double initial_capital;

    af_bt_trade_t trades[AF_BT_MAX_TRADES];
    int           trade_count;

    double equity_curve[AF_BT_MAX_EQUITY];
    int    equity_count;

    double duration_sec; /* wall-clock time of af_backtest_run() */
} af_bt_result_t;

/* ── Metric functions ───────────────────────────────────────────────────── */

/** Sum of net P&L across all trades. */
double af_bt_net_profit(const af_bt_result_t *r);

/** Fraction of winning trades [0, 1]. */
double af_bt_win_rate(const af_bt_result_t *r);

/** Gross profit / gross loss (999 if no losses). */
double af_bt_profit_factor(const af_bt_result_t *r);

/** Maximum peak-to-trough equity drawdown, expressed as a percentage. */
double af_bt_max_drawdown_pct(const af_bt_result_t *r);

/** Annualised Sharpe ratio (√252 × mean / std of bar returns). */
double af_bt_sharpe_ratio(const af_bt_result_t *r);

/** Annualised Sortino ratio (√252 × mean / downside-std of bar returns). */
double af_bt_sortino_ratio(const af_bt_result_t *r);

/** Calmar ratio: return_pct / max_drawdown_pct. */
double af_bt_calmar_ratio(const af_bt_result_t *r);

/** Average net P&L of winning trades (0 if none). */
double af_bt_avg_win(const af_bt_result_t *r);

/** Average net P&L of losing trades (0 if none, negative value). */
double af_bt_avg_loss(const af_bt_result_t *r);

/**
 * Expectancy = win_rate * avg_win + (1 − win_rate) * avg_loss.
 * A positive value indicates an edge.
 */
double af_bt_expectancy(const af_bt_result_t *r);

/** Print a formatted performance summary table to stdout. */
void af_bt_print_summary(const af_bt_result_t *r);

/* ── Engine ─────────────────────────────────────────────────────────────── */

/**
 * af_backtest_run — execute a bar-by-bar backtest.
 *
 * bars[0] = oldest, bars[count-1] = newest.
 * Returns a fully populated af_bt_result_t.
 *
 * Algorithm flow (per bar i from warmup_bars to count-2):
 *   1. If a trade is open, check next bar's high/low against SL/TP.
 *      Close on first hit; continue to next bar.
 *   2. If no open trade:
 *      a. Run af_run_indicators() and af_pattern_engine_scan() on bars[0..i].
 *      b. Call af_algo_safe_evaluate().
 *      c. If actionable, fill at bars[i+1].open ± slippage; set SL/TP from ATR.
 *   3. Position sizing: lots = capital * risk_pct / (stop_dist * 100 000),
 *      clamped to [0.01, 10.0] and rounded to nearest 0.01.
 *
 * Any open trade at the end of the bar array is closed at bars[count-1].close
 * with close_reason = "END".
 */
af_bt_result_t af_backtest_run(
    af_algorithm_t      *algo,
    const af_bt_config_t *cfg,
    const af_bar_t       *bars,
    size_t                count,
    const char           *symbol,
    af_timeframe_t        tf
);

#ifdef __cplusplus
}
#endif

#endif /* AF_BACKTEST_H */
