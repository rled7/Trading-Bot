#include "data/trade_journal.hpp"
#include "sqlite3.h"
#include <cstdio>

namespace af {

static constexpr const char *CREATE_SQL =
    "CREATE TABLE IF NOT EXISTS trades ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  run_id       TEXT    NOT NULL,"
    "  symbol       TEXT    NOT NULL,"
    "  algo         TEXT    NOT NULL,"
    "  direction    INTEGER NOT NULL,"
    "  entry_time   INTEGER NOT NULL,"
    "  entry_price  REAL    NOT NULL,"
    "  exit_time    INTEGER NOT NULL,"
    "  exit_price   REAL    NOT NULL,"
    "  lots         REAL    NOT NULL,"
    "  sl           REAL    NOT NULL,"
    "  tp           REAL    NOT NULL,"
    "  gross_pnl    REAL    NOT NULL,"
    "  commission   REAL    NOT NULL,"
    "  net_pnl      REAL    NOT NULL,"
    "  bars_held    INTEGER NOT NULL,"
    "  close_reason TEXT    NOT NULL,"
    "  client_id    TEXT    NOT NULL DEFAULT '',"
    "  created_at   TEXT    NOT NULL DEFAULT (datetime('now'))"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_trades_run    ON trades(run_id);"
    "CREATE INDEX IF NOT EXISTS idx_trades_symbol ON trades(symbol);"
    /* Partial unique index — enforces idempotency for non-empty keys only,
       so legacy rows (where client_id defaulted to '') don't collide. */
    "CREATE UNIQUE INDEX IF NOT EXISTS ux_trades_run_client"
    " ON trades(run_id, client_id) WHERE client_id != '';";

/* Best-effort schema migration: adds client_id to an older `trades` table.
   Errors (e.g. "duplicate column name") are intentionally ignored. */
static constexpr const char *MIGRATE_SQL =
    "ALTER TABLE trades ADD COLUMN client_id TEXT NOT NULL DEFAULT '';";

static constexpr const char *INSERT_SQL =
    "INSERT OR IGNORE INTO trades("
    " run_id,symbol,algo,direction,"
    " entry_time,entry_price,exit_time,exit_price,"
    " lots,sl,tp,gross_pnl,commission,net_pnl,bars_held,close_reason,client_id)"
    "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";

TradeJournal::TradeJournal(const std::string &db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        fprintf(stderr, "[Journal] open failed: %s\n",
                db_ ? sqlite3_errmsg(db_) : "(no db)");
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return;
    }
    char *err = nullptr;
    if (sqlite3_exec(db_, CREATE_SQL, nullptr, nullptr, &err) != SQLITE_OK) {
        fprintf(stderr, "[Journal] schema failed: %s\n", err ? err : "?");
        sqlite3_free(err);
        sqlite3_close(db_); db_ = nullptr;
        return;
    }
    /* Migration is best-effort: errors on already-migrated DBs are expected. */
    sqlite3_exec(db_, MIGRATE_SQL, nullptr, nullptr, nullptr);
    if (sqlite3_prepare_v2(db_, INSERT_SQL, -1, &insert_, nullptr) != SQLITE_OK) {
        fprintf(stderr, "[Journal] prepare failed: %s\n", sqlite3_errmsg(db_));
        sqlite3_close(db_); db_ = nullptr; insert_ = nullptr;
        return;
    }
}

TradeJournal::~TradeJournal() {
    if (insert_) sqlite3_finalize(insert_);
    if (db_)     sqlite3_close(db_);
}

bool TradeJournal::record(const BTTrade &t, const std::string &run_id) {
    if (!ok()) return false;
    sqlite3_reset(insert_);
    sqlite3_clear_bindings(insert_);

    sqlite3_bind_text  (insert_,  1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (insert_,  2, t.symbol,       -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (insert_,  3, t.algo,         -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (insert_,  4, (int)t.direction);
    sqlite3_bind_int64 (insert_,  5, t.entry_time);
    sqlite3_bind_double(insert_,  6, t.entry_price);
    sqlite3_bind_int64 (insert_,  7, t.exit_time);
    sqlite3_bind_double(insert_,  8, t.exit_price);
    sqlite3_bind_double(insert_,  9, t.lots);
    sqlite3_bind_double(insert_, 10, t.sl);
    sqlite3_bind_double(insert_, 11, t.tp);
    sqlite3_bind_double(insert_, 12, t.gross_pnl);
    sqlite3_bind_double(insert_, 13, t.commission);
    sqlite3_bind_double(insert_, 14, t.net_pnl());
    sqlite3_bind_int   (insert_, 15, t.bars_held);
    sqlite3_bind_text  (insert_, 16, t.close_reason, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (insert_, 17, t.client_id,    -1, SQLITE_TRANSIENT);

    if (sqlite3_step(insert_) != SQLITE_DONE) {
        fprintf(stderr, "[Journal] insert failed: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    return true;
}

int TradeJournal::row_count() const {
    if (!db_) return -1;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM trades;", -1, &st, nullptr) != SQLITE_OK)
        return -1;
    int n = -1;
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}

} /* namespace af */
