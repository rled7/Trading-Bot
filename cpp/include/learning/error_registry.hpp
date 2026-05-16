/**
 * AlgoForge — include/learning/error_registry.hpp
 * Permanent error pattern registry — blocks trades that match past losses.
 */
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "core/types.h"

namespace af {

/* ── Trade context passed to error registry check ── */
struct TradeContext {
    AF_Direction  direction{AF_DIR_NONE};
    char          symbol[12]{};
    char          algo_type[32]{};   /* "trend","reversion","breakout","scalper" */
    char          market_structure[32]{};  /* "BULL","BEAR","RANGING",... */
    char          macro_structure[32]{};   /* Higher-TF structure */
    double        rsi{50};
    double        adx{25};
    double        volume_ratio{1.0};
    double        confluence_score{60};
    int           hour_utc{12};
    int           day_of_week{1};   /* 0=Mon,6=Sun */
};

/* ── A block rule ── */
enum class BlockSeverity { HARD_BLOCK, SOFT_WARN };

struct ErrorBlock {
    std::string   id;
    std::string   description;
    std::string   category;     /* "TIMING","STRUCTURE","INDICATOR","REGIME" */
    BlockSeverity severity;
    std::string   source;       /* "HARDCODED_v1" or "LEARNED" */
    int           loss_count{0};
    double        total_loss_usd{0};
    /* Predicate: returns true if this block FIRES for the given context */
    std::function<bool(const TradeContext&)> check;
};

/* ── Check result ── */
struct BlockResult {
    bool        allowed{true};
    std::string reason;
    std::string block_id;
    bool        is_hard_block{false};
};

class LearnedBlockStore; /* fwd-decl; defined in learning/learned_block_store.hpp */

/* ── Registry ── */
class ErrorRegistry {
public:
    ErrorRegistry();
    ~ErrorRegistry();

    /**
     * Check a proposed trade against all registered blocks.
     * Returns BlockResult with allowed=false if any HARD_BLOCK fires.
     * SOFT_WARNs log but do not block.
     */
    BlockResult check(const TradeContext &ctx) const;

    /**
     * Attach a SQLite DB for learned-block persistence: load any existing
     * rows into the in-memory registry, and enable write-through so
     * subsequent add_learned_block() calls upsert into the DB.
     * Safe to call multiple times; the latest path wins.
     */
    void load_from_db(const char *db_path);

    /** Add a dynamically learned block (from TradeAnalyzer) */
    void add_learned_block(ErrorBlock block);

    void print_active_blocks() const;

    int hardcoded_count() const;
    int learned_count()   const { return static_cast<int>(learned_.size()); }
    int total_blocks_fired() const { return blocks_fired_; }

private:
    void register_hardcoded();

    std::vector<ErrorBlock>             hardcoded_;
    std::vector<ErrorBlock>             learned_;
    std::unique_ptr<LearnedBlockStore>  store_;
    mutable int                         blocks_fired_{0};
    mutable int                         warns_fired_{0};
};

} /* namespace af */
