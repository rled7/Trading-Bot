#include "af_indicators.h"
#include <math.h>
#include <stdlib.h>

static const double AF_NAN = 0.0/0.0;

/* Natural log approximation — avoids linking libm while remaining accurate.
   Uses identity: ln(x) = ln(m * 2^k) = k*ln(2) + ln(m), m in [1,2).
   ln(m) approximated via a 7th-degree minimax polynomial on [1,2). */
static double af_log(double x) {
    if (x <= 0.0) return -1e300; /* -inf sentinel */
    /* Extract exponent */
    int k = 0;
    double m = x;
    while (m >= 2.0) { m *= 0.5; ++k; }
    while (m < 1.0)  { m *= 2.0; --k; }
    /* Polynomial for ln(m) on [1,2): substitute t = (m-1)/(m+1) */
    /* ln(m) = 2*(t + t^3/3 + t^5/5 + ...) — use 8 terms for ~15 sig figs */
    double t = (m - 1.0) / (m + 1.0);
    double t2 = t * t;
    double r = t * (2.0 + t2 * (2.0/3.0 + t2 * (2.0/5.0 + t2 * (2.0/7.0
               + t2 * (2.0/9.0 + t2 * (2.0/11.0 + t2 * (2.0/13.0
               + t2 * 2.0/15.0)))))));
    static const double LN2 = 0.6931471805599453094172321;
    return r + (double)k * LN2;
}

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

/* ── WMA ───────────────────────────────────────────────────────────────── */
af_error_t af_wma(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n < period) return AF_ERR_IO;
    /* wsum = period*(period+1)/2 */
    double wsum = (double)period * ((double)period + 1.0) / 2.0;
    for (size_t i = period - 1; i < n; ++i) {
        double s = 0.0;
        for (size_t j = 0; j < period; ++j) {
            /* bar at (i - (period-1-j)) gets weight (j+1) */
            s += src[i - (period - 1 - j)] * (double)(j + 1);
        }
        out[i] = s / wsum;
    }
    return AF_OK;
}

/* ── CCI ───────────────────────────────────────────────────────────────── */
af_error_t af_cci(const double *high, const double *low, const double *close,
                  size_t n, size_t period, double constant, double *out) {
    if (!high || !low || !close || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n < period) return AF_ERR_IO;
    for (size_t i = period - 1; i < n; ++i) {
        /* Mean of TP over window */
        double sum = 0.0;
        for (size_t j = 0; j < period; ++j)
            sum += (high[i - j] + low[i - j] + close[i - j]) / 3.0;
        double mean = sum / (double)period;
        /* Mean absolute deviation */
        double mad = 0.0;
        for (size_t j = 0; j < period; ++j) {
            double tp = (high[i - j] + low[i - j] + close[i - j]) / 3.0;
            double d = tp - mean;
            mad += (d < 0.0 ? -d : d);
        }
        mad /= (double)period;
        double tp_i = (high[i] + low[i] + close[i]) / 3.0;
        out[i] = (mad > 1e-12) ? (tp_i - mean) / (constant * mad) : 0.0;
    }
    return AF_OK;
}

/* ── Williams %R ───────────────────────────────────────────────────────── */
af_error_t af_williams_r(const double *high, const double *low, const double *close,
                          size_t n, size_t period, double *out) {
    if (!high || !low || !close || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n < period) return AF_ERR_IO;
    for (size_t i = period - 1; i < n; ++i) {
        double hi = high[i], lo = low[i];
        for (size_t j = 1; j < period; ++j) {
            if (high[i - j] > hi) hi = high[i - j];
            if (low[i - j]  < lo) lo = low[i - j];
        }
        double r = hi - lo;
        out[i] = (r > 1e-12) ? -100.0 * (hi - close[i]) / r : -50.0;
    }
    return AF_OK;
}

