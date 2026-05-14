/**
 * AlgoForge — src/indicators/indicator_engine.cpp
 * Runs all indicators on a bar array and produces a typed EngineResult.
 */
#include "indicators/indicator_engine.hpp"
#include "indicators/indicators.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace af {

/* ── SignalSummary ── */
std::string SignalSummary::bias() const {
    int t = total(); if (t == 0) return "NEUTRAL";
    double r = (double)bullish / t;
    if (r >= 0.75) return "STRONG_BUY";
    if (r >= 0.55) return "BUY";
    if (r <= 0.25) return "STRONG_SELL";
    if (r <= 0.45) return "SELL";
    return "NEUTRAL";
}

IndicatorEngine::IndicatorEngine() {}

/* ── Helper: extract price arrays from bars ── */
static void extract(const AF_Bar *b, size_t n,
                    double *o, double *h, double *l, double *c, double *v) {
    for (size_t i = 0; i < n; i++) {
        if (o) o[i] = b[i].open;
        if (h) h[i] = b[i].high;
        if (l) l[i] = b[i].low;
        if (c) c[i] = b[i].close;
        if (v) v[i] = b[i].volume;
    }
}

/* ── Helper: classify scalar signal ── */
static int classify(double val, double bull_thresh, double bear_thresh) {
    if (val > bull_thresh) return  1;
    if (val < bear_thresh) return -1;
    return 0;
}

/* ── Helper: add a result and update summary ── */
static void add(EngineResult &r, IndicatorResult ir) {
    if (!ir.valid) return;
    if (ir.signal ==  1) r.summary.bullish++;
    if (ir.signal == -1) r.summary.bearish++;
    if (ir.signal ==  0) r.summary.neutral++;
    r.indicators.push_back(std::move(ir));
}

EngineResult IndicatorEngine::run(const AF_Bar *bars, size_t n,
                                   const char *, AF_Timeframe) const {
    EngineResult r;
    if (!bars || n < 2) return r;

    auto *c  = new double[n]; auto *h  = new double[n];
    auto *l  = new double[n]; auto *o  = new double[n];
    auto *v  = new double[n]; auto *tmp = new double[n];
    auto *t2  = new double[n]; auto *t3 = new double[n];

    extract(bars, n, o, h, l, c, v);

    add_trend_indicators(c, h, l, v, n, r);
    add_momentum_indicators(c, h, l, v, n, r);
    add_volatility_indicators(c, h, l, n, r);
    add_volume_indicators(c, h, l, v, n, r);
    add_sr_indicators(c, h, l, n, r);

    compute_summary(r);

    delete[] c; delete[] h; delete[] l; delete[] o;
    delete[] v; delete[] tmp; delete[] t2; delete[] t3;
    return r;
}

