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

#endif /* AF_INDICATORS_H */
