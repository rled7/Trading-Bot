/**
 * AlgoForge — c/src/paper_broker.c
 * Paper trading broker for C. Implements af_broker_t via function-pointer vtable.
 * Price simulation uses a seeded LCG random walk (no libc rand() dependency).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "af_broker.h"

#define AF_PAPER_MAX_POS   256
#define AF_PAPER_NUM_PAIRS  10
#define AF_PI 3.14159265358979323846

/* ── LCG random number generator ── */
static uint32_t lcg_next(uint32_t *s) {
    *s = (*s) * 1664525u + 1013904223u;
    return *s;
}
static double lcg_uniform(uint32_t *s) {
    return ((double)lcg_next(s) + 0.5) / 4294967296.0; /* (0,1) */
}
static double lcg_normal(uint32_t *s) {
    /* Box-Muller */
    double u1 = lcg_uniform(s);
    if (u1 < 1e-30) u1 = 1e-30;
    double u2 = lcg_uniform(s);
    return sqrt(-2.0 * log(u1)) * cos(2.0 * AF_PI * u2);
}

/* ── Per-symbol price simulator ── */
typedef struct {
    char     symbol[AF_MAX_SYMBOL_LEN];
    double   mid;
    double   vol;
    uint32_t rng;
} af_price_sim_t;

static double sim_next_mid(af_price_sim_t *s) {
    double step = s->vol / sqrt(24.0 * 60.0);
    s->mid *= (1.0 + step * lcg_normal(&s->rng));
    if (s->mid < 0.0001) s->mid = 0.0001;
    return s->mid;
}

/* ── Default spread ── */
static double default_spread(const char *sym) {
    if (!sym) return 0.0001;
    if (strstr(sym, "XAU") || strstr(sym, "XAG")) return 0.25;
    if (strstr(sym, "JPY")) return 0.030;
    return 0.00012;
}

/* ── Pair seed table ── */
static const struct { const char *sym; double start; double vol; uint32_t seed; }
PAIRS[AF_PAPER_NUM_PAIRS] = {
    {"EURUSD", 1.08500, 0.0080,  1},
    {"GBPUSD", 1.26500, 0.0100,  2},
    {"USDJPY", 149.500, 0.0070,  3},
    {"XAUUSD", 2050.00, 0.0120,  4},
    {"EURJPY", 162.000, 0.0090,  5},
    {"GBPJPY", 188.000, 0.0110,  6},
    {"AUDUSD", 0.65500, 0.0090,  7},
    {"USDCAD", 1.36000, 0.0075,  8},
    {"USDCHF", 0.90500, 0.0070,  9},
    {"EURGBP", 0.85800, 0.0060, 10},
};

/* ── Position slot ── */
typedef struct {
    int           used;
    af_position_t pos;
} af_slot_t;

/* ── Private broker state ── */
typedef struct {
    int            connected;
    double         balance;
    uint64_t       next_ticket;
    af_price_sim_t pairs[AF_PAPER_NUM_PAIRS];
    af_slot_t      slots[AF_PAPER_MAX_POS];
    uint32_t       rng_global;
} af_paper_impl_t;

/* ── Helper: find simulator by symbol ── */
static af_price_sim_t *find_sim(af_paper_impl_t *impl, const char *sym) {
    for (int i = 0; i < AF_PAPER_NUM_PAIRS; i++)
        if (strncmp(impl->pairs[i].symbol, sym, AF_MAX_SYMBOL_LEN) == 0)
            return &impl->pairs[i];
    return NULL;
}

/* ── Vtable implementations ── */

static af_error_t paper_connect(af_broker_t *self) {
    af_paper_impl_t *impl = (af_paper_impl_t *)self->impl;
    impl->connected = 1;
    printf("[PAPER] Connected | balance=%.2f\n", impl->balance);
    return AF_OK;
}

static void paper_disconnect(af_broker_t *self) {
    ((af_paper_impl_t *)self->impl)->connected = 0;
}

static int paper_is_connected(const af_broker_t *self) {
    return ((const af_paper_impl_t *)self->impl)->connected;
}

static const char *paper_broker_name(const af_broker_t *self) {
    (void)self;
    return "PaperSimulator";
}

