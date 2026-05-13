/**
 * AlgoForge — src/main.cpp
 * Production entry point. Boots all subsystems and runs the engine.
 *
 * Usage:
 *   ./algoforge                     (paper mode, reads algoforge.ini)
 *   ./algoforge --mode LIVE         (live mode, requires confirmation)
 *   ./algoforge --config path.ini
 *   ./algoforge --help
 */

#include "core/config.hpp"
#include "core/engine.hpp"
#include "core/event_bus.hpp"
#include "broker/broker.hpp"
#include "algorithms/algorithm.hpp"
#include "learning/error_registry.hpp"
#include <cstdio>
#include <cstring>
#include <csignal>
#include <memory>
#include <string>

/* ── Global engine pointer for signal handling ── */
static af::TradingEngine *g_engine = nullptr;

static void signal_handler(int sig) {
    printf("\n[Main] Signal %d received — shutting down...\n", sig);
    if (g_engine) g_engine->stop();
}

/* ── CLI parsing ── */
static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n"
           "Options:\n"
           "  --mode PAPER|LIVE   Override trading mode\n"
           "  --config FILE       Path to config INI (default: algoforge.ini)\n"
           "  --write-config      Write default config file and exit\n"
           "  --version           Print version and exit\n"
           "  --help              This message\n\n"
           "Environment variables override config file:\n"
           "  TRADING_MODE, MT5_LOGIN, MT5_PASSWORD, MT5_SERVER\n"
           "  MAX_RISK_PER_TRADE, MAX_DAILY_DRAWDOWN, PAPER_BALANCE\n",
           prog);
}

/* ── Paper broker factory (forward-declared) ── */
namespace af {
    std::unique_ptr<IBroker> make_paper_broker(double balance);
    std::unique_ptr<IBroker> make_mt5_broker();   /* defined when AF_MT5_AVAILABLE */
}

/* ── Live confirmation ── */
static bool confirm_live() {
    printf("\n\033[91m⚠  WARNING: LIVE TRADING — REAL MONEY AT RISK\033[0m\n\n");
    printf("Type 'YES I UNDERSTAND' to proceed: ");
    fflush(stdout);
    char buf[64]; if (!fgets(buf, sizeof(buf), stdin)) return false;
    return strncmp(buf, "YES I UNDERSTAND", 16) == 0;
}

int main(int argc, char **argv) {
    /* ── Parse args ── */
    std::string config_path = "algoforge.ini";
    std::string mode_override;
    bool write_config = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0)         { print_usage(argv[0]); return 0; }
        if (strcmp(argv[i], "--version") == 0)      { printf("AlgoForge %s\n", AF_VERSION); return 0; }
        if (strcmp(argv[i], "--write-config") == 0) { write_config = true; }
        if (strcmp(argv[i], "--config") == 0 && i+1 < argc)  config_path = argv[++i];
        if (strcmp(argv[i], "--mode")   == 0 && i+1 < argc)  mode_override = argv[++i];
    }

    if (write_config) {
        af::Config::write_default(config_path);
        return 0;
    }

    /* ── Load config ── */
    af::get_config() = af::Config::load(config_path);
    af::Config &cfg  = af::get_config();

    if (!mode_override.empty())
        cfg.mode = (mode_override == "LIVE") ? AF_MODE_LIVE : AF_MODE_PAPER;

    printf("\n  ╔══════════════════════════════════════╗\n"
              "  ║       ALGOFORGE TRADING BOT         ║\n"
              "  ║   Autonomous Multi-TF Strategy AI   ║\n"
              "  ║   Version %-28s║\n"
              "  ╚══════════════════════════════════════╝\n\n",
           AF_VERSION "  ");

    printf("  Mode    : %s\n", cfg.is_live() ? "LIVE ⚡" : "PAPER 📄");
    printf("  Account : %llu\n", (unsigned long long)cfg.mt5_login);
    printf("  Server  : %s\n", cfg.mt5_server.c_str());
    printf("  Risk    : %.1f%% per trade | DD limit %.1f%%\n\n",
           cfg.max_risk_per_trade*100, cfg.max_total_drawdown*100);

    /* ── Live guard ── */
    if (cfg.is_live() && !confirm_live()) {
        printf("[Main] Live trading cancelled.\n");
        return 0;
    }

    /* ── Signal handling ── */
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    /* ── Error registry (loads hardcoded blocks) ── */
    af::ErrorRegistry error_reg;
    error_reg.print_active_blocks();

    /* ── Build broker ── */
    std::shared_ptr<af::IBroker> broker;
#ifdef AF_MT5_AVAILABLE
    if (cfg.is_live()) {
        broker = af::make_mt5_broker();
    } else {
        broker = af::make_paper_broker(cfg.paper_balance);
    }
#else
    if (cfg.is_live()) {
        printf("[Main] ERROR: MT5 not available on this platform. Use PAPER mode.\n");
        return 1;
    }
    broker = af::make_paper_broker(cfg.paper_balance);
#endif

    /* ── Build algorithm registry ── */
    af::AlgorithmRegistry algo_registry;
    printf("[Main] Algorithms loaded: %d\n", algo_registry.count());
    for (auto &algo : algo_registry.all())
        printf("  - %s (magic=%u)\n", algo->name(), algo->magic());

    /* ── Build engine ── */
    af::EngineConfig ecfg;
    ecfg.mode                = cfg.mode;
    ecfg.max_risk_per_trade  = cfg.max_risk_per_trade;
    ecfg.max_daily_drawdown  = cfg.max_daily_drawdown;
    ecfg.max_total_drawdown  = cfg.max_total_drawdown;
    ecfg.max_open_positions  = cfg.max_open_positions;

    af::TradingEngine engine(broker, ecfg);
    g_engine = &engine;

    printf("\n[Main] Starting engine...\n");
    engine.run();

    printf("[Main] AlgoForge exited cleanly.\n");
    return 0;
}
