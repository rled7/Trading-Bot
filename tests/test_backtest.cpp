/**
 * AlgoForge — tests/test_backtest.cpp
 */
#include "tests/test_helpers.hpp"
#include "backtesting/backtest_engine.hpp"
#include "algorithms/algorithm.hpp"
#include "broker/broker.hpp"
#include "core/types.h"
#include <cmath>
#include <functional>
#include <string>
#include <memory>
#include <vector>
#include <algorithm>



namespace af {
    std::unique_ptr<IBroker>    make_paper_broker(double);
    std::unique_ptr<IAlgorithm> make_trend_follower(int,int,double);
    std::unique_ptr<IAlgorithm> make_mean_reversion();
    std::unique_ptr<IAlgorithm> make_breakout_trader(int,double);
}

static std::vector<AF_Bar> fetch_bars(int n=600, double drift=0.001) {
    std::vector<AF_Bar> bars(n);
    srand(42); double p=1.1;
    for (int i=0;i<n;i++){
        double o=p, nd=drift+(rand()%1000-500)*0.000001;
        p=std::max(p+nd, 0.0001);
        double h=std::max(o,p)+0.0002, l=std::min(o,p)-0.0002;
        bars[i].open=o;bars[i].high=h;bars[i].low=l;bars[i].close=p;
        bars[i].volume=500+(rand()%4000);bars[i].spread=0.0001;
        af_bar_init(&bars[i]);
    }
    return bars;
}

void test_backtest(TestRunner T) {
    section("BACKTESTING ENGINE");

    T("BacktestEngine: runs without crash (TrendFollower)", []{
        auto algo  = af::make_trend_follower(21,50,22);
        auto bars  = fetch_bars(600);
        af::BTConfig cfg; cfg.initial_capital=10000; cfg.warmup_bars=200;
        af::BacktestEngine bt(algo.get(), cfg);
        auto r = bt.run(bars.data(), bars.size(), "EURUSD", AF_TF_H1);
        CHK(r.initial_capital == 10000.0);
        CHK(r.equity_curve.size() > 0);
    });

    T("BacktestEngine: equity curve never negative", []{
        auto algo = af::make_trend_follower(21,50,22);
        auto bars = fetch_bars(600);
        af::BTConfig cfg; cfg.initial_capital=10000; cfg.warmup_bars=200;
        af::BacktestEngine bt(algo.get(), cfg);
        auto r = bt.run(bars.data(), bars.size(), "EURUSD", AF_TF_H1);
        for (double eq : r.equity_curve) CHK(eq >= 0.0);
    });

    T("BacktestEngine: win rate in [0,1]", []{
        auto algo = af::make_trend_follower(21,50,22);
        auto bars = fetch_bars(600);
        af::BTConfig cfg; cfg.initial_capital=10000; cfg.warmup_bars=200;
        af::BacktestEngine bt(algo.get(), cfg);
        auto r = bt.run(bars.data(), bars.size(), "EURUSD", AF_TF_H1);
        CHK(r.win_rate() >= 0.0 && r.win_rate() <= 1.0);
    });

    T("BacktestEngine: max drawdown non-negative", []{
        auto algo = af::make_trend_follower(21,50,22);
        auto bars = fetch_bars(600);
        af::BTConfig cfg; cfg.initial_capital=10000; cfg.warmup_bars=200;
        af::BacktestEngine bt(algo.get(), cfg);
        auto r = bt.run(bars.data(), bars.size(), "EURUSD", AF_TF_H1);
        CHK(r.max_drawdown_pct() >= 0.0);
    });

    T("BacktestEngine: summary string non-empty", []{
        auto algo = af::make_trend_follower(21,50,22);
        auto bars = fetch_bars(600);
        af::BTConfig cfg; cfg.initial_capital=10000; cfg.warmup_bars=200;
        af::BacktestEngine bt(algo.get(), cfg);
        auto r = bt.run(bars.data(), bars.size(), "EURUSD", AF_TF_H1);
        CHK_FALSE(r.summary().empty());
        CHK(r.summary().find("BACKTEST") != std::string::npos);
    });

    T("BacktestEngine: insufficient data returns empty result", []{
        auto algo = af::make_trend_follower(21,50,22);
        auto bars = fetch_bars(50);
        af::BTConfig cfg; cfg.initial_capital=10000; cfg.warmup_bars=200;
        af::BacktestEngine bt(algo.get(), cfg);
        auto r = bt.run(bars.data(), bars.size(), "EURUSD", AF_TF_H1);
        CHK(r.total_trades() == 0);
    });

    T("BacktestEngine: MeanReversion runs without crash", []{
        auto algo = af::make_mean_reversion();
        auto bars = fetch_bars(600, 0.0);
        af::BTConfig cfg; cfg.initial_capital=10000; cfg.warmup_bars=200;
        af::BacktestEngine bt(algo.get(), cfg);
        auto r = bt.run(bars.data(), bars.size(), "EURUSD", AF_TF_H1);
        CHK(r.initial_capital == 10000.0);
    });

    T("BacktestEngine: BreakoutTrader runs without crash", []{
        auto algo = af::make_breakout_trader(20, 1.5);
        auto bars = fetch_bars(600);
        af::BTConfig cfg; cfg.initial_capital=10000; cfg.warmup_bars=200;
        af::BacktestEngine bt(algo.get(), cfg);
        auto r = bt.run(bars.data(), bars.size(), "EURUSD", AF_TF_H1);
        CHK(r.initial_capital == 10000.0);
    });

    T("BacktestEngine: different seeds give different results", []{
        auto algo1 = af::make_trend_follower(21,50,22);
        auto algo2 = af::make_trend_follower(21,50,22);
        auto bars1 = fetch_bars(600, 0.002);
        auto bars2 = fetch_bars(600, -0.002);
        af::BTConfig cfg; cfg.initial_capital=10000; cfg.warmup_bars=200;
        af::BacktestEngine bt1(algo1.get(),cfg), bt2(algo2.get(),cfg);
        auto r1 = bt1.run(bars1.data(),bars1.size(),"EURUSD",AF_TF_H1);
        auto r2 = bt2.run(bars2.data(),bars2.size(),"EURUSD",AF_TF_H1);
        /* Different trends should produce different results */
        CHK(r1.net_profit() != r2.net_profit() || r1.total_trades() == 0);
    });
}
