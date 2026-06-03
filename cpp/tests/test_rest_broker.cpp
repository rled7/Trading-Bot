/**
 * AlgoForge — tests/test_rest_broker.cpp
 *
 * Parity tests for the REST broker FOUNDATION (Phase 1 / S5).
 *
 * Python oracle: python/tests/test_brokers.py
 * Ported test classes:
 *   TestRegistry         → registry: available_brokers, make_broker("paper"),
 *                          make_broker("mt5") platform-conditional,
 *                          unknown name throws
 *   TestConfigFromEnv    → BrokerConfig::from_env (env var parsing, paper flag)
 *                          and BrokerConfig::require() throwing
 *   JSON round-trip      → json_parse / json_emit on representative payloads
 *
 * NOT ported (C++ RestBroker is abstract — no concrete adapter yet):
 *   TestEndpointResolution, TestSymbolMapping, TestUnimplementedSurface
 *   (These require a concrete adapter; adapter workers cover them.)
 *
 * No network calls are made.  setenv()/unsetenv() isolate env state per test.
 */

#include "test_helpers.hpp"
#include "broker/rest_broker.hpp"
#include "broker/broker.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

/* ── Portable setenv/unsetenv ── */
#ifdef _WIN32
#include <cstdio>
static int test_setenv(const char* name, const char* value) {
    std::string kv = std::string(name) + "=" + value;
    return _putenv(kv.c_str());
}
static int test_unsetenv(const char* name) {
    std::string kv = std::string(name) + "=";
    return _putenv(kv.c_str());
}
#else
static int test_setenv(const char* name, const char* value) {
    return ::setenv(name, value, 1);
}
static int test_unsetenv(const char* name) {
    return ::unsetenv(name);
}
#endif

using namespace af::broker;

/* =========================================================================
 * Helper — a concrete test subclass of RestBroker that:
 *   1. Overrides the pure-virtual IBroker trading methods with stubs.
 *   2. Exposes _resolve_base_url for endpoint-resolution assertions.
 *   (RestBroker is abstract; we can't instantiate it directly.)
 * =========================================================================*/
class TestAdapter final : public RestBroker {
public:
    static constexpr const char* BROKER_NAME = "test_adapter";
    static constexpr const char* LIVE_URL    = "https://live.example.com/v1";
    static constexpr const char* PAPER_URL   = "https://paper.example.com/v1";

    explicit TestAdapter(BrokerConfig cfg = BrokerConfig{"test_adapter"})
        : RestBroker(std::move(cfg)) {}

    const char* broker_name() const override { return BROKER_NAME; }

    /* connect() sets base_url_ which is accessible via the public base_url() accessor. */

protected:
    const char* live_url()  const override { return LIVE_URL;  }
    const char* paper_url() const override { return PAPER_URL; }

    /* No-op stubs for abstract IBroker trading surface */
    AF_Error get_account(AF_AccountInfo&) const override { return AF_ERR_NOT_CONNECTED; }
    AF_Error get_tick(const char*, AF_Tick&) const override { return AF_ERR_NOT_CONNECTED; }
    AF_Error get_symbol_info(const char*, AF_SymbolInfo&) const override { return AF_ERR_NOT_CONNECTED; }
    AF_Error get_bars(const char*, AF_Timeframe, int, AF_Bar*, int* f) const override {
        if (f) *f = 0; return AF_ERR_NOT_CONNECTED;
    }
    AF_Error place_order(const char*, AF_OrderType, double, double,
                         double, double, uint32_t, const char*, AF_Order&) override {
        return AF_ERR_NOT_CONNECTED;
    }
    AF_Error close_position(uint64_t, double) override { return AF_ERR_NOT_CONNECTED; }
    AF_Error modify_position(uint64_t, double, double) override { return AF_ERR_NOT_CONNECTED; }
    AF_Error cancel_order(uint64_t) override { return AF_ERR_NOT_CONNECTED; }
};

/* =========================================================================
 * Test suite
 * =========================================================================*/

