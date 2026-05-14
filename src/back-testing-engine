/**
 * AlgoForge — src/backtesting/backtest_engine.cpp
 */
#include "backtesting/backtest_engine.hpp"
#include "indicators/indicator_engine.hpp"
#include "patterns/pattern_engine.hpp"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <chrono>
#include <sstream>

namespace af {

/* ── BTResult metrics ── */
int BTResult::winning_trades() const {
    return (int)std::count_if(trades.begin(), trades.end(),
                               [](const BTTrade &t){ return t.is_winner(); });
}
double BTResult::net_profit() const {
    double s=0; for (auto &t:trades) s+=t.net_pnl(); return s;
}
double BTResult::win_rate() const {
    return trades.empty() ? 0.0 : (double)winning_trades()/trades.size();
}
double BTResult::profit_factor() const {
    double gw=0, gl=0;
    for (auto &t:trades) { if (t.net_pnl()>0) gw+=t.net_pnl(); else gl+=std::abs(t.net_pnl()); }
    return gl>1e-10 ? gw/gl : 999.0;
}
double BTResult::max_drawdown_pct() const {
    if (equity_curve.empty()) return 0.0;
    double peak=equity_curve[0], mdd=0;
    for (double e : equity_curve) {
        if (e>peak) peak=e;
        double dd=(peak-e)/peak;
        if (dd>mdd) mdd=dd;
    }
    return mdd*100.0;
}
double BTResult::sharpe_ratio() const {
    if (equity_curve.size() < 2) return 0.0;
    std::vector<double> rets;
    for (size_t i=1;i<equity_curve.size();i++)
        if (equity_curve[i-1]>1e-10)
            rets.push_back((equity_curve[i]-equity_curve[i-1])/equity_curve[i-1]);
    if (rets.empty()) return 0.0;
    double mean=std::accumulate(rets.begin(),rets.end(),0.0)/rets.size();
    double var=0; for (double r:rets){double d=r-mean;var+=d*d;}
    double sd=std::sqrt(var/rets.size());
    return sd>1e-10 ? mean/sd*std::sqrt(252.0) : 0.0;
}
double BTResult::sortino_ratio() const {
    if (equity_curve.size() < 2) return 0.0;
    std::vector<double> rets;
    for (size_t i=1;i<equity_curve.size();i++)
        if (equity_curve[i-1]>1e-10)
            rets.push_back((equity_curve[i]-equity_curve[i-1])/equity_curve[i-1]);
    double mean=std::accumulate(rets.begin(),rets.end(),0.0)/rets.size();
    double neg_var=0; int nc=0;
    for (double r:rets) if (r<0){double d=r-mean;neg_var+=d*d;nc++;}
    double neg_sd=(nc>0)?std::sqrt(neg_var/nc):1e-10;
    return mean/neg_sd*std::sqrt(252.0);
}
double BTResult::calmar_ratio() const {
    double mdd=max_drawdown_pct();
    return mdd>1e-10 ? return_pct()/mdd : 0.0;
}
double BTResult::avg_win() const {
    double s=0; int c=0;
    for (auto &t:trades) if (t.is_winner()){s+=t.net_pnl();c++;}
    return c?s/c:0.0;
}
double BTResult::avg_loss() const {
    double s=0; int c=0;
    for (auto &t:trades) if (!t.is_winner()){s+=t.net_pnl();c++;}
    return c?s/c:0.0;
}
double BTResult::expectancy() const {
    double wr=win_rate();
    return wr*avg_win()+(1-wr)*avg_loss();
}

std::string BTResult::summary() const {
    std::ostringstream ss;
    ss << "\n" << std::string(55,'=') << "\n";
    ss << "  BACKTEST: " << symbol << "/" << timeframe << "\n";
    ss << std::string(55,'=') << "\n";
    ss << "  Capital   : " << initial_capital << " → " << final_capital() << "\n";
    ss << "  Return    : " << return_pct() << "%\n";
    ss << "  Trades    : " << total_trades() << " | Wins: " << winning_trades() << "\n";
    ss << "  Win Rate  : " << win_rate()*100 << "%\n";
    ss << "  Prof.Fact : " << profit_factor() << "\n";
    ss << "  Max DD    : " << max_drawdown_pct() << "%\n";
    ss << "  Sharpe    : " << sharpe_ratio() << "\n";
    ss << "  Sortino   : " << sortino_ratio() << "\n";
    ss << "  Expectancy: " << expectancy() << "\n";
    ss << "  Duration  : " << duration_sec << "s\n";
    ss << std::string(55,'=');
    return ss.str();
}

const char* BacktestEngine::tf_name(AF_Timeframe tf) {
    switch(tf){
        case AF_TF_S15: return "S15"; case AF_TF_M1:  return "M1";
        case AF_TF_M5:  return "M5";  case AF_TF_M15: return "M15";
        case AF_TF_M30: return "M30"; case AF_TF_H1:  return "H1";
        case AF_TF_H4:  return "H4";  case AF_TF_D1:  return "D1";
        case AF_TF_W1:  return "W1";  default: return "?";
    }
}

BacktestEngine::BacktestEngine(IAlgorithm *algo, BTConfig cfg)
    : algo_(algo), cfg_(cfg) {}

BTResult BacktestEngine::run(const AF_Bar *bars, size_t count,
                              const char *symbol, AF_Timeframe tf) const {
    auto t0 = std::chrono::steady_clock::now();
    BTResult result;
    result.symbol         = symbol;
    result.timeframe      = tf_name(tf);
    result.initial_capital= cfg_.initial_capital;

    if (!bars || (int)count < cfg_.warmup_bars + 20 || !algo_) {
        printf("[BT] Insufficient data: %zu bars (need %d)\n", count, cfg_.warmup_bars+20);
        return result;
    }

    IndicatorEngine  ind_eng;
    PatternEngine    pat_eng(0.60);

    double capital    = cfg_.initial_capital;
    bool   in_trade   = false;
    BTTrade open_trade{};
    result.equity_curve.push_back(capital);

    printf("[BT] %s/%s | %zu bars | algo=%s\n", symbol, tf_name(tf), count, algo_->name());

    for (size_t i = (size_t)cfg_.warmup_bars; i < count - 1; i++) {
        const AF_Bar &next = bars[i+1];

        /* ── Check open SL/TP ── */
        if (in_trade) {
            double h = next.high, l = next.low;
            bool sl_hit = (open_trade.direction==AF_DIR_LONG  && l <= open_trade.sl) ||
                          (open_trade.direction==AF_DIR_SHORT && h >= open_trade.sl);
            bool tp_hit = (open_trade.direction==AF_DIR_LONG  && h >= open_trade.tp) ||
                          (open_trade.direction==AF_DIR_SHORT && l <= open_trade.tp);

            if (sl_hit || tp_hit) {
                double exit_p = sl_hit ? open_trade.sl : open_trade.tp;
                double raw    = (open_trade.direction==AF_DIR_LONG)
                                ? (exit_p - open_trade.entry_price) * open_trade.lots * 100000.0
                                : (open_trade.entry_price - exit_p) * open_trade.lots * 100000.0;
                open_trade.gross_pnl   = raw;
                open_trade.commission  = cfg_.commission_usd * open_trade.lots;
                snprintf(open_trade.close_reason, 15, "%s", sl_hit?"SL":"TP");
                capital += open_trade.net_pnl();
                result.equity_curve.push_back(capital);
                result.trades.push_back(open_trade);
                in_trade = false;
                continue;
            }
        }

        result.equity_curve.push_back(capital);
        if (in_trade) continue;

        /* ── Run analysis ── */
        EngineResult   ind = ind_eng.run(bars, i+1, symbol, tf);
        AF_PatternResult pat = pat_eng.scan(bars, i+1);

        AlgoDecision decision = algo_->safe_evaluate(symbol, tf, bars, i+1, ind, pat);
        if (!decision.is_actionable()) continue;

        /* ── Simulate fill at next bar open ── */
        double atr        = ind.atr_value > 0 ? ind.atr_value : (bars[i].high - bars[i].low);
        double entry      = next.open;
        double slip       = cfg_.slippage_pts * 0.00001;
        AF_Direction dir  = decision.direction;

        if (dir == AF_DIR_LONG)  entry += slip;
        else                      entry -= slip;

        double sl, tp;
        if (dir == AF_DIR_LONG) {
            sl = entry - atr * cfg_.atr_sl_mult;
            tp = entry + atr * cfg_.atr_sl_mult * cfg_.rr_ratio;
        } else {
            sl = entry + atr * cfg_.atr_sl_mult;
            tp = entry - atr * cfg_.atr_sl_mult * cfg_.rr_ratio;
        }

        double stop_dist = std::abs(entry - sl);
        double risk_amt  = capital * cfg_.risk_pct;
        double lots      = (stop_dist * 100000.0 > 1e-10)
                           ? risk_amt / (stop_dist * 100000.0) : 0.01;
        lots = std::min(std::max(lots, 0.01), 10.0);
        lots = std::round(lots * 100.0) / 100.0;

        open_trade = BTTrade{};
        open_trade.entry_time  = (int64_t)i;
        snprintf(open_trade.symbol, 12, "%s", symbol);
        open_trade.direction   = dir;
        open_trade.lots        = lots;
        open_trade.entry_price = entry;
        open_trade.sl          = std::round(sl * 100000.0) / 100000.0;
        open_trade.tp          = std::round(tp * 100000.0) / 100000.0;
        snprintf(open_trade.algo, 63, "%s", algo_->name());
        in_trade = true;
    }

    /* ── Close at last bar ── */
    if (in_trade) {
        double exit_p = bars[count-1].close;
        double raw    = (open_trade.direction==AF_DIR_LONG)
                        ? (exit_p - open_trade.entry_price)*open_trade.lots*100000.0
                        : (open_trade.entry_price - exit_p)*open_trade.lots*100000.0;
        open_trade.gross_pnl  = raw;
        open_trade.commission = cfg_.commission_usd * open_trade.lots;
        snprintf(open_trade.close_reason, 15, "END");
        capital += open_trade.net_pnl();
        result.equity_curve.push_back(capital);
        result.trades.push_back(open_trade);
    }

    auto t1 = std::chrono::steady_clock::now();
    result.duration_sec = std::chrono::duration<double>(t1-t0).count();
    printf("%s\n", result.summary().c_str());
    return result;
}

} /* namespace af */
