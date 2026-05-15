/**
 * AlgoForge — src/backtest_main.cpp
 * Standalone backtest tool.
 *
 * Usage:
 *   ./af_backtest --symbol EURUSD --tf H1 --bars 2000 --algo trend
 *   ./af_backtest --symbol XAUUSD --tf H4 --bars 1000 --algo breakout
 */

#include "broker/broker.hpp"
#include "algorithms/algorithm.hpp"
#include "backtesting/backtest_engine.hpp"
#include "core/config.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace af {
    std::unique_ptr<IBroker> make_paper_broker(double balance);
    std::unique_ptr<IAlgorithm> make_trend_follower(int,int,double,uint32_t=1001);
    std::unique_ptr<IAlgorithm> make_mean_reversion();
    std::unique_ptr<IAlgorithm> make_breakout_trader(int,double);
    std::unique_ptr<IAlgorithm> make_swing_trader();
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n"
           "  --symbol EURUSD       Symbol to backtest (default: EURUSD)\n"
           "  --tf     H1           Timeframe: S15 M1 M5 M15 M30 H1 H4 D1 W1\n"
           "  --bars   2000         Number of bars to fetch\n"
           "  --algo   trend        Algorithm: trend | reversion | breakout | swing\n"
           "  --capital 10000       Starting capital\n"
           "  --risk   0.01         Risk per trade (fraction)\n"
           "  --help\n", prog);
}

static AF_Timeframe parse_tf(const char *s) {
    if (!s) return AF_TF_H1;
    if (strcmp(s,"S15")==0) return AF_TF_S15;
    if (strcmp(s,"M1" )==0) return AF_TF_M1;
    if (strcmp(s,"M5" )==0) return AF_TF_M5;
    if (strcmp(s,"M15")==0) return AF_TF_M15;
    if (strcmp(s,"M30")==0) return AF_TF_M30;
    if (strcmp(s,"H4" )==0) return AF_TF_H4;
    if (strcmp(s,"D1" )==0) return AF_TF_D1;
    if (strcmp(s,"W1" )==0) return AF_TF_W1;
    return AF_TF_H1;
}

int main(int argc, char **argv) {
    std::string symbol  = "EURUSD";
    std::string tf_str  = "H1";
    std::string algo_s  = "trend";
    int         bars    = 2000;
    double      capital = 10000.0;
    double      risk    = 0.01;

    for (int i=1;i<argc;i++) {
        if (strcmp(argv[i],"--help")==0)             { print_usage(argv[0]); return 0; }
        if (strcmp(argv[i],"--symbol")==0 && i+1<argc) symbol  = argv[++i];
        if (strcmp(argv[i],"--tf"    )==0 && i+1<argc) tf_str  = argv[++i];
        if (strcmp(argv[i],"--algo"  )==0 && i+1<argc) algo_s  = argv[++i];
        if (strcmp(argv[i],"--bars"  )==0 && i+1<argc) bars    = std::stoi(argv[++i]);
        if (strcmp(argv[i],"--capital")==0&& i+1<argc) capital = std::stod(argv[++i]);
        if (strcmp(argv[i],"--risk"  )==0 && i+1<argc) risk    = std::stod(argv[++i]);
    }

    AF_Timeframe tf = parse_tf(tf_str.c_str());

    printf("AlgoForge Backtester\n");
    printf("  Symbol  : %s\n", symbol.c_str());
    printf("  TF      : %s\n", tf_str.c_str());
    printf("  Bars    : %d\n", bars);
    printf("  Algo    : %s\n", algo_s.c_str());
    printf("  Capital : %.2f\n\n", capital);

    /* Build algo */
    std::unique_ptr<af::IAlgorithm> algo;
    if      (algo_s=="trend")     algo = af::make_trend_follower(21,50,22);
    else if (algo_s=="reversion") algo = af::make_mean_reversion();
    else if (algo_s=="breakout")  algo = af::make_breakout_trader(20,1.5);
    else if (algo_s=="swing")     algo = af::make_swing_trader();
    else {
        printf("Unknown algorithm '%s'. Use: trend | reversion | breakout | swing\n",
               algo_s.c_str());
        return 1;
    }

    /* Fetch bars via paper broker */
    auto broker = af::make_paper_broker(capital);
    broker->connect();

    std::vector<AF_Bar> bar_buf(bars);
    int filled = 0;
    AF_Error err = broker->get_bars(symbol.c_str(), tf, bars,
                                     bar_buf.data(), &filled);
    if (err != AF_OK || filled < 250) {
        printf("Failed to fetch bars (err=%d filled=%d)\n", (int)err, filled);
        return 1;
    }

    printf("Fetched %d bars. Running backtest...\n\n", filled);

    af::BTConfig cfg;
    cfg.initial_capital = capital;
    cfg.risk_pct        = risk;

    af::BacktestEngine bt_engine(algo.get(), cfg);
    auto result = bt_engine.run(bar_buf.data(), (size_t)filled,
                                 symbol.c_str(), tf);

    /* Summary already printed by engine — exit code indicates profitability */
    return result.net_profit() >= 0 ? 0 : 1;
}
