#include "learning/learned_block_store.hpp"
#include "sqlite3.h"
#include <cstdio>

namespace af {

static constexpr const char *CREATE_SQL =
    "CREATE TABLE IF NOT EXISTS learned_blocks ("
    "  id              TEXT    PRIMARY KEY,"
    "  description     TEXT    NOT NULL,"
    "  category        TEXT    NOT NULL,"
    "  severity        INTEGER NOT NULL,"
    "  source          TEXT    NOT NULL,"
    "  loss_count      INTEGER NOT NULL,"
    "  total_loss_usd  REAL    NOT NULL,"
    "  updated_at      TEXT    NOT NULL DEFAULT (datetime('now'))"
    ");";

static constexpr const char *UPSERT_SQL =
    "INSERT INTO learned_blocks("
    " id,description,category,severity,source,loss_count,total_loss_usd)"
    " VALUES(?,?,?,?,?,?,?)"
    " ON CONFLICT(id) DO UPDATE SET"
    "  description=excluded.description,"
    "  category=excluded.category,"
    "  severity=excluded.severity,"
    "  source=excluded.source,"
    "  loss_count=excluded.loss_count,"
    "  total_loss_usd=excluded.total_loss_usd,"
    "  updated_at=datetime('now');";

LearnedBlockStore::LearnedBlockStore(const std::string &db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        fprintf(stderr, "[LearnedBlockStore] open failed: %s\n",
                db_ ? sqlite3_errmsg(db_) : "(no db)");
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return;
    }
    char *err = nullptr;
    if (sqlite3_exec(db_, CREATE_SQL, nullptr, nullptr, &err) != SQLITE_OK) {
        fprintf(stderr, "[LearnedBlockStore] schema failed: %s\n", err ? err : "?");
        sqlite3_free(err);
        sqlite3_close(db_); db_ = nullptr;
        return;
    }
    if (sqlite3_prepare_v2(db_, UPSERT_SQL, -1, &upsert_, nullptr) != SQLITE_OK) {
        fprintf(stderr, "[LearnedBlockStore] prepare failed: %s\n", sqlite3_errmsg(db_));
        sqlite3_close(db_); db_ = nullptr; upsert_ = nullptr;
    }
}

LearnedBlockStore::~LearnedBlockStore() {
    if (upsert_) sqlite3_finalize(upsert_);
    if (db_)     sqlite3_close(db_);
}

bool LearnedBlockStore::upsert(const ErrorBlock &b) {
    if (!ok()) return false;
    sqlite3_reset(upsert_);
    sqlite3_clear_bindings(upsert_);

    sqlite3_bind_text  (upsert_, 1, b.id.c_str(),          -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (upsert_, 2, b.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (upsert_, 3, b.category.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (upsert_, 4, (int)b.severity);
    sqlite3_bind_text  (upsert_, 5, b.source.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (upsert_, 6, b.loss_count);
    sqlite3_bind_double(upsert_, 7, b.total_loss_usd);

    if (sqlite3_step(upsert_) != SQLITE_DONE) {
        fprintf(stderr, "[LearnedBlockStore] upsert failed: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    return true;
}

std::vector<ErrorBlock> LearnedBlockStore::load_all() const {
    std::vector<ErrorBlock> out;
    if (!db_) return out;
    sqlite3_stmt *st = nullptr;
    const char *sql = "SELECT id,description,category,severity,source,loss_count,total_loss_usd"
                      " FROM learned_blocks;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(st) == SQLITE_ROW) {
        ErrorBlock b;
        b.id             = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        b.description    = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        b.category       = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        b.severity       = static_cast<BlockSeverity>(sqlite3_column_int(st, 3));
        b.source         = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
        b.loss_count     = sqlite3_column_int(st, 5);
        b.total_loss_usd = sqlite3_column_double(st, 6);
        b.check = [](const TradeContext&) { return false; };
        out.push_back(std::move(b));
    }
    sqlite3_finalize(st);
    return out;
}

int LearnedBlockStore::row_count() const {
    if (!db_) return -1;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM learned_blocks;", -1, &st, nullptr) != SQLITE_OK)
        return -1;
    int n = -1;
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}

} /* namespace af */