void IndicatorEngine::add_trend_indicators(const double *c, const double *h,
                                            const double *l, const double *v,
                                            size_t n, EngineResult &r) const {
    auto *out = new double[n];
    auto *s1  = new double[n]; auto *s2 = new double[n]; auto *s3 = new double[n];

    /* EMA 9, 21, 50, 200 */
    for (int p : {9, 21, 50, 200}) {
        af_ema(c, n, p, out);
        if (!AF_IS_NAN(out[n-1]) && !AF_IS_NAN(out[n-2])) {
            IndicatorResult ir;
            ir.name  = std::string("EMA") + std::to_string(p);
            ir.valid = true;
            ir.value = out[n-1];
            /* EMA signal: price above/below EMA */
            ir.signal = (c[n-1] > out[n-1]) ? 1 : -1;
            /* EMA slope */
            ir.extra["slope"] = out[n-1] - out[n-2];
            r.indicators.push_back(ir);
            if (ir.signal ==  1) r.summary.bullish++;
            else                  r.summary.bearish++;
        }
    }

    /* SMA 20, 50 */
    for (int p : {20, 50}) {
        af_sma(c, n, p, out);
        if (!AF_IS_NAN(out[n-1])) {
            IndicatorResult ir;
            ir.name  = std::string("SMA") + std::to_string(p);
            ir.valid = true; ir.value = out[n-1];
            ir.signal = (c[n-1] > out[n-1]) ? 1 : -1;
            r.indicators.push_back(ir);
            if (ir.signal == 1) r.summary.bullish++;
            else                r.summary.bearish++;
        }
    }

    /* HMA 20 */
    af_hma(c, n, 20, out);
    if (!AF_IS_NAN(out[n-1]) && !AF_IS_NAN(out[n-2])) {
        IndicatorResult ir; ir.name="HMA20"; ir.valid=true; ir.value=out[n-1];
        ir.signal = (out[n-1] > out[n-2]) ? 1 : -1;
        add(r, ir);
    }

    /* MACD 12/26/9 */
    {
        auto *ms = new double[n]; auto *mg = new double[n]; auto *mh = new double[n];
        af_macd(c, n, 12, 26, 9, ms, mg, mh);
        if (!AF_IS_NAN(mh[n-1]) && !AF_IS_NAN(mh[n-2])) {
            IndicatorResult ir; ir.name="MACD_12_26_9"; ir.valid=true;
            ir.value=ms[n-1]; ir.value2=mg[n-1]; ir.value3=mh[n-1];
            ir.signal = (mh[n-1] > 0 && mh[n-1] > mh[n-2]) ? 1
                      : (mh[n-1] < 0 && mh[n-1] < mh[n-2]) ? -1 : 0;
            ir.extra["above_zero"] = ms[n-1] > 0 ? 1.0 : 0.0;
            add(r, ir);
        }
        delete[] ms; delete[] mg; delete[] mh;
    }

    /* ADX */
    {
        auto *adx=new double[n]; auto *pdi=new double[n]; auto *mdi=new double[n];
        af_adx(h, l, c, n, 14, adx, pdi, mdi);
        if (!AF_IS_NAN(adx[n-1])) {
            IndicatorResult ir; ir.name="ADX_14"; ir.valid=true;
            ir.value=adx[n-1]; ir.value2=pdi[n-1]; ir.value3=mdi[n-1];
            ir.signal = (pdi[n-1]>mdi[n-1] && adx[n-1]>20) ? 1
                      : (mdi[n-1]>pdi[n-1] && adx[n-1]>20) ? -1 : 0;
            r.adx_value = adx[n-1];
            add(r, ir);
        }
        delete[] adx; delete[] pdi; delete[] mdi;
    }

    /* Parabolic SAR */
    {
        auto *sar=new double[n]; auto *bull=new double[n];
        af_sar(h, l, n, 0.02, 0.02, 0.20, sar, bull);
        if (!AF_IS_NAN(sar[n-1])) {
            IndicatorResult ir; ir.name="SAR"; ir.valid=true;
            ir.value=sar[n-1];
            ir.signal = (bull[n-1] > 0.5) ? 1 : -1;
            add(r, ir);
        }
        delete[] sar; delete[] bull;
    }

    /* Ichimoku */
    {
        auto *ot=new double[n]; auto *ok=new double[n];
        auto *osa=new double[n]; auto *osb=new double[n]; auto *och=new double[n];
        af_ichimoku(h, l, c, n, 9, 26, 52, 26, ot, ok, osa, osb, och);
        if (!AF_IS_NAN(ot[n-1]) && !AF_IS_NAN(ok[n-1])) {
            IndicatorResult ir; ir.name="Ichimoku"; ir.valid=true;
            ir.value=ot[n-1]; ir.value2=ok[n-1];
            bool above_cloud = !AF_IS_NAN(osa[n-1]) && !AF_IS_NAN(osb[n-1]) &&
                               c[n-1] > std::max(osa[n-1], osb[n-1]);
            bool below_cloud = !AF_IS_NAN(osa[n-1]) && !AF_IS_NAN(osb[n-1]) &&
                               c[n-1] < std::min(osa[n-1], osb[n-1]);
            ir.signal = above_cloud ? 1 : below_cloud ? -1 : 0;
            add(r, ir);
        }
        delete[] ot; delete[] ok; delete[] osa; delete[] osb; delete[] och;
    }

    /* VWMA 20 */
    af_vwma(c, v, n, 20, out);
    if (!AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="VWMA20"; ir.valid=true; ir.value=out[n-1];
        ir.signal = (c[n-1] > out[n-1]) ? 1 : -1;
        add(r, ir);
    }

    delete[] out; delete[] s1; delete[] s2; delete[] s3;
}

