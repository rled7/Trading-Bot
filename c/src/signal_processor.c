/**
 * AlgoForge — c/src/signal_processor.c
 * Round 10: Signal processor implementation.
 *
 * Receives algorithm decisions, applies cooldown filtering, sizes positions
 * via the broker's tick + account data, and places market orders.
 *
 * The cooldown table is a fixed-size array (AF_SP_MAX_COOLDOWNS) scanned
 * linearly on each call.  Stale entries are reused in preference to growing
 * the table.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../include/af_risk.h"

/* ──────────────────────────────────────────────────────────────────────────
 * af_signal_processor_init
 * ────────────────────────────────────────────────────────────────────────── */
void af_signal_processor_init(af_signal_processor_t *sp,
                               af_broker_t *broker,
                               double max_risk,
                               int cooldown_sec)
{
    if (!sp) return;

    sp->broker       = broker;
    sp->max_risk     = max_risk;
    sp->cooldown_sec = cooldown_sec;
    sp->processed    = 0;
    sp->rejected     = 0;
    sp->n_cooldowns  = 0;
    memset(sp->cooldowns, 0, sizeof(sp->cooldowns));
}

/* ──────────────────────────────────────────────────────────────────────────
 * af_signal_processor_process
 *
 * Returns 1 if an order was placed, 0 otherwise.
 * ────────────────────────────────────────────────────────────────────────── */
int af_signal_processor_process(af_signal_processor_t *sp,
                                 const af_algo_decision_t *decision)
{
    if (!sp || !decision) return 0;

    /* ── Actionability gate ── */
    if (!af_algo_decision_is_actionable(decision)) {
        sp->rejected++;
        return 0;
    }

    /* ── Build cooldown key: "<SYMBOL>_L" or "<SYMBOL>_S" ── */
    char key[72];
    const char *dir_suffix = (decision->direction == AF_DIR_LONG) ? "L" : "S";
    snprintf(key, sizeof(key), "%s_%s", decision->symbol, dir_suffix);

    /* ── Cooldown check ── */
    int64_t now      = (int64_t)time(NULL);
    int     found    = -1;   /* index of matching key if any              */
    int     reuse    = -1;   /* index of first stale slot for re-use      */

    for (int i = 0; i < sp->n_cooldowns; i++) {
        if (strcmp(sp->cooldowns[i].key, key) == 0) {
            found = i;
            /* Is this entry still within the cooldown window? */
            if ((now - sp->cooldowns[i].timestamp) < (int64_t)sp->cooldown_sec) {
                sp->rejected++;
                return 0;   /* cooldown active — reject signal */
            }
            /* Stale entry: will reuse this slot */
            reuse = i;
            break;
        }
        /* Track first stale slot of *any* key for potential reuse */
        if (reuse == -1 &&
            (now - sp->cooldowns[i].timestamp) >= (int64_t)sp->cooldown_sec) {
            reuse = i;
        }
    }

    /* ── Market data ── */
    af_tick_t        tick;
    af_symbol_info_t sym;
    af_account_info_t acct;

    if (sp->broker->get_tick(sp->broker, decision->symbol, &tick) != AF_OK) {
        sp->rejected++;
        return 0;
    }
    if (sp->broker->get_symbol_info(sp->broker, decision->symbol, &sym) != AF_OK) {
        sp->rejected++;
        return 0;
    }
    if (sp->broker->get_account(sp->broker, &acct) != AF_OK) {
        sp->rejected++;
        return 0;
    }

    /* ── ATR / stop distance ── */
    double atr       = (decision->atr > 0.0) ? decision->atr : tick.ask * 0.001;
    double stop_dist = atr * 1.5;

    /* ── Lot sizing ── */
    double contract_size = (sym.contract_size > 0.0) ? sym.contract_size : 100000.0;
    double risk_amount   = acct.balance * sp->max_risk;

    double raw_lots;
    if (stop_dist > 1e-10 && contract_size > 1e-10) {
        raw_lots = risk_amount / (stop_dist * contract_size);
    } else {
        raw_lots = sym.volume_min > 0.0 ? sym.volume_min : 0.01;
    }

    double vmin  = (sym.volume_min  > 0.0) ? sym.volume_min  : 0.01;
    double vmax  = (sym.volume_max  > 0.0) ? sym.volume_max  : 100.0;
    double vstep = (sym.volume_step > 0.0) ? sym.volume_step : 0.01;
    double lots  = af_normalize_lots(raw_lots, vstep, vmin, vmax);

    /* ── Prices ── */
    int is_long = (decision->direction == AF_DIR_LONG);
    double entry = is_long ? tick.ask : tick.bid;
    double sl    = is_long ? entry - atr * 1.5 : entry + atr * 1.5;
    double tp    = is_long ? entry + atr * 3.0 : entry - atr * 3.0;

    af_order_type_t otype = is_long ? AF_ORDER_BUY : AF_ORDER_SELL;

    /* ── Place order ── */
    char comment[64];
    snprintf(comment, sizeof(comment), "AF_SP conf=%.2f", decision->confidence);

    af_order_t order;
    memset(&order, 0, sizeof(order));

    af_error_t rc = sp->broker->place_order(
        sp->broker,
        decision->symbol,
        otype,
        lots,
        0.0,   /* price=0 → market order */
        sl, tp,
        1000,  /* magic number */
        comment,
        &order
    );

    if (rc != AF_OK) {
        sp->rejected++;
        return 0;
    }

    /* ── Update cooldown table ── */
    if (found != -1) {
        /* Update the existing (now-stale) slot for this key */
        sp->cooldowns[found].timestamp = now;
    } else if (reuse != -1) {
        /* Overwrite a stale slot from a different key */
        snprintf(sp->cooldowns[reuse].key, sizeof(sp->cooldowns[reuse].key),
                 "%s", key);
        sp->cooldowns[reuse].timestamp = now;
    } else if (sp->n_cooldowns < AF_SP_MAX_COOLDOWNS) {
        /* Append new entry */
        int idx = sp->n_cooldowns++;
        snprintf(sp->cooldowns[idx].key, sizeof(sp->cooldowns[idx].key),
                 "%s", key);
        sp->cooldowns[idx].timestamp = now;
    }
    /* else: table full and no stale slot — order placed but not tracked */

    sp->processed++;
    printf("[SignalProcessor] ORDER #%llu placed: %s %s %.2f lots @ %.5f "
           "SL=%.5f TP=%.5f (conf=%.2f)\n",
           (unsigned long long)order.ticket,
           decision->symbol,
           is_long ? "BUY" : "SELL",
           lots, entry, sl, tp,
           decision->confidence);

    return 1;
}
