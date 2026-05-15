#pragma once
#include "backtesting/backtest_engine.hpp"
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace af {

class TradeJournal {
public:
    explicit TradeJournal(const std::string &db_path);
    ~TradeJournal();

    TradeJournal(const TradeJournal&)            = delete;
    TradeJournal& operator=(const TradeJournal&) = delete;

    bool ok() const { return db_ != nullptr && insert_ != nullptr; }

    bool record(const BTTrade &t, const std::string &run_id);

    int row_count() const;

private:
    sqlite3      *db_     = nullptr;
    sqlite3_stmt *insert_ = nullptr;
};

} /* namespace af */
