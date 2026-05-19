/**
 * AlgoForge — c/src/algorithms.c
 * Concrete trading algorithms + algorithm registry.
 *
 * Implements:
 *   1. TrendFollower  — EMA crossover with ADX filter
 *   2. MeanReversion  — RSI + Bollinger Band
 *   3. BreakoutTrader — Donchian Channel with volume/ATR confirm
 *   4. SwingTrader    — Pullback to EMA21
 *   5. af_algo_registry_t + init / destroy / get / evaluate_all
 *
 * Compile with: -std=c11 -Wall -Wextra -lm
 */

#include "../include/af_algorithms.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal helpers ──────────────────────────────────────────────────── */

/**
 * find_indicator — linear search for a named indicator in the engine result.
 * Returns a pointer to the af_indicator_result_t if found and valid, else NULL.
 */
static const af_indicator_result_t *
find_indicator(const af_engine_result_t *ind, const char *name)
{
    if (!ind || !name) return NULL;
    for (int i = 0; i < ind->indicator_count; ++i) {
        if (strncmp(ind->indicators[i].name, name,
                    sizeof(ind->indicators[i].name)) == 0) {
            return ind->indicators[i].valid ? &ind->indicators[i] : NULL;
        }
    }
    return NULL;
}

/** Return a zeroed NONE decision for the given symbol. */
static af_algo_decision_t make_none(const char *symbol)
{
    af_algo_decision_t d;
    memset(&d, 0, sizeof(d));
    d.signal    = AF_ALGO_NONE;
    d.direction = AF_DIR_NONE;
    d.confidence = 0.0;
    if (symbol)
        snprintf(d.symbol, sizeof(d.symbol), "%s", symbol);
    return d;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 1. TrendFollower — EMA crossover + ADX + RSI filter
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    int      fast;
    int      slow;
    double   adx_min;
    uint32_t magic_num;
    char     name_buf[48];   /* "TrendFollower_EMA{fast}/{slow}" */
} trend_follower_impl_t;

static const char *tf_name(const af_algorithm_t *self)
{
    const trend_follower_impl_t *p =
        (const trend_follower_impl_t *)self->impl;
    return p->name_buf;
}

static const char *tf_description(const af_algorithm_t *self)
{
    (void)self;
    return "EMA crossover with ADX trend-strength filter and RSI overbought/oversold guard";
}

static uint32_t tf_magic(const af_algorithm_t *self)
{
    const trend_follower_impl_t *p =
        (const trend_follower_impl_t *)self->impl;
    return p->magic_num;
}

static af_algo_decision_t tf_evaluate(
    af_algorithm_t           *self,
    const char               *symbol,
    af_timeframe_t            tf,
    const af_bar_t           *bars,
    size_t                    count,
    const af_engine_result_t *ind,
    const AF_PatternResult   *pat)
{
    (void)tf;
    (void)pat;

    trend_follower_impl_t *p = (trend_follower_impl_t *)self->impl;
    af_algo_decision_t none = make_none(symbol);

    if (!bars || count == 0 || !ind) return none;

    /* Build indicator name strings */
    char ema_fast_name[32], ema_slow_name[32];
    snprintf(ema_fast_name, sizeof(ema_fast_name), "EMA%d", p->fast);
    snprintf(ema_slow_name, sizeof(ema_slow_name), "EMA%d", p->slow);

    /* Lookup required indicators */
    const af_indicator_result_t *ir_fast = find_indicator(ind, ema_fast_name);
    const af_indicator_result_t *ir_slow = find_indicator(ind, ema_slow_name);
    const af_indicator_result_t *ir_e200 = find_indicator(ind, "EMA200");
    const af_indicator_result_t *ir_adx  = find_indicator(ind, "ADX_14");
    const af_indicator_result_t *ir_rsi  = find_indicator(ind, "RSI_14");

    /* Defensive: require at minimum fast, slow, ADX */
    if (!ir_fast || !ir_slow || !ir_adx) return none;

    double ema_fast = ir_fast->value;
    double ema_slow = ir_slow->value;
    double adx      = ir_adx->value;
    /* value2=+DI, value3=-DI for ADX */
    double pdi      = ir_adx->value2;
    double mdi      = ir_adx->value3;
    double rsi      = ir_rsi ? ir_rsi->value : 50.0;
    double ema200   = ir_e200 ? ir_e200->value : 0.0;
    double price    = bars[count - 1].close;

    /* ADX filter */
    if (adx < p->adx_min) return none;

    double conf = fmin(0.90, 0.60 + adx / 100.0 * 0.30);
    double atr_val = ind->atr_value;

    af_algo_decision_t d = make_none(symbol);
    d.atr = atr_val;

    /* Bullish: fast > slow, price > EMA200, +DI > -DI, RSI < 75 */
    if (ema_fast > ema_slow &&
        (ema200 <= 0.0 || price > ema200) &&
        pdi > mdi &&
        rsi < 75.0)
    {
        d.signal     = AF_ALGO_BUY;
        d.direction  = AF_DIR_LONG;
        d.confidence = conf;
        snprintf(d.reason, sizeof(d.reason),
                 "EMA cross up ADX=%.1f", adx);
        return d;
    }

    /* Bearish: fast < slow, price < EMA200, -DI > +DI, RSI > 25 */
    if (ema_fast < ema_slow &&
        (ema200 <= 0.0 || price < ema200) &&
        mdi > pdi &&
        rsi > 25.0)
    {
        d.signal     = AF_ALGO_SELL;
        d.direction  = AF_DIR_SHORT;
        d.confidence = conf;
        snprintf(d.reason, sizeof(d.reason),
                 "EMA cross down ADX=%.1f", adx);
        return d;
    }

    return none;
}