static af_error_t paper_get_account(const af_broker_t *self, af_account_info_t *out) {
    const af_paper_impl_t *impl = (const af_paper_impl_t *)self->impl;
    out->balance     = impl->balance;
    out->equity      = impl->balance; /* floating P&L not tracked here */
    out->margin      = 0.0;
    out->free_margin = impl->balance;
    out->profit      = 0.0;
    out->leverage    = 100;
    out->login       = 99999999;
    snprintf(out->currency, sizeof(out->currency), "USD");
    return AF_OK;
}

static af_error_t paper_get_tick(af_broker_t *self, const char *symbol, af_tick_t *out) {
    if (!symbol) return AF_ERR_INVALID_SYMBOL;
    af_paper_impl_t *impl = (af_paper_impl_t *)self->impl;
    af_price_sim_t  *sim  = find_sim(impl, symbol);
    if (!sim) return AF_ERR_INVALID_SYMBOL;
    double mid = sim_next_mid(sim);
    double sp  = default_spread(symbol);
    out->bid       = mid - sp / 2.0;
    out->ask       = mid + sp / 2.0;
    out->volume    = 500.0 + lcg_uniform(&impl->rng_global) * 2000.0;
    out->timestamp = 0;
    strncpy(out->symbol, symbol, AF_MAX_SYMBOL_LEN - 1);
    out->symbol[AF_MAX_SYMBOL_LEN - 1] = '\0';
    return AF_OK;
}

static af_error_t paper_get_symbol_info(const af_broker_t *self, const char *symbol,
                                         af_symbol_info_t *out) {
    (void)self;
    if (!symbol) return AF_ERR_INVALID_SYMBOL;
    strncpy(out->name, symbol, AF_MAX_SYMBOL_LEN - 1);
    out->name[AF_MAX_SYMBOL_LEN - 1] = '\0';
    if (strstr(symbol, "XAU") || strstr(symbol, "XAG"))
        out->digits = 2;
    else if (strstr(symbol, "JPY"))
        out->digits = 3;
    else
        out->digits = 5;
    out->point         = pow(10.0, -(double)out->digits);
    out->contract_size = 100000.0;
    out->volume_min    = 0.01;
    out->volume_max    = 100.0;
    out->volume_step   = 0.01;
    return AF_OK;
}

static af_error_t paper_get_bars(af_broker_t *self, const char *symbol, af_timeframe_t tf,
                                   int count, af_bar_t *bars_out, int *filled) {
    if (!symbol || !bars_out || !filled || count <= 0) return AF_ERR_INVALID_PARAM;
    af_paper_impl_t *impl = (af_paper_impl_t *)self->impl;
    af_price_sim_t  *sim  = find_sim(impl, symbol);
    if (!sim) return AF_ERR_INVALID_SYMBOL;

    uint32_t rng = 42u ^ (uint32_t)tf;
    double vol_per_bar = 0.0080 / sqrt(86400.0 / (double)tf) / sqrt(252.0);
    double price = sim->mid;

    for (int i = 0; i < count; i++) {
        double o = price, h = o, l = o;
        for (int t = 0; t < 4; t++) {
            double p = o * (1.0 + vol_per_bar * lcg_normal(&rng));
            if (p > h) h = p;
            if (p < l) l = p;
        }
        double c = o * (1.0 + vol_per_bar * lcg_normal(&rng));
        if (c > h) h = c;
        if (c < l) l = c;
        bars_out[i].timestamp = 0;
        bars_out[i].open      = o;
        bars_out[i].high      = h;
        bars_out[i].low       = l;
        bars_out[i].close     = c;
        bars_out[i].volume    = 500.0 + lcg_uniform(&rng) * 4500.0;
        bars_out[i].spread    = default_spread(symbol);
        price = c;
    }
    *filled = count;
    return AF_OK;
}

