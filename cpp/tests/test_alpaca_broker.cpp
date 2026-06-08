/**
 * AlgoForge — tests/test_alpaca_broker.cpp
 *
 * Parity tests for the Alpaca broker adapter.
 * Python oracle: python/tests/test_brokers_alpaca.py
 *
 * HTTP mocked via a MockAlpacaBroker overriding do_http_request(); market-data
 * calls go to the data host (full URL) and are matched the same way.
 */

#include "test_helpers.hpp"
#include "broker/rest_broker.hpp"
#include "broker/broker.hpp"

#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <optional>

using namespace af::broker;

static const char* DATA_HOST = "https://data.alpaca.markets/v2";

class MockAlpacaBroker final : public AlpacaBroker {
public:
    struct Entry { int status; std::string body; };
    std::vector<std::pair<std::string, Entry>> routes;
    struct Call { std::string method, url, body; };
    mutable std::vector<Call> calls;

    explicit MockAlpacaBroker(BrokerConfig cfg = BrokerConfig{"alpaca"})
        : AlpacaBroker(std::move(cfg)) { connected_ = true; base_url_ = PAPER_URL; }

    void add_route(const std::string& frag, int status, const std::string& body) {
        routes.push_back({frag, {status, body}});
    }

    using RestBroker::connected_;
    using RestBroker::base_url_;
    using AlpacaBroker::_alloc_ticket;
    using AlpacaBroker::_resolve_key;
    using AlpacaBroker::id_to_ticket_;

protected:
    HttpResponse do_http_request(const std::string& method, const std::string& url,
                                 const std::string& body,
                                 const std::map<std::string,std::string>&) const override {
        calls.push_back({method, url, body});
        for (const auto& [frag, e] : routes)
            if (url.find(frag) != std::string::npos) return {e.status, e.body};
        return {404, R"({"message":"no mock route"})"};
    }
};

static MockAlpacaBroker make_alpaca() {
    BrokerConfig cfg("alpaca");
    cfg.api_key = "KEY"; cfg.api_secret = "SECRET"; cfg.paper = true;
    return MockAlpacaBroker(std::move(cfg));
}

/* did any recorded call match method + url-substr + (optional) body-substr? */
static bool sawCall(const MockAlpacaBroker& b, const std::string& m,
                    const std::string& urlSub, const std::string& bodySub = "") {
    for (const auto& c : b.calls)
        if (c.method == m && c.url.find(urlSub) != std::string::npos &&
            (bodySub.empty() || c.body.find(bodySub) != std::string::npos)) return true;
    return false;
}

static const char* ACCOUNT = R"({"account_number":"PA1234","cash":"10000.50","equity":"11000.25","buying_power":"20000.00","initial_margin":"1000.00","unrealized_pl":"500.00","currency":"USD"})";
static const char* POSITIONS = R"([{"symbol":"AAPL","side":"long","qty":"5","avg_entry_price":"170.00","current_price":"175.00","unrealized_pl":"25.00"},{"symbol":"TSLA","side":"short","qty":"2","avg_entry_price":"250.00","current_price":"245.00","unrealized_pl":"10.00"}])";
static const char* ORDER_RESP = R"({"id":"abc-123-uuid","symbol":"AAPL","side":"buy","type":"market","qty":"10","filled_avg_price":null,"status":"accepted"})";
static const char* QUOTE = R"({"quote":{"bp":175.10,"ap":175.15,"bs":100,"as":200,"t":"2024-01-15T14:30:00Z"}})";
static const char* BARS = R"({"bars":[{"t":"2024-01-15T14:00:00Z","o":174.0,"h":176.0,"l":173.5,"c":175.5,"v":1234567},{"t":"2024-01-15T15:00:00Z","o":175.5,"h":177.0,"l":175.0,"c":176.8,"v":987654}]})";

