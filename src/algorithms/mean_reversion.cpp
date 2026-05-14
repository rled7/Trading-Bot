/**
 * AlgoForge — src/algorithms/mean_reversion.cpp + breakout_trader.cpp
 */
#include "algorithms/algorithm.hpp"
#include <cmath>
#include <string>

namespace af {

/* ── MeanReversion ── */
class MeanReversion final : public IAlgorithm {
public:
    MeanReversion(double rsi_os=30.0, double rsi_ob=70.0, double adx_max=25.0)
        : rsi_os_(rsi_os), rsi_ob_(rsi_ob), adx_max_(adx_max) {}

    const char* name()        const override { return "MeanReversion_RSI_BB"; }
    const char* description() const override {
        return "RSI extremes + Bollinger Band touch in ranging markets";
    }
    uint32_t magic() const override { return 1002; }

    AlgoDecision evaluate(const char *symbol, AF_Timeframe tf,
                           const AF_Bar *bars, size_t n,
                           const EngineResult &ind,
                           const AF_PatternResult &pat) override
    {
        auto rsi_r = ind.get("RSI_14");
        auto bb_r  = ind.get("BB_20_2");
        auto adx_r = ind.get("ADX_14");
        if (!rsi_r || !bb_r || !adx_r) return AlgoDecision::none(symbol);

        double rsi   = rsi_r->value;
        double adx   = adx_r->value;
        double price = bars[n-1].close;
        double lower = bb_r->value3;
        double upper = bb_r->value2;
        bool   squeeze = bb_r->extra.count("squeeze") ? bb_r->extra.at("squeeze") > 0.5 : false;

        if (adx > adx_max_ || squeeze) return AlgoDecision::none(symbol);

        if (rsi <= rsi_os_ && price <= lower * 1.002) {
            double conf = std::min(0.85, 0.55 + (rsi_os_ - rsi) / 30.0 * 0.30);
            AlgoDecision d; d.signal=AlgoSignal::BUY; d.symbol=symbol;
            d.direction=AF_DIR_LONG; d.confidence=conf; d.atr=ind.atr_value;
            d.reason = "Oversold RSI=" + std::to_string((int)rsi) + " at lower BB";
            return d;
        }
        if (rsi >= rsi_ob_ && price >= upper * 0.998) {
            double conf = std::min(0.85, 0.55 + (rsi - rsi_ob_) / 30.0 * 0.30);
            AlgoDecision d; d.signal=AlgoSignal::SELL; d.symbol=symbol;
            d.direction=AF_DIR_SHORT; d.confidence=conf; d.atr=ind.atr_value;
            d.reason = "Overbought RSI=" + std::to_string((int)rsi) + " at upper BB";
            return d;
        }
        return AlgoDecision::none(symbol);
    }

private:
    double rsi_os_, rsi_ob_, adx_max_;
};

/* ── BreakoutTrader ── */
class BreakoutTrader final : public IAlgorithm {
public:
    explicit BreakoutTrader(int dc_period=20, double vol_mult=1.5)
        : dc_period_(dc_period), vol_mult_(vol_mult) {}

    const char* name()        const override { return "Breakout_Donchian"; }
    const char* description() const override {
        return "Donchian Channel breakout + volume surge + ATR expansion";
    }
    uint32_t magic() const override { return 1003; }