static af_error_t paper_place_order(af_broker_t *self, const char *symbol,
                                     af_order_type_t type, double lots, double price,
                                     double sl, double tp, uint32_t magic,
                                     const char *comment, af_order_t *out) {
    if (!symbol)    return AF_ERR_INVALID_SYMBOL;
    if (lots < 0.01) return AF_ERR_INVALID_LOTS;
    af_paper_impl_t *impl = (af_paper_impl_t *)self->impl;

    int slot = -1;
    for (int i = 0; i < AF_PAPER_MAX_POS; i++)
        if (!impl->slots[i].used) { slot = i; break; }
    if (slot < 0) return AF_ERR_MAX_POSITIONS;

    af_price_sim_t *sim = find_sim(impl, symbol);
    if (!sim) return AF_ERR_INVALID_SYMBOL;

    double mid = sim_next_mid(sim);
    double sp  = default_spread(symbol);
    double bid = mid - sp / 2.0, ask = mid + sp / 2.0;

    double fill_p;
    switch (type) {
        case AF_ORDER_BUY:
        case AF_ORDER_BUY_STOP:    fill_p = ask;                             break;
        case AF_ORDER_SELL:
        case AF_ORDER_SELL_STOP:   fill_p = bid;                             break;
        case AF_ORDER_BUY_LIMIT:   fill_p = (price < ask) ? price : ask;    break;
        case AF_ORDER_SELL_LIMIT:  fill_p = (price > bid) ? price : bid;    break;
        default:                   fill_p = mid;                             break;
    }

    int is_long = (type == AF_ORDER_BUY || type == AF_ORDER_BUY_LIMIT || type == AF_ORDER_BUY_STOP);
    double commission = 7.0 * lots;

    af_position_t *pos = &impl->slots[slot].pos;
    memset(pos, 0, sizeof(*pos));
    pos->ticket        = impl->next_ticket++;
    strncpy(pos->symbol, symbol, AF_MAX_SYMBOL_LEN - 1);
    pos->side          = is_long ? AF_DIR_LONG : AF_DIR_SHORT;
    pos->lots          = lots;
    pos->open_price    = fill_p;
    pos->current_price = fill_p;
    pos->sl            = sl;
    pos->tp            = tp;
    pos->profit        = 0.0;
    pos->commission    = -commission;
    pos->magic         = magic;
    if (comment) strncpy(pos->comment, comment, sizeof(pos->comment) - 1);
    impl->slots[slot].used = 1;
    impl->balance -= commission;

    if (out) {
        memset(out, 0, sizeof(*out));
        out->ticket     = pos->ticket;
        out->fill_price = fill_p;
        strncpy(out->symbol, symbol, AF_MAX_SYMBOL_LEN - 1);
        out->type   = type;
        out->lots   = lots;
        out->sl     = sl;
        out->tp     = tp;
        out->magic  = magic;
    }

    printf("[PAPER] %s %.2f %s @ %.5f | SL=%.5f TP=%.5f | comm=-%.2f\n",
           is_long ? "BUY" : "SELL", lots, symbol, fill_p, sl, tp, commission);
    return AF_OK;
}

static af_error_t paper_close_position(af_broker_t *self, uint64_t ticket, double lots_to_close) {
    af_paper_impl_t *impl = (af_paper_impl_t *)self->impl;
    for (int i = 0; i < AF_PAPER_MAX_POS; i++) {
        if (!impl->slots[i].used || impl->slots[i].pos.ticket != ticket) continue;
        af_position_t  *pos = &impl->slots[i].pos;
        af_price_sim_t *sim = find_sim(impl, pos->symbol);
        double mid    = sim ? sim_next_mid(sim) : pos->open_price;
        double sp     = default_spread(pos->symbol);
        double exit_p = (pos->side == AF_DIR_LONG) ? mid - sp / 2.0 : mid + sp / 2.0;
        double close_lots = (lots_to_close > 0.0 && lots_to_close < pos->lots)
                            ? lots_to_close : pos->lots;
        double raw_pnl = (pos->side == AF_DIR_LONG)
                         ? (exit_p - pos->open_price) * close_lots * 100000.0
                         : (pos->open_price - exit_p) * close_lots * 100000.0;
        impl->balance += raw_pnl;
        printf("[PAPER] CLOSE %s %s %.2f @ %.5f | P&L=%.2f\n",
               pos->symbol, pos->side == AF_DIR_LONG ? "LONG" : "SHORT",
               close_lots, exit_p, raw_pnl);
        if (close_lots >= pos->lots)
            impl->slots[i].used = 0;
        else
            pos->lots -= close_lots;
        return AF_OK;
    }
    return AF_ERR_POSITION_NOT_FOUND;
}

static af_error_t paper_modify_position(af_broker_t *self, uint64_t ticket,
                                         double sl, double tp) {
    af_paper_impl_t *impl = (af_paper_impl_t *)self->impl;
    for (int i = 0; i < AF_PAPER_MAX_POS; i++) {
        if (impl->slots[i].used && impl->slots[i].pos.ticket == ticket) {
            impl->slots[i].pos.sl = sl;
            impl->slots[i].pos.tp = tp;
            return AF_OK;
        }
    }
    return AF_ERR_POSITION_NOT_FOUND;
}

