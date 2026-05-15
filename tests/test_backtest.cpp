/**
 * AlgoForge — tests/test_backtest.cpp
 */
#include "test_helpers.hpp"
#include "backtesting/backtest_engine.hpp"
#include "data/trade_journal.hpp"
#include "data/csv_bars.hpp"
#include "algorithms/algorithm.hpp"
#include "broker/broker.hpp"
#include "core/types.h"
#include <cmath>
#include <functional>
#include <string>
#include <memory>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <fstream>



namespace af {
    std::unique_ptr<IBroker>    make_paper_broker(double);
    std::unique_ptr<IAlgorithm> make_trend_follower(int,int,double,uint32_t=1001);
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

    T("BacktestEngine: trade journal persists every trade to SQLite", []{
        const char *db = "/tmp/af_test_journal.db";
        std::remove(db);
        auto algo = af::make_trend_follower(21,50,22);
        auto bars = fetch_bars(600, 0.002);
        af::BTConfig cfg; cfg.initial_capital=10000; cfg.warmup_bars=200;
        cfg.journal_db_path = db;
        af::BacktestEngine bt(algo.get(), cfg);
        auto r = bt.run(bars.data(), bars.size(), "EURUSD", AF_TF_H1);
        if (r.total_trades() == 0) return; /* nothing to journal on this dataset */
        af::TradeJournal j(db);
        CHK(j.ok());
        CHK_EQ(j.row_count(), r.total_trades());
        /* Trades should have non-zero exit fields now that the engine populates them */
        for (const auto &t : r.trades) {
            CHK_GT(t.exit_time, t.entry_time);
            CHK(t.exit_price > 0);
            CHK_GE(t.bars_held, 1);
        }
        std::remove(db);
    });

    T("TradeJournal: ok() is false for an unwritable path", []{
        af::TradeJournal j("/nonexistent_dir_xyz/cannot_create.db");
        CHK(!j.ok());
    });

    T("CsvBars: loads ISO datetime + header + comma format", []{
        const char *path = "/tmp/af_test_iso.csv";
        {
            std::ofstream o(path);
            o << "timestamp,open,high,low,close,volume\n"
                 "2024-01-02 00:00:00,1.0850,1.0855,1.0848,1.0852,1000\n"
                 "2024-01-02 01:00:00,1.0852,1.0860,1.0850,1.0858,1200\n"
                 "2024-01-02 02:00:00,1.0858,1.0865,1.0855,1.0860,950\n";
        }
        auto res = af::load_csv_bars(path);
        CHK(res.error.empty());
        CHK_EQ(res.rows_parsed, 3);
        CHK_EQ(res.rows_skipped, 1);              /* the header */
        CHK_EQ((int)res.bars.size(), 3);
        /* Spot-check first row */
        CHK_NEAR(res.bars[0].open,  1.0850, 1e-9);
        CHK_NEAR(res.bars[0].close, 1.0852, 1e-9);
        CHK_NEAR(res.bars[0].volume, 1000, 1e-9);
        CHK_GT(res.bars[0].timestamp, 1704153000);  /* sometime around 2024-01-02 UTC */
        /* Bars are chronological */
        CHK(res.bars[1].timestamp > res.bars[0].timestamp);
        CHK(res.bars[2].timestamp > res.bars[1].timestamp);
        /* Derived fields populated by af_bar_init */
        CHK(res.bars[0].range > 0);
        std::remove(path);
    });

    T("CsvBars: loads epoch + tab + no header", []{
        const char *path = "/tmp/af_test_epoch.tsv";
        {
            std::ofstream o(path);
            o << "1704153600\t2050.00\t2051.30\t2049.10\t2050.80\t500\n"
                 "1704157200\t2050.80\t2052.00\t2050.10\t2051.40\t450\n";
        }
        auto res = af::load_csv_bars(path);
        CHK(res.error.empty());
        CHK_EQ(res.rows_parsed, 2);
        CHK_EQ(res.rows_skipped, 0);
        CHK_EQ((int)res.bars[0].timestamp, 1704153600);
        CHK_EQ((int)res.bars[1].timestamp, 1704157200);
        std::remove(path);
    });

    T("CsvBars: bad path returns error, no bars", []{
        auto res = af::load_csv_bars("/nope/does_not_exist.csv");
        CHK(!res.error.empty());
        CHK(res.bars.empty());
    });
}