void IndicatorEngine::add_momentum_indicators(const double *c, const double *h,
                                               const double *l, const double *v,
                                               size_t n, EngineResult &r) const {
    auto *out = new double[n]; auto *out2 = new double[n];

    /* RSI 14 */
    af_rsi(c, n, 14, out);
    if (!AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="RSI_14"; ir.valid=true; ir.value=out[n-1];
        ir.signal = classify(out[n-1], 55, 45);
        r.rsi_value = out[n-1];
        ir.extra["overbought"] = out[n-1] >= 70 ? 1.0 : 0.0;
        ir.extra["oversold"]   = out[n-1] <= 30 ? 1.0 : 0.0;
        add(r, ir);
    }

    /* RSI 7 */
    af_rsi(c, n, 7, out);
    if (!AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="RSI_7"; ir.valid=true; ir.value=out[n-1];
        ir.signal = classify(out[n-1], 58, 42);
        add(r, ir);
    }

    /* Stochastic 14/3/3 */
    af_stochastic(h, l, c, n, 14, 3, 3, out, out2);
    if (!AF_IS_NAN(out[n-1]) && !AF_IS_NAN(out2[n-1])) {
        IndicatorResult ir; ir.name="Stoch_14_3"; ir.valid=true;
        ir.value=out[n-1]; ir.value2=out2[n-1];
        bool k_cross_up   = out[n-1] > out2[n-1] && out[n-2] <= out2[n-2];
        bool k_cross_down = out[n-1] < out2[n-1] && out[n-2] >= out2[n-2];
        ir.signal = k_cross_up ? 1 : k_cross_down ? -1 : 0;
        add(r, ir);
    }

    /* CCI 20 */
    af_cci(h, l, c, n, 20, 0.015, out);
    if (!AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="CCI_20"; ir.valid=true; ir.value=out[n-1];
        ir.signal = classify(out[n-1], 80, -80);
        add(r, ir);
    }

    /* Williams %R */
    af_williams_r(h, l, c, n, 14, out);
    if (!AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="WilliamsR_14"; ir.valid=true; ir.value=out[n-1];
        ir.signal = classify(out[n-1], -20, -80);
        add(r, ir);
    }

    /* ROC 12 */
    af_roc(c, n, 12, out);
    if (!AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="ROC_12"; ir.valid=true; ir.value=out[n-1];
        ir.signal = classify(out[n-1], 0.1, -0.1);
        add(r, ir);
    }

    /* MFI 14 */
    af_mfi(h, l, c, v, n, 14, out);
    if (!AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="MFI_14"; ir.valid=true; ir.value=out[n-1];
        ir.signal = classify(out[n-1], 60, 40);
        add(r, ir);
    }

    /* TRIX 14 */
    af_trix(c, n, 14, 9, out, out2);
    if (!AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="TRIX_14"; ir.valid=true;
        ir.value=out[n-1]; ir.value2=out2[n-1];
        ir.signal = (out[n-1] > 0 && out[n-1] > out[n-2]) ? 1
                  : (out[n-1] < 0 && out[n-1] < out[n-2]) ? -1 : 0;
        add(r, ir);
    }

    /* Momentum 10 */
    af_momentum(c, n, 10, out);
    if (!AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="Momentum_10"; ir.valid=true; ir.value=out[n-1];
        ir.signal = classify(out[n-1], 0, 0);
        add(r, ir);
    }

    delete[] out; delete[] out2;
}

