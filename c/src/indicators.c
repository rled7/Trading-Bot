#include "af_indicators.h"
#include <math.h>
#include <stdlib.h>

static const double AF_NAN = 0.0/0.0;

static void fill_nan(double *a, size_t n) {
    for (size_t i = 0; i < n; ++i) a[i] = AF_NAN;
}

/* Newton-Raphson sqrt — avoids linking libm while remaining accurate. */
static double af_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double g = x > 1.0 ? x * 0.5 : 1.0;
    for (int i = 0; i < 64; ++i) {
        double ng = 0.5 * (g + x / g);
        if (ng == g) break;
        g = ng;
    }
    return g;
}

static int is_nan(double x) { return x != x; }

/* Find the first index where there are `period` consecutive non-NaN values.
   Returns the index of the FIRST of that run, or -1 if not found. */
static long find_seed_start(const double *src, size_t n, size_t period) {
    size_t run = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!is_nan(src[i])) {
            run++;
            if (run == period) return (long)(i - period + 1);
        } else {
            run = 0;
        }
    }
    return -1;
}

af_error_t af_sma(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n < period) return AF_ERR_IO;

    long seed = find_seed_start(src, n, period);
    if (seed < 0) return AF_ERR_IO;
    size_t s = (size_t)seed;
    double sum = 0.0;
    for (size_t i = s; i < s + period; ++i) sum += src[i];
    out[s + period - 1] = sum / (double)period;
    for (size_t i = s + period; i < n; ++i) {
        /* If we hit a NaN downstream of the seed, propagate NaN and stop. */
        if (is_nan(src[i])) return AF_OK;
        sum += src[i] - src[i - period];
        out[i] = sum / (double)period;
    }
    return AF_OK;
}