static af_error_t paper_cancel_order(af_broker_t *self, uint64_t ticket) {
    (void)self; (void)ticket;
    return AF_OK; /* no pending order queue in simple paper broker */
}

static int paper_get_positions(const af_broker_t *self, af_position_t *out, int max_out) {
    const af_paper_impl_t *impl = (const af_paper_impl_t *)self->impl;
    int count = 0;
    for (int i = 0; i < AF_PAPER_MAX_POS && count < max_out; i++)
        if (impl->slots[i].used)
            out[count++] = impl->slots[i].pos;
    return count;
}

static int paper_get_orders(const af_broker_t *self, af_order_t *out, int max_out) {
    (void)self; (void)out; (void)max_out;
    return 0;
}

static int paper_get_position(const af_broker_t *self, uint64_t ticket, af_position_t *out) {
    const af_paper_impl_t *impl = (const af_paper_impl_t *)self->impl;
    for (int i = 0; i < AF_PAPER_MAX_POS; i++) {
        if (impl->slots[i].used && impl->slots[i].pos.ticket == ticket) {
            *out = impl->slots[i].pos;
            return 1;
        }
    }
    return 0;
}

static void paper_poll_sl_tp(af_broker_t *self) {
    af_paper_impl_t *impl = (af_paper_impl_t *)self->impl;
    uint64_t to_close[AF_PAPER_MAX_POS];
    int nclose = 0;

    for (int i = 0; i < AF_PAPER_MAX_POS; i++) {
        if (!impl->slots[i].used) continue;
        af_position_t  *pos = &impl->slots[i].pos;
        af_price_sim_t *sim = find_sim(impl, pos->symbol);
        if (!sim) continue;
        double mid = sim_next_mid(sim);
        double sp  = default_spread(pos->symbol);
        double bid = mid - sp / 2.0, ask = mid + sp / 2.0;
        pos->current_price = (pos->side == AF_DIR_LONG) ? bid : ask;

        int hit = 0;
        if (pos->sl > 0)
            hit = (pos->side == AF_DIR_LONG  && bid <= pos->sl) ||
                  (pos->side == AF_DIR_SHORT && ask >= pos->sl);
        if (!hit && pos->tp > 0)
            hit = (pos->side == AF_DIR_LONG  && bid >= pos->tp) ||
                  (pos->side == AF_DIR_SHORT && ask <= pos->tp);
        if (hit && nclose < AF_PAPER_MAX_POS)
            to_close[nclose++] = pos->ticket;
    }
    for (int i = 0; i < nclose; i++)
        paper_close_position(self, to_close[i], 0.0);
}

static void paper_destroy(af_broker_t *self) {
    free(self->impl);
    free(self);
}

/* ── Factory ── */
af_broker_t *af_make_paper_broker(double balance) {
    af_broker_t     *b    = calloc(1, sizeof(af_broker_t));
    af_paper_impl_t *impl = calloc(1, sizeof(af_paper_impl_t));
    if (!b || !impl) { free(b); free(impl); return NULL; }

    impl->balance     = balance;
    impl->next_ticket = 1000001;
    impl->rng_global  = 1337;

    for (int i = 0; i < AF_PAPER_NUM_PAIRS; i++) {
        strncpy(impl->pairs[i].symbol, PAIRS[i].sym, AF_MAX_SYMBOL_LEN - 1);
        impl->pairs[i].mid = PAIRS[i].start;
        impl->pairs[i].vol = PAIRS[i].vol;
        impl->pairs[i].rng = PAIRS[i].seed;
    }

    b->impl            = impl;
    b->connect         = paper_connect;
    b->disconnect      = paper_disconnect;
    b->is_connected    = paper_is_connected;
    b->broker_name     = paper_broker_name;
    b->get_account     = paper_get_account;
    b->get_tick        = paper_get_tick;
    b->get_symbol_info = paper_get_symbol_info;
    b->get_bars        = paper_get_bars;
    b->place_order     = paper_place_order;
    b->close_position  = paper_close_position;
    b->modify_position = paper_modify_position;
    b->cancel_order    = paper_cancel_order;
    b->get_positions   = paper_get_positions;
    b->get_orders      = paper_get_orders;
    b->get_position    = paper_get_position;
    b->poll_sl_tp      = paper_poll_sl_tp;
    b->destroy         = paper_destroy;

    return b;
}
