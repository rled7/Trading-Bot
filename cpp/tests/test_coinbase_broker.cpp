/**
 * AlgoForge — tests/test_coinbase_broker.cpp
 *
 * Parity tests for the Coinbase Advanced Trade broker adapter.
 * Python oracle: python/tests/test_brokers_coinbase.py
 *
 * The sign() test asserts a value independently verified with
 * `openssl dgst -sha256 -hmac` (see commit message) — not self-referential.
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

class MockCoinbaseBroker final : public CoinbaseBroker {
public:
    struct Entry { int status; std::string body; };
    std::vector<std::pair<std::string, Entry>> routes;
    struct Call { std::string method, url, body; };
    mutable std::vector<Call> calls;

    explicit MockCoinbaseBroker(BrokerConfig cfg = BrokerConfig{"coinbase"})
        : CoinbaseBroker(std::move(cfg)) { connected_ = true; base_url_ = LIVE_URL; }

    void add_route(const std::string& f, int s, const std::string& b) { routes.push_back({f,{s,b}}); }

    using RestBroker::connected_;
    using RestBroker::base_url_;
    using CoinbaseBroker::_register_order_id;

protected:
    HttpResponse do_http_request(const std::string& method, const std::string& url,
                                 const std::string& body,
                                 const std::map<std::string,std::string>&) const override {
        calls.push_back({method, url, body});
        for (const auto& [f, e] : routes) if (url.find(f) != std::string::npos) return {e.status, e.body};
        return {404, R"({"error":"no mock route"})"};
    }
};

static MockCoinbaseBroker make_cb() {
    BrokerConfig cfg("coinbase");
    cfg.api_key = "test_api_key"; cfg.api_secret = "test_api_secret"; cfg.paper = true;
    return MockCoinbaseBroker(std::move(cfg));
}

void test_coinbase_broker(TestRunner T) {

    /* ── 1. sign / HMAC ── */
    section("SIGN (HMAC-SHA256)");
    T("signature_matches_openssl_reference", []{
        MockCoinbaseBroker b = make_cb();
        /* verified: openssl dgst -sha256 -hmac test_api_secret over "1716000000GET/api/v3/brokerage/accounts" */
        CHK(b.sign("1716000000","GET","/api/v3/brokerage/accounts","")
            == "f80c7e569aa487bc3fcf6b80b23da530db686e5634669f59fc18f68cf3828ee4");
    });
    T("signature_includes_body", []{
        MockCoinbaseBroker b = make_cb();
        CHK(b.sign("1716000001","POST","/api/v3/brokerage/orders","")
            != b.sign("1716000001","POST","/api/v3/brokerage/orders",R"({"key":"val"})"));
    });
    T("signature_differs_by_method", []{
        MockCoinbaseBroker b = make_cb();
        CHK(b.sign("1716000002","GET","/api/v3/brokerage/accounts","")
            != b.sign("1716000002","POST","/api/v3/brokerage/accounts",""));
    });

    /* ── 2. connect ── */
    section("CONNECT");
    T("connect_success", []{
        MockCoinbaseBroker b = make_cb(); b.connected_=false;
        b.add_route("accounts",200,R"({"accounts":[]})");
        CHK(b.connect()==AF_OK); CHK(b.is_connected());
    });
    T("connect_missing_credentials", []{
        BrokerConfig c("coinbase"); c.api_key=""; c.api_secret=""; c.paper=true;
        MockCoinbaseBroker b(std::move(c)); b.connected_=false;
        CHK(b.connect()!=AF_OK); CHK(!b.is_connected());
    });
    T("connect_http_error_nonzero", []{
        MockCoinbaseBroker b = make_cb(); b.connected_=false;
        b.add_route("accounts",401,R"({"error":"unauthorized"})");
        CHK(b.connect()!=AF_OK);
    });

    /* ── 3. get_account ── */
    section("GET ACCOUNT");
    T("fiat_balances_aggregated", []{
        MockCoinbaseBroker b = make_cb();
        b.add_route("accounts",200,R"({"accounts":[{"currency":"USD","available_balance":{"value":"1000.00","currency":"USD"},"hold":{"value":"200.00","currency":"USD"}},{"currency":"BTC","available_balance":{"value":"0.5","currency":"BTC"},"hold":{"value":"0.0","currency":"BTC"}},{"currency":"USDC","available_balance":{"value":"500.00","currency":"USDC"},"hold":{"value":"0.00","currency":"USDC"}}]})");
        AF_AccountInfo i{}; CHK(b.get_account(i)==AF_OK);
        CHK_NEAR(i.balance,1700.0,1e-6); CHK_NEAR(i.equity,1700.0,1e-6); CHK_EQ((int)i.leverage,1); CHK_NEAR(i.margin,0.0,1e-9);
    });
    T("empty_accounts_zero", []{
        MockCoinbaseBroker b = make_cb(); b.add_route("accounts",200,R"({"accounts":[]})");
        AF_AccountInfo i{}; b.get_account(i); CHK_NEAR(i.balance,0.0,1e-9);
    });

    /* ── 4. get_tick ── */
    section("GET TICK");
    T("bid_ask_parsed", []{
        MockCoinbaseBroker b = make_cb();
        b.add_route("best_bid_ask",200,R"({"pricebooks":[{"product_id":"BTC-USD","bids":[{"price":"67000.00","size":"0.5"}],"asks":[{"price":"67001.50","size":"0.3"}],"time":"2024-01-01T00:00:00Z"}]})");
        AF_Tick t{}; CHK(b.get_tick("BTCUSD",t)==AF_OK);
        CHK_NEAR(t.bid,67000.00,1e-6); CHK_NEAR(t.ask,67001.50,1e-6); CHK(std::string(t.symbol)=="BTCUSD");
    });
    T("empty_pricebooks_errors", []{
        MockCoinbaseBroker b = make_cb(); b.add_route("best_bid_ask",200,R"({"pricebooks":[]})");
        AF_Tick t{}; CHK(b.get_tick("BTCUSD",t)!=AF_OK);
    });

    /* ── 5. get_symbol_info ── */
    section("GET SYMBOL INFO");
    T("symbol_info_parsed", []{
        MockCoinbaseBroker b = make_cb();
        b.add_route("/products/",200,R"({"product_id":"BTC-USD","base_increment":"0.00000001","quote_increment":"0.01","base_min_size":"0.001","base_max_size":"10000"})");
        AF_SymbolInfo s{}; CHK(b.get_symbol_info("BTCUSD",s)==AF_OK);
        CHK_NEAR(s.point,0.01,1e-9); CHK_NEAR(s.volume_min,0.001,1e-9);
        CHK_NEAR(s.volume_step,0.00000001,1e-12); CHK_NEAR(s.contract_size,1.0,1e-9); CHK_EQ(s.digits,2);
    });

    /* ── 6. get_bars ── */
    section("GET BARS");
    T("candles_oldest_first", []{
        MockCoinbaseBroker b = make_cb();
        b.add_route("candles",200,R"({"candles":[{"start":"1716000060","open":"100.0","high":"105.0","low":"99.0","close":"103.0","volume":"50.0"},{"start":"1716000000","open":"98.0","high":"101.0","low":"97.0","close":"100.0","volume":"40.0"}]})");
        AF_Bar bars[8]; int f=0; b.get_bars("BTCUSD",(AF_Timeframe)60,2,bars,&f);
        CHK_EQ(f,2); CHK_EQ((long long)bars[0].timestamp,1716000000LL); CHK_EQ((long long)bars[1].timestamp,1716000060LL);
        CHK_NEAR(bars[0].open,98.0,1e-6); CHK_NEAR(bars[0].high,101.0,1e-6); CHK_NEAR(bars[0].low,97.0,1e-6);
        CHK_NEAR(bars[0].close,100.0,1e-6); CHK_NEAR(bars[0].volume,40.0,1e-6);
    });
    T("unsupported_timeframe_errors", []{
        MockCoinbaseBroker b = make_cb(); AF_Bar bars[8]; int f=0;
        CHK(b.get_bars("BTCUSD",(AF_Timeframe)15,10,bars,&f)!=AF_OK);
    });

    /* ── 7. place_order ── */
    section("PLACE ORDER");
    T("buy_order", []{
        MockCoinbaseBroker b = make_cb();
        b.add_route("/orders",200,R"({"success":true,"success_response":{"order_id":"ord-abc-buy","product_id":"BTC-USD","side":"BUY","client_order_id":"af-123"}})");
        AF_Order o{}; CHK(b.place_order("BTCUSD",AF_ORDER_BUY,0.01,0,0,0,42,"",o)==AF_OK);
        CHK_GT((long long)o.ticket,0); CHK(o.type==AF_ORDER_BUY); CHK_NEAR(o.lots,0.01,1e-9); CHK(std::string(o.symbol)=="BTCUSD");
    });
    T("sell_order", []{
        MockCoinbaseBroker b = make_cb();
        b.add_route("/orders",200,R"({"success":true,"success_response":{"order_id":"ord-abc-sell","side":"SELL"}})");
        AF_Order o{}; b.place_order("BTCUSD",AF_ORDER_SELL,0.005,0,0,0,0,"",o);
        CHK(o.type==AF_ORDER_SELL); CHK_NEAR(o.lots,0.005,1e-9);
    });
    T("ticket_mapping_unique", []{
        MockCoinbaseBroker b = make_cb();
        b.add_route("/orders",200,R"({"success":true,"success_response":{"order_id":"ord-001"}})");
        AF_Order o{}; b.place_order("BTCUSD",AF_ORDER_BUY,0.01,0,0,0,0,"",o);
        uint64_t t1 = o.ticket;
        CHK_EQ((int)b._register_order_id("ord-001"),(int)t1);  /* idempotent */
        CHK((int)b._register_order_id("ord-002") != (int)t1);   /* distinct id → distinct ticket */
    });
    T("unsupported_type_errors", []{
        MockCoinbaseBroker b = make_cb(); AF_Order o{};
        CHK(b.place_order("BTCUSD",AF_ORDER_BUY_LIMIT,0.01,65000.0,0,0,0,"",o)!=AF_OK);
    });
    T("exchange_rejection_errors", []{
        MockCoinbaseBroker b = make_cb();
        b.add_route("/orders",200,R"({"success":false,"error_response":{"error":"INSUFFICIENT_FUND","message":"Insufficient funds"}})");
        AF_Order o{}; CHK(b.place_order("BTCUSD",AF_ORDER_BUY,100.0,0,0,0,0,"",o)!=AF_OK);
    });

    /* ── 8. get_positions ── */
    section("GET POSITIONS");
    T("nonzero_crypto_become_positions", []{
        MockCoinbaseBroker b = make_cb();
        b.add_route("accounts",200,R"({"accounts":[{"currency":"USD","available_balance":{"value":"5000.00","currency":"USD"},"hold":{"value":"0.00","currency":"USD"}},{"currency":"BTC","available_balance":{"value":"0.25","currency":"BTC"},"hold":{"value":"0.05","currency":"BTC"}},{"currency":"ETH","available_balance":{"value":"0.00","currency":"ETH"},"hold":{"value":"0.00","currency":"ETH"}}]})");
        b.add_route("best_bid_ask",200,R"({"pricebooks":[{"product_id":"BTC-USD","bids":[{"price":"67000.00"}],"asks":[{"price":"67001.00"}]}]})");
        auto ps = b.get_positions();
        CHK_EQ((int)ps.size(),1); CHK(std::string(ps[0].symbol)=="BTCUSD"); CHK(ps[0].side==AF_DIR_LONG); CHK_NEAR(ps[0].lots,0.30,1e-9);
    });

    /* ── 9. get_orders ── */
    section("GET ORDERS");
    T("open_orders_parsed", []{
        MockCoinbaseBroker b = make_cb();
        b.add_route("historical/batch",200,R"({"orders":[{"order_id":"ord-open-001","product_id":"BTC-USD","side":"BUY","order_type":"MARKET","filled_size":"0.01","average_filled_price":"67000.00","status":"OPEN"}]})");
        auto os = b.get_orders();
        CHK_EQ((int)os.size(),1); CHK(os[0].type==AF_ORDER_BUY); CHK_NEAR(os[0].lots,0.01,1e-9);
        CHK_NEAR(os[0].fill_price,67000.00,1e-6); CHK(std::string(os[0].symbol)=="BTCUSD");
    });

    /* ── 10. cancel_order ── */
    section("CANCEL ORDER");
    T("cancel_success", []{
        MockCoinbaseBroker b = make_cb();
        uint64_t tk = b._register_order_id("ord-to-cancel");
        b.add_route("batch_cancel",200,R"({"results":[{"order_id":"ord-to-cancel","success":true}]})");
        CHK(b.cancel_order(tk)==AF_OK);
    });
    T("cancel_unknown_nonzero", []{ MockCoinbaseBroker b = make_cb(); CHK(b.cancel_order(9999)!=AF_OK); });

    /* ── 11. modify_position (cancel-then-replace) ── */
    section("MODIFY POSITION");
    T("modify_delegates_to_cancel", []{
        MockCoinbaseBroker b = make_cb();
        uint64_t tk = b._register_order_id("ord-to-modify");
        b.add_route("batch_cancel",200,R"({"results":[{"order_id":"ord-to-modify","success":true}]})");
        CHK(b.modify_position(tk,1.0,2.0)==AF_OK);
    });
    T("modify_unknown_nonzero", []{ MockCoinbaseBroker b = make_cb(); CHK(b.modify_position(9999,1.0,2.0)!=AF_OK); });
}
