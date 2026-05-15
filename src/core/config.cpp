/**
 * AlgoForge — src/core/config.cpp
 * Config loading from INI file + environment variable overrides.
 */
#include "core/config.hpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>

namespace af {

static std::string trim(const std::string &s) {
    auto b = s.find_first_not_of(" \t\r\n");
    auto e = s.find_last_not_of (" \t\r\n");
    return (b == std::string::npos) ? "" : s.substr(b, e-b+1);
}

static std::string getenv_str(const char *key, const std::string &def="") {
    const char *v = std::getenv(key);
    return v ? std::string(v) : def;
}
static double getenv_dbl(const char *key, double def) {
    const char *v = std::getenv(key);
    return v ? std::stod(v) : def;
}
static int getenv_int(const char *key, int def) {
    const char *v = std::getenv(key);
    return v ? std::stoi(v) : def;
}

Config Config::load(const std::string &ini_path) {
    Config cfg;

    /* ── Read INI ── */
    std::ifstream f(ini_path);
    if (f.is_open()) {
        std::string line;
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0]=='#' || line[0]==';' || line[0]=='[') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq+1));

            /* Convert key to upper */
            std::transform(key.begin(), key.end(), key.begin(), ::toupper);

            if (key=="TRADING_MODE")       cfg.mode = (val=="LIVE") ? AF_MODE_LIVE : AF_MODE_PAPER;
            else if (key=="MT5_LOGIN")     cfg.mt5_login    = std::stoull(val);
            else if (key=="MT5_PASSWORD")  cfg.mt5_password = val;
            else if (key=="MT5_SERVER")    cfg.mt5_server   = val;
            else if (key=="MT5_PATH")      cfg.mt5_path     = val;
            else if (key=="MAX_RISK_PER_TRADE")   cfg.max_risk_per_trade  = std::stod(val);
            else if (key=="MAX_DAILY_DRAWDOWN")    cfg.max_daily_drawdown  = std::stod(val);
            else if (key=="MAX_TOTAL_DRAWDOWN")    cfg.max_total_drawdown  = std::stod(val);
            else if (key=="MAX_OPEN_POSITIONS")    cfg.max_open_positions  = std::stoi(val);
            else if (key=="PAPER_BALANCE")         cfg.paper_balance       = std::stod(val);
            else if (key=="LOG_LEVEL")             cfg.log_level           = val;
            else if (key=="LOG_DIR")               cfg.log_dir             = val;
            else if (key=="DB_PATH")               cfg.db_path             = val;
            else if (key=="TELEGRAM_ENABLED")      cfg.telegram_enabled    = (val=="true"||val=="1");
            else if (key=="TELEGRAM_BOT_TOKEN")    cfg.telegram_token      = val;
            else if (key=="TELEGRAM_CHAT_ID")      cfg.telegram_chat_id    = val;
        }
    }

    /* ── Override with environment variables ── */
    {
        auto mode = getenv_str("TRADING_MODE");
        if (!mode.empty()) cfg.mode = (mode=="LIVE") ? AF_MODE_LIVE : AF_MODE_PAPER;
    }
    {
        auto login = getenv_str("MT5_LOGIN");
        if (!login.empty()) cfg.mt5_login = std::stoull(login);
    }
    {
        auto pass = getenv_str("MT5_PASSWORD");
        if (!pass.empty()) cfg.mt5_password = pass;
    }
    {
        auto srv = getenv_str("MT5_SERVER");
        if (!srv.empty()) cfg.mt5_server = srv;
    }
    cfg.max_risk_per_trade  = getenv_dbl("MAX_RISK_PER_TRADE",  cfg.max_risk_per_trade);
    cfg.max_daily_drawdown  = getenv_dbl("MAX_DAILY_DRAWDOWN",  cfg.max_daily_drawdown);
    cfg.max_total_drawdown  = getenv_dbl("MAX_TOTAL_DRAWDOWN",  cfg.max_total_drawdown);
    cfg.max_open_positions  = getenv_int("MAX_OPEN_POSITIONS",  cfg.max_open_positions);
    cfg.paper_balance       = getenv_dbl("PAPER_BALANCE",       cfg.paper_balance);

    auto tg = getenv_str("TELEGRAM_BOT_TOKEN");
    if (!tg.empty()) { cfg.telegram_token = tg; cfg.telegram_enabled = true; }
    auto tc = getenv_str("TELEGRAM_CHAT_ID");
    if (!tc.empty()) cfg.telegram_chat_id = tc;

    return cfg;
}

void Config::write_default(const std::string &path) {
    std::ofstream f(path);
    f << "# AlgoForge Configuration\n"
         "# Copy this to algoforge.ini and fill in your details\n\n"
         "[broker]\n"
         "TRADING_MODE=PAPER\n"
         "MT5_LOGIN=12345678\n"
         "MT5_PASSWORD=your_password\n"
         "MT5_SERVER=YourBroker-Demo\n"
         "MT5_PATH=C:\\Program Files\\MetaTrader 5\\terminal64.exe\n\n"
         "[risk]\n"
         "MAX_RISK_PER_TRADE=0.01\n"
         "MAX_DAILY_DRAWDOWN=0.05\n"
         "MAX_TOTAL_DRAWDOWN=0.15\n"
         "MAX_OPEN_POSITIONS=5\n"
         "PAPER_BALANCE=10000.0\n\n"
         "[logging]\n"
         "LOG_LEVEL=info\n"
         "LOG_DIR=logs\n"
         "DB_PATH=algoforge.db\n\n"
         "[telegram]\n"
         "TELEGRAM_ENABLED=false\n"
         "TELEGRAM_BOT_TOKEN=\n"
         "TELEGRAM_CHAT_ID=\n";
    printf("[Config] Default config written to: %s\n", path.c_str());
}

/* ── Singleton ── */
static Config g_config;
Config&       get_config()        { return g_config; }
const Config& cfg()               { return g_config; }

} /* namespace af */
