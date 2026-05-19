/**
 * AlgoForge — c/src/backtest_engine.c
 * Bar-by-bar backtesting engine + indicator/pattern engine wrappers.
 *
 * Ported from cpp/src/backtesting/backtest_engine.cpp.
 * Compile with: -std=c11 -Wall -Wextra -lm
 *
 * Dependencies: -lm  (math.h: sqrt, fabs, round)
 */

#include "../include/af_backtest.h"
#include "../include/af_algorithm.h"
#include "../include/af_indicators.h"
#include "../include/af_patterns.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* ── Internal helpers ─────────────────────────────────────────────────── */

static double bt_dabs(double x) { return x < 0.0 ? -x : x; }
static double bt_dmin(double a, double b) { return a < b ? a : b; }
static double bt_dmax(double a, double b) { return a > b ? a : b; }

/* Round x to nearest multiple of step. */
static double bt_round_to(double x, double step) {
    return round(x / step) * step;
}

/* ── Timing (CLOCK_MONOTONIC preferred, clock() fallback) ─────────────── */

#ifdef CLOCK_MONOTONIC
static double bt_now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#else
static double bt_now_sec(void) {
    return (double)clock() / (double)CLOCKS_PER_SEC;
}
#endif

/* ── Timeframe name ───────────────────────────────────────────────────── */

static const char *tf_name(af_timeframe_t tf) {
    switch (tf) {
        case AF_TF_S15: return "S15";
        case AF_TF_M1:  return "M1";
        case AF_TF_M5:  return "M5";
        case AF_TF_M15: return "M15";
        case AF_TF_M30: return "M30";
        case AF_TF_H1:  return "H1";
        case AF_TF_H4:  return "H4";
        case AF_TF_D1:  return "D1";
        case AF_TF_W1:  return "W1";
        default:        return "?";
    }
}

/* ── af_bt_config_default ─────────────────────────────────────────────── */

void af_bt_config_default(af_bt_config_t *cfg) {
    if (!cfg) return;
    cfg->initial_capital = 10000.0;
    cfg->risk_pct        = 0.01;
    cfg->commission_usd  = 7.0;
    cfg->slippage_pts    = 2;
    cfg->atr_sl_mult     = 1.5;
    cfg->rr_ratio        = 2.0;
    cfg->warmup_bars     = 200;
}

/* ── Metric helpers ───────────────────────────────────────────────────── */

double af_bt_net_profit(const af_bt_result_t *r) {
    double s = 0.0;
    for (int i = 0; i < r->trade_count; i++)
        s += af_bt_trade_net_pnl(&r->trades[i]);
    return s;
}

double af_bt_win_rate(const af_bt_result_t *r) {
    if (r->trade_count == 0) return 0.0;
    int wins = 0;
    for (int i = 0; i < r->trade_count; i++)
        if (af_bt_trade_is_winner(&r->trades[i])) wins++;
    return (double)wins / (double)r->trade_count;
}

double af_bt_profit_factor(const af_bt_result_t *r) {
    double gw = 0.0, gl = 0.0;
    for (int i = 0; i < r->trade_count; i++) {
        double pnl = af_bt_trade_net_pnl(&r->trades[i]);
        if (pnl > 0.0) gw += pnl;
        else            gl += bt_dabs(pnl);
    }
    return (gl > 1e-10) ? gw / gl : 999.0;
}

double af_bt_max_drawdown_pct(const af_bt_result_t *r) {
    if (r->equity_count == 0) return 0.0;
    double peak = r->equity_curve[0], mdd = 0.0;
    for (int i = 0; i < r->equity_count; i++) {
        double e = r->equity_curve[i];
        if (e > peak) peak = e;
        double dd = (peak > 1e-10) ? (peak - e) / peak : 0.0;
        if (dd > mdd) mdd = dd;
    }
    return mdd * 100.0;
}

