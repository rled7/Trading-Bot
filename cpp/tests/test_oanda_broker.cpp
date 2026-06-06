/**
 * AlgoForge — tests/test_oanda_broker.cpp
 *
 * Parity tests for the OANDA v20 broker adapter.
 * Python oracle: python/tests/test_brokers_oanda.py
 *
 * All HTTP is mocked — no real network. A MockOandaBroker subclass overrides
 * do_http_request() to return canned JSON keyed by URL substring and records
 * every request, mirroring the Python pattern of patching broker._request.
 */

#include "test_helpers.hpp"
#include "broker/rest_broker.hpp"
#include "broker/broker.hpp"

#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <optional>

/* oanda_parse_ts has external linkage in oanda_broker.cpp — declare to test directly */
namespace af { namespace broker { int64_t oanda_parse_ts(const std::string&); } }

using namespace af::broker;

/* =========================================================================
 * MockOandaBroker — test double (overrides the HTTP seam)
 * =========================================================================*/
class MockOandaBroker final : public OandaBroker {
public:
    struct Entry { int status; std::string body; };
    std::vector<std::pair<std::string, Entry>> routes;
    struct Call { std::string method, url, body; };
    mutable std::vector<Call> calls;

    explicit MockOandaBroker(BrokerConfig cfg = BrokerConfig{"oanda"})
        : OandaBroker(std::move(cfg)) {
        connected_ = true;
        base_url_  = PAPER_URL;
    }

    void add_route(const std::string& frag, int status, const std::string& body) {
        routes.push_back({frag, {status, body}});
    }

    /* re-expose inherited protected members for white-box parity assertions */
    using RestBroker::connected_;
    using RestBroker::base_url_;

protected:
    HttpResponse do_http_request(const std::string& method,
                                 const std::string& url,
                                 const std::string& body,
                                 const std::map<std::string,std::string>& /*hdrs*/) const override {
        calls.push_back({method, url, body});
        for (const auto& [frag, entry] : routes) {
            if (url.find(frag) != std::string::npos) return {entry.status, entry.body};
        }
        return {404, R"({"errorMessage":"no mock route"})"};
    }
};

static MockOandaBroker make_oanda(const std::string& account_id = "123-456-789") {
    BrokerConfig cfg("oanda");
    cfg.api_key    = "test-key";
    cfg.account_id = account_id;
    cfg.paper      = true;
    return MockOandaBroker(std::move(cfg));
}

/* ── Sample fixtures (mirror the Python module-level fixtures) ── */
static const char* ACCOUNT_SUMMARY = R"({"account":{"balance":"10000.00","NAV":"10050.00","marginUsed":"200.00","marginAvailable":"9800.00","unrealizedPL":"50.00","marginRate":"0.02","currency":"USD","id":"123-456-789"}})";
static const char* PRICING = R"({"prices":[{"instrument":"EUR_USD","bids":[{"price":"1.10990","liquidity":10000000}],"asks":[{"price":"1.11010","liquidity":10000000}],"time":"2024-01-15T10:30:00.000000000Z","status":"tradeable"}]})";
static const char* INSTRUMENTS = R"({"instruments":[{"name":"EUR_USD","type":"CURRENCY","displayPrecision":5,"pipLocation":-4,"minimumTradeSize":"1","maximumOrderUnits":"100000000"}]})";
static const char* CANDLES = R"({"instrument":"EUR_USD","granularity":"H1","candles":[{"time":"2024-01-15T09:00:00.000000000Z","mid":{"o":"1.10500","h":"1.10800","l":"1.10300","c":"1.10700"},"volume":15432,"complete":true},{"time":"2024-01-15T10:00:00.000000000Z","mid":{"o":"1.10700","h":"1.11100","l":"1.10600","c":"1.11000"},"volume":18921,"complete":false}]})";
static const char* ORDER_FILL = R"({"orderFillTransaction":{"id":"5001","type":"ORDER_FILL","price":"1.11010","tradeOpened":{"tradeID":"4001","units":"10000"}}})";
static const char* ORDER_FILL_SELL = R"({"orderFillTransaction":{"id":"5002","type":"ORDER_FILL","price":"1.10990","tradeOpened":{"tradeID":"4002","units":"-10000"}}})";
static const char* OPEN_TRADES = R"({"trades":[{"id":"4001","instrument":"EUR_USD","price":"1.10500","currentUnits":"10000","unrealizedPL":"49.00","financing":"-0.70","stopLossOrder":{"price":"1.10000","timeInForce":"GTC"},"takeProfitOrder":{"price":"1.12000","timeInForce":"GTC"}},{"id":"4002","instrument":"GBP_USD","price":"1.27000","currentUnits":"-5000","currentPrice":"1.26800","unrealizedPL":"10.00","financing":"0.00"}]})";
static const char* PENDING_ORDERS = R"({"orders":[{"id":"7001","type":"LIMIT","instrument":"EUR_USD","units":"10000","price":"1.09000","stopLossOnFill":{"price":"1.08500"},"takeProfitOnFill":{"price":"1.10500"}},{"id":"7002","type":"STOP","instrument":"GBP_USD","units":"-5000","price":"1.28000"}]})";