/* ── ROC ───────────────────────────────────────────────────────────────── */
af_error_t af_roc(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n <= period) return AF_ERR_IO;
    for (size_t i = period; i < n; ++i) {
        double p = src[i - period];
        out[i] = (p > 1e-12 || p < -1e-12) ? (src[i] - p) / p * 100.0 : 0.0;
    }
    return AF_OK;
}

/* ── MFI ───────────────────────────────────────────────────────────────── */
af_error_t af_mfi(const double *high, const double *low, const double *close,
                  const double *volume, size_t n, size_t period, double *out) {
    if (!high || !low || !close || !volume || !out || period == 0)
        return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n <= period) return AF_ERR_IO;
    for (size_t i = period; i < n; ++i) {
        double pmf = 0.0, nmf = 0.0;
        for (size_t j = 0; j < period; ++j) {
            size_t k = i - j;
            double tp = (high[k] + low[k] + close[k]) / 3.0;
            double mf = tp * volume[k];
            /* Compare TP to previous bar's TP */
            double pp = (k > 0) ? (high[k-1] + low[k-1] + close[k-1]) / 3.0 : tp;
            if (tp >= pp) pmf += mf;
            else          nmf += mf;
        }
        double r = (nmf > 1e-12) ? pmf / nmf : 100.0;
        out[i] = 100.0 - 100.0 / (1.0 + r);
    }
    return AF_OK;
}

/* ── VWAP ──────────────────────────────────────────────────────────────── */
af_error_t af_vwap(const double *high, const double *low, const double *close,
                   const double *volume, size_t n, double *out) {
    if (!high || !low || !close || !volume || !out) return AF_ERR_INVALID_PARAM;
    if (n == 0) return AF_ERR_IO;
    double cpv = 0.0, cv = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double vv = volume[i] > 0.0 ? volume[i] : 1.0;
        double tp = (high[i] + low[i] + close[i]) / 3.0;
        cpv += tp * vv;
        cv  += vv;
        out[i] = cpv / cv;
    }
    return AF_OK;
}

/* ── HMA ───────────────────────────────────────────────────────────────── */
/* Matches cpp/ af_hma exactly.
   half = (int)sqrt(period/2), clamped to >=1 (note: cpp uses sqrt(period/2.0))
   sq   = (int)sqrt(period),   clamped to >=2
   raw  = 2*WMA(half) - WMA(period); out = WMA(raw, sq) */
af_error_t af_hma(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period < 2) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);

    int half = (int)af_sqrt((double)period / 2.0);
    if (half < 1) half = 1;
    int sq = (int)af_sqrt((double)period);
    if (sq < 2) sq = 2;

    double *wh  = (double*)malloc(n * sizeof(double));
    double *wf  = (double*)malloc(n * sizeof(double));
    double *raw = (double*)malloc(n * sizeof(double));
    if (!wh || !wf || !raw) { free(wh); free(wf); free(raw); return AF_ERR_IO; }

    af_wma(src, n, (size_t)half, wh);
    af_wma(src, n, period, wf);
    for (size_t i = 0; i < n; ++i)
        raw[i] = (is_nan(wh[i]) || is_nan(wf[i])) ? AF_NAN : 2.0 * wh[i] - wf[i];
    af_wma(raw, n, (size_t)sq, out);

    free(wh); free(wf); free(raw);
    return AF_OK;
}

/* ── DEMA ──────────────────────────────────────────────────────────────── */
/* Matches cpp/ af_dema exactly.
   out[i] = 2*EMA1[i] - EMA2[i], NaN if either is NaN. */
af_error_t af_dema(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);

    double *e1 = (double*)malloc(n * sizeof(double));
    double *e2 = (double*)malloc(n * sizeof(double));
    if (!e1 || !e2) { free(e1); free(e2); return AF_ERR_IO; }

    af_ema(src, n, period, e1);
    af_ema(e1,  n, period, e2);
    for (size_t i = 0; i < n; ++i)
        out[i] = (is_nan(e1[i]) || is_nan(e2[i])) ? AF_NAN : 2.0 * e1[i] - e2[i];

    free(e1); free(e2);
    return AF_OK;
}

