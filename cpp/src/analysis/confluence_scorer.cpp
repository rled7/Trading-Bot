/**
 * AlgoForge — src/analysis/confluence_scorer.cpp
 * Confluence scoring, session filter, volatility regime classifier.
 */
#include "analysis/analysis.hpp"
#include "indicators/indicators.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace af {

/* ── Pattern category weights ── */
static double pat_weight(AF_PatternCategory cat) {
    switch(cat){
        case AF_PAT_CANDLESTICK: return 0.60;
        case AF_PAT_CHART:       return 0.90;
        case AF_PAT_HARMONIC:    return 0.85;
        case AF_PAT_ELLIOTT:     return 0.80;
        case AF_PAT_WYCKOFF:     return 1.00;
        case AF_PAT_VOLUME:      return 0.75;
    }
    return 0.75;
}

/* ── ConfluenceScore helpers ── */
const char* ConfluenceScore::bias_label() const {
    if (score>=80 && direction== 1) return "STRONG BUY";
    if (score>=60 && direction== 1) return "BUY";
    if (score>=80 && direction==-1) return "STRONG SELL";
    if (score>=60 && direction==-1) return "SELL";
    return "NEUTRAL";
}

ConfluenceScore ConfluenceScorer::score_timeframe(
    const char *symbol, const char *timeframe,
    const SignalSummary *ind_sum, const AF_PatternResult *pat,
    int struct_sig, double struct_conf,
    int trend_sig,  double trend_conf) const
{
    ConfluenceScore sc{};
    sc.symbol    = symbol ? symbol : "";
    sc.timeframe = timeframe ? timeframe : "";

    double bull_pts=0, bear_pts=0;

    /* Indicator signals */
    if (ind_sum) {
        sc.ind_bull = ind_sum->bullish;
        sc.ind_bear = ind_sum->bearish;
        bull_pts += sc.ind_bull;
        bear_pts += sc.ind_bear;
    }

    /* Pattern signals */
    if (pat) {
        sc.pat_bull = pat->bullish_count;
        sc.pat_bear = pat->bearish_count;
        for (int i=0;i<pat->count;i++){
            const AF_PatternMatch &m = pat->matches[i];
            double w = pat_weight(m.category) * m.confidence * 3.0;
            if (m.direction == AF_PAT_DIR_BULLISH) bull_pts += w;
            if (m.direction == AF_PAT_DIR_BEARISH) bear_pts += w;
        }
    }

    /* Structure */
    sc.structure_signal = struct_sig;
    if (struct_sig ==  1) bull_pts += struct_conf * 5.0;
    if (struct_sig == -1) bear_pts += struct_conf * 5.0;

    /* Trend */
    sc.trend_signal = trend_sig;
    if (trend_sig ==  1) bull_pts += trend_conf * 4.0;
    if (trend_sig == -1) bear_pts += trend_conf * 4.0;

    double total = bull_pts + bear_pts;
    if (total < 1e-10) {
        sc.score=0; sc.direction=0; sc.grade='F'; sc.confidence=0;
        return sc;
    }

    double bull_pct = bull_pts / total * 100.0;
    double bear_pct = bear_pts / total * 100.0;
    sc.score        = std::max(bull_pct, bear_pct);
    sc.direction    = (bull_pts >= bear_pts) ? 1 : -1;

    double margin = std::abs(bull_pct - bear_pct);
    if (margin < 10.0) sc.direction = 0;

    sc.grade      = sc.score >= 80 ? 'A'
                  : sc.score >= 65 ? 'B'
                  : sc.score >= 50 ? 'C'
                  : sc.score >= 35 ? 'D' : 'F';
    sc.confidence = std::min(1.0, sc.score/100.0*(1+margin/200.0));
    return sc;
}

/* ── Session Filter ── */
static const double SESS_DEFAULT[24] = {
    0.30,0.25,0.20,0.20,0.25,0.35,
    0.55,0.80,0.85,0.90,0.90,0.90,
    1.00,1.00,1.00,0.95,0.85,0.70,
    0.55,0.45,0.40,0.30,0.20,0.25
};
static const double SESS_USDJPY[24] = {
    0.80,0.85,0.85,0.80,0.75,0.70,
    0.75,0.90,0.95,0.80,0.70,0.65,
    0.85,0.90,0.85,0.75,0.60,0.50,
    0.40,0.35,0.30,0.40,0.55,0.70
};
static const double SESS_XAUUSD[24] = {
    0.35,0.30,0.25,0.25,0.30,0.40,
    0.60,0.75,0.85,0.90,0.90,0.90,
    1.00,1.00,0.95,0.85,0.70,0.50,
    0.40,0.35,0.30,0.25,0.20,0.25
};