double af_bt_sharpe_ratio(const af_bt_result_t *r) {
    if (r->equity_count < 2) return 0.0;

    /* Collect bar returns */
    double rets[AF_BT_MAX_EQUITY];
    int    nrets = 0;
    for (int i = 1; i < r->equity_count; i++) {
        double prev = r->equity_curve[i - 1];
        if (prev > 1e-10)
            rets[nrets++] = (r->equity_curve[i] - prev) / prev;
    }
    if (nrets == 0) return 0.0;

    double mean = 0.0;
    for (int i = 0; i < nrets; i++) mean += rets[i];
    mean /= (double)nrets;

    double var = 0.0;
    for (int i = 0; i < nrets; i++) {
        double d = rets[i] - mean;
        var += d * d;
    }
    double sd = sqrt(var / (double)nrets);
    return (sd > 1e-10) ? mean / sd * sqrt(252.0) : 0.0;
}

double af_bt_sortino_ratio(const af_bt_result_t *r) {
    if (r->equity_count < 2) return 0.0;

    double rets[AF_BT_MAX_EQUITY];
    int    nrets = 0;
    for (int i = 1; i < r->equity_count; i++) {
        double prev = r->equity_curve[i - 1];
        if (prev > 1e-10)
            rets[nrets++] = (r->equity_curve[i] - prev) / prev;
    }
    if (nrets == 0) return 0.0;

    double mean = 0.0;
    for (int i = 0; i < nrets; i++) mean += rets[i];
    mean /= (double)nrets;

    double neg_var = 0.0;
    int    nc = 0;
    for (int i = 0; i < nrets; i++) {
        if (rets[i] < 0.0) {
            double d = rets[i] - mean;
            neg_var += d * d;
            nc++;
        }
    }
    double neg_sd = (nc > 0) ? sqrt(neg_var / (double)nc) : 1e-10;
    return mean / neg_sd * sqrt(252.0);
}

double af_bt_calmar_ratio(const af_bt_result_t *r) {
    double mdd = af_bt_max_drawdown_pct(r);
    if (mdd < 1e-10) return 0.0;
    double return_pct = af_bt_net_profit(r) / r->initial_capital * 100.0;
    return return_pct / mdd;
}

double af_bt_avg_win(const af_bt_result_t *r) {
    double s = 0.0;
    int    c = 0;
    for (int i = 0; i < r->trade_count; i++) {
        if (af_bt_trade_is_winner(&r->trades[i])) {
            s += af_bt_trade_net_pnl(&r->trades[i]);
            c++;
        }
    }
    return c ? s / (double)c : 0.0;
}

double af_bt_avg_loss(const af_bt_result_t *r) {
    double s = 0.0;
    int    c = 0;
    for (int i = 0; i < r->trade_count; i++) {
        if (!af_bt_trade_is_winner(&r->trades[i])) {
            s += af_bt_trade_net_pnl(&r->trades[i]);
            c++;
        }
    }
    return c ? s / (double)c : 0.0;
}

double af_bt_expectancy(const af_bt_result_t *r) {
    double wr = af_bt_win_rate(r);
    return wr * af_bt_avg_win(r) + (1.0 - wr) * af_bt_avg_loss(r);
}

/* ── af_bt_print_summary ──────────────────────────────────────────────── */

void af_bt_print_summary(const af_bt_result_t *r) {
    double net    = af_bt_net_profit(r);
    double ret    = (r->initial_capital > 1e-10) ? net / r->initial_capital * 100.0 : 0.0;
    double final  = r->initial_capital + net;
    int    wins   = 0;
    for (int i = 0; i < r->trade_count; i++)
        if (af_bt_trade_is_winner(&r->trades[i])) wins++;

    printf("\n%s\n", "=======================================================");
    printf("  BACKTEST: %s/%s\n", r->symbol, r->timeframe);
    printf("%s\n", "=======================================================");
    printf("  Capital   : %.2f -> %.2f\n", r->initial_capital, final);
    printf("  Return    : %.4f%%\n", ret);
    printf("  Trades    : %d | Wins: %d\n", r->trade_count, wins);
    printf("  Win Rate  : %.2f%%\n", af_bt_win_rate(r) * 100.0);
    printf("  Prof.Fact : %.4f\n", af_bt_profit_factor(r));
    printf("  Max DD    : %.4f%%\n", af_bt_max_drawdown_pct(r));
    printf("  Sharpe    : %.4f\n", af_bt_sharpe_ratio(r));
    printf("  Sortino   : %.4f\n", af_bt_sortino_ratio(r));
    printf("  Expectancy: %.4f\n", af_bt_expectancy(r));
    printf("  Duration  : %.6fs\n", r->duration_sec);
    printf("%s\n", "=======================================================");
}