/* ── TEMA ──────────────────────────────────────────────────────────────── */
/* Matches cpp/ af_tema exactly.
   out[i] = 3*EMA1[i] - 3*EMA2[i] + EMA3[i], NaN if any is NaN. */
af_error_t af_tema(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);

    double *e1 = (double*)malloc(n * sizeof(double));
    double *e2 = (double*)malloc(n * sizeof(double));
    double *e3 = (double*)malloc(n * sizeof(double));
    if (!e1 || !e2 || !e3) { free(e1); free(e2); free(e3); return AF_ERR_IO; }

    af_ema(src, n, period, e1);
    af_ema(e1,  n, period, e2);
    af_ema(e2,  n, period, e3);
    for (size_t i = 0; i < n; ++i)
        out[i] = (is_nan(e1[i]) || is_nan(e2[i]) || is_nan(e3[i]))
                 ? AF_NAN : 3.0 * e1[i] - 3.0 * e2[i] + e3[i];

    free(e1); free(e2); free(e3);
    return AF_OK;
}

/* ── TRIX ──────────────────────────────────────────────────────────────── */
/* Matches cpp/ af_trix (trix output only).
   EMA3 = EMA(EMA(EMA(src, period), period), period)
   out[i] = (EMA3[i] - EMA3[i-1]) / EMA3[i-1] * 100, when EMA3[i-1] != 0. */
af_error_t af_trix(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);

    double *e1 = (double*)malloc(n * sizeof(double));
    double *e2 = (double*)malloc(n * sizeof(double));
    double *e3 = (double*)malloc(n * sizeof(double));
    if (!e1 || !e2 || !e3) { free(e1); free(e2); free(e3); return AF_ERR_IO; }

    af_ema(src, n, period, e1);
    af_ema(e1,  n, period, e2);
    af_ema(e2,  n, period, e3);

    /* out already fill_nan'd; compute ROC of e3. */
    for (size_t i = 1; i < n; ++i) {
        if (!is_nan(e3[i]) && !is_nan(e3[i - 1])) {
            double prev = e3[i - 1];
            if (prev > 1e-12 || prev < -1e-12)
                out[i] = (e3[i] - prev) / prev * 100.0;
        }
    }

    free(e1); free(e2); free(e3);
    return AF_OK;
}

/* ══ NEW INDICATORS (16) ═══════════════════════════════════════════════════ */

/* ── Momentum ──────────────────────────────────────────────────────────── */
af_error_t af_momentum(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n <= period) return AF_ERR_IO;
    for (size_t i = period; i < n; ++i)
        out[i] = src[i] - src[i - period];
    return AF_OK;
}

/* ── True Range ────────────────────────────────────────────────────────── */
af_error_t af_true_range(const double *high, const double *low, const double *close,
                          size_t n, double *out) {
    if (!high || !low || !close || !out || n == 0) return AF_ERR_INVALID_PARAM;
    out[0] = high[0] - low[0];
    for (size_t i = 1; i < n; ++i) {
        double hl  = high[i] - low[i];
        double hpc = dabs(high[i] - close[i - 1]);
        double lpc = dabs(low[i]  - close[i - 1]);
        out[i] = max3(hl, hpc, lpc);
    }
    return AF_OK;
}

/* ── Wilder EMA ────────────────────────────────────────────────────────── */
af_error_t af_wilder_ema(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    /* Find first run of `period` consecutive non-NaN values */
    long seed = find_seed_start(src, n, period);
    if (seed < 0) return AF_ERR_IO;
    size_t s = (size_t)seed;
    const double alpha = 1.0 / (double)period;
    double sum = 0.0;
    for (size_t i = s; i < s + period; ++i) sum += src[i];
    out[s + period - 1] = sum / (double)period;
    for (size_t i = s + period; i < n; ++i) {
        if (is_nan(src[i])) return AF_OK;
        out[i] = src[i] * alpha + out[i - 1] * (1.0 - alpha);
    }
    return AF_OK;
}

