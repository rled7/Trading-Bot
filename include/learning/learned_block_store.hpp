#pragma once
#include "learning/error_registry.hpp"
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace af {

/**
 * Persistent store for ErrorRegistry's learned blocks.
 *
 * Persists metadata + stats (id, description, category, severity, source,
 * loss_count, total_loss_usd). The std::function predicate is not
 * serialized — reloaded blocks come back with check = always-false, so
 * the historical record survives but the rule itself is inert until the
 * application re-registers a live predicate via add_learned_block.
 */
class LearnedBlockStore {
public:
    explicit LearnedBlockStore(const std::string &db_path);
    ~LearnedBlockStore();

    LearnedBlockStore(const LearnedBlockStore&)            = delete;
    LearnedBlockStore& operator=(const LearnedBlockStore&) = delete;

    bool ok() const { return db_ != nullptr && upsert_ != nullptr; }

    /** Upsert by id; replaces all metadata fields with the supplied block. */
    bool upsert(const ErrorBlock &b);

    /** Load every row as an ErrorBlock with an always-false check predicate. */
    std::vector<ErrorBlock> load_all() const;

    int row_count() const;

private:
    sqlite3      *db_     = nullptr;
    sqlite3_stmt *upsert_ = nullptr;
};

} /* namespace af */
