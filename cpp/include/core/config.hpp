/**
 * AlgoForge — include/core/config.hpp
 * Application-wide settings. Loaded from algoforge.ini or environment vars.
 */
#pragma once
#include <string>
#include <cstdint>
#include "core/types.h"

namespace af {

struct Config {
    /* Broker */
    AF_TradingMode mode          = AF_MODE_PAPER;
    uint64_t       mt5_login     = 0;
    std::string    mt5_password;
    std::string    mt5_server;
    std::string    mt5_path;

    /* Risk */
    double  max_risk_per_trade   = 0.01;
    double  max_daily_drawdown   = 0.05;
    double  max_total_drawdown   = 0.15;
    int     max_open_positions   = 5;
    double  max_lots             = 1.0;
    double  min_lots             = 0.01;

    /* Paper trading */
    double  paper_balance        = 10000.0;

    /* Execution */
    double  signal_threshold     = 62.0;
    int     signal_cooldown_sec  = 300;

    /* Logging */
    std::string log_dir          = "logs";
    std::string log_level        = "info";

    /* Telegram */
    bool        telegram_enabled = false;
    std::string telegram_token;
    std::string telegram_chat_id;

    /* Database */
    std::string db_path          = "algoforge.db";

    bool is_live()  const { return mode == AF_MODE_LIVE;  }
    bool is_paper() const { return mode == AF_MODE_PAPER; }

    /**
     * Load from INI file, then override with environment variables.
     * Returns true on success.
     */
    static Config load(const std::string &ini_path = "algoforge.ini");

    /** Write a default config file */
    static void write_default(const std::string &path = "algoforge.ini");
};

/* Global singleton */
Config&       get_config();
const Config& cfg();

} /* namespace af */