SessionScore eval_session(const char *symbol, int hour_utc, int dow) {
    SessionScore s{};
    s.hour_utc    = hour_utc;
    s.day_of_week = dow;

    if (dow == 6) {
        s.session="DEAD"; s.quality=0.0; s.tradeable=false;
        snprintf(s.reason,sizeof(s.reason),"Sunday — thin market");
        return s;
    }
    if (dow == 4 && hour_utc >= 20) {
        s.session="DEAD"; s.quality=0.0; s.tradeable=false;
        snprintf(s.reason,sizeof(s.reason),"Friday 20:00+ UTC — weekend gap risk");
        return s;
    }

    bool is_jpy = symbol && (strstr(symbol,"JPY")!=nullptr);
    bool dead   = is_jpy ? (hour_utc==22||hour_utc==23)
                         : (hour_utc>=22||hour_utc<=3);
    if (dead) {
        s.session="DEAD"; s.quality=0.05; s.tradeable=false;
        snprintf(s.reason,sizeof(s.reason),"Dead hours %02d:00 UTC",hour_utc);
        return s;
    }

    const double *map = (symbol && strstr(symbol,"JPY")) ? SESS_USDJPY
                       :(symbol && strstr(symbol,"XAU")) ? SESS_XAUUSD
                       : SESS_DEFAULT;
    s.quality = map[hour_utc];

    if      (hour_utc>=12 && hour_utc<=16) s.session="OVERLAP";
    else if (hour_utc>= 7 && hour_utc<=11) s.session="LONDON";
    else if (hour_utc>=17 && hour_utc<=20) s.session="NY";
    else                                    s.session="ASIA";

    s.tradeable = s.quality >= 0.45;
    snprintf(s.reason,sizeof(s.reason),"%s session quality=%.0f%%",
             s.session, s.quality*100);
    return s;
}

/* ── Volatility Regime ── */
const char* vol_regime_name(VolRegime r) {
    switch(r){
        case VolRegime::HIGH_TRENDING: return "HIGH_TRENDING";
        case VolRegime::HIGH_RANGING:  return "HIGH_RANGING";
        case VolRegime::LOW_TRENDING:  return "LOW_TRENDING";
        case VolRegime::LOW_RANGING:   return "LOW_RANGING";
        case VolRegime::SQUEEZE:       return "SQUEEZE";
        case VolRegime::EXPANDING:     return "EXPANDING";
    }
    return "LOW_RANGING";
}

RegimeResult classify_regime(const AF_Bar *bars, size_t n) {
    RegimeResult r{};
    r.regime         = VolRegime::LOW_RANGING;
    r.size_multiplier= 0.80;

    if (!bars || n < 100) return r;

    auto *h=new double[n],*l=new double[n],*c=new double[n];
    for (size_t i=0;i<n;i++){h[i]=bars[i].high;l[i]=bars[i].low;c[i]=bars[i].close;}

    /* ATR */
    double *atr=new double[n];
    af_atr(h,l,c,n,14,atr);
    double atr_now = !std::isnan(atr[n-1]) ? atr[n-1] : 0.001;
    size_t look = std::min(n,(size_t)100);
    int less_than=0;
    for (size_t i=n-look;i<n;i++) if (!std::isnan(atr[i])&&atr[i]<atr_now) less_than++;
    r.atr_percentile = (double)less_than / (double)look * 100.0;

    /* BB bandwidth for squeeze */
    double *bu=new double[n],*bm=new double[n],*bl=new double[n],*bw=new double[n];
    af_bollinger(c,n,20,2.0,bu,bm,bl,bw,nullptr);
    double bw_now = !std::isnan(bw[n-1]) ? bw[n-1] : 1.0;
    double bw_min = bw_now;
    for (size_t i=n-std::min(n,(size_t)100);i<n;i++)
        if (!std::isnan(bw[i])&&bw[i]<bw_min) bw_min=bw[i];
    r.bb_squeeze = (bw_now <= bw_min * 1.05);

    double bw_slope = (n>=5&&!std::isnan(bw[n-1])&&!std::isnan(bw[n-5]))
                      ? bw[n-1]-bw[n-5] : 0;
    bool expanding = (bw_slope>0) && !r.bb_squeeze;

    /* ADX */
    double *adx=new double[n],*pdi=new double[n],*mdi=new double[n];
    af_adx(h,l,c,n,14,adx,pdi,mdi);
    r.adx      = !std::isnan(adx[n-1]) ? adx[n-1] : 20;
    r.trending = r.adx >= 23.0;

    if      (r.bb_squeeze)                            { r.regime=VolRegime::SQUEEZE;       r.size_multiplier=0.0;  }
    else if (expanding && r.atr_percentile>50)        { r.regime=VolRegime::EXPANDING;     r.size_multiplier=1.2;  }
    else if (r.atr_percentile>=65 && r.trending)     { r.regime=VolRegime::HIGH_TRENDING; r.size_multiplier=1.0;  }
    else if (r.atr_percentile>=65 && !r.trending)    { r.regime=VolRegime::HIGH_RANGING;  r.size_multiplier=0.3;  }
    else if (r.atr_percentile<=35 && r.trending)     { r.regime=VolRegime::LOW_TRENDING;  r.size_multiplier=0.75; }
    else                                              { r.regime=VolRegime::LOW_RANGING;   r.size_multiplier=0.80; }

    delete[]h;delete[]l;delete[]c;delete[]atr;
    delete[]bu;delete[]bm;delete[]bl;delete[]bw;
    delete[]adx;delete[]pdi;delete[]mdi;
    return r;
}

} /* namespace af */
