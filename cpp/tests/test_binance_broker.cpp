/**
 * AlgoForge — tests/test_binance_broker.cpp
 *
 * Parity tests for the Binance Spot broker adapter.
 * Python oracle: python/tests/test_brokers_binance.py
 *
 * The signing test uses a signature value independently verified against
 * `openssl dgst -sha256 -hmac` (see commit message), plus the published
 * Wikipedia HMAC-SHA256 vector — so crypto correctness is not self-referential.
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

class MockBinanceBroker final : public BinanceBroker {
public:
    struct Entry { int status; std::string body; };
    std::vector<std::pair<std::string, Entry>> routes;
    struct Call { std::string method, url, body; };
    mutable std::vector<Call> calls;

    explicit MockBinanceBroker(BrokerConfig cfg = BrokerConfig{"binance"})
        : BinanceBroker(std::move(cfg)) { connected_ = true; base_url_ = PAPER_URL; }

    void add_route(const std::string& f, int s, const std::string& b) { routes.push_back({f,{s,b}}); }

    using RestBroker::connected_;
    using RestBroker::base_url_;
    using BinanceBroker::_to_broker_symbol;

protected:
    HttpResponse do_http_request(const std::string& method, const std::string& url,
                                 const std::string& body,
                                 const std::map<std::string,std::string>&) const override {
        calls.push_back({method, url, body});
        for (const auto& [f, e] : routes) if (url.find(f) != std::string::npos) return {e.status, e.body};
        return {404, R"({"code":-1,"msg":"no mock route"})"};
    }
};

static MockBinanceBroker make_binance() {
    BrokerConfig cfg("binance");
    cfg.api_key = "test_api_key"; cfg.api_secret = "test_api_secret"; cfg.paper = true;
    return MockBinanceBroker(std::move(cfg));
}

static bool sawCall(const MockBinanceBroker& b, const std::string& m, const std::string& urlSub) {
    for (const auto& c : b.calls) if (c.method == m && c.url.find(urlSub) != std::string::npos) return true;
    return false;
}

void test_binance_broker(TestRunner T) {

    /* ── 1. signed params / HMAC ── */
    section("SIGNED PARAMS / HMAC-SHA256");
    T("signature_matches_openssl_reference", []{
        MockBinanceBroker b = make_binance();
        auto p = b.signed_params({{"symbol","BTCUSDT"},{"side","BUY"},{"type","MARKET"},{"quantity","0.001"}}, 1700000000000LL);
        /* verified: openssl dgst -sha256 -hmac test_api_secret over the (sorted) query */
        CHK(p["signature"] == "38862c00cc77e5a642bf6a16ed152a3662d01bb8c76a4d9225deb2714838eb4f");
        CHK(p["timestamp"] == "1700000000000");
        CHK(p["recvWindow"] == "5000");
    });
    T("hmac_matches_published_vector", []{
        MockBinanceBroker b = make_binance();
        CHK(b.hmac_sha256_hex("key","The quick brown fox jumps over the lazy dog")
            == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
    });
    T("signature_changes_with_secret", []{
        MockBinanceBroker b1 = make_binance();
        BrokerConfig c2("binance"); c2.api_key="k"; c2.api_secret="other_secret"; c2.paper=true;
        MockBinanceBroker b2(std::move(c2));
        auto s1 = b1.signed_params({{"a","1"}}, 1700000000LL)["signature"];
        auto s2 = b2.signed_params({{"a","1"}}, 1700000000LL)["signature"];
        CHK(s1 != s2);
    });

    /* ── 2. connect ── */
    section("CONNECT");
    T("connect_success", []{
        MockBinanceBroker b = make_binance(); b.connected_=false;
        b.add_route("ping",200,"{}"); b.add_route("/api/v3/account",200,R"({"balances":[]})");
        CHK(b.connect()==AF_OK); CHK(b.is_connected());
    });
    T("connect_missing_credentials", []{
        BrokerConfig c("binance"); c.api_key="k"; c.api_secret=""; c.paper=true;
        MockBinanceBroker b(std::move(c)); b.connected_=false;
        CHK(b.connect()!=AF_OK); CHK(!b.is_connected());
    });

    /* ── 3. get_account ── */
    section("GET ACCOUNT");
    T("account_sums_stablecoins", []{
        MockBinanceBroker b = make_binance();
        b.add_route("/api/v3/account",200,R"({"balances":[{"asset":"USDT","free":"1000.00","locked":"200.00"},{"asset":"BTC","free":"0.5","locked":"0.0"},{"asset":"BUSD","free":"300.00","locked":"0.0"}]})");
        AF_AccountInfo i{}; CHK(b.get_account(i)==AF_OK);
        CHK_NEAR(i.balance,1500.0,1e-6); CHK(std::string(i.currency)=="USDT"); CHK_EQ((int)i.leverage,1);
    });
    T("account_empty_balances_zero", []{
        MockBinanceBroker b = make_binance(); b.add_route("/api/v3/account",200,R"({"balances":[]})");
        AF_AccountInfo i{}; b.get_account(i); CHK_NEAR(i.balance,0.0,1e-9);
    });

    /* ── 4. get_tick ── */
    section("GET TICK");
    T("tick_parses_bid_ask", []{
        MockBinanceBroker b = make_binance();
        b.add_route("bookTicker",200,R"({"symbol":"BTCUSDT","bidPrice":"29000.50","askPrice":"29001.00"})");
        AF_Tick t{}; CHK(b.get_tick("BTC/USDT",t)==AF_OK);
        CHK(std::string(t.symbol)=="BTC/USDT"); CHK_NEAR(t.bid,29000.50,1e-6); CHK_NEAR(t.ask,29001.00,1e-6); CHK_GT(t.timestamp,0);
    });
    T("tick_symbol_normalised_in_url", []{
        MockBinanceBroker b = make_binance(); b.add_route("bookTicker",200,R"({"bidPrice":"1.0","askPrice":"1.1"})");
        AF_Tick t{}; b.get_tick("BTC_USDT",t); CHK(sawCall(b,"GET","BTCUSDT"));
    });

    /* ── 5. get_bars ── */
    section("GET BARS");
    T("bars_parse_klines", []{
        MockBinanceBroker b = make_binance();
        b.add_route("klines",200,R"([[1700000000000,"29000.0","29100.0","28900.0","29050.0","100.5",1700000059999,"2000000",10,"50.0","1000000","0"],[1700000060000,"29050.0","29200.0","29000.0","29150.0","80.0",1700000119999,"2200000",8,"40.0","1200000","0"]])");
        AF_Bar bars[8]; int f=0; b.get_bars("BTCUSDT",(AF_Timeframe)60,2,bars,&f);
        CHK_EQ(f,2); CHK_EQ((long long)bars[0].timestamp,1700000000000LL);
        CHK_NEAR(bars[0].open,29000.0,1e-6); CHK_NEAR(bars[0].high,29100.0,1e-6);
        CHK_NEAR(bars[0].low,28900.0,1e-6); CHK_NEAR(bars[0].close,29050.0,1e-6); CHK_NEAR(bars[0].volume,100.5,1e-6);
    });
    T("bars_unsupported_timeframe_errors", []{
        MockBinanceBroker b = make_binance(); AF_Bar bars[8]; int f=0;
        CHK(b.get_bars("BTCUSDT",(AF_Timeframe)999,10,bars,&f)!=AF_OK);
    });

    /* ── 6. place_order ── */
    section("PLACE ORDER");
    T("buy_market", []{
        MockBinanceBroker b = make_binance();
        b.add_route("/api/v3/order",200,R"({"orderId":111,"symbol":"BTCUSDT","status":"FILLED","side":"BUY","type":"MARKET","origQty":"0.001","executedQty":"0.001","cummulativeQuoteQty":"29.05","fills":[{"price":"29050.0","qty":"0.001"}]})");
        AF_Order o{}; CHK(b.place_order("BTC/USDT",AF_ORDER_BUY,0.001,0,0,0,0,"",o)==AF_OK);
        CHK_EQ((int)o.ticket,111); CHK(o.type==AF_ORDER_BUY); CHK_NEAR(o.lots,0.001,1e-9); CHK_NEAR(o.fill_price,29050.0,1e-6);
    });
    T("sell_market", []{
        MockBinanceBroker b = make_binance();
        b.add_route("/api/v3/order",200,R"({"orderId":222,"side":"SELL","executedQty":"0.001","cummulativeQuoteQty":"29.0","fills":[{"price":"29000.0"}]})");
        AF_Order o{}; b.place_order("BTC/USDT",AF_ORDER_SELL,0.001,0,0,0,0,"",o);
        CHK_EQ((int)o.ticket,222); CHK(o.type==AF_ORDER_SELL);
    });
    T("fill_price_from_cumulative", []{
        MockBinanceBroker b = make_binance();
        b.add_route("/api/v3/order",200,R"({"orderId":333,"executedQty":"0.002","cummulativeQuoteQty":"60.0","origQty":"0.002","side":"BUY"})");
        AF_Order o{}; b.place_order("BTC/USDT",AF_ORDER_BUY,0.002,0,0,0,0,"",o); CHK_NEAR(o.fill_price,30000.0,1e-6);
    });
    T("rejects_limit_type", []{
        MockBinanceBroker b = make_binance(); AF_Order o{};
        CHK(b.place_order("BTCUSDT",AF_ORDER_BUY_LIMIT,0.001,0,0,0,0,"",o)!=AF_OK);
    });

    /* ── 7. get_positions ── */
    section("GET POSITIONS");
    T("nonzero_balances_become_positions", []{
        MockBinanceBroker b = make_binance();
        b.add_route("/api/v3/account",200,R"({"balances":[{"asset":"USDT","free":"1000.0","locked":"0.0"},{"asset":"BTC","free":"0.5","locked":"0.1"},{"asset":"ETH","free":"2.0","locked":"0.0"},{"asset":"SOL","free":"0.0","locked":"0.0"}]})");
        auto ps = b.get_positions(); CHK_EQ((int)ps.size(),2);
        bool btc=false,eth=false; for(auto&p:ps){ if(std::string(p.symbol)=="BTCUSDT")btc=true; if(std::string(p.symbol)=="ETHUSDT")eth=true; CHK(p.side==AF_DIR_LONG); CHK_GT(p.lots,0.0);}
        CHK(btc); CHK(eth);
    });
    T("btc_lots_free_plus_locked", []{
        MockBinanceBroker b = make_binance(); b.add_route("/api/v3/account",200,R"({"balances":[{"asset":"BTC","free":"0.3","locked":"0.2"}]})");
        auto ps=b.get_positions(); CHK_EQ((int)ps.size(),1); CHK_NEAR(ps[0].lots,0.5,1e-9);
    });

    /* ── 8. get_orders ── */
    section("GET ORDERS");
    T("orders_map_fields", []{
        MockBinanceBroker b = make_binance();
        b.add_route("openOrders",200,R"([{"orderId":999,"symbol":"BTCUSDT","side":"BUY","type":"LIMIT","origQty":"0.01","price":"28000.0","cummulativeQuoteQty":"0.0"}])");
        auto os=b.get_orders(); CHK_EQ((int)os.size(),1); CHK_EQ((int)os[0].ticket,999); CHK(os[0].type==AF_ORDER_BUY); CHK_NEAR(os[0].price,28000.0,1e-6);
    });

    /* ── 9. cancel_order ── */
    section("CANCEL ORDER");
    T("cancel_existing", []{
        MockBinanceBroker b = make_binance();
        b.add_route("openOrders",200,R"([{"orderId":555,"symbol":"BTCUSDT","side":"BUY","origQty":"0.01","price":"28000.0"}])");
        b.add_route("/api/v3/order",200,R"({"orderId":555,"status":"CANCELED"})");
        CHK(b.cancel_order(555)==AF_OK); CHK(sawCall(b,"DELETE","/api/v3/order"));
    });
    T("cancel_nonexistent_nonzero", []{
        MockBinanceBroker b = make_binance(); b.add_route("openOrders",200,"[]");
        CHK(b.cancel_order(9999)!=AF_OK);
    });

    /* ── 10. error path ── */
    section("ERROR PATH");
    T("http_error_returns_nonzero", []{
        MockBinanceBroker b = make_binance(); b.add_route("bookTicker",400,R"({"code":-1100,"msg":"Illegal"})");
        AF_Tick t{}; CHK(b.get_tick("BTCUSDT",t)!=AF_OK);
    });

    /* ── 11. get_position ── */
    section("GET POSITION");
    T("get_position_matching", []{
        MockBinanceBroker b = make_binance(); b.add_route("/api/v3/account",200,R"({"balances":[{"asset":"BTC","free":"0.5","locked":"0.0"}]})");
        auto ps=b.get_positions(); CHK_EQ((int)ps.size(),1); uint64_t tk=ps[0].ticket;
        auto p=b.get_position(tk); CHK(p.has_value()); CHK_EQ((int)p->ticket,(int)tk);
    });
    T("get_position_unknown_nullopt", []{
        MockBinanceBroker b = make_binance(); b.add_route("/api/v3/account",200,R"({"balances":[]})");
        CHK(!b.get_position(9999999).has_value());
    });

    /* ── 12. symbol mapping ── */
    section("SYMBOL MAPPING");
    T("slash_removed",  []{ MockBinanceBroker b = make_binance(); CHK(b._to_broker_symbol("BTC/USDT")=="BTCUSDT"); });
    T("underscore_removed", []{ MockBinanceBroker b = make_binance(); CHK(b._to_broker_symbol("ETH_USDT")=="ETHUSDT"); });
    T("already_normalised", []{ MockBinanceBroker b = make_binance(); CHK(b._to_broker_symbol("BTCUSDT")=="BTCUSDT"); });
    T("lowercase_uppercased", []{ MockBinanceBroker b = make_binance(); CHK(b._to_broker_symbol("btcusdt")=="BTCUSDT"); });

    /* ── 13. modify_position ── */
    section("MODIFY POSITION");
    T("modify_returns_zero_noop", []{ MockBinanceBroker b = make_binance(); CHK(b.modify_position(12345,28000.0,31000.0)==AF_OK); });
}