static void tf_destroy(af_algorithm_t *self)
{
    if (!self) return;
    free(self->impl);
    free(self);
}

af_algorithm_t *af_make_trend_follower(int fast, int slow,
                                        double adx_min, uint32_t magic)
{
    af_algorithm_t *a = (af_algorithm_t *)malloc(sizeof(af_algorithm_t));
    if (!a) return NULL;

    trend_follower_impl_t *p =
        (trend_follower_impl_t *)malloc(sizeof(trend_follower_impl_t));
    if (!p) { free(a); return NULL; }

    p->fast      = fast;
    p->slow      = slow;
    p->adx_min   = adx_min;
    p->magic_num = magic;
    snprintf(p->name_buf, sizeof(p->name_buf),
             "TrendFollower_EMA%d/%d", fast, slow);

    a->name        = tf_name;
    a->description = tf_description;
    a->magic       = tf_magic;
    a->evaluate    = tf_evaluate;
    a->enabled     = 1;
    a->impl        = p;

    /* Store destroy in impl wrapper — access via registry */
    /* We'll use a dedicated function pointer; store it separately.
     * Since af_algorithm_t has no destroy slot, we embed a compatible wrapper.
     * The registry uses af_algo_registry_destroy which calls algo->impl-based
     * destroy.  We store destroy as a compatible static. */

    /* Actually we need a destroy callback — add it to the impl struct via
     * a small trampoline.  We cast the unused `enabled` field trick is wrong.
     * Instead we keep a separate internal destroy table keyed by algo pointer.
     *
     * Cleanest solution: store destroy ptr as the first member of all impl
     * structs, and registry casts to retrieve it.
     *
     * Redesign: see the destroy_fn mechanism in registry below.
     * For now store NULL; registry will call a type-specific free.
     * The registry destroy will use a generic: free(impl); free(algo).
     */
    (void)tf_destroy; /* suppress unused warning — used by registry */
    return a;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 2. MeanReversion — RSI + Bollinger Bands
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    int    rsi_os;   /* oversold threshold (default 30) */
    int    rsi_ob;   /* overbought threshold (default 70) */
    int    adx_max;  /* ADX ceiling — trending market filter (default 25) */
} mean_rev_impl_t;

static const char *mr_name(const af_algorithm_t *self)
{
    (void)self;
    return "MeanReversion_RSI_BB";
}

static const char *mr_description(const af_algorithm_t *self)
{
    (void)self;
    return "RSI oversold/overbought mean-reversion with Bollinger Band confirmation and ADX range filter";
}

static uint32_t mr_magic(const af_algorithm_t *self)
{
    (void)self;
    return 1002;
}

