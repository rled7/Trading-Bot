/**
 * AlgoForge — tests/test_broker.cpp
 */
#include "test_helpers.hpp"
#include "broker/broker.hpp"
#include "core/types.h"
#include <cstring>
#include <cmath>
#include <functional>
#include <string>
#include <memory>



namespace af { std::unique_ptr<IBroker> make_paper_broker(double); }

void test_broker(TestRunner T) {
    section("PAPER BROKER");

    T("PaperBroker: connects successfully", []{
        auto b = af::make_paper_broker(10000);
        CHK(b->connect() == AF_OK);
        CHK(b->is_connected());
        CHK_FALSE(b->broker_name() == nullptr);
    });

    T("PaperBroker: account info has valid fields", []{
        auto b = af::make_paper_broker(10000);
        b->connect();
        AF_AccountInfo a{};
        CHK(b->get_account(a) == AF_OK);
        CHK_NEAR(a.balance, 10000.0, 0.01);
        CHK_GT(a.equity, 0);
        CHK_GT(a.leverage, 0u);
    });

    T("PaperBroker: get_tick returns valid bid/ask", []{
        auto b = af::make_paper_broker(10000); b->connect();
        AF_Tick t{};
        CHK(b->get_tick("EURUSD", t) == AF_OK);
        CHK_GT(t.bid, 0);
        CHK_GT(t.ask, t.bid);
        CHK(strcmp(t.symbol,"EURUSD")==0);
    });

    T("PaperBroker: invalid symbol returns error", []{
        auto b = af::make_paper_broker(10000); b->connect();
        AF_Tick t{};
        CHK(b->get_tick("FAKESYM", t) == AF_ERR_INVALID_SYMBOL);
    });

    T("PaperBroker: get_bars returns correct count", []{
        auto b = af::make_paper_broker(10000); b->connect();
        AF_Bar bars[50]; int filled=0;
        CHK(b->get_bars("EURUSD", AF_TF_H1, 50, bars, &filled) == AF_OK);
        CHK_EQ(filled, 50);
        for (int i=0;i<filled;i++){
            CHK(bars[i].high >= bars[i].open);
            CHK(bars[i].high >= bars[i].close);
            CHK(bars[i].low  <= bars[i].open);
            CHK(bars[i].low  <= bars[i].close);
        }
    });

    T("PaperBroker: place BUY order creates position", []{
        auto b = af::make_paper_broker(10000); b->connect();
        AF_Order ord{};
        CHK(b->place_order("EURUSD",AF_ORDER_BUY,0.10,0,0,0,1001,"test",ord)==AF_OK);
        CHK_GT(ord.ticket, 0u);
        auto positions = b->get_positions();
        CHK_EQ(positions.size(), 1u);
        CHK(positions[0].side == AF_DIR_LONG);
        CHK_NEAR(positions[0].lots, 0.10, 0.001);
    });

    T("PaperBroker: place SELL order creates short position", []{
        auto b = af::make_paper_broker(10000); b->connect();
        AF_Order ord{};
        CHK(b->place_order("GBPUSD",AF_ORDER_SELL,0.05,0,0,0,1002,"",ord)==AF_OK);
        auto pos = b->get_positions();
        CHK_EQ(pos.size(), 1u);
        CHK(pos[0].side == AF_DIR_SHORT);
    });

    T("PaperBroker: close position removes it", []{
        auto b = af::make_paper_broker(10000); b->connect();
        AF_Order ord{};
        b->place_order("EURUSD",AF_ORDER_BUY,0.10,0,0,0,1001,"",ord);
        CHK(b->close_position(ord.ticket) == AF_OK);
        CHK_EQ(b->get_positions().size(), 0u);
    });

    T("PaperBroker: partial close reduces lots", []{
        auto b = af::make_paper_broker(10000); b->connect();
        AF_Order ord{};
        b->place_order("EURUSD",AF_ORDER_BUY,0.10,0,0,0,1001,"",ord);
        CHK(b->close_position(ord.ticket, 0.05) == AF_OK);
        auto pos = b->get_positions();
        CHK_EQ(pos.size(), 1u);           /* still open */
        CHK_NEAR(pos[0].lots, 0.05, 0.001);
    });

    T("PaperBroker: modify SL/TP updates position", []{
        auto b = af::make_paper_broker(10000); b->connect();
        AF_Order ord{};
        b->place_order("EURUSD",AF_ORDER_BUY,0.10,0,0,0,1001,"",ord);
        CHK(b->modify_position(ord.ticket, 1.0500, 1.1500) == AF_OK);
        auto pos = b->get_position(ord.ticket);
        CHK(pos.has_value());
        CHK_NEAR(pos->sl, 1.0500, 0.00001);
        CHK_NEAR(pos->tp, 1.1500, 0.00001);
    });

    T("PaperBroker: poll_sl_tp does not crash", []{
        auto b = af::make_paper_broker(10000); b->connect();
        AF_Order ord{};
        AF_Tick tick{};
        b->get_tick("EURUSD", tick);
        b->place_order("EURUSD",AF_ORDER_BUY,0.01,0,tick.bid+0.001,tick.ask+0.01,1001,"",ord);
        b->poll_sl_tp(); /* should not throw */
        CHK(true);
    });

    T("PaperBroker: multiple symbols tracked independently", []{
        auto b = af::make_paper_broker(10000); b->connect();
        AF_Order o1{},o2{},o3{};
        b->place_order("EURUSD",AF_ORDER_BUY, 0.10,0,0,0,1001,"",o1);
        b->place_order("GBPUSD",AF_ORDER_SELL,0.05,0,0,0,1002,"",o2);
        b->place_order("XAUUSD",AF_ORDER_BUY, 0.01,0,0,0,1003,"",o3);
        CHK_EQ(b->get_positions().size(), 3u);
        b->close_position(o2.ticket);
        CHK_EQ(b->get_positions().size(), 2u);
    });

    T("PaperBroker: all 4 default symbols have valid ticks", []{
        auto b = af::make_paper_broker(10000); b->connect();
        for (auto sym : {"EURUSD","GBPUSD","USDJPY","XAUUSD"}) {
            AF_Tick t{};
            CHK(b->get_tick(sym, t) == AF_OK);
            CHK_GT(t.bid, 0);
            CHK_GT(t.ask, t.bid);
        }
    });
}
