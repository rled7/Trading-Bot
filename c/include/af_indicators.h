#ifndef AF_INDICATORS_H
#define AF_INDICATORS_H

#include <stddef.h>
#include "af_types.h"

/* All indicators write to caller-allocated `out` of length `n`.
   Indices where the indicator is not yet defined are set to NaN.
   Returns AF_OK on success, AF_ERR_INVALID_PARAM for bad args,
   AF_ERR_IO if there is not enough data to produce a single value. */

af_error_t af_sma(const double *src, size_t n, size_t period, double *out);
af_error_t af_ema(const double *src, size_t n, size_t period, double *out);
af_error_t af_rsi(const double *src, size_t n, size_t period, double *out);
af_error_t af_atr(const double *high, const double *low, const double *close,
                  size_t n, size_t period, double *out);

/* macd_line = EMA(fast) - EMA(slow); signal_line = EMA(macd_line, signal);
   histogram = macd_line - signal_line. NaN-propagated. */
af_error_t af_macd(const double *src, size_t n,
                   size_t fast, size_t slow, size_t signal,
                   double *macd_line, double *signal_line, double *histogram);

/* Bollinger Bands: middle = SMA(period); upper/lower = middle ± mult*stddev
   (population std: variance = sum((x-middle)^2) / period). */
af_error_t af_bollinger(const double *src, size_t n, size_t period, double mult,
                        double *upper, double *middle, double *lower);

/* Stochastic: raw %K = 100*(close - lowest_low) / (highest_high - lowest_low);
   k_out = SMA(rawK, d_period); d_out = SMA(k_out, d_period). */
af_error_t af_stochastic(const double *high, const double *low, const double *close,
                         size_t n, size_t k_period, size_t d_period,
                         double *k_out, double *d_out);

/* On-Balance Volume: out[0]=0; out[i]=out[i-1]+vol[i] if close up,
   -vol[i] if close down, 0 if flat. */
af_error_t af_obv(const double *close, const double *volume, size_t n, double *out);

/* ADX with Wilder smoothing (alpha=1/period).
   Requires n >= 2*period+1 for first valid output. */
af_error_t af_adx(const double *high, const double *low, const double *close,
                  size_t n, size_t period,
                  double *adx_out, double *plus_di, double *minus_di);

/* WMA: weighted moving average; weight of most-recent bar = period,
   next = period-1, ..., oldest = 1. Sum of weights = period*(period+1)/2. */
af_error_t af_wma(const double *src, size_t n, size_t period, double *out);

/* CCI: (TP - SMA(TP,period)) / (constant * mean_abs_deviation(TP,period))
   where TP = (high + low + close) / 3. */
af_error_t af_cci(const double *high, const double *low, const double *close,
                  size_t n, size_t period, double constant, double *out);

/* Williams %R = -100 * (highest_high - close) / (highest_high - lowest_low)
   over a rolling window of `period` bars. */
af_error_t af_williams_r(const double *high, const double *low, const double *close,
                          size_t n, size_t period, double *out);

/* ROC: rate of change = (close[i] - close[i-period]) / close[i-period] * 100. */
af_error_t af_roc(const double *src, size_t n, size_t period, double *out);

/* MFI: money flow index.  Positive/negative money flow over `period` bars,
   then 100 - 100/(1 + pos_flow/neg_flow).  Returns AF_ERR_INVALID_PARAM if
   any pointer is NULL; returns AF_ERR_IO if n <= period. */
af_error_t af_mfi(const double *high, const double *low, const double *close,
                  const double *volume, size_t n, size_t period, double *out);

/* VWAP: cumulative (TP*volume) / cumulative volume from bar 0.
   No session reset.  Returns AF_ERR_INVALID_PARAM if any pointer is NULL. */
af_error_t af_vwap(const double *high, const double *low, const double *close,
                   const double *volume, size_t n, double *out);

/* Keltner Channels:
     middle = EMA(close, ema_period)
     upper  = middle + mult * ATR(ema_period=atr_period)
     lower  = middle - mult * ATR */
af_error_t af_keltner(const double *high, const double *low, const double *close,
                      size_t n, size_t ema_period, size_t atr_period, double mult,
                      double *upper, double *middle, double *lower);

/* HMA: Hull Moving Average.
   half = (int)sqrt(period/2), clamped to >=1;
   sq   = (int)sqrt(period),   clamped to >=2.
   raw[i] = 2*WMA(half)[i] - WMA(period)[i]; out = WMA(raw, sq).
   Matches cpp/ af_hma exactly. */
af_error_t af_hma(const double *src, size_t n, size_t period, double *out);

/* DEMA: Double Exponential Moving Average.
   out[i] = 2*EMA1[i] - EMA2[i]  where EMA2 = EMA(EMA1, period).
   Matches cpp/ af_dema exactly. */
af_error_t af_dema(const double *src, size_t n, size_t period, double *out);

/* TEMA: Triple Exponential Moving Average.
   out[i] = 3*EMA1[i] - 3*EMA2[i] + EMA3[i].
   Matches cpp/ af_tema exactly. */
af_error_t af_tema(const double *src, size_t n, size_t period, double *out);

/* TRIX: triple-smoothed EMA rate-of-change * 100.
   out[i] = (EMA3[i] - EMA3[i-1]) / EMA3[i-1] * 100.
   Matches cpp/ af_trix (trix output only; signal line not exposed here). */
af_error_t af_trix(const double *src, size_t n, size_t period, double *out);

/* ── New indicators (16) ────────────────────────────────────────────────── */

/* MOMENTUM: out[i] = src[i] - src[i-period]. */
af_error_t af_momentum(const double *src, size_t n, size_t period, double *out);