void test_rest_broker(TestRunner T) {

    /* =====================================================================
     * REGISTRY TESTS
     * Ported from: python/tests/test_brokers.py :: TestRegistry
     * ===================================================================== */
    section("REGISTRY");

    T("available_brokers includes paper and mt5", []{
        auto brokers = available_brokers();
        auto has = [&](const std::string& name) {
            return std::find(brokers.begin(), brokers.end(), name) != brokers.end();
        };
        CHK(has("paper"));
        CHK(has("mt5"));
    });

    T("make_broker(paper) returns an IBroker that connects", []{
        /* Mirrors Python: test_paper_routes_to_paper_broker */
        auto b = make_broker("paper", true, 5000.0);
        CHK(b != nullptr);
        /* PaperBroker::connect() returns AF_OK */
        CHK(b->connect() == AF_OK);
        CHK(b->is_connected());
    });

    T("make_broker(PAPER) case-insensitive", []{
        /* Mirrors Python: test_case_insensitive */
        auto b = make_broker("PAPER", true, 1000.0);
        CHK(b != nullptr);
    });

    T("make_broker(unknown) throws invalid_argument", []{
        /* Mirrors Python: test_unknown_broker_raises */
        bool threw = false;
        try {
            make_broker("dogecoin-casino");
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHK(threw);
    });

    T("make_broker(mt5) on non-Windows throws invalid_argument", []{
        /* On macOS/Linux AF_MT5_AVAILABLE is not defined; must throw. */
#ifdef AF_MT5_AVAILABLE
        /* On Windows the MT5 stub should be constructable. */
        auto b = make_broker("mt5");
        CHK(b != nullptr);
#else
        bool threw = false;
        try {
            make_broker("mt5");
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHK(threw);
#endif
    });

    T("available_brokers is non-empty and sorted", []{
        auto brokers = available_brokers();
        CHK(!brokers.empty());
        /* Verify sorted order */
        for (size_t i = 1; i < brokers.size(); ++i) {
            CHK(brokers[i-1] <= brokers[i]);
        }
    });

    /* =====================================================================
     * CONFIG / ENV TESTS
     * Ported from: python/tests/test_brokers.py :: TestConfigFromEnv
     * ===================================================================== */
    section("BROKER CONFIG / ENV");

    T("from_env reads AF_<NAME>_* prefixed vars", []{
        /* Mirrors Python: test_reads_prefixed_vars */
        test_setenv("AF_OANDA_API_KEY",  "key123");
        test_setenv("AF_OANDA_ACCOUNT",  "acct-9");
        test_unsetenv("AF_OANDA_API_SECRET");
        test_unsetenv("AF_OANDA_BASE_URL");
        test_unsetenv("AF_OANDA_PAPER");

        BrokerConfig cfg = BrokerConfig::from_env("oanda", true);
        CHK(cfg.api_key    == "key123");
        CHK(cfg.account_id == "acct-9");
        CHK(cfg.paper == true);          /* default preserved */

        test_unsetenv("AF_OANDA_API_KEY");
        test_unsetenv("AF_OANDA_ACCOUNT");
    });

    T("from_env AF_<NAME>_PAPER=false overrides default true", []{
        /* Mirrors Python: test_env_paper_override */
        test_setenv("AF_OANDA_PAPER", "false");

        BrokerConfig cfg = BrokerConfig::from_env("oanda", true);
        CHK(cfg.paper == false);

        test_unsetenv("AF_OANDA_PAPER");
    });

    T("from_env AF_<NAME>_PAPER=0 overrides default true", []{
        test_setenv("AF_OANDA_PAPER", "0");

        BrokerConfig cfg = BrokerConfig::from_env("oanda", true);
        CHK(cfg.paper == false);

        test_unsetenv("AF_OANDA_PAPER");
    });

    T("from_env AF_<NAME>_PAPER=true forces paper mode", []{
        test_setenv("AF_OANDA_PAPER", "true");

        BrokerConfig cfg = BrokerConfig::from_env("oanda", false);
        CHK(cfg.paper == true);

        test_unsetenv("AF_OANDA_PAPER");
    });

    T("from_env AF_<NAME>_PAPER=1 forces paper mode", []{
        test_setenv("AF_OANDA_PAPER", "1");

        BrokerConfig cfg = BrokerConfig::from_env("oanda", false);
        CHK(cfg.paper == true);

        test_unsetenv("AF_OANDA_PAPER");
    });

    T("from_env empty PAPER env var uses default", []{
        test_setenv("AF_OANDA_PAPER", "");

        BrokerConfig cfg_paper = BrokerConfig::from_env("oanda", true);
        BrokerConfig cfg_live  = BrokerConfig::from_env("oanda", false);
        CHK(cfg_paper.paper == true);
        CHK(cfg_live.paper  == false);

        test_unsetenv("AF_OANDA_PAPER");
    });

    T("from_env name is preserved and uppercased prefix is used", []{
        test_setenv("AF_BINANCE_API_KEY",    "binkey");
        test_setenv("AF_BINANCE_API_SECRET", "binsecret");

        BrokerConfig cfg = BrokerConfig::from_env("binance", true);
        CHK(cfg.name       == "binance");
        CHK(cfg.api_key    == "binkey");
        CHK(cfg.api_secret == "binsecret");

        test_unsetenv("AF_BINANCE_API_KEY");
        test_unsetenv("AF_BINANCE_API_SECRET");
    });

    T("require() throws BrokerError on missing api_key", []{
        /* Mirrors Python: test_require_raises_on_missing */
        BrokerConfig cfg("oanda");
        /* api_key and account_id are empty */
        bool threw = false;
        try {
            cfg.require({"api_key", "account_id"});
        } catch (const BrokerError& e) {
            threw = true;
            /* Message should mention AF_OANDA_API_KEY */
            std::string msg = e.what();
            CHK(msg.find("AF_OANDA_API_KEY") != std::string::npos);
        }
        CHK(threw);
    });

    T("require() does not throw when all fields are present", []{
        BrokerConfig cfg("oanda");
        cfg.api_key    = "key";
        cfg.account_id = "acct";
        bool threw = false;
        try {
            cfg.require({"api_key", "account_id"});
        } catch (...) {
            threw = true;
        }
        CHK(!threw);
    });

    T("require() lists all missing fields in error message", []{
        BrokerConfig cfg("alpaca");
        bool threw = false;
        try {
            cfg.require({"api_key", "api_secret"});
        } catch (const BrokerError& e) {
            threw = true;
            std::string msg = e.what();
            CHK(msg.find("AF_ALPACA_API_KEY")    != std::string::npos);
            CHK(msg.find("AF_ALPACA_API_SECRET") != std::string::npos);
        }
        CHK(threw);
    });

    /* =====================================================================
     * ENDPOINT RESOLUTION TESTS
     * Not directly from Python test_brokers.py (Python tests OandaBroker
     * directly), but the logic is in RestBroker — testable via TestAdapter.
     * ===================================================================== */
    section("ENDPOINT RESOLUTION");

    T("paper=true resolves to PAPER_URL after connect()", []{
        /* connect() calls _resolve_base_url() and stores in base_url_.
         * _on_connect() is a no-op for TestAdapter so connect() succeeds. */
        BrokerConfig cfg("test_adapter");
        cfg.paper = true;
        TestAdapter ta(cfg);
        CHK(ta.connect() == AF_OK);
        CHK(ta.base_url() == "https://paper.example.com/v1");
    });

    T("paper=false resolves to LIVE_URL after connect()", []{
        BrokerConfig cfg("test_adapter");
        cfg.paper = false;
        TestAdapter ta(cfg);
        CHK(ta.connect() == AF_OK);
        CHK(ta.base_url() == "https://live.example.com/v1");
    });

    T("explicit base_url overrides paper/live", []{
        /* Mirrors Python: test_explicit_base_url_override */
        BrokerConfig cfg("test_adapter");
        cfg.base_url = "https://example.test/v3/";
        TestAdapter ta(cfg);
        CHK(ta.connect() == AF_OK);
        /* Trailing slash must be stripped (mirrors Python .rstrip('/')) */
        CHK(ta.base_url() == "https://example.test/v3");
    });

    T("trailing slashes in base_url are stripped", []{
        BrokerConfig cfg("test_adapter");
        cfg.base_url = "https://example.test///";
        TestAdapter ta(cfg);
        CHK(ta.connect() == AF_OK);
        CHK(ta.base_url() == "https://example.test");
    });

    /* =====================================================================
     * JSON ROUND-TRIP TESTS
     * Not in Python (Python uses stdlib json); C++ must verify our bridge.
     * ===================================================================== */
    section("JSON ROUND-TRIP (json_parse / json_emit)");

    T("parse null literal", []{
        JsonVal v = json_parse("null");
        CHK(v.is_null());
    });

    T("parse boolean true / false", []{
        JsonVal t = json_parse("true");
        CHK(t.type == JsonVal::Type::Bool);
        CHK(t.as_bool() == true);

        JsonVal f = json_parse("false");
        CHK(f.type == JsonVal::Type::Bool);
        CHK(f.as_bool() == false);
    });

    T("parse integer number", []{
        JsonVal v = json_parse("42");
        CHK(v.is_num());
        CHK(v.as_int() == 42LL);
    });

    T("parse double number", []{
        JsonVal v = json_parse("3.14");
        CHK(v.is_num());
        CHK_NEAR(v.as_double(), 3.14, 1e-10);
    });

    T("parse string value", []{
        JsonVal v = json_parse("\"hello world\"");
        CHK(v.is_str());
        CHK(v.as_str() == "hello world");
    });

    T("parse empty object", []{
        JsonVal v = json_parse("{}");
        CHK(v.is_obj());
        CHK(v.size() == 0u);
    });

    T("parse flat object with mixed types", []{
        JsonVal v = json_parse(R"({"price":1.0825,"symbol":"EURUSD","paper":true})");
        CHK(v.is_obj());
        CHK(v.has("price"));
        CHK(v.has("symbol"));
        CHK(v.has("paper"));
        CHK_NEAR(v["price"].as_double(), 1.0825, 1e-10);
        CHK(v["symbol"].as_str() == "EURUSD");
        CHK(v["paper"].as_bool() == true);
    });

    T("parse nested object", []{
        JsonVal v = json_parse(R"({"account":{"balance":10000.0,"currency":"USD"}})");
        CHK(v.is_obj());
        CHK(v.has("account"));
        const JsonVal& acct = v["account"];
        CHK(acct.is_obj());
        CHK_NEAR(acct["balance"].as_double(), 10000.0, 1e-6);
        CHK(acct["currency"].as_str() == "USD");
    });

    T("parse array of numbers", []{
        JsonVal v = json_parse("[1,2,3]");
        CHK(v.is_arr());
        CHK(v.size() == 3u);
        CHK(v[0].as_int() == 1LL);
        CHK(v[1].as_int() == 2LL);
        CHK(v[2].as_int() == 3LL);
    });

    T("parse array of objects (broker candles payload)", []{
        const char* json = R"([
            {"open":1.0800,"high":1.0850,"low":1.0780,"close":1.0840,"volume":12345},
            {"open":1.0840,"high":1.0860,"low":1.0820,"close":1.0830,"volume":9876}
        ])";
        JsonVal v = json_parse(json);
        CHK(v.is_arr());
        CHK(v.size() == 2u);
        CHK(v[0].has("open"));
        CHK_NEAR(v[0]["high"].as_double(), 1.0850, 1e-6);
        CHK(v[1]["volume"].as_int() == 9876LL);
    });

    T("emit null", []{
        JsonVal v;  /* default = Null */
        CHK(json_emit(v) == "null");
    });

    T("emit boolean", []{
        JsonVal t; t.type = JsonVal::Type::Bool; t.boolean = true;
        CHK(json_emit(t) == "true");
        JsonVal f; f.type = JsonVal::Type::Bool; f.boolean = false;
        CHK(json_emit(f) == "false");
    });

    T("emit number round-trips via parse", []{
        JsonVal v; v.type = JsonVal::Type::Number; v.num = 1.0825;
        std::string s = json_emit(v);
        JsonVal rt = json_parse(s);
        CHK(rt.is_num());
        CHK_NEAR(rt.as_double(), 1.0825, 1e-12);
    });

    T("emit string with escape sequences", []{
        JsonVal v; v.type = JsonVal::Type::String; v.str = "a\"b\\c\nd";
        std::string s = json_emit(v);
        /* Round-trip */
        JsonVal rt = json_parse(s);
        CHK(rt.as_str() == "a\"b\\c\nd");
    });

    T("emit object with sorted keys", []{
        JsonVal obj = json_from_map({{"z","1"},{"a","2"},{"m","3"}});
        std::string s = json_emit(obj);
        /* Keys must appear in sorted order: a, m, z */
        size_t pa = s.find("\"a\"");
        size_t pm = s.find("\"m\"");
        size_t pz = s.find("\"z\"");
        CHK(pa < pm && pm < pz);
    });

    T("json_from_map produces Object with string values", []{
        JsonVal obj = json_from_map({{"side","BUY"},{"qty","0.10"}});
        CHK(obj.is_obj());
        CHK(obj.has("side"));
        CHK(obj["side"].as_str() == "BUY");
        CHK(obj["qty"].as_str()  == "0.10");
    });

    T("parse → emit → parse round-trip preserves structure", []{
        /* Representative broker order-book payload */
        const char* json = R"({
            "bids": [{"price":"1.0820","liquidity":10000000},
                     {"price":"1.0819","liquidity":5000000}],
            "asks": [{"price":"1.0822","liquidity":10000000}]
        })";
        JsonVal first  = json_parse(json);
        std::string emitted = json_emit(first);
        JsonVal second = json_parse(emitted);

        CHK(second.has("bids"));
        CHK(second.has("asks"));
        CHK(second["bids"].size() == 2u);
        CHK(second["asks"].size() == 1u);
        CHK(second["bids"][0]["price"].as_str() == "1.0820");
    });

    T("parse throws on malformed JSON", []{
        bool threw = false;
        try {
            json_parse("{bad json}");
        } catch (const std::runtime_error&) {
            threw = true;
        }
        CHK(threw);
    });

    T("out-of-bounds array access returns null", []{
        JsonVal v = json_parse("[1,2,3]");
        const JsonVal& out = v[99];
        CHK(out.is_null());
    });

    T("missing object key returns null", []{
        JsonVal v = json_parse(R"({"a":1})");
        const JsonVal& out = v["nonexistent"];
        CHK(out.is_null());
    });

    T("get() on non-object returns null", []{
        JsonVal v = json_parse("42");
        const JsonVal& out = v["key"];
        CHK(out.is_null());
    });
}
