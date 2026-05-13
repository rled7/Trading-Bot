/**
 * AlgoForge — tests/test_learning.cpp
 */
#include "tests/test_helpers.hpp"
#include "learning/error_registry.hpp"
#include "core/types.h"
#include <cstring>
#include <functional>
#include <string>



using namespace af;

static TradeContext make_ctx(
    AF_Direction dir=AF_DIR_LONG,
    const char *sym="EURUSD",
    const char *algo="trend",
    const char *mstruct="BULL",
    const char *macro="BULL",
    double rsi=52, double adx=26,
    double vol=1.2, double conf=65,
    int hour=10, int dow=1)
{
    TradeContext c{};
    c.direction = dir;
    snprintf(c.symbol,          sizeof(c.symbol),           "%s", sym);
    snprintf(c.algo_type,       sizeof(c.algo_type),        "%s", algo);
    snprintf(c.market_structure,sizeof(c.market_structure), "%s", mstruct);
    snprintf(c.macro_structure, sizeof(c.macro_structure),  "%s", macro);
    c.rsi=rsi; c.adx=adx; c.volume_ratio=vol;
    c.confluence_score=conf; c.hour_utc=hour; c.day_of_week=dow;
    return c;
}

void test_learning(TestRunner T) {
    section("LEARNING SYSTEM — ERROR REGISTRY");

    T("ErrorRegistry: loads 8+ hardcoded blocks on construction", []{
        ErrorRegistry er;
        CHK_GE(er.hardcoded_count(), 8);
    });

    T("ErrorRegistry: ALLOWS valid trade context", []{
        ErrorRegistry er;
        auto ctx = make_ctx(AF_DIR_LONG,"EURUSD","trend","BULL","BULL",52,26,1.2,65,10,1);
        auto r = er.check(ctx);
        CHK(r.allowed);
    });

    T("ErrorRegistry: blocks dead-hour trades (02:00 UTC)", []{
        ErrorRegistry er;
        auto ctx = make_ctx(AF_DIR_LONG,"EURUSD","trend","BULL","BULL",52,26,1.2,65,2,1);
        auto r = er.check(ctx);
        CHK_FALSE(r.allowed);
        CHK(r.is_hard_block);
    });

    T("ErrorRegistry: blocks Friday 20:00+ UTC", []{
        ErrorRegistry er;
        auto ctx = make_ctx(AF_DIR_LONG,"EURUSD","trend","BULL","BULL",52,26,1.2,65,21,4);
        auto r = er.check(ctx);
        CHK_FALSE(r.allowed);
    });

    T("ErrorRegistry: blocks LONG in STRONG_BEAR macro structure", []{
        ErrorRegistry er;
        auto ctx = make_ctx(AF_DIR_LONG,"EURUSD","trend","BEAR","STRONG_BEAR",52,26,1.2,65,10,1);
        auto r = er.check(ctx);
        CHK_FALSE(r.allowed);
        CHK(r.is_hard_block);
    });

    T("ErrorRegistry: blocks SHORT in STRONG_BULL macro structure", []{
        ErrorRegistry er;
        auto ctx = make_ctx(AF_DIR_SHORT,"EURUSD","trend","BULL","STRONG_BULL",52,26,1.2,65,10,1);
        auto r = er.check(ctx);
        CHK_FALSE(r.allowed);
        CHK(r.is_hard_block);
    });

    T("ErrorRegistry: blocks overbought LONG (RSI >= 68)", []{
        ErrorRegistry er;
        auto ctx = make_ctx(AF_DIR_LONG,"EURUSD","trend","BULL","BULL",72,26,1.2,65,10,1);
        auto r = er.check(ctx);
        CHK_FALSE(r.allowed);
        CHK(r.is_hard_block);
    });

    T("ErrorRegistry: blocks oversold SHORT (RSI <= 32)", []{
        ErrorRegistry er;
        auto ctx = make_ctx(AF_DIR_SHORT,"EURUSD","trend","BEAR","BEAR",28,26,1.2,65,10,1);
        auto r = er.check(ctx);
        CHK_FALSE(r.allowed);
        CHK(r.is_hard_block);
    });

    T("ErrorRegistry: blocks trend algo when ADX < 18", []{
        ErrorRegistry er;
        auto ctx = make_ctx(AF_DIR_LONG,"EURUSD","trend","RANGING","RANGING",52,15,1.2,65,10,1);
        auto r = er.check(ctx);
        CHK_FALSE(r.allowed);
        CHK(r.is_hard_block);
    });

    T("ErrorRegistry: blocks weak confluence (score < 50)", []{
        ErrorRegistry er;
        auto ctx = make_ctx(AF_DIR_LONG,"EURUSD","trend","BULL","BULL",52,26,1.2,42,10,1);
        auto r = er.check(ctx);
        CHK_FALSE(r.allowed);
    });

    T("ErrorRegistry: USDJPY Tokyo session NOT blocked (23:00 = blocked, 03:00 = allowed)", []{
        ErrorRegistry er;
        /* 03:00 UTC — should be allowed for JPY pairs */
        auto ctx_jpy = make_ctx(AF_DIR_LONG,"USDJPY","trend","BULL","BULL",52,26,1.2,65,3,1);
        auto r = er.check(ctx_jpy);
        CHK(r.allowed);  /* USDJPY dead hours are only 22-23, not 0-3 */
    });

    T("ErrorRegistry: USDJPY 22:00 UTC IS blocked", []{
        ErrorRegistry er;
        auto ctx = make_ctx(AF_DIR_LONG,"USDJPY","trend","BULL","BULL",52,26,1.2,65,22,1);
        auto r = er.check(ctx);
        CHK_FALSE(r.allowed);
    });

    T("ErrorRegistry: block_reason is non-empty when blocked", []{
        ErrorRegistry er;
        auto ctx = make_ctx(AF_DIR_LONG,"EURUSD","trend","BULL","BULL",72,26,1.2,65,10,1);
        auto r = er.check(ctx);
        CHK_FALSE(r.reason.empty());
        CHK_FALSE(r.block_id.empty());
    });

    T("ErrorRegistry: learned block added and checked", []{
        ErrorRegistry er;
        ErrorBlock b;
        b.id          = "TEST_LEARNED_BLOCK";
        b.description = "Test: block EURUSD at noon";
        b.category    = "TEST";
        b.severity    = BlockSeverity::HARD_BLOCK;
        b.loss_count  = 10;  /* above escalation threshold */
        b.total_loss_usd = -500.0;
        b.check = [](const TradeContext &ctx) {
            return std::string(ctx.symbol) == "EURUSD" && ctx.hour_utc == 12;
        };
        er.add_learned_block(std::move(b));
        CHK(er.learned_count() == 1);

        /* Should be blocked */
        auto bad = make_ctx(AF_DIR_LONG,"EURUSD","trend","BULL","BULL",52,26,1.2,65,12,1);
        CHK_FALSE(er.check(bad).allowed);

        /* Different symbol: allowed */
        auto ok = make_ctx(AF_DIR_LONG,"GBPUSD","trend","BULL","BULL",52,26,1.2,65,12,1);
        CHK(er.check(ok).allowed);
    });

    T("ErrorRegistry: stats track fired block count", []{
        ErrorRegistry er;
        auto ctx = make_ctx(AF_DIR_LONG,"EURUSD","trend","BULL","BULL",52,26,1.2,65,2,1);
        er.check(ctx);
        er.check(ctx);
        CHK_GE(er.total_blocks_fired(), 2);
    });

    T("ErrorRegistry: breakout algo allowed with ADX >= 18", []{
        ErrorRegistry er;
        auto ctx = make_ctx(AF_DIR_LONG,"EURUSD","breakout","BULL","BULL",52,22,1.2,65,10,1);
        auto r = er.check(ctx);
        CHK(r.allowed);
    });

    T("ErrorRegistry: scalper algo not blocked by ADX rule", []{
        ErrorRegistry er;
        /* scalper is not in the {trend, breakout} set */
        auto ctx = make_ctx(AF_DIR_LONG,"EURUSD","scalper","RANGING","RANGING",52,12,1.2,65,10,1);
        /* May still be blocked by confluence or other rules — just check ADX rule doesn't fire */
        auto r = er.check(ctx);
        /* We just verify no crash and block_id (if blocked) is not the ADX rule */
        if (!r.allowed) CHK(r.block_id != "TREND_ALGO_IN_RANGE_ADX_LT_18");
    });
}
