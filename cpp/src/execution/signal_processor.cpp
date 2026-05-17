/**
 * AlgoForge — src/execution/signal_processor.cpp
 * Converts AlgoDecisions into broker orders after risk approval.
 */
#include "algorithms/algorithm.hpp"
#include "broker/broker.hpp"
#include "learning/error_registry.hpp"
#include "core/event_bus.hpp"
#include "core/types.h"
#include <unordered_map>
#include <string>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <memory>

namespace af {

class SignalProcessor {
public:
    SignalProcessor(IBroker *broker,
                    ErrorRegistry *error_reg,
                    double max_risk = 0.01,
                    int cooldown_sec = 300)
        : broker_(broker), err_reg_(error_reg),
          max_risk_(max_risk), cooldown_sec_(cooldown_sec)
    {}

    /**
     * Process an algo decision. Places an order if:
     *   - Not in cooldown for this (symbol, direction)
     *   - Error registry allows the trade
     *   - Broker accepts the order
     * Returns true if an order was placed.
     */
    bool process(const AlgoDecision &decision) {
        if (!decision.is_actionable()) return false;

        /* Deduplication cooldown */
        std::string key = decision.symbol +
                          (decision.direction==AF_DIR_LONG ? "_L" : "_S");
        time_t now = time(nullptr);
        auto it = last_signal_.find(key);
        if (it != last_signal_.end() && (now - it->second) < cooldown_sec_) {
            return false;
        }

        /* Error registry check */
        if (err_reg_) {
            AF_AccountInfo acct{};
            broker_->get_account(acct);

            TradeContext ctx{};
            ctx.direction = decision.direction;
            snprintf(ctx.symbol, sizeof(ctx.symbol), "%s", decision.symbol.c_str());
            snprintf(ctx.algo_type, sizeof(ctx.algo_type), "trend");
            snprintf(ctx.market_structure, sizeof(ctx.market_structure), "BULL");
            snprintf(ctx.macro_structure,  sizeof(ctx.macro_structure),  "BULL");
            ctx.confluence_score = decision.confidence * 100.0;
            ctx.adx              = 25;
            ctx.rsi              = 52;
            ctx.volume_ratio     = 1.2;

            struct tm *t = gmtime(&now);
            ctx.hour_utc    = t ? t->tm_hour : 12;
            ctx.day_of_week = t ? t->tm_wday : 1;

            auto block = err_reg_->check(ctx);
            if (!block.allowed) {
                printf("[SigProc] BLOCKED %s: %s\n",
                       decision.symbol.c_str(), block.reason.c_str());
                return false;
            }
        }

        /* Get current price */
        AF_Tick tick{};
        if (broker_->get_tick(decision.symbol.c_str(), tick) != AF_OK) return false;

        /* Calculate lot size (simple fixed-risk) */
        AF_SymbolInfo sym{};
        broker_->get_symbol_info(decision.symbol.c_str(), sym);
        AF_AccountInfo acct{};
        broker_->get_account(acct);

        double atr = decision.atr > 0 ? decision.atr : tick.ask * 0.001;
        double stop_dist = atr * 1.5;
        double risk_amt  = acct.balance * max_risk_;
        double lots = risk_amt / (stop_dist * sym.contract_size);
        lots = std::max(sym.volume_min, std::min(sym.volume_max,
               std::round(lots / sym.volume_step) * sym.volume_step));

        /* SL / TP */
        double entry = (decision.direction==AF_DIR_LONG) ? tick.ask : tick.bid;
        double sl, tp;
        if (decision.direction == AF_DIR_LONG) {
            sl = entry - atr * 1.5;
            tp = entry + atr * 3.0;
        } else {
            sl = entry + atr * 1.5;
            tp = entry - atr * 3.0;
        }

        /* Place order */
        AF_OrderType otype = (decision.direction==AF_DIR_LONG) ? AF_ORDER_BUY : AF_ORDER_SELL;
        AF_Order order{};
        AF_Error err = broker_->place_order(
            decision.symbol.c_str(), otype, lots, 0,
            sl, tp, 1000, decision.reason.c_str(), order
        );

        if (err == AF_OK) {
            last_signal_[key] = now;
            processed_++;
            printf("[SigProc] PLACED %s %s %.2f lots @ %.5f SL=%.5f TP=%.5f\n",
                   decision.direction==AF_DIR_LONG?"BUY":"SELL",
                   decision.symbol.c_str(), lots, entry, sl, tp);

            /* Emit event */
            EventBus::instance().emit(EventType::ORDER_PLACED,
                                       OrderPayload{order});
            return true;
        }

        return false;
    }

    int processed() const { return processed_; }
    int rejected()  const { return rejected_;  }

private:
    IBroker       *broker_;
    ErrorRegistry *err_reg_;
    double         max_risk_;
    int            cooldown_sec_;
    int            processed_{0}, rejected_{0};
    std::unordered_map<std::string, time_t> last_signal_;
};

} /* namespace af */
