/**
 * AlgoForge — tests/test_indicators.cpp
 * Unit tests for all indicator math functions.
 */
#include "test_helpers.hpp"
#include "indicators/indicators.h"
#include "indicators/indicator_engine.hpp"
#include "core/types.h"
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>



/* ── Data generation ── */
static std::vector<double> make_uptrend(int n, double start=1.0, double drift=0.002) {
    std::vector<double> v(n); v[0]=start;
    srand(42);
    for (int i=1;i<n;i++) v[i]=v[i-1]+drift+(rand()%1000-500)*0.0000008;
    return v;
}
static std::vector<double> make_downtrend(int n, double start=1.2, double drift=-0.002) {
    return make_uptrend(n, start, drift);
}
static std::vector<double> make_flat(int n, double base=1.1) {
    std::vector<double> v(n);
    srand(7);
    for (int i=0;i<n;i++) v[i]=base+(rand()%200-100)*0.000001;
    return v;
}
static std::vector<double> make_const(int n, double val=1.1) {
    return std::vector<double>(n, val);
}

static AF_BarArray make_bar_array(int n, const char *sym="EURUSD",
                                      AF_Timeframe tf=AF_TF_H1,
                                      double drift=0.002) {
    auto close = make_uptrend(n, 1.1, drift);
    auto *bars = new AF_Bar[n]();
    for (int i=0;i<n;i++) {
        bars[i].close   = close[i];
        bars[i].open    = close[i]*(0.9998 + (rand()%4)*0.0001);
        bars[i].high    = close[i]*1.0005;
        bars[i].low     = close[i]*0.9995;
        bars[i].volume  = 1000+(rand()%4000);
        bars[i].spread  = 0.0001;
        af_bar_init(&bars[i]);
    }
    AF_BarArray arr;
    arr.bars = bars; arr.count = n;
    snprintf(arr.symbol, sizeof(arr.symbol), "%s", sym);
    arr.tf = tf;
    return arr;
}

