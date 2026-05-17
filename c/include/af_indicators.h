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

#endif /* AF_INDICATORS_H */
