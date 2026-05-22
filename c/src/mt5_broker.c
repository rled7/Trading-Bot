/**
 * AlgoForge — c/src/mt5_broker.c
 *
 * MT5 broker stub for the C port. Mirrors cpp/src/broker/mt5_broker.cpp:
 * every vtable method returns AF_ERR_NOT_CONNECTED (or 0/empty for the
 * int-returning query methods), and connect() emits a clear stderr
 * message directing operators to PAPER mode.
 *
 * Real MT5 connectivity in C would require linking against the
 * MetaTrader 5 DLL/headers, which are not part of this repo and only
 * exist on Windows. Use the Python MT5Broker for live trading; this
 * stub exists so C builds can select a broker by name without #ifdefs
 * leaking across the codebase.
 */

#include <stdio.h>
#include <stdlib.h>
#include "af_broker.h"

/* ── vtable methods ── */

static af_error_t mt5_connect(af_broker_t *self) {
    (void)self;
    fprintf(stderr,
        "[MT5] connect() called but MT5 connectivity is not implemented in this build.\n"
        "      The C MT5 broker is a stub — real connectivity requires linking\n"
        "      against the MetaTrader 5 DLL/API. Set TRADING_MODE=PAPER to use the\n"
        "      paper broker instead, or run the Python port for live MT5.\n");
    return AF_ERR_NOT_CONNECTED;
}

static void mt5_disconnect(af_broker_t *self) {
    (void)self;
}

static int mt5_is_connected(const af_broker_t *self) {
    (void)self;
    return 0;
}

static const char *mt5_broker_name(const af_broker_t *self) {
    (void)self;
    return "MT5 (stub)";
}

static af_error_t mt5_get_account(const af_broker_t *self, af_account_info_t *out) {
    (void)self; (void)out;
    return AF_ERR_NOT_CONNECTED;
}

static af_error_t mt5_get_tick(af_broker_t *self, const char *symbol, af_tick_t *out) {
    (void)self; (void)symbol; (void)out;
    return AF_ERR_NOT_CONNECTED;
}

static af_error_t mt5_get_symbol_info(const af_broker_t *self, const char *symbol,
                                      af_symbol_info_t *out) {
    (void)self; (void)symbol; (void)out;
    return AF_ERR_NOT_CONNECTED;
}

static af_error_t mt5_get_bars(af_broker_t *self, const char *symbol, af_timeframe_t tf,
                               int count, af_bar_t *bars_out, int *filled) {
    (void)self; (void)symbol; (void)tf; (void)count; (void)bars_out;
    if (filled) *filled = 0;
    return AF_ERR_NOT_CONNECTED;
}

static af_error_t mt5_place_order(af_broker_t *self, const char *symbol, af_order_type_t type,
                                  double lots, double price, double sl, double tp,
                                  uint32_t magic, const char *comment, af_order_t *out) {
    (void)self; (void)symbol; (void)type; (void)lots; (void)price;
    (void)sl; (void)tp; (void)magic; (void)comment; (void)out;
    return AF_ERR_NOT_CONNECTED;
}

static af_error_t mt5_close_position(af_broker_t *self, uint64_t ticket, double lots) {
    (void)self; (void)ticket; (void)lots;
    return AF_ERR_NOT_CONNECTED;
}

static af_error_t mt5_modify_position(af_broker_t *self, uint64_t ticket, double sl, double tp) {
    (void)self; (void)ticket; (void)sl; (void)tp;
    return AF_ERR_NOT_CONNECTED;
}

static af_error_t mt5_cancel_order(af_broker_t *self, uint64_t ticket) {
    (void)self; (void)ticket;
    return AF_ERR_NOT_CONNECTED;
}

static int mt5_get_positions(const af_broker_t *self, af_position_t *out, int max_out) {
    (void)self; (void)out; (void)max_out;
    return 0;
}

static int mt5_get_orders(const af_broker_t *self, af_order_t *out, int max_out) {
    (void)self; (void)out; (void)max_out;
    return 0;
}

static int mt5_get_position(const af_broker_t *self, uint64_t ticket, af_position_t *out) {
    (void)self; (void)ticket; (void)out;
    return 0;
}

static void mt5_poll_sl_tp(af_broker_t *self) {
    (void)self;
}

static void mt5_destroy(af_broker_t *self) {
    free(self);
}

/* ── Factory ── */
af_broker_t *af_make_mt5_broker(void) {
    af_broker_t *b = calloc(1, sizeof(af_broker_t));
    if (!b) return NULL;

    b->impl            = NULL; /* stub has no per-instance state */
    b->connect         = mt5_connect;
    b->disconnect      = mt5_disconnect;
    b->is_connected    = mt5_is_connected;
    b->broker_name     = mt5_broker_name;
    b->get_account     = mt5_get_account;
    b->get_tick        = mt5_get_tick;
    b->get_symbol_info = mt5_get_symbol_info;
    b->get_bars        = mt5_get_bars;
    b->place_order     = mt5_place_order;
    b->close_position  = mt5_close_position;
    b->modify_position = mt5_modify_position;
    b->cancel_order    = mt5_cancel_order;
    b->get_positions   = mt5_get_positions;
    b->get_orders      = mt5_get_orders;
    b->get_position    = mt5_get_position;
    b->poll_sl_tp      = mt5_poll_sl_tp;
    b->destroy         = mt5_destroy;

    return b;
}