void IndicatorEngine::add_volatility_indicators(const double *c, const double *h,
                                                 const double *l, size_t n,
                                                 EngineResult &r) const {
    auto *out=new double[n]; auto *u=new double[n];
    auto *m=new double[n];   auto *lo=new double[n];
    auto *bw=new double[n];  auto *pb=new double[n];

    /* ATR 14 */
    af_atr(h, l, c, n, 14, out);
    if (!AF_IS_NAN(out[n-1])) {
        r.atr_value = out[n-1];
        IndicatorResult ir; ir.name="ATR_14"; ir.valid=true; ir.value=out[n-1];
        ir.signal = 0; /* ATR is regime info, not directional */
        r.indicators.push_back(ir);
        r.summary.neutral++;
    }

    /* Bollinger Bands 20/2 */
    af_bollinger(c, n, 20, 2.0, u, m, lo, bw, pb);
    if (!AF_IS_NAN(u[n-1])) {
        IndicatorResult ir; ir.name="BB_20_2"; ir.valid=true;
        ir.value=m[n-1]; ir.value2=u[n-1]; ir.value3=lo[n-1];
        ir.extra["bandwidth"] = bw[n-1];
        ir.extra["pctb"]      = pb[n-1];
        ir.extra["squeeze"]   = (bw[n-1] < bw[n-10]) ? 1.0 : 0.0;
        /* Signal: price near lower band = bullish, near upper = bearish */
        ir.signal = (c[n-1] <= lo[n-1]*1.001) ? 1 : (c[n-1] >= u[n-1]*0.999) ? -1 : 0;
        add(r, ir);
    }

    /* Keltner 20/10/2 */
    af_keltner(h, l, c, n, 20, 10, 2.0, u, m, lo);
    if (!AF_IS_NAN(u[n-1])) {
        IndicatorResult ir; ir.name="KC_20_2"; ir.valid=true;
        ir.value=m[n-1]; ir.value2=u[n-1]; ir.value3=lo[n-1];
        ir.signal = (c[n-1] > u[n-1]) ? 1 : (c[n-1] < lo[n-1]) ? -1 : 0;
        add(r, ir);
    }

    /* Donchian 20 */
    af_donchian(h, l, n, 20, u, m, lo);
    if (!AF_IS_NAN(u[n-1])) {
        IndicatorResult ir; ir.name="DC_20"; ir.valid=true;
        ir.value=m[n-1]; ir.value2=u[n-1]; ir.value3=lo[n-1];
        ir.signal = (c[n-1] >= u[n-1]*0.999) ? 1 : (c[n-1] <= lo[n-1]*1.001) ? -1 : 0;
        add(r, ir);
    }

    /* Hist Vol 20 */
    af_hist_vol(c, n, 20, 252, out);
    if (!AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="HistVol_20"; ir.valid=true; ir.value=out[n-1];
        ir.signal = 0;
        r.indicators.push_back(ir); r.summary.neutral++;
    }

    delete[] out; delete[] u; delete[] m; delete[] lo; delete[] bw; delete[] pb;
}

