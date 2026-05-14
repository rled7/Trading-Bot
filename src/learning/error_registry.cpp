/**
 * AlgoForge — src/learning/error_registry.cpp
 */
#include "learning/error_registry.hpp"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace af {

ErrorRegistry::ErrorRegistry() {
    register_hardcoded();
}

void ErrorRegistry::register_hardcoded() {
    /* ── TIMING ── */
    hardcoded_.push_back({
        "ENTRY_DEAD_HOURS",
        "No entries between 22:00-04:00 UTC (low liquidity, wide spreads)",
        "TIMING", BlockSeverity::HARD_BLOCK, "HARDCODED_v1", 0, 0,
        [](const TradeContext &ctx) {
            int h = ctx.hour_utc;
            /* JPY pairs: narrower dead hours (22-23 only) */
            std::string sym(ctx.symbol);
            bool is_jpy = sym.find("JPY") != std::string::npos;
            if (is_jpy) return h == 22 || h == 23;
            return h == 22 || h == 23 || h == 0 || h == 1 || h == 2 || h == 3;
        }
    });

    hardcoded_.push_back({
        "ENTRY_FRIDAY_LATE",
        "No new positions Friday after 20:00 UTC (weekend gap risk)",
        "TIMING", BlockSeverity::HARD_BLOCK, "HARDCODED_v1", 0, 0,
        [](const TradeContext &ctx) {
            return ctx.day_of_week == 4 && ctx.hour_utc >= 20;
        }
    });

    /* ── STRUCTURAL ── */
    hardcoded_.push_back({
        "COUNTER_TREND_LONG_IN_BEAR",
        "No LONG entries when macro structure is STRONG_BEAR or BEAR",
        "STRUCTURE", BlockSeverity::HARD_BLOCK, "HARDCODED_v1", 0, 0,
        [](const TradeContext &ctx) {
            std::string ms(ctx.macro_structure);
            return ctx.direction == AF_DIR_LONG &&
                   (ms == "STRONG_BEAR" || ms == "BEAR");
        }
    });

    hardcoded_.push_back({
        "COUNTER_TREND_SHORT_IN_BULL",
        "No SHORT entries when macro structure is STRONG_BULL or BULL",
        "STRUCTURE", BlockSeverity::HARD_BLOCK, "HARDCODED_v1", 0, 0,
        [](const TradeContext &ctx) {
            std::string ms(ctx.macro_structure);
            return ctx.direction == AF_DIR_SHORT &&
                   (ms == "STRONG_BULL" || ms == "BULL");
        }
    });

    /* ── INDICATOR ── */
    hardcoded_.push_back({
        "OVERBOUGHT_LONG_RSI_GTE_68",
        "No LONG entries when RSI_14 >= 68 (buying into overbought)",
        "INDICATOR", BlockSeverity::HARD_BLOCK, "HARDCODED_v1", 0, 0,
        [](const TradeContext &ctx) {
            return ctx.direction == AF_DIR_LONG && ctx.rsi >= 68.0;
        }
    });

    hardcoded_.push_back({
        "OVERSOLD_SHORT_RSI_LTE_32",
        "No SHORT entries when RSI_14 <= 32 (selling into oversold)",
        "INDICATOR", BlockSeverity::HARD_BLOCK, "HARDCODED_v1", 0, 0,
        [](const TradeContext &ctx) {
            return ctx.direction == AF_DIR_SHORT && ctx.rsi <= 32.0;
        }
    });

    /* ── REGIME ── */
    hardcoded_.push_back({
        "TREND_ALGO_IN_RANGE_ADX_LT_18",
        "Trend/breakout algos blocked when ADX < 18 (no trend to follow)",
        "REGIME", BlockSeverity::HARD_BLOCK, "HARDCODED_v1", 0, 0,
        [](const TradeContext &ctx) {
            std::string at(ctx.algo_type);
            return (at == "trend" || at == "breakout") && ctx.adx < 18.0;
        }
    });

    hardcoded_.push_back({
        "WEAK_CONFLUENCE_ENTRY",
        "No entries when confluence score < 50 (signals not aligned)",
        "INDICATOR", BlockSeverity::HARD_BLOCK, "HARDCODED_v1", 0, 0,
        [](const TradeContext &ctx) {
            return ctx.confluence_score < 50.0;
        }
    });

    /* ── SOFT WARN ── */
    hardcoded_.push_back({
        "DRY_VOLUME_ENTRY",
        "Volume < 40% of average (low conviction — watch for false break)",
        "INDICATOR", BlockSeverity::SOFT_WARN, "HARDCODED_v1", 0, 0,
        [](const TradeContext &ctx) {
            return ctx.volume_ratio < 0.40;
        }
    });
}

BlockResult ErrorRegistry::check(const TradeContext &ctx) const {
    /* 1. Hardcoded blocks (permanent) */
    for (auto &b : hardcoded_) {
        if (b.check(ctx)) {
            if (b.severity == BlockSeverity::HARD_BLOCK) {
                blocks_fired_++;
                return { false, b.description, b.id, true };
            } else {
                warns_fired_++;
                printf("[WARN:%s] %s\n", b.id.c_str(), b.description.c_str());
            }
        }
    }

    /* 2. Learned blocks (from DB) */
    for (auto &b : learned_) {
        if (b.check && b.check(ctx)) {
            if (b.loss_count >= 5 || b.severity == BlockSeverity::HARD_BLOCK) {
                blocks_fired_++;
                return { false,
                         b.description + " (fired " + std::to_string(b.loss_count) +
                         " times, $" + std::to_string((int)std::abs(b.total_loss_usd)) + " lost)",
                         b.id, true };
            } else {
                warns_fired_++;
                printf("[LEARNED_WARN:%s] %s (x%d)\n",
                       b.id.c_str(), b.description.c_str(), b.loss_count);
            }
        }
    }

    return { true, "", "", false };
}

void ErrorRegistry::add_learned_block(ErrorBlock block) {
    /* Upsert by ID */
    for (auto &b : learned_) {
        if (b.id == block.id) {
            b.loss_count++;
            b.total_loss_usd += block.total_loss_usd;
            return;
        }
    }
    learned_.push_back(std::move(block));
}

int ErrorRegistry::hardcoded_count() const {
    return (int)std::count_if(hardcoded_.begin(), hardcoded_.end(),
                               [](const ErrorBlock &b){ return b.severity==BlockSeverity::HARD_BLOCK; });
}

void ErrorRegistry::print_active_blocks() const {
    printf("\n%s\n", std::string(60,'=').c_str());
    printf("  ErrorRegistry — %d hardcoded + %d learned blocks\n",
           (int)hardcoded_.size(), (int)learned_.size());
    printf("%s\n", std::string(60,'=').c_str());
    printf("  HARDCODED (permanent):\n");
    for (auto &b : hardcoded_) {
        printf("    %s [%s] %s\n",
               b.severity==BlockSeverity::HARD_BLOCK?"BLOCK":"WARN ",
               b.id.c_str(), b.description.c_str());
    }
    if (!learned_.empty()) {
        printf("  LEARNED FROM LOSSES:\n");
        for (auto &b : learned_) {
            printf("    BLOCK [%s] x%d losses $%.0f — %s\n",
                   b.id.c_str(), b.loss_count,
                   std::abs(b.total_loss_usd), b.description.c_str());
        }
    }
    printf("%s\n\n", std::string(60,'=').c_str());
}

void ErrorRegistry::load_from_db(const char *) {
    /* SQLite loading would go here — stub for now */
}

} /* namespace af */
