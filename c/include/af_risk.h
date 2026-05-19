/**
 * AlgoForge — c/include/af_risk.h
 * Round 10: Risk + Execution types and function declarations.
 *
 * Covers:
 *   - Position sizing  (af_size_position, af_normalize_lots, af_kelly_fraction)
 *   - Drawdown guard   (af_drawdown_guard_t + init/check)
 *   - Signal processor (af_signal_processor_t + init/process)
 */
#ifndef AF_RISK_H
#define AF_RISK_H

#include <stdint.h>
#include "af_types.h"
#include "af_broker.h"
#include "af_algorithm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ══════════════════════════════════════════════════════════════════════════
 * Position sizing
 * ══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    AF_SIZE_FIXED_RISK = 0,   /* risk_pct of balance per trade            */
    AF_SIZE_ATR_RISK   = 1,   /* stop distance derived from ATR           */
    AF_SIZE_KELLY      = 2    /* half-Kelly fraction capped at risk_pct   */
} af_sizing_method_t;

typedef struct {
    double lots;          /* normalised lot size to trade                 */
    double risk_amount;   /* currency units risked                        */
    double risk_pct;      /* effective risk fraction (0-1)                */
    double stop_distance; /* stop distance used (in price units)          */
    int    capped;        /* 1 if lots were clamped to volume_min/max     */
} af_size_result_t;

typedef enum {
    AF_SL_ATR        = 0,
    AF_SL_SWING      = 1,
    AF_SL_STRUCTURE  = 2,
    AF_SL_FIXED_PTS  = 3
} af_sl_method_t;

typedef struct {
    double       price;     /* absolute stop-loss price                   */
    double       distance;  /* distance from entry in price units         */
    af_sl_method_t method;
} af_sl_result_t;

typedef struct {
    double tp1, tp2, tp3;  /* take-profit price levels                   */
    double rr1, rr2, rr3;  /* reward-to-risk ratios for each level       */
} af_tp_result_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Drawdown guard
 * ══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    AF_GUARD_GREEN  = 0,   /* within limits — trading allowed             */
    AF_GUARD_YELLOW = 1,   /* approaching limits — warning                */
    AF_GUARD_RED    = 2    /* limit breached — trading halted             */
} af_guard_state_t;

typedef struct {
    af_guard_state_t state;
    double daily_pnl;        /* unrealised P&L since day start             */
    double daily_pnl_pct;    /* daily_pnl / daily_start_equity             */
    double total_dd_pct;     /* (peak_equity - current) / peak_equity      */
    double peak_equity;      /* highest equity seen                        */
    double current_equity;   /* equity passed to last check()              */
    int    trading_allowed;  /* 1 = GREEN or YELLOW; 0 = RED               */
    char   reason[128];      /* human-readable halt reason                 */
} af_guard_status_t;

/* Default limits (may be overridden via af_drawdown_guard_init) */
#define AF_DRAWDOWN_DAILY_LIMIT  0.05   /* 5 % daily loss triggers RED  */
#define AF_DRAWDOWN_TOTAL_LIMIT  0.15   /* 15 % total drawdown triggers RED */

typedef struct {
    double daily_limit;          /* fractional daily loss limit            */
    double total_limit;          /* fractional total drawdown limit        */
    double peak_equity;          /* running high-water mark                */
    double daily_start_equity;   /* equity at start of current day         */
    double initial_balance;      /* balance at initialisation              */
    af_guard_state_t state;
    int    reset_day;            /* encoded day: tm_yday + tm_year*1000    */
    int    halt_count;           /* number of times RED was entered        */
    char   halt_reason[256];
} af_drawdown_guard_t;

void            af_drawdown_guard_init (af_drawdown_guard_t *g,
                                         double balance, double equity,
                                         double daily_limit, double total_limit);

af_guard_status_t af_drawdown_guard_check(af_drawdown_guard_t *g,
                                           double equity, double balance);

/* ══════════════════════════════════════════════════════════════════════════
 * Signal processor
 * ══════════════════════════════════════════════════════════════════════════ */

#define AF_SP_MAX_COOLDOWNS 64

typedef struct {
    char    key[72];      /* "<SYMBOL>_L" or "<SYMBOL>_S"                 */
    int64_t timestamp;    /* unix time of last trade on this key          */
} af_sp_cooldown_t;

typedef struct {
    af_broker_t      *broker;
    double            max_risk;        /* fraction of balance to risk      */
    int               cooldown_sec;    /* minimum seconds between signals  */
    int               processed;       /* orders successfully placed       */
    int               rejected;        /* signals rejected                 */
    af_sp_cooldown_t  cooldowns[AF_SP_MAX_COOLDOWNS];
    int               n_cooldowns;
} af_signal_processor_t;

void af_signal_processor_init(af_signal_processor_t *sp,
                               af_broker_t *broker,
                               double max_risk,
                               int cooldown_sec);

int  af_signal_processor_process(af_signal_processor_t *sp,
                                  const af_algo_decision_t *decision);

/* ══════════════════════════════════════════════════════════════════════════
 * Position-sizer helpers (also used internally by signal processor)
 * ══════════════════════════════════════════════════════════════════════════ */

/**
 * af_normalize_lots — round lots to the nearest valid step and clamp.
 *
 * @param lots      raw lot size
 * @param step      broker volume step (e.g. 0.01); treated as 0.01 if ≤ 0
 * @param min_lots  broker minimum volume
 * @param max_lots  broker maximum volume
 * @return          normalised lot size in [min_lots, max_lots]
 */
double af_normalize_lots(double lots,
                          double step,
                          double min_lots,
                          double max_lots);

/**
 * af_kelly_fraction — half-Kelly stake, floored at 0.005, capped at max_risk.
 *
 * edge  = win_rate * rr - (1 - win_rate)
 * kelly = edge / rr          (full-Kelly)
 * half  = kelly * 0.5
 *
 * @param win_rate  historical win rate (0–1)
 * @param rr        reward-to-risk ratio (> 0)
 * @param max_risk  upper cap on the returned fraction
 * @return          clamped half-Kelly fraction
 */
double af_kelly_fraction(double win_rate, double rr, double max_risk);

/**
 * af_size_position — compute a normalised lot size for the given parameters.
 *
 * @param method        sizing algorithm to use
 * @param balance       account balance in account currency
 * @param entry_price   intended entry price
 * @param stop_loss     stop-loss price (≤ 0 means "derive from ATR")
 * @param sym           symbol info (must not be NULL)
 * @param atr           current ATR; used for ATR-based stop if stop_loss ≤ 0
 * @param risk_pct      fraction of balance to risk (e.g. 0.01 for 1 %)
 * @param max_lots      hard cap on lot size
 * @param min_lots      hard floor on lot size
 * @param win_rate      historical win rate (used only for AF_SIZE_KELLY)
 * @param rr_ratio      reward-to-risk ratio (used only for AF_SIZE_KELLY)
 * @param out           result struct to populate (must not be NULL)
 * @return              AF_OK on success, AF_ERR_INVALID_PARAM on bad inputs
 */
af_error_t af_size_position(af_sizing_method_t method,
                             double balance,
                             double entry_price,
                             double stop_loss,
                             const af_symbol_info_t *sym,
                             double atr,
                             double risk_pct,
                             double max_lots,
                             double min_lots,
                             double win_rate,
                             double rr_ratio,
                             af_size_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* AF_RISK_H */