static af_algo_decision_t mr_evaluate(
    af_algorithm_t           *self,
    const char               *symbol,
    af_timeframe_t            tf,
    const af_bar_t           *bars,
    size_t                    count,
    const af_engine_result_t *ind,
    const AF_PatternResult   *pat)
{
    (void)tf;
    (void)pat;

    mean_rev_impl_t *p = (mean_rev_impl_t *)self->impl;
    af_algo_decision_t none = make_none(symbol);

    if (!bars || count == 0 || !ind) return none;

    const af_indicator_result_t *ir_rsi = find_indicator(ind, "RSI_14");
    const af_indicator_result_t *ir_bb  = find_indicator(ind, "BB_20_2");
    const af_indicator_result_t *ir_adx = find_indicator(ind, "ADX_14");

    if (!ir_rsi || !ir_bb) return none;

    double rsi   = ir_rsi->value;
    double adx   = ir_adx ? ir_adx->value : 0.0;
    /* BB: value=middle, value2=upper, value3=lower */
    double upper = ir_bb->value2;
    double lower = ir_bb->value3;
    double price = bars[count - 1].close;

    /* ADX ceiling: only trade in ranging markets */
    if (ir_adx && adx > (double)p->adx_max) return none;

    /* BB squeeze check — look for "squeeze" in the extra signal field.
     * The indicator engine may encode squeeze as signal==0 with a near-zero
     * bandwidth.  We guard with a bandwidth threshold instead. */
    double bandwidth = (upper - lower);
    if (bandwidth <= 0.0) return none;  /* degenerate / squeeze */

    af_algo_decision_t d = make_none(symbol);
    d.atr = ind->atr_value;

    /* Oversold: RSI at or below OS threshold AND price at/below lower band */
    if (rsi <= (double)p->rsi_os && price <= lower * 1.002) {
        double conf = fmin(0.85,
            0.55 + (double)(p->rsi_os - (int)rsi) / 30.0 * 0.30);
        d.signal     = AF_ALGO_BUY;
        d.direction  = AF_DIR_LONG;
        d.confidence = conf;
        snprintf(d.reason, sizeof(d.reason),
                 "Oversold RSI=%.1f at lower BB", rsi);
        return d;
    }

    /* Overbought: RSI at or above OB threshold AND price at/above upper band */
    if (rsi >= (double)p->rsi_ob && price >= upper * 0.998) {
        double conf = fmin(0.85,
            0.55 + ((int)rsi - p->rsi_ob) / 30.0 * 0.30);
        d.signal     = AF_ALGO_SELL;
        d.direction  = AF_DIR_SHORT;
        d.confidence = conf;
        snprintf(d.reason, sizeof(d.reason),
                 "Overbought RSI=%.1f at upper BB", rsi);
        return d;
    }

    return none;
}

af_algorithm_t *af_make_mean_reversion(void)
{
    af_algorithm_t *a = (af_algorithm_t *)malloc(sizeof(af_algorithm_t));
    if (!a) return NULL;

    mean_rev_impl_t *p = (mean_rev_impl_t *)malloc(sizeof(mean_rev_impl_t));
    if (!p) { free(a); return NULL; }

    p->rsi_os  = 30;
    p->rsi_ob  = 70;
    p->adx_max = 25;

    a->name        = mr_name;
    a->description = mr_description;
    a->magic       = mr_magic;
    a->evaluate    = mr_evaluate;
    a->enabled     = 1;
    a->impl        = p;
    return a;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 3. BreakoutTrader — Donchian Channel + volume surge + ATR expansion
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    int    dc_period;  /* Donchian channel period (default 20) */
    double vol_mult;   /* Volume surge multiplier (default 1.5) */
} breakout_impl_t;

static const char *bt_name(const af_algorithm_t *self)
{
    (void)self;
    return "Breakout_Donchian";
}

static const char *bt_description(const af_algorithm_t *self)
{
    (void)self;
    return "Donchian Channel breakout with volume surge and ATR expansion confirmation";
}

static uint32_t bt_magic(const af_algorithm_t *self)
{
    (void)self;
    return 1003;
}