/* ── VWMA ──────────────────────────────────────────────────────────────── */
af_error_t af_vwma(const double *src, const double *volume, size_t n,
                   size_t period, double *out) {
    if (!src || !volume || !out || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n < period) return AF_ERR_IO;
    for (size_t i = period - 1; i < n; ++i) {
        double pv = 0.0, v = 0.0;
        for (size_t j = 0; j < period; ++j) {
            double vv = volume[i - j];
            if (vv <= 0.0) vv = 1.0;
            pv += src[i - j] * vv;
            v  += vv;
        }
        out[i] = (v > 1e-12) ? pv / v : src[i];
    }
    return AF_OK;
}

/* ── Historical Volatility ─────────────────────────────────────────────── */
/* Uses sample std (divide by period-1), annualised with 252 trading days. */
af_error_t af_hist_vol(const double *src, size_t n, size_t period, double *out) {
    if (!src || !out || period < 2) return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n <= period) return AF_ERR_IO;
    for (size_t i = period; i < n; ++i) {
        double mean = 0.0;
        for (size_t j = 0; j < period; ++j) {
            double p = src[i - 1 - j];
            if (p > 1e-12) mean += af_log(src[i - j] / p);
        }
        mean /= (double)period;
        double var = 0.0;
        for (size_t j = 0; j < period; ++j) {
            double p = src[i - 1 - j];
            if (p > 1e-12) {
                double r = af_log(src[i - j] / p) - mean;
                var += r * r;
            }
        }
        double sd = af_sqrt(var / (double)(period - 1));
        out[i] = sd * af_sqrt(252.0) * 100.0;
    }
    return AF_OK;
}

/* ── CMF ───────────────────────────────────────────────────────────────── */
af_error_t af_cmf(const double *high, const double *low, const double *close,
                  const double *volume, size_t n, size_t period, double *out) {
    if (!high || !low || !close || !volume || !out || period == 0)
        return AF_ERR_INVALID_PARAM;
    fill_nan(out, n);
    if (n < period) return AF_ERR_IO;
    for (size_t i = period - 1; i < n; ++i) {
        double mfvs = 0.0, vs = 0.0;
        for (size_t j = 0; j < period; ++j) {
            double r = high[i - j] - low[i - j];
            double clv = (r > 1e-12)
                ? ((close[i - j] - low[i - j]) - (high[i - j] - close[i - j])) / r
                : 0.0;
            mfvs += clv * volume[i - j];
            vs   += volume[i - j];
        }
        out[i] = (vs > 1e-12) ? mfvs / vs : 0.0;
    }
    return AF_OK;
}

/* ── Accumulation/Distribution ─────────────────────────────────────────── */
af_error_t af_acc_dist(const double *high, const double *low, const double *close,
                       const double *volume, size_t n, double *out) {
    if (!high || !low || !close || !volume || !out || n == 0)
        return AF_ERR_INVALID_PARAM;
    out[0] = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double r = high[i] - low[i];
        double clv = (r > 1e-12)
            ? ((close[i] - low[i]) - (high[i] - close[i])) / r
            : 0.0;
        out[i] = (i == 0 ? 0.0 : out[i - 1]) + clv * volume[i];
    }
    return AF_OK;
}

/* ── Force Index ───────────────────────────────────────────────────────── */
af_error_t af_force_index(const double *close, const double *volume, size_t n,
                           size_t period, double *out) {
    if (!close || !volume || !out || n < 2 || period == 0) return AF_ERR_INVALID_PARAM;
    double *raw = (double*)malloc(n * sizeof(double));
    if (!raw) return AF_ERR_IO;
    raw[0] = 0.0;
    for (size_t i = 1; i < n; ++i)
        raw[i] = (close[i] - close[i - 1]) * volume[i];
    af_error_t e = af_ema(raw, n, period, out);
    free(raw);
    return e;
}

