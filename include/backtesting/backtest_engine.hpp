/**
 * AlgoForge — include/backtesting/backtest_engine.hpp
 * Bar-by-bar backtesting engine with full performance metrics.
 */
#pragma once
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <limits>
#include "core/types.h"
#include "algorithms/algorithm.hpp"

namespace af {

/* ── Trade record ── */
struct BTTrade {
    int64_t  entry_time{0}, exit_time{0};
    char     symbol[12]{};
    AF_Direction direction{AF_DIR_NONE};
    double   lots{0}, entry_price{0}, exit_price{0};
    double   sl{0}, tp{0};
    double   gross_pnl{0}, commission{0};
    int      bars_held{0};
    char     close_reason[16]{};   /* "SL" | "TP" | "END" */
    char     algo[64]{};

    double net_pnl()   const { return gross_pnl - commission; }
    bool   is_winner() const { return net_pnl() > 0; }
};

/* ── Backtest result ── */
struct BTResult {
    std::string symbol, timeframe;
    double initial_capital{10000};
    std::vector<BTTrade>  trades;
    std::vector<double>   equity_curve;
    double duration_sec{0};

    /* ── Metrics (computed lazily) ── */
    int    total_trades()  const { return static_cast<int>(trades.size()); }
    int    winning_trades()const;
    double net_profit()    const;
    double final_capital() const { return initial_capital + net_profit(); }
    double return_pct()    const { return net_profit() / initial_capital * 100.0; }
    double win_rate()      const;
    double profit_factor() const;
    double max_drawdown_pct() const;
    double sharpe_ratio()  const;
    double sortino_ratio() const;
    double calmar_ratio()  const;
    double avg_win()       const;
    double avg_loss()      const;
    double expectancy()    const;

    std::string summary() const;
};

/* ── Backtest config ── */
struct BTConfig {
    double initial_capital = 10000.0;
    double risk_pct        = 0.01;
    double commission_usd  = 7.0;    /* USD per lot round-trip */
    int    slippage_pts    = 2;
    double atr_sl_mult     = 1.5;
    double rr_ratio        = 2.0;
    int    warmup_bars     = 200;
};

/* ── Engine ── */
class BacktestEngine {
public:
    BacktestEngine(IAlgorithm *algo, BTConfig cfg = {});

    BTResult run(const AF_Bar *bars, size_t count,
                  const char *symbol, AF_Timeframe tf) const;

    BTResult run(const AF_BarArray &arr) const {
        return run(arr.bars, arr.count, arr.symbol, arr.tf);
    }

private:
    IAlgorithm *algo_;
    BTConfig    cfg_;

    static const char* tf_name(AF_Timeframe tf);
};

} /* namespace af */