/* ── af_backtest_run ──────────────────────────────────────────────────── */

af_bt_result_t af_backtest_run(
    af_algorithm_t       *algo,
    const af_bt_config_t *cfg,
    const af_bar_t       *bars,
    size_t                count,
    const char           *symbol,
    af_timeframe_t        tf)
{
    double t0 = bt_now_sec();

    af_bt_result_t result;
    memset(&result, 0, sizeof(result));

    /* Populate header fields */
    snprintf(result.symbol,    sizeof(result.symbol),    "%s", symbol ? symbol : "");
    snprintf(result.timeframe, sizeof(result.timeframe), "%s", tf_name(tf));
    result.initial_capital = cfg->initial_capital;

    /* Validate inputs */
    if (!bars || (int)count < cfg->warmup_bars + 20 || !algo) {
        printf("[BT] Insufficient data: %zu bars (need %d)\n",
               count, cfg->warmup_bars + 20);
        result.duration_sec = bt_now_sec() - t0;
        return result;
    }

    printf("[BT] %s/%s | %zu bars | algo=%s\n",
           symbol, tf_name(tf), count,
           algo->name ? algo->name(algo) : "?");

    double capital  = cfg->initial_capital;
    int    in_trade = 0;

    af_bt_trade_t open_trade;
    memset(&open_trade, 0, sizeof(open_trade));

    /* Push initial equity point */
    if (result.equity_count < AF_BT_MAX_EQUITY)
        result.equity_curve[result.equity_count++] = capital;

    /* ── Bar-by-bar loop ──────────────────────────────────────────────── */
    for (size_t i = (size_t)cfg->warmup_bars; i < count - 1; i++) {
        const af_bar_t *next = &bars[i + 1];

        /* ── 1. Check open SL/TP on next bar's H/L ─────────────────── */
        if (in_trade) {
            double h = next->high;
            double l = next->low;

            int sl_hit = (open_trade.direction == AF_DIR_LONG  && l <= open_trade.sl) ||
                         (open_trade.direction == AF_DIR_SHORT && h >= open_trade.sl);
            int tp_hit = (open_trade.direction == AF_DIR_LONG  && h >= open_trade.tp) ||
                         (open_trade.direction == AF_DIR_SHORT && l <= open_trade.tp);

            if (sl_hit || tp_hit) {
                double exit_p = sl_hit ? open_trade.sl : open_trade.tp;
                double raw;
                if (open_trade.direction == AF_DIR_LONG)
                    raw = (exit_p - open_trade.entry_price) * open_trade.lots * 100000.0;
                else
                    raw = (open_trade.entry_price - exit_p) * open_trade.lots * 100000.0;

                open_trade.gross_pnl  = raw;
                open_trade.commission = cfg->commission_usd * open_trade.lots;
                open_trade.exit_price = exit_p;
                open_trade.exit_time  = (int64_t)(i + 1);
                open_trade.bars_held  = (int)((int64_t)(i + 1) - open_trade.entry_time);
                snprintf(open_trade.close_reason, sizeof(open_trade.close_reason),
                         "%s", sl_hit ? "SL" : "TP");

                capital += af_bt_trade_net_pnl(&open_trade);

                if (result.equity_count < AF_BT_MAX_EQUITY)
                    result.equity_curve[result.equity_count++] = capital;

                if (result.trade_count < AF_BT_MAX_TRADES)
                    result.trades[result.trade_count++] = open_trade;

                in_trade = 0;
                continue; /* skip to next bar */
            }
        }

        /* Push equity point for this bar (even if still in trade, no close) */
        if (result.equity_count < AF_BT_MAX_EQUITY)
            result.equity_curve[result.equity_count++] = capital;

        if (in_trade) continue; /* already in a trade, nothing to enter */

        /* ── 2. Run engines and evaluate ─────────────────────────────── */
        af_engine_result_t  ind = af_run_indicators(bars, i + 1, symbol, tf);
        AF_PatternResult    pat = af_pattern_engine_scan(bars, i + 1);

        af_algo_decision_t decision = af_algo_safe_evaluate(
            algo, symbol, tf, bars, i + 1, &ind, &pat);

        if (!af_algo_decision_is_actionable(&decision)) continue;

        /* ── 3. Simulate fill at next bar open ± slippage ─────────────── */
        double atr = (ind.atr_value > 0.0)
                     ? ind.atr_value
                     : (bars[i].high - bars[i].low);

        double entry  = next->open;
        double slip   = (double)cfg->slippage_pts * 0.00001;
        int    dir    = decision.direction;

        if (dir == AF_DIR_LONG)  entry += slip;
        else                      entry -= slip;

        double sl, tp;
        if (dir == AF_DIR_LONG) {
            sl = entry - atr * cfg->atr_sl_mult;
            tp = entry + atr * cfg->atr_sl_mult * cfg->rr_ratio;
        } else {
            sl = entry + atr * cfg->atr_sl_mult;
            tp = entry - atr * cfg->atr_sl_mult * cfg->rr_ratio;
        }

        /* ── 4. Position sizing ──────────────────────────────────────── */
        double stop_dist = bt_dabs(entry - sl);
        double risk_amt  = capital * cfg->risk_pct;
        double lots      = (stop_dist * 100000.0 > 1e-10)
                           ? risk_amt / (stop_dist * 100000.0)
                           : 0.01;
        lots = bt_dmin(bt_dmax(lots, 0.01), 10.0);
        lots = bt_round_to(lots, 0.01);

        /* ── 5. Record open trade ────────────────────────────────────── */
        memset(&open_trade, 0, sizeof(open_trade));
        open_trade.entry_time  = (int64_t)i;
        snprintf(open_trade.symbol, sizeof(open_trade.symbol), "%s", symbol);
        open_trade.direction   = dir;
        open_trade.lots        = lots;
        open_trade.entry_price = entry;
        open_trade.sl          = bt_round_to(sl, 0.00001);
        open_trade.tp          = bt_round_to(tp, 0.00001);
        snprintf(open_trade.algo, sizeof(open_trade.algo), "%s",
                 algo->name ? algo->name(algo) : "?");
        in_trade = 1;
    }

    /* ── Close any open trade at last bar ──────────────────────────────── */
    if (in_trade) {
        double exit_p = bars[count - 1].close;
        double raw;
        if (open_trade.direction == AF_DIR_LONG)
            raw = (exit_p - open_trade.entry_price) * open_trade.lots * 100000.0;
        else
            raw = (open_trade.entry_price - exit_p) * open_trade.lots * 100000.0;

        open_trade.gross_pnl  = raw;
        open_trade.commission = cfg->commission_usd * open_trade.lots;
        open_trade.exit_price = exit_p;
        open_trade.exit_time  = (int64_t)(count - 1);
        open_trade.bars_held  = (int)((int64_t)(count - 1) - open_trade.entry_time);
        snprintf(open_trade.close_reason, sizeof(open_trade.close_reason), "END");

        capital += af_bt_trade_net_pnl(&open_trade);

        if (result.equity_count < AF_BT_MAX_EQUITY)
            result.equity_curve[result.equity_count++] = capital;

        if (result.trade_count < AF_BT_MAX_TRADES)
            result.trades[result.trade_count++] = open_trade;
    }

    result.duration_sec = bt_now_sec() - t0;
    af_bt_print_summary(&result);
    return result;
}