af_error_t af_ema(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n < period) return AF_ERR_IO;

    long seed = find_seed_start(src, n, period);
    if (seed < 0) return AF_ERR_IO;
    size_t s = (size_t)seed;
    const double alpha = 2.0 / (double)(period + 1);
    double sum = 0.0;
    for (size_t i = s; i < s + period; ++i) sum += src[i];
    out[s + period - 1] = sum / (double)period;
    for (size_t i = s + period; i < n; ++i) {
        if (is_nan(src[i])) return AF_OK;
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

/* ── MACD ──────────────────────────────────────────────────────────────── */
af_error_t af_macd(const double *src, size_t n,
                   size_t fast, size_t slow, size_t signal,
                   double *macd_line, double *signal_line, double *histogram)
{
    if (!src || !macd_line || !signal_line || !histogram) return AF_ERR_INVALID_PARAM;
    if (fast == 0 || slow == 0 || signal == 0) return AF_ERR_INVALID_PARAM;

    double *ef = (double*)malloc(n * sizeof(double));
    double *es = (double*)malloc(n * sizeof(double));
    if (!ef || !es) { free(ef); free(es); return AF_ERR_IO; }

    af_ema(src, n, fast, ef);
    af_ema(src, n, slow, es);

    /* macd_line = EMA(fast) - EMA(slow); NaN if either is NaN */
    for (size_t i = 0; i < n; ++i) {
        if (ef[i] != ef[i] || es[i] != es[i])
            macd_line[i] = AF_NAN;
        else
            macd_line[i] = ef[i] - es[i];
    }
    free(ef);
    free(es);

    /* signal_line = EMA(macd_line, signal) */
    af_ema(macd_line, n, signal, signal_line);

    /* histogram = macd_line - signal_line */
    for (size_t i = 0; i < n; ++i) {
        if (macd_line[i] != macd_line[i] || signal_line[i] != signal_line[i])
            histogram[i] = AF_NAN;
        else
            histogram[i] = macd_line[i] - signal_line[i];
    }
    return AF_OK;
}

/* ── Bollinger Bands ───────────────────────────────────────────────────── */
af_error_t af_bollinger(const double *src, size_t n, size_t period, double mult,
                        double *upper, double *middle, double *lower)
{
    if (!src || !upper || !middle || !lower || period == 0) return AF_ERR_INVALID_PARAM;

    /* middle = SMA(period) */
    af_error_t rc = af_sma(src, n, period, middle);
    fill_nan(upper, n);
    fill_nan(lower, n);
    if (rc != AF_OK) return rc;

    /* population stddev over the same window */
    for (size_t i = period - 1; i < n; ++i) {
        double m = middle[i];
        if (m != m) continue; /* NaN guard */
        double var = 0.0;
        for (size_t j = 0; j < period; ++j) {
            double d = src[i - j] - m;
            var += d * d;
        }
        double sd = af_sqrt(var / (double)period);
        upper[i] = m + mult * sd;
        lower[i] = m - mult * sd;
    }
    return AF_OK;
}

/* ── Stochastic ────────────────────────────────────────────────────────── */
af_error_t af_stochastic(const double *high, const double *low, const double *close,
                         size_t n, size_t k_period, size_t d_period,
                         double *k_out, double *d_out)
{
    if (!high || !low || !close || !k_out || !d_out) return AF_ERR_INVALID_PARAM;
    if (k_period == 0 || d_period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(k_out, n);
    fill_nan(d_out, n);
    if (n < k_period) return AF_ERR_IO;

    double *rawK = (double*)malloc(n * sizeof(double));
    if (!rawK) return AF_ERR_IO;
    fill_nan(rawK, n);

    for (size_t i = k_period - 1; i < n; ++i) {
        double lo = low[i], hi = high[i];
        for (size_t j = 1; j < k_period; ++j) {
            if (low[i - j]  < lo) lo = low[i - j];
            if (high[i - j] > hi) hi = high[i - j];
        }
        double r = hi - lo;
        rawK[i] = (r > 1e-12) ? 100.0 * (close[i] - lo) / r : 50.0;
    }

    /* k_out = SMA(rawK, d_period); d_out = SMA(k_out, d_period) */
    af_sma(rawK, n, d_period, k_out);
    af_sma(k_out, n, d_period, d_out);
    free(rawK);
    return AF_OK;
}

/* ── OBV ───────────────────────────────────────────────────────────────── */
af_error_t af_obv(const double *close, const double *volume, size_t n, double *out)
{
    if (!close || !volume || !out || n == 0) return AF_ERR_INVALID_PARAM;
    out[0] = 0.0;
    for (size_t i = 1; i < n; ++i) {
        double d = close[i] - close[i - 1];
        out[i] = out[i - 1] + (d > 0 ? volume[i] : d < 0 ? -volume[i] : 0.0);
    }
    return AF_OK;
}

/* ── ADX ───────────────────────────────────────────────────────────────── */
af_error_t af_adx(const double *high, const double *low, const double *close,
                  size_t n, size_t period,
                  double *adx_out, double *plus_di, double *minus_di)
{
    if (!high || !low || !close || !adx_out || !plus_di || !minus_di || period == 0)
        return AF_ERR_INVALID_PARAM;
    fill_nan(adx_out, n);
    fill_nan(plus_di, n);
    fill_nan(minus_di, n);
    /* Need at least 2*period+1 bars for full output (matches cpp/ guard). */
    if (n < 2 * period + 1) return AF_ERR_IO;

    const double alpha = 1.0 / (double)period;

    double *tr  = (double*)malloc(n * sizeof(double));
    double *pdm = (double*)malloc(n * sizeof(double));
    double *mdm = (double*)malloc(n * sizeof(double));
    if (!tr || !pdm || !mdm) { free(tr); free(pdm); free(mdm); return AF_ERR_IO; }

    tr[0] = pdm[0] = mdm[0] = 0.0;
    for (size_t i = 1; i < n; ++i) {
        double hl  = high[i] - low[i];
        double hpc = dabs(high[i] - close[i - 1]);
        double lpc = dabs(low[i]  - close[i - 1]);
        tr[i] = max3(hl, hpc, lpc);
        double up = high[i] - high[i - 1];
        double dn = low[i - 1] - low[i];
        pdm[i] = (up > dn && up > 0) ? up : 0.0;
        mdm[i] = (dn > up && dn > 0) ? dn : 0.0;
    }

    /* Seed: simple sum of bars 1..period */
    double atr_v = 0.0, pdm_s = 0.0, mdm_s = 0.0;
    for (size_t i = 1; i <= period; ++i) {
        atr_v += tr[i];
        pdm_s += pdm[i];
        mdm_s += mdm[i];
    }

    /* Wilder smoothing from index `period` onwards; first DX seeds ADX. */
    for (size_t i = period; i < n; ++i) {
        if (i > period) {
            atr_v = atr_v * (1.0 - alpha) + tr[i];
            pdm_s = pdm_s * (1.0 - alpha) + pdm[i];
            mdm_s = mdm_s * (1.0 - alpha) + mdm[i];
        }
        double pdi = (atr_v > 1e-12) ? 100.0 * pdm_s / atr_v : 0.0;
        double mdi = (atr_v > 1e-12) ? 100.0 * mdm_s / atr_v : 0.0;
        plus_di[i]  = pdi;
        minus_di[i] = mdi;
        double denom = pdi + mdi;
        double dx = (denom > 1e-12) ? 100.0 * dabs(pdi - mdi) / denom : 0.0;
        /* ADX: first value = dx; subsequent = Wilder smoothed */
        adx_out[i] = (i == period) ? dx : adx_out[i - 1] * (1.0 - alpha) + dx * alpha;
    }

    free(tr); free(pdm); free(mdm);
    return AF_OK;
}