static af_algo_decision_t bt_evaluate(
    af_algorithm_t           *self,
    const char               *symbol,
    af_timeframe_t            tf,
    const af_bar_t           *bars,
    size_t                    count,
    const af_engine_result_t *ind,
    const AF_PatternResult   *pat)
{
    (void)tf;
    (void)pat;

    breakout_impl_t *p = (breakout_impl_t *)self->impl;
    af_algo_decision_t none = make_none(symbol);

    if (!bars || (int)count < p->dc_period + 1 || !ind) return none;

    const af_indicator_result_t *ir_dc   = find_indicator(ind, "DC_20");
    const af_indicator_result_t *ir_macd = find_indicator(ind, "MACD_12_26_9");

    if (!ir_dc) return none;

    /* DC: value=middle, value2=upper, value3=lower */
    double dc_upper    = ir_dc->value2;
    double dc_lower    = ir_dc->value3;
    double macd_hist   = ir_macd ? ir_macd->value3 : 0.0;
    double price       = bars[count - 1].close;
    double cur_vol     = bars[count - 1].volume;

    /* Compute average volume and average range over last dc_period bars */
    size_t lookback = (size_t)p->dc_period;
    if (count < lookback + 1) return none;

    double avg_vol   = 0.0;
    double range_avg = 0.0;
    for (size_t i = count - 1 - lookback; i < count - 1; ++i) {
        avg_vol   += bars[i].volume;
        range_avg += (bars[i].high - bars[i].low);
    }
    avg_vol   /= (double)lookback;
    range_avg /= (double)lookback;

    double cur_range  = bars[count - 1].high - bars[count - 1].low;
    int vol_surge     = (avg_vol > 0.0) && (cur_vol > avg_vol * p->vol_mult);
    int atr_expand    = (range_avg > 0.0) && (cur_range > range_avg * 1.1);

    double conf = fmin(0.88,
        0.60 + (vol_surge ? 0.15 : 0.0) + (atr_expand ? 0.10 : 0.0));

    double vol_pct = (avg_vol > 0.0)
        ? ((cur_vol - avg_vol) / avg_vol * 100.0)
        : 0.0;

    af_algo_decision_t d = make_none(symbol);
    d.atr = ind->atr_value;

    /* BUY: price breaks above Donchian upper with volume and ATR confirmation */
    if (price >= dc_upper && vol_surge && atr_expand && macd_hist > 0.0) {
        d.signal     = AF_ALGO_BUY;
        d.direction  = AF_DIR_LONG;
        d.confidence = conf;
        snprintf(d.reason, sizeof(d.reason),
                 "DC breakout up vol=%.0f%%", vol_pct);
        return d;
    }

    /* SELL: price breaks below Donchian lower with volume and ATR confirmation */
    if (price <= dc_lower && vol_surge && atr_expand && macd_hist < 0.0) {
        d.signal     = AF_ALGO_SELL;
        d.direction  = AF_DIR_SHORT;
        d.confidence = conf;
        snprintf(d.reason, sizeof(d.reason),
                 "DC breakout down vol=%.0f%%", vol_pct);
        return d;
    }

    return none;
}

