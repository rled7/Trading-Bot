/**
 * AlgoForge — src/algorithms/trend_follower.cpp
 * EMA crossover trend-following strategy.
 */
#include "algorithms/algorithm.hpp"
#include <cstring>
#include <cstdio>

namespace af {

class TrendFollower final : public IAlgorithm {
public:
    TrendFollower(int fast=21, int slow=50, double adx_min=22.0)
        : fast_(fast), slow_(slow), adx_min_(adx_min) {}

    const char* name()        const override { return name_.c_str(); }
    const char* description() const override {
        return "EMA crossover + ADX filter + RSI extreme guard";
    }
    uint32_t magic() const override { return 1001; }

    AlgoDecision evaluate(const char *symbol, AF_Timeframe tf,
                           const AF_Bar *bars, size_t n,
                           const EngineResult &ind,
                           const AF_PatternResult &pat) override
    {
        auto ema_fast = ind.get("EMA" + std::to_string(fast_));
        auto ema_slow = ind.get("EMA" + std::to_string(slow_));
        auto ema200   = ind.get("EMA200");
        auto adx_r    = ind.get("ADX_14");
        auto rsi_r    = ind.get("RSI_14");

        if (!ema_fast || !ema_slow || !ema200) return AlgoDecision::none(symbol);

        double fast_v = ema_fast->value;
        double slow_v = ema_slow->value;
        double e200   = ema200->value;
        double adx    = adx_r ? adx_r->value : 0.0;
        double pdi    = adx_r ? adx_r->value2 : 0.0;
        double mdi    = adx_r ? adx_r->value3 : 0.0;
        double rsi    = rsi_r ? rsi_r->value : 50.0;
        double price  = bars[n-1].close;
        double atr    = ind.atr_value;

        if (adx < adx_min_) return AlgoDecision::none(symbol);

        bool bullish = fast_v > slow_v && price > e200 && pdi > mdi;
        bool bearish = fast_v < slow_v && price < e200 && mdi > pdi;

        if (bullish && rsi < 75.0) {
            double conf = std::min(0.90, 0.60 + adx / 100.0 * 0.30);
            AlgoDecision d;
            d.signal = AlgoSignal::BUY; d.symbol = symbol;
            d.direction = AF_DIR_LONG; d.confidence = conf; d.atr = atr;
            snprintf(d.reason.data(), 255, "EMA%d>EMA%d ADX=%.1f RSI=%.1f",
                     fast_, slow_, adx, rsi);
            d.reason = std::string("EMA cross up ADX=") + std::to_string((int)adx);
            return d;
        }

        if (bearish && rsi > 25.0) {
            double conf = std::min(0.90, 0.60 + adx / 100.0 * 0.30);
            AlgoDecision d;
            d.signal = AlgoSignal::SELL; d.symbol = symbol;
            d.direction = AF_DIR_SHORT; d.confidence = conf; d.atr = atr;
            d.reason = std::string("EMA cross down ADX=") + std::to_string((int)adx);
            return d;
        }

        return AlgoDecision::none(symbol);
    }

private:
    int fast_, slow_;
    double adx_min_;
    std::string name_ = std::string("TrendFollower_EMA") +
                        std::to_string(21) + "/" + std::to_string(50);
};

/* Factory */
std::unique_ptr<IAlgorithm> make_trend_follower(int fast, int slow, double adx_min) {
    return std::make_unique<TrendFollower>(fast, slow, adx_min);
}

} /* namespace af */