/* helper: did any recorded call match method + url-substring + (optional) body-substring? */
static bool sawCall(const MockOandaBroker& b, const std::string& method,
                    const std::string& urlSub, const std::string& bodySub = "") {
    for (const auto& c : b.calls)
        if (c.method == method && c.url.find(urlSub) != std::string::npos &&
            (bodySub.empty() || c.body.find(bodySub) != std::string::npos))
            return true;
    return false;
}

/* =========================================================================
 * Test suite
 * =========================================================================*/
void test_oanda_broker(TestRunner T) {

    /* ── 1. connect() success ── */
    section("CONNECT — success");
    T("connect_returns_AF_OK_with_valid_creds", []{
        MockOandaBroker b = make_oanda("acct-123");
        b.connected_ = false;
        CHK(b.connect() == AF_OK);
    });
    T("is_connected_true_after_connect", []{
        MockOandaBroker b = make_oanda("acct-123");
        b.connected_ = false;
        b.connect();
        CHK(b.is_connected());
    });
    T("broker_name_is_oanda", []{
        MockOandaBroker b = make_oanda();
        CHK(std::string(b.broker_name()) == "oanda");
    });

    /* ── 2. connect() failure on missing creds ── */
    section("CONNECT — missing credentials");
    T("connect_missing_api_key_returns_nonzero", []{
        BrokerConfig cfg("oanda"); cfg.api_key = ""; cfg.account_id = "acct-123"; cfg.paper = true;
        MockOandaBroker b(std::move(cfg)); b.connected_ = false;
        CHK(b.connect() != AF_OK);
    });
    T("connect_missing_account_id_returns_nonzero", []{
        BrokerConfig cfg("oanda"); cfg.api_key = "my-key"; cfg.account_id = ""; cfg.paper = true;
        MockOandaBroker b(std::move(cfg)); b.connected_ = false;
        CHK(b.connect() != AF_OK);
    });
    T("connect_empty_creds_returns_nonzero", []{
        BrokerConfig cfg("oanda"); cfg.api_key = ""; cfg.account_id = ""; cfg.paper = true;
        MockOandaBroker b(std::move(cfg)); b.connected_ = false;
        CHK(b.connect() != AF_OK);
    });

    /* ── 3. Symbol mapping ── */
    section("SYMBOL MAPPING");
    T("eurusd_to_eur_usd", []{ MockOandaBroker b = make_oanda(); CHK(b.to_broker_symbol("EURUSD") == "EUR_USD"); });
    T("slash_form_to_oanda",  []{ MockOandaBroker b = make_oanda(); CHK(b.to_broker_symbol("EUR/USD") == "EUR_USD"); });
    T("already_underscore",   []{ MockOandaBroker b = make_oanda(); CHK(b.to_broker_symbol("EUR_USD") == "EUR_USD"); });
    T("from_broker_symbol",   []{ MockOandaBroker b = make_oanda(); CHK(b.from_broker_symbol("EUR_USD") == "EURUSD"); });

    /* ── 4. get_account ── */
    section("GET ACCOUNT");
    T("get_account_returns_AF_OK", []{
        MockOandaBroker b = make_oanda(); b.add_route("summary", 200, ACCOUNT_SUMMARY);
        AF_AccountInfo info{}; CHK(b.get_account(info) == AF_OK);
    });
    T("balance_mapped", []{ MockOandaBroker b = make_oanda(); b.add_route("summary",200,ACCOUNT_SUMMARY); AF_AccountInfo i{}; b.get_account(i); CHK_NEAR(i.balance,10000.0,1e-6); });
    T("equity_from_NAV", []{ MockOandaBroker b = make_oanda(); b.add_route("summary",200,ACCOUNT_SUMMARY); AF_AccountInfo i{}; b.get_account(i); CHK_NEAR(i.equity,10050.0,1e-6); });
    T("margin_used_mapped", []{ MockOandaBroker b = make_oanda(); b.add_route("summary",200,ACCOUNT_SUMMARY); AF_AccountInfo i{}; b.get_account(i); CHK_NEAR(i.margin,200.0,1e-6); });
    T("free_margin_mapped", []{ MockOandaBroker b = make_oanda(); b.add_route("summary",200,ACCOUNT_SUMMARY); AF_AccountInfo i{}; b.get_account(i); CHK_NEAR(i.free_margin,9800.0,1e-6); });
    T("unrealized_pl_as_profit", []{ MockOandaBroker b = make_oanda(); b.add_route("summary",200,ACCOUNT_SUMMARY); AF_AccountInfo i{}; b.get_account(i); CHK_NEAR(i.profit,50.0,1e-6); });
    T("leverage_from_margin_rate", []{ MockOandaBroker b = make_oanda(); b.add_route("summary",200,ACCOUNT_SUMMARY); AF_AccountInfo i{}; b.get_account(i); CHK_EQ((int)i.leverage,50); });
    T("currency_USD", []{ MockOandaBroker b = make_oanda(); b.add_route("summary",200,ACCOUNT_SUMMARY); AF_AccountInfo i{}; b.get_account(i); CHK(std::string(i.currency)=="USD"); });
    T("login_zero_for_string_account", []{ MockOandaBroker b = make_oanda(); b.add_route("summary",200,ACCOUNT_SUMMARY); AF_AccountInfo i{}; b.get_account(i); CHK_EQ((int)i.login,0); });

    /* ── 5. get_tick ── */
    section("GET TICK");
    T("tick_returns_AF_OK", []{ MockOandaBroker b = make_oanda(); b.add_route("pricing",200,PRICING); AF_Tick t{}; CHK(b.get_tick("EURUSD",t)==AF_OK); });
    T("tick_symbol_preserved", []{ MockOandaBroker b = make_oanda(); b.add_route("pricing",200,PRICING); AF_Tick t{}; b.get_tick("EURUSD",t); CHK(std::string(t.symbol)=="EURUSD"); });
    T("tick_bid_mapped", []{ MockOandaBroker b = make_oanda(); b.add_route("pricing",200,PRICING); AF_Tick t{}; b.get_tick("EURUSD",t); CHK_NEAR(t.bid,1.10990,1e-6); });
    T("tick_ask_mapped", []{ MockOandaBroker b = make_oanda(); b.add_route("pricing",200,PRICING); AF_Tick t{}; b.get_tick("EURUSD",t); CHK_NEAR(t.ask,1.11010,1e-6); });
    T("tick_timestamp_parsed", []{ MockOandaBroker b = make_oanda(); b.add_route("pricing",200,PRICING); AF_Tick t{}; b.get_tick("EURUSD",t); CHK_GT(t.timestamp,0); });

    /* ── 6. get_symbol_info ── */
    section("GET SYMBOL INFO");
    T("symbol_info_returns_AF_OK", []{ MockOandaBroker b = make_oanda(); b.add_route("instruments",200,INSTRUMENTS); AF_SymbolInfo s{}; CHK(b.get_symbol_info("EURUSD",s)==AF_OK); });
    T("digits_correct", []{ MockOandaBroker b = make_oanda(); b.add_route("instruments",200,INSTRUMENTS); AF_SymbolInfo s{}; b.get_symbol_info("EURUSD",s); CHK_EQ(s.digits,5); });
    T("point_correct", []{ MockOandaBroker b = make_oanda(); b.add_route("instruments",200,INSTRUMENTS); AF_SymbolInfo s{}; b.get_symbol_info("EURUSD",s); CHK_NEAR(s.point,0.00001,1e-10); });
    T("volume_min", []{ MockOandaBroker b = make_oanda(); b.add_route("instruments",200,INSTRUMENTS); AF_SymbolInfo s{}; b.get_symbol_info("EURUSD",s); CHK_NEAR(s.volume_min,0.00001,1e-10); });

    /* ── 7. get_bars ── */
    section("GET BARS");
    T("bars_correct_count", []{ MockOandaBroker b = make_oanda(); b.add_route("candles",200,CANDLES); AF_Bar bars[8]; int f=0; b.get_bars("EURUSD",(AF_Timeframe)3600,2,bars,&f); CHK_EQ(f,2); });
    T("bar_ohlc_mapped", []{
        MockOandaBroker b = make_oanda(); b.add_route("candles",200,CANDLES);
        AF_Bar bars[8]; int f=0; b.get_bars("EURUSD",(AF_Timeframe)3600,2,bars,&f);
        CHK_NEAR(bars[0].open,1.10500,1e-6); CHK_NEAR(bars[0].high,1.10800,1e-6);
        CHK_NEAR(bars[0].low,1.10300,1e-6);  CHK_NEAR(bars[0].close,1.10700,1e-6);
    });
    T("bar_volume_mapped", []{ MockOandaBroker b = make_oanda(); b.add_route("candles",200,CANDLES); AF_Bar bars[8]; int f=0; b.get_bars("EURUSD",(AF_Timeframe)3600,2,bars,&f); CHK_NEAR(bars[0].volume,15432.0,1e-6); });
    T("bar_timestamp_parsed", []{ MockOandaBroker b = make_oanda(); b.add_route("candles",200,CANDLES); AF_Bar bars[8]; int f=0; b.get_bars("EURUSD",(AF_Timeframe)3600,2,bars,&f); CHK_GT(bars[0].timestamp,0); });
    T("granularity_M1_for_60s", []{
        MockOandaBroker b = make_oanda(); b.add_route("candles",200,CANDLES);
        AF_Bar bars[8]; int f=0; b.get_bars("EURUSD",(AF_Timeframe)60,5,bars,&f);
        CHK(sawCall(b,"GET","granularity=M1"));
    });
    T("empty_candles_returns_empty", []{
        MockOandaBroker b = make_oanda(); b.add_route("candles",200,R"({"candles":[]})");
        AF_Bar bars[8]; int f=99; b.get_bars("EURUSD",(AF_Timeframe)3600,10,bars,&f); CHK_EQ(f,0);
    });

    /* ── 8. place_order BUY market ── */
    section("PLACE ORDER — BUY");
    T("buy_returns_AF_OK", []{ MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL); AF_Order o{}; CHK(b.place_order("EURUSD",AF_ORDER_BUY,0.10,0,0,0,0,"",o)==AF_OK); });
    T("buy_ticket_is_trade_id", []{ MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL); AF_Order o{}; b.place_order("EURUSD",AF_ORDER_BUY,0.10,0,0,0,0,"",o); CHK_EQ((int)o.ticket,4001); });
    T("buy_fill_price_mapped", []{ MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL); AF_Order o{}; b.place_order("EURUSD",AF_ORDER_BUY,0.10,0,0,0,0,"",o); CHK_NEAR(o.fill_price,1.11010,1e-6); });
    T("buy_type_preserved", []{ MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL); AF_Order o{}; b.place_order("EURUSD",AF_ORDER_BUY,0.10,0,0,0,0,"",o); CHK(o.type==AF_ORDER_BUY); });
    T("buy_lots_preserved", []{ MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL); AF_Order o{}; b.place_order("EURUSD",AF_ORDER_BUY,0.10,0,0,0,0,"",o); CHK_NEAR(o.lots,0.10,1e-9); });
    T("buy_units_positive_10000_in_request", []{
        MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL); AF_Order o{};
        b.place_order("EURUSD",AF_ORDER_BUY,0.10,0,0,0,0,"",o);
        CHK(sawCall(b,"POST","orders","\"10000\""));
    });
    T("buy_sl_tp_attached_when_nonzero", []{
        MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL); AF_Order o{};
        b.place_order("EURUSD",AF_ORDER_BUY,0.10,0,1.10000,1.12000,0,"",o);
        CHK(sawCall(b,"POST","orders","stopLossOnFill"));
        CHK(sawCall(b,"POST","orders","takeProfitOnFill"));
        CHK(sawCall(b,"POST","orders","1.10000"));
        CHK(sawCall(b,"POST","orders","1.12000"));
    });
    T("buy_magic_and_comment_preserved", []{
        MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL); AF_Order o{};
        b.place_order("EURUSD",AF_ORDER_BUY,0.10,0,0,0,42,"test comment",o);
        CHK_EQ((int)o.magic,42); CHK(std::string(o.comment)=="test comment");
    });

    /* ── 9. place_order SELL ── */
    section("PLACE ORDER — SELL");
    T("sell_units_negative_in_request", []{
        MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL_SELL); AF_Order o{};
        b.place_order("EURUSD",AF_ORDER_SELL,0.10,0,0,0,0,"",o);
        CHK(sawCall(b,"POST","orders","\"-10000\""));
    });
    T("sell_ticket_returned", []{ MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL_SELL); AF_Order o{}; b.place_order("EURUSD",AF_ORDER_SELL,0.10,0,0,0,0,"",o); CHK_EQ((int)o.ticket,4002); });
    T("sell_05_lots_to_minus_5000", []{
        MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL_SELL); AF_Order o{};
        b.place_order("EURUSD",AF_ORDER_SELL,0.05,0,0,0,0,"",o);
        CHK(sawCall(b,"POST","orders","\"-5000\""));
    });

    /* ── 10. close_position ── */
    section("CLOSE POSITION");
    T("close_returns_AF_OK", []{ MockOandaBroker b = make_oanda(); b.add_route("close",200,R"({"orderFillTransaction":{}})"); CHK(b.close_position(4001)==AF_OK); });
    T("close_uses_PUT_with_ticket_and_close", []{
        MockOandaBroker b = make_oanda(); b.add_route("close",200,"{}");
        b.close_position(4001);
        CHK(sawCall(b,"PUT","4001")); CHK(sawCall(b,"PUT","close"));
    });
    T("close_nonzero_on_broker_error", []{ MockOandaBroker b = make_oanda(); b.add_route("close",404,R"({"errorMessage":"trade not found"})"); CHK(b.close_position(9999)!=AF_OK); });
    T("partial_close_sends_units", []{
        MockOandaBroker b = make_oanda(); b.add_route("close",200,"{}");
        b.close_position(4001,0.05);
        CHK(sawCall(b,"PUT","close","\"5000\""));
    });

    /* ── 11. modify_position ── */
    section("MODIFY POSITION");
    T("modify_returns_AF_OK", []{ MockOandaBroker b = make_oanda(); b.add_route("trades",200,"{}"); CHK(b.modify_position(4001,1.09500,1.12500)==AF_OK); });
    T("modify_uses_PUT", []{ MockOandaBroker b = make_oanda(); b.add_route("trades",200,"{}"); b.modify_position(4001,1.09500,1.12500); CHK(sawCall(b,"PUT","trades")); });
    T("modify_nonzero_on_error", []{ MockOandaBroker b = make_oanda(); b.add_route("trades",404,R"({"errorMessage":"trade closed"})"); CHK(b.modify_position(9999,1.09000,1.12000)!=AF_OK); });
    T("modify_sl_tp_in_body", []{
        MockOandaBroker b = make_oanda(); b.add_route("trades",200,"{}");
        b.modify_position(4001,1.09500,1.12500);
        CHK(sawCall(b,"PUT","trades","stopLoss")); CHK(sawCall(b,"PUT","trades","takeProfit"));
        CHK(sawCall(b,"PUT","trades","1.09500")); CHK(sawCall(b,"PUT","trades","1.12500"));
    });

    /* ── 12. cancel_order ── */
    section("CANCEL ORDER");
    T("cancel_returns_AF_OK", []{ MockOandaBroker b = make_oanda(); b.add_route("cancel",200,"{}"); CHK(b.cancel_order(7001)==AF_OK); });
    T("cancel_uses_PUT_with_ticket_and_cancel", []{
        MockOandaBroker b = make_oanda(); b.add_route("cancel",200,"{}");
        b.cancel_order(7001);
        CHK(sawCall(b,"PUT","7001")); CHK(sawCall(b,"PUT","cancel"));
    });
    T("cancel_nonzero_on_error", []{ MockOandaBroker b = make_oanda(); b.add_route("cancel",404,R"({"errorMessage":"order not found"})"); CHK(b.cancel_order(9999)!=AF_OK); });

    /* ── 13. get_positions ── */
    section("GET POSITIONS");
    T("positions_count_2", []{ MockOandaBroker b = make_oanda(); b.add_route("openTrades",200,OPEN_TRADES); CHK_EQ((int)b.get_positions().size(),2); });
    T("long_position_mapped", []{
        MockOandaBroker b = make_oanda(); b.add_route("openTrades",200,OPEN_TRADES);
        auto ps = b.get_positions(); const AF_Position* lp=nullptr;
        for (auto& p : ps) if (p.ticket==4001) lp=&p;
        CHK(lp!=nullptr); CHK(lp->side==AF_DIR_LONG); CHK_NEAR(lp->lots,0.10,1e-9);
        CHK_NEAR(lp->open_price,1.10500,1e-6); CHK_NEAR(lp->current_price,1.10500,1e-6);
        CHK_NEAR(lp->profit,49.0,1e-6); CHK_NEAR(lp->sl,1.10000,1e-6); CHK_NEAR(lp->tp,1.12000,1e-6);
    });
    T("short_position_mapped", []{
        MockOandaBroker b = make_oanda(); b.add_route("openTrades",200,OPEN_TRADES);
        auto ps = b.get_positions(); const AF_Position* sp=nullptr;
        for (auto& p : ps) if (p.ticket==4002) sp=&p;
        CHK(sp!=nullptr); CHK(sp->side==AF_DIR_SHORT); CHK_NEAR(sp->lots,0.05,1e-9);
    });
    T("position_symbol_mapping_applied", []{
        MockOandaBroker b = make_oanda(); b.add_route("openTrades",200,OPEN_TRADES);
        auto ps = b.get_positions(); bool eur=false,gbp=false;
        for (auto& p : ps){ if(std::string(p.symbol)=="EURUSD")eur=true; if(std::string(p.symbol)=="GBPUSD")gbp=true; }
        CHK(eur); CHK(gbp);
    });

    /* ── 14. get_position by ticket ── */
    section("GET POSITION BY TICKET");
    T("get_position_found", []{ MockOandaBroker b = make_oanda(); b.add_route("openTrades",200,OPEN_TRADES); auto p=b.get_position(4001); CHK(p.has_value()); CHK_EQ((int)p->ticket,4001); });
    T("get_position_not_found_nullopt", []{ MockOandaBroker b = make_oanda(); b.add_route("openTrades",200,OPEN_TRADES); CHK(!b.get_position(9999).has_value()); });

    /* ── 15. get_orders ── */
    section("GET ORDERS");
    T("orders_count_2", []{ MockOandaBroker b = make_oanda(); b.add_route("pendingOrders",200,PENDING_ORDERS); CHK_EQ((int)b.get_orders().size(),2); });
    T("buy_limit_order_mapped", []{
        MockOandaBroker b = make_oanda(); b.add_route("pendingOrders",200,PENDING_ORDERS);
        auto os = b.get_orders(); const AF_Order* bl=nullptr;
        for (auto& o : os) if (o.ticket==7001) bl=&o;
        CHK(bl!=nullptr); CHK(bl->type==AF_ORDER_BUY_LIMIT); CHK_NEAR(bl->lots,0.10,1e-9); CHK_NEAR(bl->price,1.09000,1e-6);
    });
    T("sell_stop_order_mapped", []{
        MockOandaBroker b = make_oanda(); b.add_route("pendingOrders",200,PENDING_ORDERS);
        auto os = b.get_orders(); const AF_Order* ss=nullptr;
        for (auto& o : os) if (o.ticket==7002) ss=&o;
        CHK(ss!=nullptr); CHK(ss->type==AF_ORDER_SELL_STOP); CHK_NEAR(ss->lots,0.05,1e-9);
    });

    /* ── 16. error propagation ── */
    section("ERROR PROPAGATION");
    T("get_account_error_returns_nonzero", []{ MockOandaBroker b = make_oanda(); b.add_route("summary",401,R"({"errorMessage":"unauthorized"})"); AF_AccountInfo i{}; CHK(b.get_account(i)!=AF_OK); });
    T("get_tick_no_prices_returns_nonzero", []{ MockOandaBroker b = make_oanda(); b.add_route("pricing",200,R"({"prices":[]})"); AF_Tick t{}; CHK(b.get_tick("EURUSD",t)!=AF_OK); });
    T("place_order_malformed_returns_nonzero", []{ MockOandaBroker b = make_oanda(); b.add_route("orders",200,R"({"unexpectedKey":{}})"); AF_Order o{}; CHK(b.place_order("EURUSD",AF_ORDER_BUY,0.10,0,0,0,0,"",o)!=AF_OK); });

    /* ── 17. timestamp parser ── */
    section("TIMESTAMP PARSER");
    T("ts_nanosecond_z", []{ CHK_GT(oanda_parse_ts("2024-01-15T10:30:00.000000000Z"),0); });
    T("ts_microsecond",  []{ CHK_GT(oanda_parse_ts("2024-01-15T10:30:00.123456Z"),0); });
    T("ts_no_fractional",[]{ CHK_GT(oanda_parse_ts("2024-01-15T10:30:00Z"),0); });

    /* ── 18. lots→units conversion ── */
    section("LOTS → UNITS");
    T("1_lot_buy_is_100000", []{ MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL); AF_Order o{}; b.place_order("EURUSD",AF_ORDER_BUY,1.0,0,0,0,0,"",o); CHK(sawCall(b,"POST","orders","\"100000\"")); });
    T("001_lot_buy_is_1000", []{ MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL); AF_Order o{}; b.place_order("EURUSD",AF_ORDER_BUY,0.01,0,0,0,0,"",o); CHK(sawCall(b,"POST","orders","\"1000\"")); });
    T("05_lot_sell_is_minus_50000", []{ MockOandaBroker b = make_oanda(); b.add_route("orders",200,ORDER_FILL_SELL); AF_Order o{}; b.place_order("EURUSD",AF_ORDER_SELL,0.5,0,0,0,0,"",o); CHK(sawCall(b,"POST","orders","\"-50000\"")); });
}
