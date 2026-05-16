#include "af_indicators.h"
#include <math.h>
#include <stdlib.h>

static const double AF_NAN = 0.0/0.0;

static void fill_nan(double *a, size_t n) {
    for (size_t i = 0; i < n; ++i) a[i] = AF_NAN;
}

af_error_t af_sma(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n < period) return AF_ERR_IO;

    double sum = 0.0;
    for (size_t i = 0; i < period; ++i) sum += src[i];
    out[period - 1] = sum / (double)period;
    for (size_t i = period; i < n; ++i) {
        sum += src[i] - src[i - period];
        out[i] = sum / (double)period;
    }
    return AF_OK;
}

af_error_t af_ema(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n < period) return AF_ERR_IO;

    const double alpha = 2.0 / (double)(period + 1);
    double seed = 0.0;
    for (size_t i = 0; i < period; ++i) seed += src[i];
    out[period - 1] = seed / (double)period;
    for (size_t i = period; i < n; ++i) {
        out[i] = src[i] * alpha + out[i - 1] * (1.0 - alpha);
    }
    return AF_OK;
}

af_error_t af_rsi(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n <= period) return AF_ERR_IO;

    const double alpha = 1.0 / (double)period;
    double ag = 0.0, al = 0.0;
    for (size_t i = 1; i <= period; ++i) {
        double d = src[i] - src[i - 1];
        if (d > 0) ag += d;
        else       al += -d;
    }
    ag /= (double)period;
    al /= (double)period;

    /* First valid RSI is at index = period (matches cpp/ reference). */
    for (size_t i = period; i < n; ++i) {
        if (i > period) {
            double d = src[i] - src[i - 1];
            double u = d > 0 ?  d : 0.0;
            double v = d < 0 ? -d : 0.0;
            ag = ag * (1.0 - alpha) + u * alpha;
            al = al * (1.0 - alpha) + v * alpha;
        }
        double rs   = (al > 1e-12) ? ag / al : 100.0;
        out[i]      = 100.0 - 100.0 / (1.0 + rs);
    }
    return AF_OK;
}

static double max3(double a, double b, double c) {
    double m = a > b ? a : b;
    return m > c ? m : c;
}

static double dabs(double x) { return x < 0 ? -x : x; }

af_error_t af_atr(const double *high, const double *low, const double *close,
                  size_t n, size_t period, double *out) {
    if (!high || !low || !close || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n <= period) return AF_ERR_IO;

    /* Build TR in a scratch buffer, then Wilder-smooth over `period`. */
    double *tr = (double*)malloc(n * sizeof(double));
    if (!tr) return AF_ERR_IO;
    tr[0] = high[0] - low[0];
    for (size_t i = 1; i < n; ++i) {
        tr[i] = max3(high[i] - low[i],
                     dabs(high[i] - close[i - 1]),
                     dabs(low[i]  - close[i - 1]));
    }

    /* Wilder: seed = simple mean of first `period`, then EMA with alpha = 1/period. */
    const double alpha = 1.0 / (double)period;
    double seed = 0.0;
    for (size_t i = 0; i < period; ++i) seed += tr[i];
    out[period - 1] = seed / (double)period;
    for (size_t i = period; i < n; ++i) {
        out[i] = tr[i] * alpha + out[i - 1] * (1.0 - alpha);
    }
    free(tr);
    return AF_OK;
}
