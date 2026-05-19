/**
 * AlgoForge — c/src/drawdown_guard.c
 * Round 10: Drawdown guard implementation.
 *
 * Tracks daily and total drawdown.  When limits are breached the guard
 * transitions to RED and sets trading_allowed = 0.  A YELLOW warning is
 * emitted when 75 % of a limit is approached.
 *
 * Daily state resets at UTC midnight (detected by a change in tm_yday).
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../include/af_risk.h"

/* ──────────────────────────────────────────────────────────────────────────
 * Internal helper: encode today as (tm_year * 1000 + tm_yday)
 * ────────────────────────────────────────────────────────────────────────── */
static int current_day(void)
{
    time_t now = time(NULL);
    struct tm *t = gmtime(&now);
    return t->tm_yday + t->tm_year * 1000;
}

/* ──────────────────────────────────────────────────────────────────────────
 * af_drawdown_guard_init
 * ────────────────────────────────────────────────────────────────────────── */
void af_drawdown_guard_init(af_drawdown_guard_t *g,
                             double balance,
                             double equity,
                             double daily_limit,
                             double total_limit)
{
    if (!g) return;

    g->daily_limit       = (daily_limit > 0.0) ? daily_limit : AF_DRAWDOWN_DAILY_LIMIT;
    g->total_limit       = (total_limit > 0.0) ? total_limit : AF_DRAWDOWN_TOTAL_LIMIT;
    g->initial_balance   = balance;
    g->peak_equity       = equity;
    g->daily_start_equity = equity;
    g->state             = AF_GUARD_GREEN;
    g->reset_day         = current_day();
    g->halt_count        = 0;
    memset(g->halt_reason, 0, sizeof(g->halt_reason));
}

/* ──────────────────────────────────────────────────────────────────────────
 * af_drawdown_guard_check
 * ────────────────────────────────────────────────────────────────────────── */
af_guard_status_t af_drawdown_guard_check(af_drawdown_guard_t *g,
                                           double equity,
                                           double balance)
{
    af_guard_status_t status;
    memset(&status, 0, sizeof(status));

    /* ── Daily reset at UTC midnight ── */
    int today = current_day();
    if (today != g->reset_day) {
        g->daily_start_equity = equity;
        g->reset_day          = today;
        /* Auto-recover from RED at start of new day */
        if (g->state == AF_GUARD_RED) {
            g->state = AF_GUARD_GREEN;
            memset(g->halt_reason, 0, sizeof(g->halt_reason));
        }
    }

    /* ── Update high-water mark ── */
    if (equity > g->peak_equity) {
        g->peak_equity = equity;
    }

    /* ── Metrics ── */
    double daily_pnl  = equity - g->daily_start_equity;
    double daily_pct  = daily_pnl / (g->daily_start_equity + 1e-10);
    double total_dd   = (g->peak_equity - equity) / (g->peak_equity + 1e-10);

    double warn_thresh = 0.75;

    /* ── State machine ── */
    if (daily_pct <= -(g->daily_limit) || total_dd >= g->total_limit) {
        /* Breach → RED */
        if (g->state != AF_GUARD_RED) {
            g->halt_count++;
        }
        g->state = AF_GUARD_RED;

        if (daily_pct <= -(g->daily_limit)) {
            snprintf(g->halt_reason, sizeof(g->halt_reason),
                     "Daily loss %.2f%% exceeds limit %.2f%%",
                     fabs(daily_pct) * 100.0,
                     g->daily_limit * 100.0);
        } else {
            snprintf(g->halt_reason, sizeof(g->halt_reason),
                     "Total drawdown %.2f%% exceeds limit %.2f%%",
                     total_dd * 100.0,
                     g->total_limit * 100.0);
        }

    } else if (daily_pct <= -(g->daily_limit * warn_thresh) ||
               total_dd  >=  (g->total_limit * warn_thresh)) {
        /* Approaching limit → YELLOW (only escalate, never de-escalate from RED) */
        if (g->state == AF_GUARD_GREEN) {
            g->state = AF_GUARD_YELLOW;
        }
    } else {
        /* Clear of all thresholds: recover YELLOW → GREEN */
        if (g->state == AF_GUARD_YELLOW) {
            g->state = AF_GUARD_GREEN;
        }
    }

    /* ── Fill status struct ── */
    status.state           = g->state;
    status.daily_pnl       = daily_pnl;
    status.daily_pnl_pct   = daily_pct;
    status.total_dd_pct    = total_dd;
    status.peak_equity     = g->peak_equity;
    status.current_equity  = equity;
    status.trading_allowed = (g->state != AF_GUARD_RED) ? 1 : 0;

    if (g->state == AF_GUARD_RED) {
        snprintf(status.reason, sizeof(status.reason), "%s", g->halt_reason);
    } else if (g->state == AF_GUARD_YELLOW) {
        snprintf(status.reason, sizeof(status.reason),
                 "Warning: approaching drawdown limits (daily %.2f%%, total_dd %.2f%%)",
                 fabs(daily_pct) * 100.0, total_dd * 100.0);
    } else {
        snprintf(status.reason, sizeof(status.reason), "OK");
    }

    (void)balance; /* balance is available if needed for future extensions */

    return status;
}