/* ── Volume Oscillator ─────────────────────────────────────────────────── */
af_error_t af_vol_osc(const double *volume, size_t n, size_t fast, size_t slow,
                      double *out) {
    if (!volume || !out || fast == 0 || slow == 0) return AF_ERR_INVALID_PARAM;
    double *ef = (double*)malloc(n * sizeof(double));
    double *es = (double*)malloc(n * sizeof(double));
    if (!ef || !es) { free(ef); free(es); return AF_ERR_IO; }
    af_ema(volume, n, fast, ef);
    af_ema(volume, n, slow, es);
    for (size_t i = 0; i < n; ++i) {
        if (!is_nan(ef[i]) && !is_nan(es[i]) && (es[i] > 1e-12 || es[i] < -1e-12))
            out[i] = (ef[i] - es[i]) / es[i] * 100.0;
        else
            out[i] = AF_NAN;
    }
    free(ef); free(es);
    return AF_OK;
}

/* ── Donchian Channels ─────────────────────────────────────────────────── */
af_error_t af_donchian(const double *high, const double *low, size_t n, size_t period,
                       double *upper, double *middle, double *lower) {
    if (!high || !low || !upper || !lower || period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(upper, n);
    fill_nan(lower, n);
    if (middle) fill_nan(middle, n);
    if (n < period) return AF_ERR_IO;
    for (size_t i = period - 1; i < n; ++i) {
        double hi = high[i], lo = low[i];
        for (size_t j = 1; j < period; ++j) {
            if (high[i - j] > hi) hi = high[i - j];
            if (low[i - j]  < lo) lo = low[i - j];
        }
        upper[i] = hi;
        lower[i] = lo;
        if (middle) middle[i] = (hi + lo) * 0.5;
    }
    return AF_OK;
}

/* ── Pivot Classic ─────────────────────────────────────────────────────── */
af_error_t af_pivot_classic(double high, double low, double close,
                             double *p, double *r1, double *s1,
                             double *r2, double *s2, double *r3, double *s3) {
    if (!p || !r1 || !s1 || !r2 || !s2 || !r3 || !s3) return AF_ERR_INVALID_PARAM;
    *p  = (high + low + close) / 3.0;
    *r1 = 2.0 * (*p) - low;
    *r2 = (*p) + (high - low);
    *r3 = (*p) + 2.0 * (high - low);
    *s1 = 2.0 * (*p) - high;
    *s2 = (*p) - (high - low);
    *s3 = (*p) - 2.0 * (high - low);
    return AF_OK;
}

/* ── Pivot Fibonacci ───────────────────────────────────────────────────── */
af_error_t af_pivot_fibonacci(double high, double low, double close,
                               double *p, double *r1, double *s1,
                               double *r2, double *s2, double *r3, double *s3) {
    if (!p || !r1 || !s1 || !r2 || !s2 || !r3 || !s3) return AF_ERR_INVALID_PARAM;
    double r = high - low;
    *p  = (high + low + close) / 3.0;
    *r1 = (*p) + r * 0.382;
    *r2 = (*p) + r * 0.618;
    *r3 = (*p) + r * 1.000;
    *s1 = (*p) - r * 0.382;
    *s2 = (*p) - r * 0.618;
    *s3 = (*p) - r * 1.000;
    return AF_OK;
}

/* ── Pivot Camarilla ───────────────────────────────────────────────────── */
/* Task spec: p, r1, s1, r2, s2, r3, s3. Math from cpp/ af_pivot_camarilla.
   p = (H+L+C)/3; R1=C+r*1.1/12, R2=C+r*1.1/6, R3=C+r*1.1/4;
   S1=C-r*1.1/12, S2=C-r*1.1/6, S3=C-r*1.1/4. */
af_error_t af_pivot_camarilla(double high, double low, double close,
                               double *p, double *r1, double *s1,
                               double *r2, double *s2, double *r3, double *s3) {
    if (!p || !r1 || !s1 || !r2 || !s2 || !r3 || !s3) return AF_ERR_INVALID_PARAM;
    double r = high - low;
    *p  = (high + low + close) / 3.0;
    *r1 = close + r * 1.1 / 12.0;
    *r2 = close + r * 1.1 / 6.0;
    *r3 = close + r * 1.1 / 4.0;
    *s1 = close - r * 1.1 / 12.0;
    *s2 = close - r * 1.1 / 6.0;
    *s3 = close - r * 1.1 / 4.0;
    return AF_OK;
}

/* ── Fibonacci Retracement ─────────────────────────────────────────────── */
/* 7 levels: 0%, 23.6%, 38.2%, 50%, 61.8%, 78.6%, 100%.
   Level[0] = swing_high (0% retracement), Level[6] = swing_low (100%). */
af_error_t af_fibonacci(double swing_high, double swing_low, double *out_levels) {
    if (!out_levels) return AF_ERR_INVALID_PARAM;
    static const double F[] = {0.0, 0.236, 0.382, 0.500, 0.618, 0.786, 1.000};
    double range = swing_high - swing_low;
    for (size_t i = 0; i < 7; ++i)
        out_levels[i] = swing_high - F[i] * range;
    return AF_OK;
}

/* ── SAR ───────────────────────────────────────────────────────────────── */
af_error_t af_sar(const double *high, const double *low, size_t n,
                  double start, double step, double max,
                  double *out_sar, double *out_bull) {
    if (!high || !low || !out_sar || !out_bull || n < 2) return AF_ERR_INVALID_PARAM;
    out_sar[0]  = low[0];
    out_bull[0] = 1.0;
    double ep    = high[0];
    double accel = start;
    for (size_t i = 1; i < n; ++i) {
        int bull = (out_bull[i - 1] > 0.5) ? 1 : 0;
        double sar, nep = ep;
        if (bull) {
            sar = out_sar[i - 1] + accel * (ep - out_sar[i - 1]);
            /* Clamp SAR to not exceed last two lows */
            double prev_low2 = (i >= 2) ? low[i - 2] : low[i - 1];
            double min_low = low[i - 1] < prev_low2 ? low[i - 1] : prev_low2;
            if (sar > min_low) sar = min_low;
            if (low[i] < sar) {
                /* Reversal to bearish */
                sar   = ep;
                nep   = low[i];
                accel = start;
                bull  = 0;
            } else if (high[i] > ep) {
                nep   = high[i];
                accel = accel + step < max ? accel + step : max;
            }
        } else {
            sar = out_sar[i - 1] - accel * (out_sar[i - 1] - ep);
            /* Clamp SAR to not be below last two highs */
            double prev_high2 = (i >= 2) ? high[i - 2] : high[i - 1];
            double max_high = high[i - 1] > prev_high2 ? high[i - 1] : prev_high2;
            if (sar < max_high) sar = max_high;
            if (high[i] > sar) {
                /* Reversal to bullish */
                sar   = ep;
                nep   = high[i];
                accel = start;
                bull  = 1;
            } else if (low[i] < ep) {
                nep   = low[i];
                accel = accel + step < max ? accel + step : max;
            }
        }
        ep          = nep;
        out_sar[i]  = sar;
        out_bull[i] = bull ? 1.0 : 0.0;
    }
    return AF_OK;
}

/* ── Ichimoku ──────────────────────────────────────────────────────────── */
/* shift = kijun (standard 26). Senkou A/B written at i+shift; chikou at i-shift. */
af_error_t af_ichimoku(const double *high, const double *low, const double *close,
                       size_t n, size_t tenkan, size_t kijun, size_t senkou_b,
                       double *tenkan_out, double *kijun_out,
                       double *senkou_a, double *senkou_b_out, double *chikou) {
    if (!high || !low || !close || !tenkan_out || !kijun_out || n == 0)
        return AF_ERR_INVALID_PARAM;
    fill_nan(tenkan_out, n);
    fill_nan(kijun_out, n);
    if (senkou_a)     fill_nan(senkou_a, n);
    if (senkou_b_out) fill_nan(senkou_b_out, n);
    if (chikou)       fill_nan(chikou, n);

    size_t shift = kijun; /* standard Ichimoku displacement */

    for (size_t i = 0; i < n; ++i) {
        /* Tenkan-sen */
        if (i >= tenkan - 1) {
            double hi = high[i], lo = low[i];
            for (size_t j = 1; j < tenkan; ++j) {
                if (high[i - j] > hi) hi = high[i - j];
                if (low[i - j]  < lo) lo = low[i - j];
            }
            tenkan_out[i] = (hi + lo) / 2.0;
        }
        /* Kijun-sen */
        if (i >= kijun - 1) {
            double hi = high[i], lo = low[i];
            for (size_t j = 1; j < kijun; ++j) {
                if (high[i - j] > hi) hi = high[i - j];
                if (low[i - j]  < lo) lo = low[i - j];
            }
            kijun_out[i] = (hi + lo) / 2.0;
        }
    }

    /* Senkou Span A: (Tenkan + Kijun) / 2, shifted forward by `shift` */
    if (senkou_a) {
        for (size_t i = 0; i + shift < n; ++i) {
            if (!is_nan(tenkan_out[i]) && !is_nan(kijun_out[i]))
                senkou_a[i + shift] = (tenkan_out[i] + kijun_out[i]) / 2.0;
        }
    }

    /* Senkou Span B: midpoint of senkou_b-bar range, shifted forward */
    if (senkou_b_out) {
        for (size_t i = 0; i < n; ++i) {
            if (i < senkou_b - 1) continue;
            double hi = high[i], lo = low[i];
            for (size_t j = 1; j < senkou_b; ++j) {
                if (high[i - j] > hi) hi = high[i - j];
                if (low[i - j]  < lo) lo = low[i - j];
            }
            size_t dst = i + shift;
            if (dst < n) senkou_b_out[dst] = (hi + lo) / 2.0;
        }
    }

    /* Chikou Span: close shifted back by `shift` */
    if (chikou) {
        for (size_t i = shift; i < n; ++i)
            chikou[i - shift] = close[i];
    }

    return AF_OK;
}

/* ── Keltner Channels ──────────────────────────────────────────────────── */
af_error_t af_keltner(const double *high, const double *low, const double *close,
                      size_t n, size_t ema_period, size_t atr_period, double mult,
                      double *upper, double *middle, double *lower) {
    if (!high || !low || !close || !upper || !middle || !lower) return AF_ERR_INVALID_PARAM;
    if (ema_period == 0 || atr_period == 0) return AF_ERR_INVALID_PARAM;
    fill_nan(upper, n);
    fill_nan(lower, n);

    /* middle = EMA(close, ema_period) */
    af_error_t rc = af_ema(close, n, ema_period, middle);
    if (rc != AF_OK) return rc;

    /* ATR scratch buffer */
    double *atr = (double*)malloc(n * sizeof(double));
    if (!atr) return AF_ERR_IO;
    af_atr(high, low, close, n, atr_period, atr);

    for (size_t i = 0; i < n; ++i) {
        if (is_nan(middle[i]) || is_nan(atr[i])) {
            upper[i] = AF_NAN;
            lower[i] = AF_NAN;
            continue;
        }
        upper[i] = middle[i] + mult * atr[i];
        lower[i] = middle[i] - mult * atr[i];
    }
    free(atr);
    return AF_OK;
}