af_algorithm_t *af_make_breakout_trader(int dc_period, double vol_mult)
{
    af_algorithm_t *a = (af_algorithm_t *)malloc(sizeof(af_algorithm_t));
    if (!a) return NULL;

    breakout_impl_t *p = (breakout_impl_t *)malloc(sizeof(breakout_impl_t));
    if (!p) { free(a); return NULL; }

    p->dc_period = dc_period;
    p->vol_mult  = vol_mult;

    a->name        = bt_name;
    a->description = bt_description;
    a->magic       = bt_magic;
    a->evaluate    = bt_evaluate;
    a->enabled     = 1;
    a->impl        = p;
    return a;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 4. SwingTrader — Pullback to EMA21 in trend
 * ═══════════════════════════════════════════════════════════════════════════ */

/* No configurable params — all constants embedded in evaluate */
typedef struct {
    int dummy; /* C requires at least one member */
} swing_impl_t;

static const char *sw_name(const af_algorithm_t *self)
{
    (void)self;
    return "SwingTrader_PullbackEMA";
}

static const char *sw_description(const af_algorithm_t *self)
{
    (void)self;
    return "Pullback-to-EMA21 swing trading strategy; fires only on H4/D1 timeframes";
}

static uint32_t sw_magic(const af_algorithm_t *self)
{
    (void)self;
    return 1004;
}

static af_algo_decision_t sw_evaluate(
    af_algorithm_t           *self,
    const char               *symbol,
    af_timeframe_t            tf,
    const af_bar_t           *bars,
    size_t                    count,
    const af_engine_result_t *ind,
    const AF_PatternResult   *pat)
{
    (void)self;

    af_algo_decision_t none = make_none(symbol);

    /* Only H4 or D1 */
    if (tf != AF_TF_H4 && tf != AF_TF_D1) return none;
    if (!bars || count == 0 || !ind || !pat) return none;

    const af_indicator_result_t *ir_e21  = find_indicator(ind, "EMA21");
    const af_indicator_result_t *ir_e50  = find_indicator(ind, "EMA50");
    const af_indicator_result_t *ir_e200 = find_indicator(ind, "EMA200");
    const af_indicator_result_t *ir_rsi  = find_indicator(ind, "RSI_14");

    if (!ir_e21 || !ir_e50 || !ir_e200) return none;

    double ema21  = ir_e21->value;
    double ema50  = ir_e50->value;
    double ema200 = ir_e200->value;
    double rsi    = ir_rsi ? ir_rsi->value : 50.0;
    double price  = bars[count - 1].close;

    /* at_ema21: price within 0.3% of EMA21 */
    int at_ema21 = (ema21 > 0.0) &&
                   (fabs(price - ema21) / ema21 < 0.003);

    /* Trend alignment */
    int bull_trend = (ema21 > ema50) && (ema50 > ema200);
    int bear_trend = (ema21 < ema50) && (ema50 < ema200);

    af_algo_decision_t d = make_none(symbol);
    d.atr = ind->atr_value;

    /* BUY: bullish trend, pullback to EMA21, RSI in neutral-low zone,
     *      at least one bullish candlestick pattern */
    if (bull_trend && at_ema21 && rsi > 38.0 && rsi < 58.0 &&
        pat->bullish_count > 0)
    {
        d.signal     = AF_ALGO_BUY;
        d.direction  = AF_DIR_LONG;
        d.confidence = 0.75;
        snprintf(d.reason, sizeof(d.reason),
                 "Pullback to EMA21 in uptrend");
        return d;
    }

    /* SELL: bearish trend, rally to EMA21, RSI in neutral-high zone,
     *       at least one bearish candlestick pattern */
    if (bear_trend && at_ema21 && rsi > 42.0 && rsi < 62.0 &&
        pat->bearish_count > 0)
    {
        d.signal     = AF_ALGO_SELL;
        d.direction  = AF_DIR_SHORT;
        d.confidence = 0.75;
        snprintf(d.reason, sizeof(d.reason),
                 "Rally to EMA21 in downtrend");
        return d;
    }

    return none;
}

af_algorithm_t *af_make_swing_trader(void)
{
    af_algorithm_t *a = (af_algorithm_t *)malloc(sizeof(af_algorithm_t));
    if (!a) return NULL;

    swing_impl_t *p = (swing_impl_t *)malloc(sizeof(swing_impl_t));
    if (!p) { free(a); return NULL; }

    p->dummy = 0;

    a->name        = sw_name;
    a->description = sw_description;
    a->magic       = sw_magic;
    a->evaluate    = sw_evaluate;
    a->enabled     = 1;
    a->impl        = p;
    return a;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 5. Algorithm Registry
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Generic destroy: free impl and the algo struct itself.
 * Works for any algo whose impl is a single flat malloc'd block.
 */
static void generic_algo_destroy(af_algorithm_t *algo)
{
    if (!algo) return;
    free(algo->impl);
    free(algo);
}

void af_algo_registry_init(af_algo_registry_t *r)
{
    if (!r) return;
    memset(r, 0, sizeof(*r));

    int n = 0;

    /* [0] TrendFollower — primary (21/50, ADX>=22) */
    if (n < AF_ALGO_MAX_ALGOS)
        r->algos[n++] = af_make_trend_follower(21, 50, 22.0, 1001);

    /* [1] TrendFollower — scalp variant (9/21, ADX>=20) */
    if (n < AF_ALGO_MAX_ALGOS)
        r->algos[n++] = af_make_trend_follower(9, 21, 20.0, 1005);

    /* [2] MeanReversion */
    if (n < AF_ALGO_MAX_ALGOS)
        r->algos[n++] = af_make_mean_reversion();

    /* [3] BreakoutTrader */
    if (n < AF_ALGO_MAX_ALGOS)
        r->algos[n++] = af_make_breakout_trader(20, 1.5);

    /* [4] SwingTrader */
    if (n < AF_ALGO_MAX_ALGOS)
        r->algos[n++] = af_make_swing_trader();

    r->count = n;
}

void af_algo_registry_destroy(af_algo_registry_t *r)
{
    if (!r) return;
    for (int i = 0; i < r->count; ++i) {
        if (r->algos[i])
            generic_algo_destroy(r->algos[i]);
        r->algos[i] = NULL;
    }
    r->count = 0;
}

af_algorithm_t *af_algo_registry_get(const af_algo_registry_t *r,
                                      const char *name)
{
    if (!r || !name) return NULL;
    for (int i = 0; i < r->count; ++i) {
        if (r->algos[i] && r->algos[i]->name) {
            if (strcmp(r->algos[i]->name(r->algos[i]), name) == 0)
                return r->algos[i];
        }
    }
    return NULL;
}

int af_algo_registry_evaluate_all(const af_algo_registry_t   *r,
                                   const char                 *symbol,
                                   af_timeframe_t              tf,
                                   const af_bar_t             *bars,
                                   size_t                      count,
                                   const af_engine_result_t   *ind,
                                   const AF_PatternResult     *pat,
                                   af_algo_decision_t         *out,
                                   int                         max_out)
{
    if (!r || !out || max_out <= 0) return 0;

    int written = 0;
    for (int i = 0; i < r->count && written < max_out; ++i) {
        if (!r->algos[i]) continue;

        af_algo_decision_t d = af_algo_safe_evaluate(
            r->algos[i], symbol, tf, bars, count, ind, pat);

        if (af_algo_decision_is_actionable(&d)) {
            out[written++] = d;
        }
    }
    return written;
}