    AlgoDecision evaluate(const char *symbol, AF_Timeframe tf,
                           const AF_Bar *bars, size_t n,
                           const EngineResult &ind,
                           const AF_PatternResult &pat) override
    {
        auto dc_r   = ind.get("DC_20");
        auto macd_r = ind.get("MACD_12_26_9");
        if (!dc_r) return AlgoDecision::none(symbol);

        double price  = bars[n-1].close;
        double upper  = dc_r->value2;
        double lower  = dc_r->value3;
        double atr    = ind.atr_value;

        /* Volume */
        double avg_vol = 0;
        size_t lookback = std::min(n-1, (size_t)20);
        for (size_t i=1; i<=lookback; i++) avg_vol += bars[n-1-i].volume;
        avg_vol /= (double)lookback;
        double cur_vol = bars[n-1].volume;
        bool vol_surge = cur_vol > avg_vol * vol_mult_;

        /* ATR expansion */
        double range_avg = 0;
        for (size_t i=1; i<=lookback; i++) range_avg += (bars[n-1-i].high - bars[n-1-i].low);
        range_avg /= (double)lookback;
        bool atr_expand = (bars[n-1].high - bars[n-1].low) > range_avg * 1.1;

        bool macd_bull = macd_r && macd_r->value3 > 0;
        bool macd_bear = macd_r && macd_r->value3 < 0;

        if (price >= upper && vol_surge && atr_expand && macd_bull) {
            double conf = std::min(0.88, 0.60 + (vol_surge?0.15:0) + (atr_expand?0.10:0));
            AlgoDecision d; d.signal=AlgoSignal::BUY; d.symbol=symbol;
            d.direction=AF_DIR_LONG; d.confidence=conf; d.atr=atr;
            d.reason = "DC breakout up vol=" + std::to_string((int)(cur_vol/avg_vol*100)) + "%";
            return d;
        }
        if (price <= lower && vol_surge && atr_expand && macd_bear) {
            double conf = std::min(0.88, 0.60 + (vol_surge?0.15:0) + (atr_expand?0.10:0));
            AlgoDecision d; d.signal=AlgoSignal::SELL; d.symbol=symbol;
            d.direction=AF_DIR_SHORT; d.confidence=conf; d.atr=atr;
            d.reason = "DC breakout down vol=" + std::to_string((int)(cur_vol/avg_vol*100)) + "%";
            return d;
        }
        return AlgoDecision::none(symbol);
    }

private:
    int    dc_period_;
    double vol_mult_;
};

/* ── SwingTrader ── */
class SwingTrader final : public IAlgorithm {
public:
    SwingTrader() = default;
    const char* name()        const override { return "SwingTrader_PullbackEMA"; }
    const char* description() const override { return "Pullback to EMA21 in trend direction"; }
    uint32_t magic() const override { return 1004; }

    AlgoDecision evaluate(const char *symbol, AF_Timeframe tf,
                           const AF_Bar *bars, size_t n,
                           const EngineResult &ind,
                           const AF_PatternResult &pat) override
    {
        if (tf != AF_TF_H4 && tf != AF_TF_D1) return AlgoDecision::none(symbol);

        auto e21  = ind.get("EMA21");
        auto e50  = ind.get("EMA50");
        auto e200 = ind.get("EMA200");
        auto rsi  = ind.get("RSI_14");
        if (!e21 || !e50 || !e200) return AlgoDecision::none(symbol);

        double price     = bars[n-1].close;
        double at_ema21  = std::abs(price - e21->value) / e21->value < 0.003;
        double rsi_val   = rsi ? rsi->value : 50.0;

        bool bull_trend = e21->value > e50->value && e50->value > e200->value;
        bool bear_trend = e21->value < e50->value && e50->value < e200->value;

        if (bull_trend && at_ema21 && rsi_val > 38 && rsi_val < 58) {
            /* Need bullish pattern confirmation */
            if (pat.bullish_count > 0) {
                AlgoDecision d; d.signal=AlgoSignal::BUY; d.symbol=symbol;
                d.direction=AF_DIR_LONG; d.confidence=0.75; d.atr=ind.atr_value;
                d.reason = "Pullback to EMA21 in uptrend";
                return d;
            }
        }
        if (bear_trend && at_ema21 && rsi_val > 42 && rsi_val < 62) {
            if (pat.bearish_count > 0) {
                AlgoDecision d; d.signal=AlgoSignal::SELL; d.symbol=symbol;
                d.direction=AF_DIR_SHORT; d.confidence=0.75; d.atr=ind.atr_value;
                d.reason = "Rally to EMA21 in downtrend";
                return d;
            }
        }
        return AlgoDecision::none(symbol);
    }
};

/* ── Factories ── */
std::unique_ptr<IAlgorithm> make_mean_reversion() {
    return std::make_unique<MeanReversion>();
}
std::unique_ptr<IAlgorithm> make_breakout_trader(int dc, double vm) {
    return std::make_unique<BreakoutTrader>(dc, vm);
}
std::unique_ptr<IAlgorithm> make_swing_trader() {
    return std::make_unique<SwingTrader>();
}

} /* namespace af */
