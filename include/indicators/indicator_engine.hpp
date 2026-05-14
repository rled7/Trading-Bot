/**
 * AlgoForge — include/indicators/indicator_engine.hpp
 * C++ wrapper around the C indicator library. Runs all indicators on a bar
 * array and returns a typed result with signals and scalar values.
 */
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include "core/types.h"

namespace af {

/* ── Signal summary ── */
struct SignalSummary {
    int bullish{0}, bearish{0}, neutral{0};
    int total()    const { return bullish + bearish + neutral; }
    int net()      const { return bullish - bearish; }
    std::string bias() const;
};

/* ── Single indicator result ── */
struct IndicatorResult {
    std::string  name;
    bool         valid{false};
    int          signal{0};    /* +1 / 0 / -1 */
    double       value{0};     /* primary scalar */
    double       value2{0};    /* secondary (e.g. signal line) */
    double       value3{0};    /* tertiary (e.g. histogram) */

    /* Additional named scalars */
    std::unordered_map<std::string, double> extra;
};

/* ── Full engine output ── */
struct EngineResult {
    std::vector<IndicatorResult> indicators;
    SignalSummary                summary;
    double                       atr_value{0};   /* current ATR — used by risk module */
    double                       adx_value{0};
    double                       rsi_value{50};
    double                       vwap_value{0};
    std::string                  bias;            /* "STRONG_BUY" … "STRONG_SELL" */

    std::optional<IndicatorResult> get(const std::string &name) const {
        for (auto &r : indicators) if (r.name == name) return r;
        return std::nullopt;
    }
    double latest(const std::string &name, double fallback = 0.0) const {
        auto r = get(name);
        return r ? r->value : fallback;
    }
};

/* ── Indicator engine ── */
class IndicatorEngine {
public:
    IndicatorEngine();

    /**
     * Run all indicators on the provided bar array.
     * bars[0] = oldest, bars[count-1] = newest.
     */
    EngineResult run(const AF_Bar *bars, size_t count,
                     const char *symbol = "UNKNOWN",
                     AF_Timeframe tf = AF_TF_H1) const;

    /* Convenience: run from AF_BarArray */
    EngineResult run(const AF_BarArray &arr) const {
        return run(arr.bars, arr.count, arr.symbol, arr.tf);
    }

    int indicator_count() const { return 38; } /* 38 indicators registered */

private:
    void add_trend_indicators(const double *c, const double *h,
                               const double *l, const double *v,
                               size_t n, EngineResult &r) const;
    void add_momentum_indicators(const double *c, const double *h,
                                  const double *l, const double *v,
                                  size_t n, EngineResult &r) const;
    void add_volatility_indicators(const double *c, const double *h,
                                    const double *l, size_t n, EngineResult &r) const;
    void add_volume_indicators(const double *c, const double *h,
                                const double *l, const double *v,
                                size_t n, EngineResult &r) const;
    void add_sr_indicators(const double *c, const double *h,
                            const double *l, size_t n, EngineResult &r) const;

    void compute_summary(EngineResult &r) const;
};

} /* namespace af */
