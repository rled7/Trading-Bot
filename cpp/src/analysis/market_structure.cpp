/**
 * AlgoForge — src/analysis/market_structure.cpp
 * HH/HL/LH/LL swing detection, BOS, ChoCH, trend classification.
 */
#include "analysis/analysis.hpp"
#include "indicators/indicators.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace af {

const char* trend_state_name(TrendState s) {
    switch(s){
        case TrendState::STRONG_BULL: return "STRONG_BULL";
        case TrendState::BULL:        return "BULL";
        case TrendState::RANGING:     return "RANGING";
        case TrendState::BEAR:        return "BEAR";
        case TrendState::STRONG_BEAR: return "STRONG_BEAR";
    }
    return "RANGING";
}

const char* trend_bias_name(TrendBias b) {
    switch(b){
        case TrendBias::STRONG_BULL: return "STRONG_BULL";
        case TrendBias::BULL:        return "BULL";
        case TrendBias::NEUTRAL:     return "NEUTRAL";
        case TrendBias::BEAR:        return "BEAR";
        case TrendBias::STRONG_BEAR: return "STRONG_BEAR";
    }
    return "NEUTRAL";
}

MarketStructure::MarketStructure(int swing_strength)
    : swing_strength_(swing_strength) {}

StructureResult MarketStructure::analyse(const AF_Bar *bars, size_t n) const {
    StructureResult r{};
    r.trend      = TrendState::RANGING;
    r.signal     = 0;
    r.confidence = 0.4;

    if (!bars || (int)n < swing_strength_ * 4) return r;

    int s = swing_strength_;

    /* Detect swing highs and lows */
    for (size_t i = (size_t)s; i < n - (size_t)s; i++) {
        bool is_sh = true, is_sl = true;
        for (int j = 1; j <= s; j++) {
            if (bars[i].high <= bars[i-j].high || bars[i].high <= bars[i+j].high) is_sh=false;
            if (bars[i].low  >= bars[i-j].low  || bars[i].low  >= bars[i+j].low)  is_sl=false;
        }
        if (is_sh) r.swing_highs.push_back({i, bars[i].high, true});
        if (is_sl) r.swing_lows.push_back ({i, bars[i].low,  false});
    }

    if (r.swing_highs.size() < 2 || r.swing_lows.size() < 2) return r;

    /* Last 2–3 swings */
    auto &sh = r.swing_highs;
    auto &sl = r.swing_lows;
    bool hh = sh.back().price > sh[sh.size()-2].price;
    bool hl = sl.back().price > sl[sl.size()-2].price;
    bool lh = sh.back().price < sh[sh.size()-2].price;
    bool ll = sl.back().price < sl[sl.size()-2].price;

    if      (hh && hl) { r.trend=TrendState::STRONG_BULL; r.signal= 1; r.confidence=0.90; }
    else if (hh || hl) { r.trend=TrendState::BULL;        r.signal= 1; r.confidence=0.70; }
    else if (ll && lh) { r.trend=TrendState::STRONG_BEAR; r.signal=-1; r.confidence=0.90; }
    else if (ll || lh) { r.trend=TrendState::BEAR;        r.signal=-1; r.confidence=0.70; }
    else               { r.trend=TrendState::RANGING;     r.signal= 0; r.confidence=0.40; }

    r.last_swing_high = sh.back().price;
    r.last_swing_low  = sl.back().price;

    /* Break of Structure */
    double cur = bars[n-1].close;
    if (r.trend==TrendState::BEAR || r.trend==TrendState::STRONG_BEAR)
        r.bos = r.choch = (cur > sh.back().price);
    else if (r.trend==TrendState::BULL || r.trend==TrendState::STRONG_BULL)
        r.bos = r.choch = (cur < sl.back().price);

    if (r.choch) r.signal = -r.signal;

    return r;
}

/* ── TrendClassifier ── */
TrendClassification TrendClassifier::classify(const AF_Bar *bars, size_t n) const {
    TrendClassification r{};
    r.bias       = TrendBias::NEUTRAL;
    r.signal     = 0;
    r.confidence = 0.4;

    if (!bars || n < 200) return r;

    auto *c  = new double[n];
    for (size_t i=0;i<n;i++) c[i]=bars[i].close;

    double *e9=new double[n],*e21=new double[n],
           *e50=new double[n],*e200=new double[n];

    af_ema(c,n,  9,e9);
    af_ema(c,n, 21,e21);
    af_ema(c,n, 50,e50);
    af_ema(c,n,200,e200);

    /* ADX */
    auto *h=new double[n],*l=new double[n];
    for (size_t i=0;i<n;i++){h[i]=bars[i].high;l[i]=bars[i].low;}
    double *adx=new double[n],*pdi=new double[n],*mdi=new double[n];
    af_adx(h,l,c,n,14,adx,pdi,mdi);

    double price=c[n-1];
    int bull_score=0, bear_score=0;

    if (!std::isnan(e9[n-1])  && price>e9[n-1])   bull_score++;
    if (!std::isnan(e9[n-1])  && price<e9[n-1])   bear_score++;
    if (!std::isnan(e21[n-1]) && price>e21[n-1])  bull_score++;
    if (!std::isnan(e21[n-1]) && price<e21[n-1])  bear_score++;
    if (!std::isnan(e50[n-1]) && price>e50[n-1])  bull_score++;
    if (!std::isnan(e50[n-1]) && price<e50[n-1])  bear_score++;
    if (!std::isnan(e200[n-1])&& price>e200[n-1]) bull_score++;
    if (!std::isnan(e200[n-1])&& price<e200[n-1]) bear_score++;
    /* EMA alignment */
    if (!std::isnan(e9[n-1])&&!std::isnan(e21[n-1])&&e9[n-1]>e21[n-1]) bull_score++;
    if (!std::isnan(e9[n-1])&&!std::isnan(e21[n-1])&&e9[n-1]<e21[n-1]) bear_score++;
    if (!std::isnan(e21[n-1])&&!std::isnan(e50[n-1])&&e21[n-1]>e50[n-1]) bull_score++;
    if (!std::isnan(e21[n-1])&&!std::isnan(e50[n-1])&&e21[n-1]<e50[n-1]) bear_score++;
    if (!std::isnan(e50[n-1])&&!std::isnan(e200[n-1])&&e50[n-1]>e200[n-1]) bull_score++;
    if (!std::isnan(e50[n-1])&&!std::isnan(e200[n-1])&&e50[n-1]<e200[n-1]) bear_score++;

    r.adx      = std::isnan(adx[n-1]) ? 0 : adx[n-1];
    r.trending = r.adx > 20.0;
    int net    = bull_score - bear_score;

    if      (net>=6 && r.trending) { r.bias=TrendBias::STRONG_BULL; r.signal= 1; }
    else if (net>=3)                { r.bias=TrendBias::BULL;        r.signal= 1; }
    else if (net<=-6&&r.trending)  { r.bias=TrendBias::STRONG_BEAR; r.signal=-1; }
    else if (net<=-3)              { r.bias=TrendBias::BEAR;        r.signal=-1; }
    else                           { r.bias=TrendBias::NEUTRAL;     r.signal= 0; }

    r.confidence = std::min(0.95, 0.40 + std::abs(net)*0.07 + (r.trending?0.10:0));

    delete[]c;delete[]e9;delete[]e21;delete[]e50;delete[]e200;
    delete[]h;delete[]l;delete[]adx;delete[]pdi;delete[]mdi;
    return r;
}

} /* namespace af */