/* ── af_run_indicators implementation ─────────────────────────────────── */
/*
 * Runs a representative subset of indicators and populates af_engine_result_t.
 * In a full implementation this mirrors IndicatorEngine::run() from C++.
 * The key scalars consumed by the backtest engine are:
 *   atr_value  — used for SL/TP sizing
 *   rsi_value  — default 50 (neutral)
 *   adx_value  — trend strength
 *   vwap_value — volume-weighted price anchor
 *
 * Each indicator result is stored in the indicators[] array with a name,
 * signal, and value.  Algorithms may look them up by name.
 */

/* Helper: extract close, high, low, volume arrays from bar array. */
static void extract_ohlcv(const af_bar_t *bars, size_t n,
                           double *c, double *h, double *l, double *v)
{
    for (size_t i = 0; i < n; i++) {
        c[i] = bars[i].close;
        h[i] = bars[i].high;
        l[i] = bars[i].low;
        v[i] = bars[i].volume;
    }
}

/* Stack-allocate per-bar arrays up to this limit; larger inputs are capped. */
#define AF_IND_BUF 4096

af_engine_result_t af_run_indicators(const af_bar_t *bars, size_t count,
                                      const char *symbol,
                                      af_timeframe_t tf)
{
    af_engine_result_t r;
    memset(&r, 0, sizeof(r));
    r.rsi_value = 50.0; /* sensible default */

    (void)symbol; /* reserved for future symbol-specific logic */
    (void)tf;

    if (!bars || count < 2) return r;

    size_t n = count < AF_IND_BUF ? count : AF_IND_BUF;
    const af_bar_t *src = bars + (count - n); /* use most recent n bars */

    /* Static buffers — this function is not re-entrant, which is acceptable
       for a single-threaded backtest engine. */
    static double c[AF_IND_BUF], h[AF_IND_BUF], l[AF_IND_BUF], v[AF_IND_BUF];
    static double out1[AF_IND_BUF], out2[AF_IND_BUF], out3[AF_IND_BUF];

    extract_ohlcv(src, n, c, h, l, v);

    int idx = 0; /* index into r.indicators[] */

    /* ── ATR(14) ──────────────────────────────────────────────────────── */
    if (n >= 15 && idx < AF_IND_MAX_INDICATORS) {
        if (af_atr(h, l, c, n, 14, out1) == AF_OK) {
            double val = out1[n - 1];
            int    valid = (val == val); /* NaN check */
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "ATR14");
            ir->valid  = valid;
            ir->signal = 0;
            ir->value  = valid ? val : 0.0;
            if (valid) r.atr_value = val;
        }
    }

    /* ── RSI(14) ──────────────────────────────────────────────────────── */
    if (n >= 15 && idx < AF_IND_MAX_INDICATORS) {
        if (af_rsi(c, n, 14, out1) == AF_OK) {
            double val   = out1[n - 1];
            int    valid = (val == val);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "RSI14");
            ir->valid  = valid;
            ir->value  = valid ? val : 50.0;
            if (valid) {
                r.rsi_value = val;
                ir->signal  = (val > 60.0) ? 1 : (val < 40.0) ? -1 : 0;
            }
        }
    }

    /* ── SMA(20) ──────────────────────────────────────────────────────── */
    if (n >= 20 && idx < AF_IND_MAX_INDICATORS) {
        if (af_sma(c, n, 20, out1) == AF_OK) {
            double val   = out1[n - 1];
            int    valid = (val == val);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "SMA20");
            ir->valid  = valid;
            ir->value  = valid ? val : 0.0;
            if (valid)
                ir->signal = (c[n - 1] > val) ? 1 : (c[n - 1] < val) ? -1 : 0;
        }
    }

    /* ── EMA(50) ──────────────────────────────────────────────────────── */
    if (n >= 50 && idx < AF_IND_MAX_INDICATORS) {
        if (af_ema(c, n, 50, out1) == AF_OK) {
            double val   = out1[n - 1];
            int    valid = (val == val);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "EMA50");
            ir->valid  = valid;
            ir->value  = valid ? val : 0.0;
            if (valid)
                ir->signal = (c[n - 1] > val) ? 1 : (c[n - 1] < val) ? -1 : 0;
        }
    }

    /* ── EMA(200) ─────────────────────────────────────────────────────── */
    if (n >= 200 && idx < AF_IND_MAX_INDICATORS) {
        if (af_ema(c, n, 200, out1) == AF_OK) {
            double val   = out1[n - 1];
            int    valid = (val == val);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "EMA200");
            ir->valid  = valid;
            ir->value  = valid ? val : 0.0;
            if (valid)
                ir->signal = (c[n - 1] > val) ? 1 : (c[n - 1] < val) ? -1 : 0;
        }
    }

    /* ── MACD(12,26,9) ────────────────────────────────────────────────── */
    if (n >= 35 && idx < AF_IND_MAX_INDICATORS) {
        if (af_macd(c, n, 12, 26, 9, out1, out2, out3) == AF_OK) {
            double hist  = out3[n - 1];
            int    valid = (hist == hist);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "MACD");
            ir->valid  = valid;
            ir->value  = valid ? out1[n - 1] : 0.0;
            ir->value2 = valid ? out2[n - 1] : 0.0;
            ir->value3 = valid ? hist : 0.0;
            if (valid)
                ir->signal = (hist > 0.0) ? 1 : (hist < 0.0) ? -1 : 0;
        }
    }

    /* ── Bollinger Bands(20, 2.0) ─────────────────────────────────────── */
    if (n >= 20 && idx < AF_IND_MAX_INDICATORS) {
        static double bb_upper[AF_IND_BUF], bb_lower[AF_IND_BUF];
        if (af_bollinger(c, n, 20, 2.0, bb_upper, out1, bb_lower) == AF_OK) {
            double upper = bb_upper[n - 1];
            double lower = bb_lower[n - 1];
            double mid   = out1[n - 1];
            int    valid = (mid == mid);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "BBAND20");
            ir->valid  = valid;
            ir->value  = valid ? upper : 0.0;
            ir->value2 = valid ? mid   : 0.0;
            ir->value3 = valid ? lower : 0.0;
            if (valid) {
                double price = c[n - 1];
                ir->signal = (price < lower) ? 1 : (price > upper) ? -1 : 0;
            }
        }
    }

    /* ── ADX(14) ──────────────────────────────────────────────────────── */
    if (n >= 30 && idx < AF_IND_MAX_INDICATORS) {
        static double adx_out[AF_IND_BUF], pdi[AF_IND_BUF], mdi[AF_IND_BUF];
        if (af_adx(h, l, c, n, 14, adx_out, pdi, mdi) == AF_OK) {
            double val   = adx_out[n - 1];
            int    valid = (val == val);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "ADX14");
            ir->valid  = valid;
            ir->value  = valid ? val           : 0.0;
            ir->value2 = valid ? pdi[n - 1]    : 0.0;
            ir->value3 = valid ? mdi[n - 1]    : 0.0;
            if (valid) {
                r.adx_value = val;
                ir->signal  = (pdi[n - 1] > mdi[n - 1]) ? 1 : -1;
            }
        }
    }

    /* ── Stochastic(5,3) ──────────────────────────────────────────────── */
    if (n >= 10 && idx < AF_IND_MAX_INDICATORS) {
        static double stoch_k[AF_IND_BUF], stoch_d[AF_IND_BUF];
        if (af_stochastic(h, l, c, n, 5, 3, stoch_k, stoch_d) == AF_OK) {
            double k     = stoch_k[n - 1];
            int    valid = (k == k);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "STOCH53");
            ir->valid  = valid;
            ir->value  = valid ? k : 50.0;
            ir->value2 = valid ? stoch_d[n - 1] : 50.0;
            if (valid)
                ir->signal = (k < 20.0) ? 1 : (k > 80.0) ? -1 : 0;
        }
    }

    /* ── VWAP ─────────────────────────────────────────────────────────── */
    if (n >= 2 && idx < AF_IND_MAX_INDICATORS) {
        if (af_vwap(h, l, c, v, n, out1) == AF_OK) {
            double val   = out1[n - 1];
            int    valid = (val == val);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "VWAP");
            ir->valid  = valid;
            ir->value  = valid ? val : 0.0;
            if (valid) {
                r.vwap_value = val;
                ir->signal   = (c[n - 1] > val) ? 1 : (c[n - 1] < val) ? -1 : 0;
            }
        }
    }

    /* ── WMA(20) ──────────────────────────────────────────────────────── */
    if (n >= 20 && idx < AF_IND_MAX_INDICATORS) {
        if (af_wma(c, n, 20, out1) == AF_OK) {
            double val   = out1[n - 1];
            int    valid = (val == val);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "WMA20");
            ir->valid  = valid;
            ir->value  = valid ? val : 0.0;
            if (valid)
                ir->signal = (c[n - 1] > val) ? 1 : (c[n - 1] < val) ? -1 : 0;
        }
    }

    /* ── CCI(20) ──────────────────────────────────────────────────────── */
    if (n >= 20 && idx < AF_IND_MAX_INDICATORS) {
        if (af_cci(h, l, c, n, 20, 0.015, out1) == AF_OK) {
            double val   = out1[n - 1];
            int    valid = (val == val);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "CCI20");
            ir->valid  = valid;
            ir->value  = valid ? val : 0.0;
            if (valid)
                ir->signal = (val > 100.0) ? 1 : (val < -100.0) ? -1 : 0;
        }
    }

    /* ── OBV ──────────────────────────────────────────────────────────── */
    if (n >= 2 && idx < AF_IND_MAX_INDICATORS) {
        if (af_obv(c, v, n, out1) == AF_OK) {
            double val   = out1[n - 1];
            double prev  = (n >= 2) ? out1[n - 2] : val;
            int    valid = (val == val);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "OBV");
            ir->valid  = valid;
            ir->value  = valid ? val : 0.0;
            if (valid)
                ir->signal = (val > prev) ? 1 : (val < prev) ? -1 : 0;
        }
    }

    /* ── Williams %R(14) ──────────────────────────────────────────────── */
    if (n >= 15 && idx < AF_IND_MAX_INDICATORS) {
        if (af_williams_r(h, l, c, n, 14, out1) == AF_OK) {
            double val   = out1[n - 1];
            int    valid = (val == val);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "WILLR14");
            ir->valid  = valid;
            ir->value  = valid ? val : -50.0;
            if (valid)
                ir->signal = (val < -80.0) ? 1 : (val > -20.0) ? -1 : 0;
        }
    }

    /* ── ROC(10) ──────────────────────────────────────────────────────── */
    if (n >= 11 && idx < AF_IND_MAX_INDICATORS) {
        if (af_roc(c, n, 10, out1) == AF_OK) {
            double val   = out1[n - 1];
            int    valid = (val == val);
            af_indicator_result_t *ir = &r.indicators[idx++];
            snprintf(ir->name, sizeof(ir->name), "ROC10");
            ir->valid  = valid;
            ir->value  = valid ? val : 0.0;
            if (valid)
                ir->signal = (val > 0.0) ? 1 : (val < 0.0) ? -1 : 0;
        }
    }

    /* ── Tally signal summary ─────────────────────────────────────────── */
    r.indicator_count = idx;
    for (int i = 0; i < idx; i++) {
        if (!r.indicators[i].valid) continue;
        if      (r.indicators[i].signal  > 0) r.bullish++;
        else if (r.indicators[i].signal  < 0) r.bearish++;
        else                                   r.neutral++;
    }

    return r;
}