void IndicatorEngine::add_volume_indicators(const double *c, const double *h,
                                             const double *l, const double *v,
                                             size_t n, EngineResult &r) const {
    auto *out=new double[n]; auto *u1=new double[n];
    auto *u2=new double[n]; auto *l1=new double[n]; auto *l2=new double[n];

    /* OBV */
    af_obv(c, v, n, out);
    if (n >= 2 && !AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="OBV"; ir.valid=true; ir.value=out[n-1];
        ir.signal = (out[n-1] > out[n-2]) ? 1 : -1;
        ir.extra["slope"] = out[n-1]-out[n-2];
        add(r, ir);
    }

    /* VWAP */
    af_vwap(h, l, c, v, n, out, u1, u2, l1, l2);
    if (!AF_IS_NAN(out[n-1])) {
        r.vwap_value = out[n-1];
        IndicatorResult ir; ir.name="VWAP"; ir.valid=true; ir.value=out[n-1];
        ir.signal = (c[n-1] > out[n-1]) ? 1 : -1;
        add(r, ir);
    }

    /* CMF 20 */
    af_cmf(h, l, c, v, n, 20, out);
    if (!AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="CMF_20"; ir.valid=true; ir.value=out[n-1];
        ir.signal = classify(out[n-1], 0.05, -0.05);
        add(r, ir);
    }

    /* A/D Line */
    af_acc_dist(h, l, c, v, n, out);
    if (n >= 2 && !AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="AccDist"; ir.valid=true; ir.value=out[n-1];
        ir.signal = (out[n-1] > out[n-2]) ? 1 : -1;
        add(r, ir);
    }

    /* Force Index 13 */
    af_force_index(c, v, n, 13, out);
    if (!AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="ForceIdx_13"; ir.valid=true; ir.value=out[n-1];
        ir.signal = classify(out[n-1], 0, 0);
        add(r, ir);
    }

    /* Volume Oscillator 5/20 */
    af_vol_osc(v, n, 5, 20, out);
    if (!AF_IS_NAN(out[n-1])) {
        IndicatorResult ir; ir.name="VolOsc_5_20"; ir.valid=true; ir.value=out[n-1];
        ir.signal = classify(out[n-1], 2.0, -2.0);
        add(r, ir);
    }

    delete[] out; delete[] u1; delete[] u2; delete[] l1; delete[] l2;
}

void IndicatorEngine::add_sr_indicators(const double *c, const double *h,
                                         const double *l, size_t n,
                                         EngineResult &r) const {
    if (n < 2) return;
    double r3,r2,r1,p,s1,s2,s3;
    double prev_h=h[n-2], prev_l=l[n-2], prev_c=c[n-2];

    /* Classic Pivots */
    af_pivot_classic(prev_h, prev_l, prev_c, &r3,&r2,&r1,&p,&s1,&s2,&s3);
    {
        IndicatorResult ir; ir.name="Pivot_Classic"; ir.valid=true; ir.value=p;
        ir.value2=r1; ir.value3=s1;
        ir.extra["R2"]=r2; ir.extra["R3"]=r3; ir.extra["S2"]=s2; ir.extra["S3"]=s3;
        ir.signal = (c[n-1] > p) ? 1 : -1;
        add(r, ir);
    }

    /* Fibonacci Pivots */
    af_pivot_fibonacci(prev_h, prev_l, prev_c, &r3,&r2,&r1,&p,&s1,&s2,&s3);
    {
        IndicatorResult ir; ir.name="Pivot_Fib"; ir.valid=true; ir.value=p;
        ir.value2=r1; ir.value3=s1;
        ir.signal = (c[n-1] > p) ? 1 : -1;
        add(r, ir);
    }

    /* Fibonacci Levels from 50-bar swing */
    if (n >= 50) {
        double sh=h[n-50], sl=l[n-50];
        for (size_t i=n-49;i<n;i++){if(h[i]>sh)sh=h[i];if(l[i]<sl)sl=l[i];}
        bool up = c[n-1] > (sh+sl)/2;
        double fibs[7]; af_fibonacci(sh, sl, up, fibs, 7);
        /* Find nearest Fib level */
        double nearest=fibs[0], min_dist=std::abs(c[n-1]-fibs[0]);
        for (int i=1;i<7;i++){double d=std::abs(c[n-1]-fibs[i]);if(d<min_dist){min_dist=d;nearest=fibs[i];}}
        IndicatorResult ir; ir.name="FibLevels_50"; ir.valid=true; ir.value=nearest;
        ir.signal = (c[n-1] > nearest) ? 1 : -1;
        add(r, ir);
    }
}

void IndicatorEngine::compute_summary(EngineResult &r) const {
    r.bias = r.summary.bias();
}

} /* namespace af */
