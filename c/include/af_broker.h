#ifndef AF_BROKER_H
#define AF_BROKER_H

#include <stdint.h>
#include "af_types.h"

/**
 * af_broker_t — vtable-based broker interface (mirrors C++ IBroker).
 *
 * All mutable methods take af_broker_t* (non-const self).
 * Query-only methods take const af_broker_t*.
 *
 * get_positions / get_orders fill a caller-supplied array; the return
 * value is the number of entries written (capped at max_out).
 *
 * get_position returns 1 and fills *out on success, 0 if not found.
 *
 * Always call destroy() when done — it frees both the impl block
 * and the broker struct itself.
 */

typedef struct af_broker_s af_broker_t;

struct af_broker_s {
    /* ── Connection ── */
    af_error_t  (*connect)      (af_broker_t *self);
    void        (*disconnect)   (af_broker_t *self);
    int         (*is_connected) (const af_broker_t *self);
    const char *(*broker_name)  (const af_broker_t *self);

    /* ── Account ── */
    af_error_t  (*get_account)  (const af_broker_t *self, af_account_info_t *out);

    /* ── Market data ── */
    af_error_t  (*get_tick)        (af_broker_t *self, const char *symbol, af_tick_t *out);
    af_error_t  (*get_symbol_info) (const af_broker_t *self, const char *symbol, af_symbol_info_t *out);
    af_error_t  (*get_bars)        (af_broker_t *self, const char *symbol, af_timeframe_t tf,
                                    int count, af_bar_t *bars_out, int *filled);

    /* ── Trading ── */
    af_error_t  (*place_order)     (af_broker_t *self, const char *symbol, af_order_type_t type,
                                    double lots, double price, double sl, double tp,
                                    uint32_t magic, const char *comment, af_order_t *out);
    af_error_t  (*close_position)  (af_broker_t *self, uint64_t ticket, double lots);
    af_error_t  (*modify_position) (af_broker_t *self, uint64_t ticket, double sl, double tp);
    af_error_t  (*cancel_order)    (af_broker_t *self, uint64_t ticket);

    /* ── Queries ── */
    int         (*get_positions)   (const af_broker_t *self, af_position_t *out, int max_out);
    int         (*get_orders)      (const af_broker_t *self, af_order_t *out, int max_out);
    int         (*get_position)    (const af_broker_t *self, uint64_t ticket, af_position_t *out);

    /* ── Simulation helper ── */
    void        (*poll_sl_tp)  (af_broker_t *self);

    /* ── Lifecycle ── */
    void        (*destroy)     (af_broker_t *self);

    void *impl; /* private implementation state */
};

/* Factory — allocates and returns a fully wired paper broker. */
af_broker_t *af_make_paper_broker(double balance);

/* Factory — allocates and returns a fully wired MT5 broker stub.
 * Returns a vtable whose methods all yield AF_ERR_NOT_CONNECTED;
 * real MT5 connectivity in C requires linking the MetaTrader 5 DLL,
 * which is not part of this repo. Use the Python port for live MT5. */
af_broker_t *af_make_mt5_broker(void);

#endif /* AF_BROKER_H */
