/**
 * AlgoForge — include/indicators/indicators.h
 * All technical indicator functions. Pure C, no allocation, no global state.
 *
 * Convention:
 *   src[0] = oldest, src[n-1] = newest
 *   out[] same length as src; leading NaN where invalid
 *   Caller allocates all arrays.
 */
#ifndef AF_INDICATORS_H
#define AF_INDICATORS_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Helpers ── */
void   af_fill_nan  (double *a, size_t n);
double af_linreg_slope(const double *src, size_t n, int period);
int    af_crossover (const double *s1, const double *s2, size_t i); /* +1/-1/0 */
AF_Error af_wilder_ema(const double *src, size_t n, int period, double *out);

/* ══ TREND ══════════════════════════════════════════════════════════════════ */
AF_Error af_sma (const double *src, size_t n, int period, double *out);
AF_Error af_ema (const double *src, size_t n, int period, double *out);
AF_Error af_wma (const double *src, size_t n, int period, double *out);
AF_Error af_hma (const double *src, size_t n, int period, double *out);
AF_Error af_dema(const double *src, size_t n, int period, double *out);
AF_Error af_tema(const double *src, size_t n, int period, double *out);
AF_Error af_vwma(const double *src, const double *vol, size_t n, int period, double *out);

/* out_macd/out_sig/out_hist each length n */
AF_Error af_macd(const double *src, size_t n, int fast, int slow, int sig,
                  double *out_macd, double *out_sig, double *out_hist);

/* out_adx/out_pdi/out_mdi each length n */
AF_Error af_adx(const double *h, const double *l, const double *c, size_t n, int period,
                 double *out_adx, double *out_pdi, double *out_mdi);

/* out_sar/out_bull each length n; out_bull=1.0 bullish */
AF_Error af_sar(const double *h, const double *l, size_t n,
                 double start, double step, double max,
                 double *out_sar, double *out_bull);

/* Ichimoku — all out arrays length n */
AF_Error af_ichimoku(const double *h, const double *l, const double *c, size_t n,
                      int tenkan, int kijun, int senkou_b, int shift,
                      double *out_t, double *out_k,
                      double *out_sa, double *out_sb, double *out_ch);

/* ══ MOMENTUM ════════════════════════════════════════════════════════════════ */
AF_Error af_rsi       (const double *src, size_t n, int period, double *out);
AF_Error af_stochastic(const double *h, const double *l, const double *c, size_t n,
                        int kp, int dp, int smooth, double *out_k, double *out_d);
AF_Error af_cci       (const double *h, const double *l, const double *c, size_t n,
                        int period, double constant, double *out);
AF_Error af_williams_r(const double *h, const double *l, const double *c, size_t n,
                        int period, double *out);
AF_Error af_roc       (const double *src, size_t n, int period, double *out);
AF_Error af_mfi       (const double *h, const double *l, const double *c,
                        const double *v, size_t n, int period, double *out);
AF_Error af_trix      (const double *src, size_t n, int period, int sig,
                        double *out_trix, double *out_sig);
AF_Error af_momentum  (const double *src, size_t n, int period, double *out);

/* ══ VOLATILITY ══════════════════════════════════════════════════════════════ */
AF_Error af_true_range(const double *h, const double *l, const double *c, size_t n, double *out);
AF_Error af_atr       (const double *h, const double *l, const double *c, size_t n,
                        int period, double *out);

/* out_upper/out_mid/out_lower/out_bw/out_pctb each length n (bw/pctb may be NULL) */
AF_Error af_bollinger (const double *src, size_t n, int period, double mult,
                        double *out_upper, double *out_mid, double *out_lower,
                        double *out_bw,    double *out_pctb);

AF_Error af_keltner  (const double *h, const double *l, const double *c, size_t n,
                       int ema_p, int atr_p, double mult,
                       double *out_upper, double *out_mid, double *out_lower);

AF_Error af_donchian (const double *h, const double *l, size_t n, int period,
                       double *out_upper, double *out_mid, double *out_lower);

AF_Error af_hist_vol  (const double *src, size_t n, int period, int tdays, double *out);

/* ══ VOLUME ══════════════════════════════════════════════════════════════════ */
AF_Error af_obv  (const double *c, const double *v, size_t n, double *out);
AF_Error af_vwap (const double *h, const double *l, const double *c,
                   const double *v, size_t n,
                   double *out_vwap, double *out_u1, double *out_u2,
                   double *out_l1,   double *out_l2);
AF_Error af_cmf  (const double *h, const double *l, const double *c,
                   const double *v, size_t n, int period, double *out);
AF_Error af_acc_dist(const double *h, const double *l, const double *c,
                      const double *v, size_t n, double *out);
AF_Error af_force_index(const double *c, const double *v, size_t n, int period, double *out);
AF_Error af_vol_osc(const double *v, size_t n, int fast, int slow, double *out);

/* ══ SUPPORT / RESISTANCE ════════════════════════════════════════════════════ */
AF_Error af_pivot_classic  (double h, double l, double c,
                              double *r3, double *r2, double *r1, double *p,
                              double *s1, double *s2, double *s3);
AF_Error af_pivot_fibonacci(double h, double l, double c,
                              double *r3, double *r2, double *r1, double *p,
                              double *s1, double *s2, double *s3);
AF_Error af_pivot_camarilla(double h, double l, double c,
                              double *r4, double *r3, double *r2, double *r1,
                              double *s1, double *s2, double *s3, double *s4);

/* fib_levels[7]: 0%,23.6%,38.2%,50%,61.8%,78.6%,100% */
AF_Error af_fibonacci(double swing_high, double swing_low, bool uptrend,
                       double *fib_levels, size_t count);

#ifdef __cplusplus
}
#endif
#endif /* AF_INDICATORS_H */