/* ── af_pattern_engine_scan implementation ───────────────────────────── */
/*
 * Wraps af_scan_patterns() and maps af_pattern_match_t → AF_PatternMatch
 * inside AF_PatternResult.  Mirrors PatternEngine::scan().
 */

AF_PatternResult af_pattern_engine_scan(const af_bar_t *bars, size_t count)
{
    AF_PatternResult pr;
    memset(&pr, 0, sizeof(pr));

    if (!bars || count < 2) return pr;

    /* Temporary storage for raw scan output */
    static af_pattern_match_t raw[AF_MAX_PATTERN_MATCHES * 2];
    size_t found = af_scan_patterns(bars, count, raw,
                                    AF_MAX_PATTERN_MATCHES * 2);

    /* Copy into AF_PatternResult, mapping fields */
    int filled = 0;
    for (size_t i = 0; i < found && filled < AF_MAX_PATTERN_MATCHES; i++) {
        AF_PatternMatch *pm = &pr.matches[filled++];
        snprintf(pm->name, sizeof(pm->name), "%s", raw[i].name ? raw[i].name : "");
        pm->category  = AF_PAT_CANDLESTICK; /* default; no category in af_pattern_match_t */
        pm->direction = (raw[i].signal > 0) ? AF_PAT_DIR_BULLISH :
                        (raw[i].signal < 0) ? AF_PAT_DIR_BEARISH :
                                              AF_PAT_DIR_NEUTRAL;
        pm->confidence = 0.70;              /* default confidence */
        pm->bar_index  = raw[i].bar_index;

        if (raw[i].signal > 0) pr.bullish_count++;
        else if (raw[i].signal < 0) pr.bearish_count++;
    }
    pr.count = filled;

    return pr;
}