void test_indicators(TestRunner T) {
    section("INDICATORS");

    /* ── SMA ── */
    T("SMA: output length matches input", []{
        auto v = make_uptrend(100); double out[100];
        CHK(af_sma(v.data(),100,20,out)==AF_OK);
        for (int i=0;i<19;i++) CHK(std::isnan(out[i]));
        CHK_FALSE(std::isnan(out[19]));
    });

    T("SMA: short data returns INSUFFICIENT_DATA", []{
        double src[5]={1,2,3,4,5}, out[5];
        CHK(af_sma(src,5,20,out)==AF_ERR_INSUFFICIENT_DATA);
    });

    T("SMA: value is arithmetic mean of last period bars", []{
        double src[10]={1,2,3,4,5,6,7,8,9,10}, out[10];
        af_sma(src,10,5,out);
        CHK_NEAR(out[9], 8.0, 1e-9); /* avg(6,7,8,9,10)=8 */
    });

    /* ── EMA ── */
    T("EMA: responds faster than SMA (smaller lag to price)", []{
        auto c = make_uptrend(200); double e[200], s[200];
        af_ema(c.data(),200,20,e); af_sma(c.data(),200,20,s);
        double price=c[199];
        CHK(std::abs(e[199]-price) <= std::abs(s[199]-price)+1e-5);
    });

    T("EMA: returns INSUFFICIENT_DATA for tiny input", []{
        double src[5]={1,2,3,4,5}, out[5];
        CHK(af_ema(src,5,20,out)==AF_ERR_INSUFFICIENT_DATA);
    });

    /* ── WMA / HMA / DEMA / TEMA ── */
    T("WMA: valid output for 100 bars", []{
        auto v=make_uptrend(100); double out[100];
        CHK(af_wma(v.data(),100,20,out)==AF_OK);
        CHK_FALSE(std::isnan(out[99]));
    });
    T("HMA: valid and non-NaN at end", []{
        auto v=make_uptrend(200); double out[200];
        CHK(af_hma(v.data(),200,20,out)==AF_OK);
        CHK_FALSE(std::isnan(out[199]));
    });
    T("DEMA: valid for 100 bars", []{
        auto v=make_uptrend(100); double out[100];
        CHK(af_dema(v.data(),100,20,out)==AF_OK);
        CHK_FALSE(std::isnan(out[99]));
    });
    T("TEMA: valid for 100 bars", []{
        auto v=make_uptrend(100); double out[100];
        CHK(af_tema(v.data(),100,20,out)==AF_OK);
        CHK_FALSE(std::isnan(out[99]));
    });

    /* ── MACD ── */
    T("MACD: histogram = MACD - signal", []{
        auto v=make_uptrend(100); double m[100],s[100],h[100];
        CHK(af_macd(v.data(),100,12,26,9,m,s,h)==AF_OK);
        CHK_NEAR(h[99], m[99]-s[99], 1e-10);
    });

    /* ── ADX ── */
    T("ADX: positive value in trending market", []{
        auto h=make_uptrend(200,1.105,0.003);
        auto l=make_uptrend(200,1.095,0.003);
        auto c=make_uptrend(200,1.100,0.003);
        double adx[200],pdi[200],mdi[200];
        CHK(af_adx(h.data(),l.data(),c.data(),200,14,adx,pdi,mdi)==AF_OK);
        CHK_GT(adx[199],0);
        CHK_GT(pdi[199],mdi[199]); /* uptrend: +DI > -DI */
    });

    /* ── RSI ── */
    T("RSI: always in [0,100]", []{
        { auto v=make_uptrend(100);   double out[100];
          CHK(af_rsi(v.data(),100,14,out)==AF_OK);
          CHK_FALSE(std::isnan(out[99])); CHK(out[99]>=0&&out[99]<=100); }
        { auto v=make_downtrend(100); double out[100];
          CHK(af_rsi(v.data(),100,14,out)==AF_OK);
          CHK_FALSE(std::isnan(out[99])); CHK(out[99]>=0&&out[99]<=100); }
        { auto v=make_flat(100);      double out[100];
          CHK(af_rsi(v.data(),100,14,out)==AF_OK);
          CHK_FALSE(std::isnan(out[99])); CHK(out[99]>=0&&out[99]<=100); }
    });
    T("RSI: lower in downtrend than uptrend", []{
        auto up=make_uptrend(100);   double r_up[100];
        auto dn=make_downtrend(100); double r_dn[100];
        af_rsi(up.data(),100,14,r_up);
        af_rsi(dn.data(),100,14,r_dn);
        CHK_GT(r_up[99], r_dn[99]);
    });

    /* ── Stochastic ── */
    T("Stochastic: K and D in [0,100]", []{
        auto h=make_uptrend(100,1.105,0.002);
        auto l=make_uptrend(100,1.095,0.002);
        auto c=make_uptrend(100,1.100,0.002);
        double k[100],d[100];
        CHK(af_stochastic(h.data(),l.data(),c.data(),100,14,3,3,k,d)==AF_OK);
        CHK(k[99]>=0 && k[99]<=100);
        CHK(d[99]>=0 && d[99]<=100);
    });

    /* ── ATR ── */
    T("ATR: always positive", []{
        auto h=make_uptrend(100,1.105,0.002);
        auto l=make_uptrend(100,1.095,0.002);
        auto c=make_uptrend(100,1.100,0.002);
        double out[100];
        CHK(af_atr(h.data(),l.data(),c.data(),100,14,out)==AF_OK);
        CHK_GT(out[99],0);
    });

    /* ── Bollinger Bands ── */
    T("Bollinger: upper > mid > lower", []{
        auto v=make_uptrend(100); double u[100],m[100],lo[100],bw[100],pb[100];
        CHK(af_bollinger(v.data(),100,20,2.0,u,m,lo,bw,pb)==AF_OK);
        CHK_GT(u[99],m[99]); CHK_GT(m[99],lo[99]);
    });

    /* ── Donchian ── */
    T("Donchian: upper >= mid >= lower", []{
        auto h=make_uptrend(100,1.105,0.002);
        auto l=make_uptrend(100,1.095,0.002);
        double u[100],m[100],lo[100];
        CHK(af_donchian(h.data(),l.data(),100,20,u,m,lo)==AF_OK);
        CHK(u[99]>=m[99] && m[99]>=lo[99]);
    });

    /* ── OBV ── */
    T("OBV: rises in consistent uptrend", []{
        int n=100;
        std::vector<double> c(n),v(n);
        c[0]=1.0; for(int i=1;i<n;i++) c[i]=c[i-1]+0.001;
        for(int i=0;i<n;i++) v[i]=1000;
        double out[100];
        CHK(af_obv(c.data(),v.data(),n,out)==AF_OK);
        CHK_GT(out[n-1],out[0]);
    });

    /* ── VWAP ── */
    T("VWAP: value between period high and low", []{
        auto h=make_uptrend(100,1.105,0.002);
        auto l=make_uptrend(100,1.095,0.002);
        auto c=make_uptrend(100,1.100,0.002);
        auto v=make_const (100,1000.0);
        double vw[100],u1[100],u2[100],l1[100],l2[100];
        CHK(af_vwap(h.data(),l.data(),c.data(),v.data(),100,vw,u1,u2,l1,l2)==AF_OK);
        double lo=*std::min_element(l.begin(),l.end());
        double hi=*std::max_element(h.begin(),h.end());
        CHK(vw[99]>=lo && vw[99]<=hi);
    });

    /* ── Pivot Points ── */
    T("Pivot: R1 > P > S1 (classic)", []{
        double r3,r2,r1,p,s1,s2,s3;
        CHK(af_pivot_classic(1.2,1.0,1.1,&r3,&r2,&r1,&p,&s1,&s2,&s3)==AF_OK);
        CHK_GT(r1,p); CHK_GT(p,s1);
    });

    T("Pivot: R1 > P > S1 (fibonacci)", []{
        double r3,r2,r1,p,s1,s2,s3;
        CHK(af_pivot_fibonacci(1.2,1.0,1.1,&r3,&r2,&r1,&p,&s1,&s2,&s3)==AF_OK);
        CHK_GT(r1,p); CHK_GT(p,s1);
    });

    /* ── Fibonacci retracement ── */
    T("Fibonacci: 7 levels filled, 0% level = swing_high in uptrend", []{
        double fl[7];
        CHK(af_fibonacci(1.2, 1.0, true, fl, 7)==AF_OK);
        CHK_NEAR(fl[0], 1.2, 1e-9); /* 0% = high */
        CHK_NEAR(fl[6], 1.0, 1e-9); /* 100% = low */
        CHK(fl[3] > fl[6] && fl[3] < fl[0]); /* 50% between */
    });

    /* ── Indicator Engine ── */
    T("IndicatorEngine: runs all 38 indicators, returns valid summary", []{
        af::IndicatorEngine eng;
        auto arr = make_bar_array(300);
        auto res = eng.run(arr.bars, arr.count, arr.symbol, arr.tf);
        delete[] arr.bars;
        CHK(res.summary.total() > 20);
        CHK(res.atr_value > 0);
        CHK_FALSE(res.bias.empty());
    });

    T("IndicatorEngine: uptrend → bullish bias", []{
        af::IndicatorEngine eng;
        auto arr = make_bar_array(300, "EURUSD", AF_TF_H1, 0.003);
        auto res = eng.run(arr.bars, arr.count, arr.symbol, arr.tf);
        delete[] arr.bars;
        CHK(res.summary.bullish >= res.summary.bearish);
    });

    T("IndicatorEngine: downtrend → bearish bias", []{
        af::IndicatorEngine eng;
        auto arr = make_bar_array(300, "EURUSD", AF_TF_H1, -0.003);
        auto res = eng.run(arr.bars, arr.count, arr.symbol, arr.tf);
        delete[] arr.bars;
        CHK(res.summary.bearish >= res.summary.bullish);
    });

    T("IndicatorEngine: zero-volume bars handled gracefully", []{
        af::IndicatorEngine eng;
        auto arr = make_bar_array(200);
        for (size_t i=0;i<arr.count;i++) arr.bars[i].volume=0;
        auto res = eng.run(arr.bars, arr.count, arr.symbol, arr.tf);
        delete[] arr.bars;
        CHK(res.summary.total() > 0);
    });

    T("IndicatorEngine: identical OHLC bars (zero range) handled", []{
        af::IndicatorEngine eng;
        int n=200;
        auto *bars = new AF_Bar[n]();
        for (int i=0;i<n;i++){
            bars[i].open=bars[i].high=bars[i].low=bars[i].close=1.1;
            bars[i].volume=1000; af_bar_init(&bars[i]);
        }
        auto res = eng.run(bars, n, "EURUSD", AF_TF_H1);
        delete[] bars;
        CHK(res.summary.total() >= 0);
    });

    T("IndicatorEngine: short data (< 20 bars) does not crash", []{
        af::IndicatorEngine eng;
        auto arr = make_bar_array(10);
        auto res = eng.run(arr.bars, arr.count, arr.symbol, arr.tf);
        delete[] arr.bars;
        CHK(res.summary.total() >= 0);
    });
}