void test_alpaca_broker(TestRunner T) {

    /* ── 1. connect ── */
    section("CONNECT");
    T("connect_with_valid_creds", []{ MockAlpacaBroker b = make_alpaca(); b.connected_=false; CHK(b.connect()==AF_OK); CHK(b.is_connected()); });
    T("connect_missing_api_key", []{ BrokerConfig c("alpaca"); c.api_key=""; c.api_secret="S"; c.paper=true; MockAlpacaBroker b(std::move(c)); b.connected_=false; CHK(b.connect()!=AF_OK); CHK(!b.is_connected()); });
    T("connect_missing_api_secret", []{ BrokerConfig c("alpaca"); c.api_key="K"; c.api_secret=""; c.paper=true; MockAlpacaBroker b(std::move(c)); b.connected_=false; CHK(b.connect()!=AF_OK); CHK(!b.is_connected()); });
    T("connect_idempotent", []{ MockAlpacaBroker b = make_alpaca(); b.connected_=false; b.connect(); CHK(b.connect()==AF_OK); });
    T("disconnect", []{ MockAlpacaBroker b = make_alpaca(); b.disconnect(); CHK(!b.is_connected()); });

    /* ── 2. get_account ── */
    section("GET ACCOUNT");
    T("account_returns_AF_OK", []{ MockAlpacaBroker b = make_alpaca(); b.add_route("/account",200,ACCOUNT); AF_AccountInfo i{}; CHK(b.get_account(i)==AF_OK); });
    T("account_balance_from_cash", []{ MockAlpacaBroker b = make_alpaca(); b.add_route("/account",200,ACCOUNT); AF_AccountInfo i{}; b.get_account(i); CHK_NEAR(i.balance,10000.50,1e-6); });
    T("account_equity", []{ MockAlpacaBroker b = make_alpaca(); b.add_route("/account",200,ACCOUNT); AF_AccountInfo i{}; b.get_account(i); CHK_NEAR(i.equity,11000.25,1e-6); });
    T("account_free_margin_from_buying_power", []{ MockAlpacaBroker b = make_alpaca(); b.add_route("/account",200,ACCOUNT); AF_AccountInfo i{}; b.get_account(i); CHK_NEAR(i.free_margin,20000.00,1e-6); });
    T("account_margin", []{ MockAlpacaBroker b = make_alpaca(); b.add_route("/account",200,ACCOUNT); AF_AccountInfo i{}; b.get_account(i); CHK_NEAR(i.margin,1000.00,1e-6); });
    T("account_profit", []{ MockAlpacaBroker b = make_alpaca(); b.add_route("/account",200,ACCOUNT); AF_AccountInfo i{}; b.get_account(i); CHK_NEAR(i.profit,500.00,1e-6); });
    T("account_currency", []{ MockAlpacaBroker b = make_alpaca(); b.add_route("/account",200,ACCOUNT); AF_AccountInfo i{}; b.get_account(i); CHK(std::string(i.currency)=="USD"); });
    T("account_login_nonzero_for_present_acct", []{ MockAlpacaBroker b = make_alpaca(); b.add_route("/account",200,ACCOUNT); AF_AccountInfo i{}; b.get_account(i); CHK(i.login!=0); });

    /* ── 3. get_positions ── */
    section("GET POSITIONS");
    T("positions_count_2", []{ MockAlpacaBroker b = make_alpaca(); b.add_route("/positions",200,POSITIONS); CHK_EQ((int)b.get_positions().size(),2); });
    T("aapl_long_mapped", []{
        MockAlpacaBroker b = make_alpaca(); b.add_route("/positions",200,POSITIONS);
        auto ps = b.get_positions(); const AF_Position& a = ps[0];
        CHK(std::string(a.symbol)=="AAPL"); CHK(a.side==AF_DIR_LONG);
        CHK_NEAR(a.lots,5.0,1e-9); CHK_NEAR(a.open_price,170.0,1e-6);
        CHK_NEAR(a.current_price,175.0,1e-6); CHK_NEAR(a.profit,25.0,1e-6);
    });
    T("positions_ticket_int_positive", []{ MockAlpacaBroker b = make_alpaca(); b.add_route("/positions",200,POSITIONS); auto ps=b.get_positions(); for(auto&p:ps) CHK_GT((long long)p.ticket,0); });
    T("positions_ticket_stable", []{ MockAlpacaBroker b = make_alpaca(); b.add_route("/positions",200,POSITIONS); auto p1=b.get_positions(); auto p2=b.get_positions(); CHK_EQ((int)p1[0].ticket,(int)p2[0].ticket); });

    /* ── 4. place_order ── */
    section("PLACE ORDER");
    T("buy_market_order_fields", []{
        MockAlpacaBroker b = make_alpaca(); b.add_route("/orders",200,ORDER_RESP); AF_Order o{};
        b.place_order("AAPL",AF_ORDER_BUY,10.0,0,0,0,0,"",o);
        CHK(std::string(o.symbol)=="AAPL"); CHK(o.type==AF_ORDER_BUY); CHK_NEAR(o.lots,10.0,1e-9); CHK_GT((long long)o.ticket,0);
        CHK(sawCall(b,"POST","/orders","\"buy\"")); CHK(sawCall(b,"POST","/orders","\"market\""));
        CHK(sawCall(b,"POST","/orders","\"10.0\"")); CHK(sawCall(b,"POST","/orders","\"AAPL\""));
    });
    T("sell_market_body_side", []{
        MockAlpacaBroker b = make_alpaca(); b.add_route("/orders",200,R"({"id":"def-456-uuid","side":"sell"})"); AF_Order o{};
        b.place_order("AAPL",AF_ORDER_SELL,5.0,0,0,0,0,"",o);
        CHK(o.type==AF_ORDER_SELL); CHK(sawCall(b,"POST","/orders","\"sell\""));
    });
    T("limit_order_includes_limit_price", []{
        MockAlpacaBroker b = make_alpaca(); b.add_route("/orders",200,R"({"id":"lim-789-uuid"})"); AF_Order o{};
        b.place_order("AAPL",AF_ORDER_BUY_LIMIT,3.0,150.0,0,0,0,"",o);
        CHK(sawCall(b,"POST","/orders","\"limit\"")); CHK(sawCall(b,"POST","/orders","limit_price"));
    });
    T("lots_maps_to_qty_string_7_5", []{
        MockAlpacaBroker b = make_alpaca(); b.add_route("/orders",200,ORDER_RESP); AF_Order o{};
        b.place_order("AAPL",AF_ORDER_BUY,7.5,0,0,0,0,"",o);
        CHK(sawCall(b,"POST","/orders","\"7.5\""));
    });
    T("drops_sl_tp_honestly", []{
        MockAlpacaBroker b = make_alpaca(); b.add_route("/orders",200,ORDER_RESP); AF_Order o{};
        b.place_order("AAPL",AF_ORDER_BUY,10.0,0,140.0,160.0,0,"",o);
        CHK_EQ(o.sl,0.0); CHK_EQ(o.tp,0.0);
        CHK(!sawCall(b,"POST","/orders","stop_price")); CHK(!sawCall(b,"POST","/orders","limit_price"));
    });

    /* ── 5. get_tick (data host) ── */
    section("GET TICK");
    T("tick_maps_and_hits_data_host", []{
        MockAlpacaBroker b = make_alpaca(); b.add_route("quotes/latest",200,QUOTE); AF_Tick t{};
        CHK(b.get_tick("AAPL",t)==AF_OK);
        CHK_NEAR(t.bid,175.10,1e-6); CHK_NEAR(t.ask,175.15,1e-6); CHK(std::string(t.symbol)=="AAPL"); CHK_GT(t.timestamp,0);
        CHK(sawCall(b,"GET",DATA_HOST)); CHK(sawCall(b,"GET","AAPL")); CHK(sawCall(b,"GET","quotes/latest"));
    });

    /* ── 6. get_bars (data host) ── */
    section("GET BARS");
    T("bars_map_and_hit_data_host", []{
        MockAlpacaBroker b = make_alpaca(); b.add_route("/bars",200,BARS); AF_Bar bars[8]; int f=0;
        b.get_bars("AAPL",(AF_Timeframe)3600,2,bars,&f);
        CHK_EQ(f,2); CHK_NEAR(bars[0].open,174.0,1e-6); CHK_NEAR(bars[0].high,176.0,1e-6); CHK_GT(bars[0].timestamp,0);
        CHK(sawCall(b,"GET",DATA_HOST)); CHK(sawCall(b,"GET","/bars"));
        CHK(sawCall(b,"GET","timeframe=1Hour")); CHK(sawCall(b,"GET","limit=2"));
    });
    T("bars_timeframe_1Day_for_86400", []{
        MockAlpacaBroker b = make_alpaca(); b.add_route("/bars",200,BARS); AF_Bar bars[8]; int f=0;
        b.get_bars("AAPL",(AF_Timeframe)86400,5,bars,&f);
        CHK(sawCall(b,"GET","timeframe=1Day"));
    });

    /* ── 7. cancel_order ── */
    section("CANCEL ORDER");
    T("cancel_success", []{
        MockAlpacaBroker b = make_alpaca(); b.add_route("/orders",200,"{}");
        uint64_t tk = b._alloc_ticket("order-uuid-999");
        CHK(b.cancel_order(tk)==AF_OK); CHK(sawCall(b,"DELETE","order-uuid-999"));
    });
    T("cancel_unknown_ticket", []{ MockAlpacaBroker b = make_alpaca(); CHK(b.cancel_order(99999)!=AF_OK); });

    /* ── 8. close_position ── */
    section("CLOSE POSITION");
    T("close_full_success_no_qty", []{
        MockAlpacaBroker b = make_alpaca(); b.add_route("/positions",200,"{}");
        uint64_t tk = b._alloc_ticket("AAPL");
        CHK(b.close_position(tk)==AF_OK);
        CHK(sawCall(b,"DELETE","AAPL")); CHK(!sawCall(b,"DELETE","qty="));
    });
    T("close_partial_sends_qty", []{
        MockAlpacaBroker b = make_alpaca(); b.add_route("/positions",200,"{}");
        uint64_t tk = b._alloc_ticket("TSLA");
        CHK(b.close_position(tk,2.0)==AF_OK);
        CHK(sawCall(b,"DELETE","qty=2.0"));
    });

    /* ── 9. modify_position ── */
    section("MODIFY POSITION");
    T("modify_position_ticket_returns_error_no_api", []{
        MockAlpacaBroker b = make_alpaca();
        uint64_t tk = b._alloc_ticket("AAPL");  /* symbol = position key (no hyphen) */
        CHK(b.modify_position(tk,140.0,160.0)!=AF_OK);
        CHK(b.calls.empty());  /* must NOT touch the network */
    });
    T("modify_order_uuid_attempts_replace", []{
        MockAlpacaBroker b = make_alpaca(); b.add_route("/orders",200,"{}");
        uint64_t tk = b._alloc_ticket("ord-abc-123");  /* UUID = order key (has hyphen) */
        CHK(b.modify_position(tk,140.0,160.0)==AF_OK);
        CHK(sawCall(b,"PATCH","ord-abc-123")); CHK(sawCall(b,"PATCH","ord-abc-123","\"140.0\""));
        CHK(sawCall(b,"PATCH","ord-abc-123","\"160.0\""));
    });
    T("modify_unknown_ticket_returns_error", []{ MockAlpacaBroker b = make_alpaca(); CHK(b.modify_position(99999,1.0,2.0)!=AF_OK); });

    /* ── 10. error path ── */
    section("ERROR PATH");
    T("get_account_http_error_returns_nonzero", []{ MockAlpacaBroker b = make_alpaca(); b.add_route("/account",401,R"({"message":"forbidden"})"); AF_AccountInfo i{}; CHK(b.get_account(i)!=AF_OK); });
}