/* TRUE RANGE: out[0] = high[0]-low[0]; out[i] = max(H-L, |H-prevC|, |L-prevC|). */
af_error_t af_true_range(const double *high, const double *low, const double *close,
                          size_t n, double *out);

/* WILDER EMA: seed = SMA(period), then alpha = 1/period. */
af_error_t af_wilder_ema(const double *src, size_t n, size_t period, double *out);

/* VWMA: volume-weighted moving average over rolling `period` bars. */
af_error_t af_vwma(const double *src, const double *volume, size_t n,
                   size_t period, double *out);

/* HIST_VOL: historical volatility = stdev(log-returns, period) * sqrt(252) * 100.
   Uses sample std (divide by period-1). */
af_error_t af_hist_vol(const double *src, size_t n, size_t period, double *out);

/* CMF: Chaikin Money Flow over `period` bars. */
af_error_t af_cmf(const double *high, const double *low, const double *close,
                  const double *volume, size_t n, size_t period, double *out);

/* ACC_DIST: Accumulation/Distribution Line. */
af_error_t af_acc_dist(const double *high, const double *low, const double *close,
                       const double *volume, size_t n, double *out);

/* FORCE INDEX: EMA(period) of (close[i]-close[i-1])*volume[i]. */
af_error_t af_force_index(const double *close, const double *volume, size_t n,
                           size_t period, double *out);

/* VOL_OSC: Volume Oscillator = (EMA(fast) - EMA(slow)) / EMA(slow) * 100. */
af_error_t af_vol_osc(const double *volume, size_t n, size_t fast, size_t slow,
                      double *out);

/* DONCHIAN: upper = highest high, lower = lowest low over `period`.
   middle = (upper + lower) / 2. */
af_error_t af_donchian(const double *high, const double *low, size_t n, size_t period,
                       double *upper, double *middle, double *lower);

/* PIVOT_CLASSIC: p=(H+L+C)/3; R1=2p-L, R2=p+(H-L), R3=p+2(H-L);
                  S1=2p-H, S2=p-(H-L), S3=p-2(H-L). */
af_error_t af_pivot_classic(double high, double low, double close,
                             double *p, double *r1, double *s1,
                             double *r2, double *s2, double *r3, double *s3);

/* PIVOT_FIBONACCI: p=(H+L+C)/3; R1=p+0.382*r, R2=p+0.618*r, R3=p+r;
                    S1=p-0.382*r, S2=p-0.618*r, S3=p-r; r=H-L. */
af_error_t af_pivot_fibonacci(double high, double low, double close,
                               double *p, double *r1, double *s1,
                               double *r2, double *s2, double *r3, double *s3);

/* PIVOT_CAMARILLA: R1=C+r*1.1/12, R2=C+r*1.1/6, R3=C+r*1.1/4;
                    S1=C-r*1.1/12, S2=C-r*1.1/6, S3=C-r*1.1/4; r=H-L.
                    p = (H+L+C)/3. */
af_error_t af_pivot_camarilla(double high, double low, double close,
                               double *p, double *r1, double *s1,
                               double *r2, double *s2, double *r3, double *s3);

/* FIBONACCI retracement: 7 levels at 0%, 23.6%, 38.2%, 50%, 61.8%, 78.6%, 100%.
   Levels run from swing_high (0%) down to swing_low (100%).
   out_levels must be caller-allocated with at least 7 doubles. */
af_error_t af_fibonacci(double swing_high, double swing_low, double *out_levels);

/* SAR: Parabolic Stop and Reverse.
   out_bull[i] = 1.0 if bullish, 0.0 if bearish. */
af_error_t af_sar(const double *high, const double *low, size_t n,
                  double start, double step, double max,
                  double *out_sar, double *out_bull);

/* ICHIMOKU: all output arrays length n.
   shift is implicitly kijun (standard 26-bar displacement).
   Senkou A and B are shifted forward by kijun bars (written at i+kijun).
   Chikou is close shifted back by kijun bars (written at i-kijun). */
af_error_t af_ichimoku(const double *high, const double *low, const double *close,
                       size_t n, size_t tenkan, size_t kijun, size_t senkou_b,
                       double *tenkan_out, double *kijun_out,
                       double *senkou_a, double *senkou_b_out, double *chikou);

/* ── Indicator engine ─────────────────────────────────────────────────────
 * Runs all indicators on a bar array and returns a compact summary result
 * with scalar values used by the algorithm layer and backtest engine.
 * Mirrors the C++ EngineResult / IndicatorEngine interface.
 * ─────────────────────────────────────────────────────────────────────── */

#define AF_IND_MAX_INDICATORS 64

typedef struct {
    char   name[32];
    int    valid;       /* 1 if the indicator produced a value */
    int    signal;      /* +1 bullish / 0 neutral / -1 bearish */
    double value;       /* primary scalar */
    double value2;      /* secondary (e.g. signal line) */
    double value3;      /* tertiary  (e.g. histogram) */
} af_indicator_result_t;

typedef struct {
    af_indicator_result_t indicators[AF_IND_MAX_INDICATORS];
    int                   indicator_count;

    /* Quick-access scalars for the algo/backtest layer */
    double atr_value;   /* current ATR; >0 when valid */
    double adx_value;
    double rsi_value;   /* default 50 when not yet valid */
    double vwap_value;

    /* Signal summary */
    int bullish;
    int bearish;
    int neutral;
} af_engine_result_t;

/**
 * af_run_indicators — runs all registered indicators on bars[0..count-1].
 * bars[0] = oldest, bars[count-1] = newest.
 * Returns a populated af_engine_result_t; scalar fields default to 0 / 50
 * when there is insufficient data.
 */
af_engine_result_t af_run_indicators(const af_bar_t *bars, size_t count,
                                      const char *symbol,
                                      af_timeframe_t tf);

#endif /* AF_INDICATORS_H */
