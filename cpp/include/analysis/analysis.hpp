/**
 * AlgoForge — include/analysis/analysis.hpp
 * Market structure, confluence scoring, and filter types.
 */
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "core/types.h"
#include "indicators/indicator_engine.hpp"
#include "patterns/pattern_types.h"

namespace af {

/* ── Market Structure ── */
enum class TrendState {
    STRONG_BULL, BULL, RANGING, BEAR, STRONG_BEAR
};
const char* trend_state_name(TrendState s);

struct SwingPoint {
    size_t index; double price; bool is_high;
};

struct StructureResult {
    TrendState  trend;
    int         signal;       /* 1, 0, -1 */
    double      confidence;
    bool        bos;          /* break of structure */
    bool        choch;        /* change of character */
    double      last_swing_high;
    double      last_swing_low;
    std::vector<SwingPoint> swing_highs;
    std::vector<SwingPoint> swing_lows;
};

class MarketStructure {
public:
    explicit MarketStructure(int swing_strength = 3);
    StructureResult analyse(const AF_Bar *bars, size_t n) const;
private:
    int swing_strength_;
};

/* ── Trend Classifier ── */
enum class TrendBias {
    STRONG_BULL, BULL, NEUTRAL, BEAR, STRONG_BEAR
};
const char* trend_bias_name(TrendBias b);

struct TrendClassification {
    TrendBias bias;
    int       signal;
    double    confidence;
    double    adx;
    bool      trending;
};

class TrendClassifier {
public:
    TrendClassification classify(const AF_Bar *bars, size_t n) const;
};

/* ── Confluence Score ── */
struct ConfluenceScore {
    std::string symbol, timeframe;
    double      score;      /* 0–100 */
    int         direction;  /* 1, 0, -1 */
    char        grade;      /* A B C D F */
    double      confidence;
    int         ind_bull, ind_bear;
    int         pat_bull,  pat_bear;
    int         structure_signal;
    int         trend_signal;
    std::vector<std::string> reasons;

    bool  is_tradeable()  const { return score >= 60 && direction != 0; }
    const char* bias_label() const;
};

class ConfluenceScorer {
public:
    ConfluenceScore score_timeframe(
        const char            *symbol,
        const char            *timeframe,
        const SignalSummary   *ind_summary   = nullptr,
        const AF_PatternResult*pat_result    = nullptr,
        int  structure_signal  = 0, double struct_conf  = 0,
        int  trend_signal      = 0, double trend_conf   = 0
    ) const;
};

/* ── Session Filter ── */
struct SessionScore {
    const char *session;   /* "OVERLAP","LONDON","NY","ASIA","DEAD" */
    double quality;        /* 0–1 */
    bool   tradeable;
    char   reason[128];
    int    hour_utc;
    int    day_of_week;
};

SessionScore eval_session(const char *symbol, int hour_utc, int day_of_week);

/* ── Volatility Regime ── */
enum class VolRegime {
    HIGH_TRENDING, HIGH_RANGING, LOW_TRENDING, LOW_RANGING, SQUEEZE, EXPANDING
};
const char* vol_regime_name(VolRegime r);

struct RegimeResult {
    VolRegime   regime;
    double      atr_percentile;
    bool        bb_squeeze;
    double      adx;
    bool        trending;
    double      size_multiplier;
};

RegimeResult classify_regime(const AF_Bar *bars, size_t n);

} /* namespace af */
