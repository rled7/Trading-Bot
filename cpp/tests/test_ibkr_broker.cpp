/**
 * AlgoForge — tests/test_ibkr_broker.cpp
 *
 * Parity tests for the IBKR broker adapter.
 *
 * Python oracle: python/tests/test_brokers_ibkr.py
 *
 * All HTTP is mocked — no real network calls. Tests override do_http_request()
 * via a MockIbkrBroker subclass, mirroring the Python pattern of patching
 * broker._request / _get / _post with MagicMock.
 *
 * Ported test classes:
 *   TestConnect           → _on_connect / connect() lifecycle
 *   TestConidResolution   → _resolve_conid caching and lookup
 *   TestGetAccount        → get_account field mapping
 *   TestGetTick           → get_tick snapshot parsing
 *   TestPlaceOrder        → order placement + confirmation reply loop
 *   TestGetPositions      → get_positions / get_position parsing
 *   TestErrorPath         → cancel_order, modify_position, error propagation
 *   TestSafeFloat         → _safe_float edge cases
 *   TestSummaryAmount     → _summary_amount edge cases
 *   TestParseOrder        → _parse_order field mapping
 *   TestGetBars           → get_bars timeframe mapping + tail trimming
 *   TestGetOrders         → get_orders dict/array shapes
 */

#include "test_helpers.hpp"
#include "broker/rest_broker.hpp"
#include "broker/broker.hpp"

#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <optional>

using namespace af::broker;

/* =========================================================================
 * MockIbkrBroker — test double for IbkrBroker
 *
 * Overrides do_http_request() to return canned JSON responses keyed by
 * a user-supplied path→response map. Mirrors Python's approach of
 * patching broker._request / _get / _post.
 * =========================================================================*/
class MockIbkrBroker final : public IbkrBroker {
public:
    /* Response entry: {status, body} */
    struct Entry { int status; std::string body; };

    /* Route responses: if path (substring) matches, return that entry.
     * Routes are checked in insertion order. */
    std::vector<std::pair<std::string, Entry>> routes;

    /* All requests recorded: {method, url, body} */
    struct Call { std::string method, url, body; };
    mutable std::vector<Call> calls;

    explicit MockIbkrBroker(BrokerConfig cfg = BrokerConfig{"ibkr"})
        : IbkrBroker(std::move(cfg)) {
        /* Pre-mark as connected with correct base URL so tests skip connect(). */
        connected_ = true;
        base_url_  = "https://localhost:5000/v1/api";
    }

    void add_route(const std::string& path_substr, int status, const std::string& body) {
        routes.push_back({path_substr, {status, body}});
    }

    /* Remove all routes matching the given substr (for cleanup/reset). */
    void clear_routes() { routes.clear(); }

    /* Expose conid_cache_ for white-box assertions. */
    void set_conid(const std::string& sym, int64_t conid) {
        std::string key = sym;
        for (char& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        conid_cache_[key] = conid;
    }
    int64_t get_conid(const std::string& sym) const {
        std::string key = sym;
        for (char& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        auto it = conid_cache_.find(key);
        return (it != conid_cache_.end()) ? it->second : -1;
    }
    bool has_conid(const std::string& sym) const {
        return get_conid(sym) >= 0;
    }

    /* Static helpers exposed for test use */
    using IbkrBroker::_safe_float;
    using IbkrBroker::_summary_amount;
    /* Inherited protected members re-exposed for white-box parity assertions */
    using RestBroker::connected_;
    using RestBroker::base_url_;
    using IbkrBroker::_resolve_conid;

protected:
    HttpResponse do_http_request(const std::string& method,
                                  const std::string& url,
                                  const std::string& body,
                                  const std::map<std::string,std::string>& /*hdrs*/) const override {
        calls.push_back({method, url, body});
        for (const auto& [substr, entry] : routes) {
            if (url.find(substr) != std::string::npos) {
                return {entry.status, entry.body};
            }
        }
        /* No matching route: return 404 */
        return {404, R"({"error":"no mock route"})"};
    }
};

/* =========================================================================
 * Helper: build a MockIbkrBroker with account "U1234567"
 * =========================================================================*/
static MockIbkrBroker make_mock(const std::string& account_id = "U1234567") {
    BrokerConfig cfg("ibkr");
    cfg.account_id = account_id;
    cfg.paper      = true;
    return MockIbkrBroker(std::move(cfg));
}

/* =========================================================================
 * Test suite
 * =========================================================================*/

void test_ibkr_broker(TestRunner T) {

    /* =====================================================================
     * TestConnect — mirrors Python test_brokers_ibkr.py :: TestConnect
     * ===================================================================== */
    section("CONNECT / _on_connect");

    T("connect_authenticated_returns_AF_OK", []{
        /* Mirrors Python: test_connect_authenticated_returns_zero */
        MockIbkrBroker b = make_mock();
        b.connected_ = false;
        b.base_url_  = "https://localhost:5000/v1/api";
        b.add_route("/iserver/auth/status", 200, R"({"authenticated":true})");

        AF_Error result = b.connect();
        CHK(result == AF_OK);
        CHK(b.is_connected());
    });

    T("connect_not_authenticated_returns_nonzero", []{
        /* Mirrors Python: test_connect_not_authenticated_returns_nonzero */
        MockIbkrBroker b = make_mock();
        b.connected_ = false;
        b.base_url_  = "https://localhost:5000/v1/api";
        b.add_route("/iserver/auth/status", 200, R"({"authenticated":false})");

        AF_Error result = b.connect();
        CHK(result != AF_OK);
        CHK(!b.is_connected());
    });

    T("connect_missing_account_id_returns_nonzero", []{
        /* Mirrors Python: test_connect_missing_account_id_returns_nonzero */
        BrokerConfig cfg("ibkr");
        cfg.account_id = "";  /* no account id */
        MockIbkrBroker b(std::move(cfg));
        b.connected_ = false;
        b.base_url_  = "https://localhost:5000/v1/api";

        AF_Error result = b.connect();
        CHK(result != AF_OK);
        CHK(!b.is_connected());
    });

    T("connect_gateway_network_error_returns_nonzero", []{
        /* Mirrors Python: test_connect_gateway_network_error_returns_nonzero */
        MockIbkrBroker b = make_mock();
        b.connected_ = false;
        b.base_url_  = "https://localhost:5000/v1/api";
        /* Route returns HTTP 500 to trigger BrokerError */
        b.add_route("/iserver/auth/status", 500, R"({"error":"network error"})");

        AF_Error result = b.connect();
        CHK(result != AF_OK);
    });

    /* =====================================================================
     * TestConidResolution — mirrors Python :: TestConidResolution
     * ===================================================================== */
    section("CONID RESOLUTION");

    T("resolve_conid_returns_correct_id", []{
        /* Mirrors Python: test_resolve_conid_returns_correct_id */
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/secdef/search",
            200,
            R"([{"conid":265598,"ticker":"AAPL","companyName":"Apple Inc"}])");

        int64_t conid = b._resolve_conid("AAPL");
        CHK(conid == 265598);
    });

    T("resolve_conid_caches_result", []{
        /* Mirrors Python: test_resolve_conid_caches_result
         * Only one HTTP call despite two _resolve_conid calls. */
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/secdef/search",
            200,
            R"([{"conid":265598,"ticker":"AAPL"}])");

        b._resolve_conid("AAPL");
        b._resolve_conid("AAPL");  /* second call must use cache */

        /* Only one HTTP request should have been made */
        size_t secdef_calls = 0;
        for (const auto& call : b.calls) {
            if (call.url.find("secdef/search") != std::string::npos)
                ++secdef_calls;
        }
        CHK(secdef_calls == 1);
    });

    T("resolve_conid_cache_keyed_uppercase", []{
        /* Mirrors Python: test_resolve_conid_cache_keyed_uppercase */
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/secdef/search",
            200,
            R"([{"conid":265598,"ticker":"AAPL"}])");

        b._resolve_conid("AAPL");

        CHK(b.has_conid("AAPL"));
        CHK(b.get_conid("AAPL") == 265598);
    });

    T("resolve_conid_no_results_raises_BrokerError", []{
        /* Mirrors Python: test_resolve_conid_no_results_raises_broker_error */
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/secdef/search", 200, R"([])");

        bool threw = false;
        try {
            b._resolve_conid("UNKNOWN");
        } catch (const BrokerError&) {
            threw = true;
        }
        CHK(threw);
    });

    T("resolve_conid_falls_back_to_first_when_no_exact_match", []{
        /* If no exact ticker match, uses results[0]["conid"] */
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/secdef/search",
            200,
            R"([{"conid":999111,"ticker":"AAPL_OPT"},{"conid":265598,"ticker":"AAPL"}])");

        int64_t conid = b._resolve_conid("AAPL");
        /* Exact match "AAPL" -> 265598 */
        CHK(conid == 265598);
    });

    T("resolve_conid_case_insensitive_symbol_input", []{
        /* Lowercase "aapl" should still resolve and cache under "AAPL" */
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/secdef/search",
            200,
            R"([{"conid":265598,"ticker":"AAPL"}])");

        int64_t conid = b._resolve_conid("aapl");
        CHK(conid == 265598);
        CHK(b.has_conid("AAPL"));  /* cache uses uppercase */
    });

    /* =====================================================================
     * TestGetAccount — mirrors Python :: TestGetAccount
     * ===================================================================== */
    section("GET ACCOUNT");

    T("get_account_maps_fields_correctly", []{
        /* Mirrors Python: test_get_account_maps_fields_correctly */
        MockIbkrBroker b = make_mock("U1234567");
        b.add_route("/portfolio/U1234567/summary", 200, R"({
            "netliquidation":  {"amount":100000.0,"currency":"USD"},
            "availablefunds":  {"amount":80000.0, "currency":"USD"},
            "totalcashvalue":  {"amount":95000.0, "currency":"USD"},
            "unrealizedpnl":   {"amount":2500.0,  "currency":"USD"},
            "grosspositionvalue":{"amount":20000.0,"currency":"USD"}
        })");

        AF_AccountInfo info{};
        AF_Error err = b.get_account(info);
        CHK(err == AF_OK);
        CHK_NEAR(info.equity,      100000.0, 1e-6);
        CHK_NEAR(info.free_margin, 80000.0,  1e-6);
        CHK_NEAR(info.balance,     95000.0,  1e-6);
        CHK_NEAR(info.profit,      2500.0,   1e-6);
        CHK_NEAR(info.margin,      20000.0,  1e-6);
        CHK(std::string(info.currency) == "USD");
        CHK(info.login == 1234567ULL);
        CHK(info.leverage == 1);
    });

    T("get_account_balance_fallback_to_equity_when_cash_zero", []{
        /* When totalcashvalue is 0, balance should fall back to equity */
        MockIbkrBroker b = make_mock("U7777777");
        b.add_route("/portfolio/U7777777/summary", 200, R"({
            "netliquidation": {"amount":50000.0,"currency":"USD"},
            "availablefunds": {"amount":40000.0,"currency":"USD"}
        })");

        AF_AccountInfo info{};
        AF_Error err = b.get_account(info);
        CHK(err == AF_OK);
        /* totalcashvalue missing -> balance falls back to equity */
        CHK_NEAR(info.balance, 50000.0, 1e-6);
    });

    T("get_account_calls_correct_endpoint", []{
        /* Mirrors Python: test_get_account_calls_correct_endpoint */
        MockIbkrBroker b = make_mock("U9999999");
        b.add_route("/portfolio/U9999999/summary", 200, R"({
            "netliquidation": {"amount":1.0,"currency":"USD"},
            "availablefunds": {"amount":1.0,"currency":"USD"}
        })");

        AF_AccountInfo info{};
        b.get_account(info);

        bool found = false;
        for (const auto& call : b.calls) {
            if (call.url.find("U9999999") != std::string::npos &&
                call.url.find("summary")  != std::string::npos) {
                found = true;
            }
        }
        CHK(found);
    });

    T("get_account_flat_amounts_no_nested_dict", []{
        /* Some IBKR gateway versions return flat numbers instead of nested dicts */
        MockIbkrBroker b = make_mock("U1111111");
        b.add_route("/portfolio/U1111111/summary", 200, R"({
            "netliquidation": 12345.0,
            "availablefunds": 10000.0,
            "totalcashvalue": 11000.0,
            "unrealizedpnl":  500.0,
            "grosspositionvalue": 2000.0
        })");

        AF_AccountInfo info{};
        AF_Error err = b.get_account(info);
        CHK(err == AF_OK);
        CHK_NEAR(info.equity, 12345.0, 1e-6);
        CHK_NEAR(info.balance, 11000.0, 1e-6);
    });

    /* =====================================================================
     * TestGetTick — mirrors Python :: TestGetTick
     * ===================================================================== */
    section("GET TICK");

    T("get_tick_parses_snapshot_fields", []{
        /* Mirrors Python: test_get_tick_parses_snapshot_fields */
        MockIbkrBroker b = make_mock();
        b.set_conid("AAPL", 265598);
        b.add_route("/iserver/marketdata/snapshot", 200,
            R"([{"31":"182.50","84":"182.48","86":"182.52","conid":265598}])");

        AF_Tick tick{};
        AF_Error err = b.get_tick("AAPL", tick);
        CHK(err == AF_OK);
        CHK(std::string(tick.symbol) == "AAPL");
        CHK_NEAR(tick.bid, 182.48, 1e-6);
        CHK_NEAR(tick.ask, 182.52, 1e-6);
        CHK(tick.timestamp > 0);
    });

    T("get_tick_falls_back_to_last_when_bid_ask_missing", []{
        /* Mirrors Python: test_get_tick_falls_back_to_last_when_bid_ask_missing */
        MockIbkrBroker b = make_mock();
        b.set_conid("MSFT", 272093);
        b.add_route("/iserver/marketdata/snapshot", 200,
            R"([{"31":"375.00","conid":272093}])");

        AF_Tick tick{};
        AF_Error err = b.get_tick("MSFT", tick);
        CHK(err == AF_OK);
        CHK_NEAR(tick.bid, 375.0, 1e-6);
        CHK_NEAR(tick.ask, 375.0, 1e-6);
    });

    T("get_tick_empty_response_raises", []{
        /* Mirrors Python: test_get_tick_empty_response_raises */
        MockIbkrBroker b = make_mock();
        b.set_conid("XYZ", 999);
        b.add_route("/iserver/marketdata/snapshot", 200, R"([])");

        AF_Tick tick{};
        bool threw = false;
        try {
            b.get_tick("XYZ", tick);
        } catch (const BrokerError&) {
            threw = true;
        }
        CHK(threw);
    });

    T("get_tick_numeric_field_values", []{
        /* Fields can come back as numbers (not strings) in some gateway versions */
        MockIbkrBroker b = make_mock();
        b.set_conid("TSLA", 76792991);
        b.add_route("/iserver/marketdata/snapshot", 200,
            R"([{"31":250.5,"84":250.45,"86":250.55,"conid":76792991}])");

        AF_Tick tick{};
        AF_Error err = b.get_tick("TSLA", tick);
        CHK(err == AF_OK);
        CHK_NEAR(tick.bid, 250.45, 1e-6);
        CHK_NEAR(tick.ask, 250.55, 1e-6);
    });

    /* =====================================================================
     * TestPlaceOrder — mirrors Python :: TestPlaceOrder
     * ===================================================================== */
    section("PLACE ORDER");

    T("place_order_simple_success", []{
        /* Mirrors Python: test_place_order_simple_success */
        MockIbkrBroker b = make_mock();
        b.set_conid("AAPL", 265598);
        b.add_route("/iserver/account/U1234567/orders", 200,
            R"([{"order_id":987654,"order_status":"Submitted"}])");

        AF_Order out{};
        AF_Error err = b.place_order("AAPL", AF_ORDER_BUY, 100.0,
                                      0.0, 0.0, 0.0, 0, "", out);
        CHK(err == AF_OK);
        CHK(out.ticket == 987654ULL);
        CHK(std::string(out.symbol) == "AAPL");
        CHK(out.type == AF_ORDER_BUY);
        CHK_NEAR(out.lots, 100.0, 1e-9);
    });

    T("place_order_confirm_reply_flow", []{
        /* Mirrors Python: test_place_order_confirm_reply_flow
         * First response is a confirmation prompt; second is the real order. */
        MockIbkrBroker b = make_mock();
        b.set_conid("TSLA", 76792991);
        /* Orders endpoint returns confirmation prompt */
        b.add_route("/iserver/account/U1234567/orders", 200,
            R"([{"id":"confirm-abc-123","message":["Confirm this order?"]}])");
        /* Reply endpoint returns success */
        b.add_route("/iserver/reply/confirm-abc-123", 200,
            R"([{"order_id":111222,"order_status":"PreSubmitted"}])");

        AF_Order out{};
        AF_Error err = b.place_order("TSLA", AF_ORDER_SELL, 50.0,
                                      0.0, 0.0, 0.0, 0, "", out);
        CHK(err == AF_OK);
        CHK(out.ticket == 111222ULL);

        /* Verify two POST calls were made (initial + reply) */
        size_t post_count = 0;
        for (const auto& call : b.calls)
            if (call.method == "POST") ++post_count;
        CHK(post_count == 2);
    });

    T("place_order_chained_confirm_replies", []{
        /* Mirrors Python: test_place_order_chained_confirm_replies
         * Two sequential confirmation rounds before order result. */
        MockIbkrBroker b = make_mock();
        b.set_conid("NVDA", 4815747);
        b.add_route("/iserver/account/U1234567/orders", 200,
            R"([{"id":"reply-1","message":["Confirm?"]}])");
        b.add_route("/iserver/reply/reply-1", 200,
            R"([{"id":"reply-2","message":["Really confirm?"]}])");
        b.add_route("/iserver/reply/reply-2", 200,
            R"([{"order_id":555666,"order_status":"Submitted"}])");

        AF_Order out{};
        AF_Error err = b.place_order("NVDA", AF_ORDER_BUY, 10.0,
                                      0.0, 0.0, 0.0, 0, "", out);
        CHK(err == AF_OK);
        CHK(out.ticket == 555666ULL);
    });

    T("place_order_no_order_id_raises_BrokerError", []{
        /* Mirrors Python: test_place_order_no_order_id_raises
         * Always returns confirmation prompts — never resolves. */
        MockIbkrBroker b = make_mock();
        b.set_conid("FOO", 12345);
        b.add_route("/iserver/account/U1234567/orders", 200,
            R"([{"id":"confirm-x","message":["Loop forever?"]}])");
        b.add_route("/iserver/reply/confirm-x", 200,
            R"([{"id":"confirm-x","message":["Loop forever?"]}])");

        AF_Order out{};
        bool threw = false;
        try {
            b.place_order("FOO", AF_ORDER_BUY, 1.0, 0.0, 0.0, 0.0, 0, "", out);
        } catch (const BrokerError&) {
            threw = true;
        }
        CHK(threw);
    });

    T("place_order_sends_correct_quantity", []{
        /* Mirrors Python: test_place_order_lots_as_quantity
         * Verifies request body contains quantity=250 */
        MockIbkrBroker b = make_mock();
        b.set_conid("IBM", 8314);
        b.add_route("/iserver/account/U1234567/orders", 200,
            R"([{"order_id":777,"order_status":"Submitted"}])");

        AF_Order out{};
        b.place_order("IBM", AF_ORDER_BUY, 250.0, 0.0, 0.0, 0.0, 0, "", out);

        /* Check the POST body contains "quantity":250 */
        bool found = false;
        for (const auto& call : b.calls) {
            if (call.method == "POST" &&
                call.url.find("/orders") != std::string::npos &&
                call.body.find("\"quantity\"") != std::string::npos &&
                call.body.find("250") != std::string::npos) {
                found = true;
            }
        }
        CHK(found);
    });

    T("place_order_buy_limit_includes_price", []{
        MockIbkrBroker b = make_mock();
        b.set_conid("AMZN", 3691937);
        b.add_route("/iserver/account/U1234567/orders", 200,
            R"([{"order_id":888,"order_status":"Submitted"}])");

        AF_Order out{};
        b.place_order("AMZN", AF_ORDER_BUY_LIMIT, 5.0, 130.0, 0.0, 0.0, 0, "", out);

        bool found_lmt = false, found_price = false;
        for (const auto& call : b.calls) {
            if (call.method == "POST" && call.url.find("/orders") != std::string::npos) {
                found_lmt   = (call.body.find("\"LMT\"") != std::string::npos);
                found_price = (call.body.find("\"price\"") != std::string::npos);
            }
        }
        CHK(found_lmt);
        CHK(found_price);
    });

    T("place_order_sell_stop_uses_aux_price", []{
        MockIbkrBroker b = make_mock();
        b.set_conid("GOOG", 208813719);
        b.add_route("/iserver/account/U1234567/orders", 200,
            R"([{"order_id":999,"order_status":"Submitted"}])");

        AF_Order out{};
        b.place_order("GOOG", AF_ORDER_SELL_STOP, 2.0, 135.0, 0.0, 0.0, 0, "", out);

        bool found_stp = false, found_aux = false;
        for (const auto& call : b.calls) {
            if (call.method == "POST" && call.url.find("/orders") != std::string::npos) {
                found_stp = (call.body.find("\"STP\"") != std::string::npos);
                found_aux = (call.body.find("\"auxPrice\"") != std::string::npos);
            }
        }
        CHK(found_stp);
        CHK(found_aux);
    });

    T("place_order_orderId_camelCase_variant", []{
        /* IBKR sometimes returns "orderId" (camelCase) instead of "order_id" */
        MockIbkrBroker b = make_mock();
        b.set_conid("META", 107113386);
        b.add_route("/iserver/account/U1234567/orders", 200,
            R"([{"orderId":444333,"order_status":"Submitted"}])");

        AF_Order out{};
        AF_Error err = b.place_order("META", AF_ORDER_BUY, 10.0,
                                      0.0, 0.0, 0.0, 0, "", out);
        CHK(err == AF_OK);
        CHK(out.ticket == 444333ULL);
    });

    /* =====================================================================
     * TestGetPositions — mirrors Python :: TestGetPositions
     * ===================================================================== */
    section("GET POSITIONS");

    T("get_positions_parses_response", []{
        /* Mirrors Python: test_get_positions_parses_response */
        MockIbkrBroker b = make_mock();
        b.add_route("/portfolio/U1234567/positions/0", 200, R"([
            {
                "conid":265598,"ticker":"AAPL",
                "position":100.0,"avgCost":175.50,
                "mktPrice":182.00,"unrealizedPnl":650.0
            }
        ])");

        auto positions = b.get_positions();
        CHK(positions.size() == 1);
        const AF_Position& pos = positions[0];
        CHK(pos.ticket == 265598ULL);
        CHK(std::string(pos.symbol) == "AAPL");
        CHK(pos.side == AF_DIR_LONG);
        CHK_NEAR(pos.lots, 100.0, 1e-6);
        CHK_NEAR(pos.open_price, 175.50, 1e-6);
        CHK_NEAR(pos.profit, 650.0, 1e-6);
    });

    T("get_positions_short_position_direction", []{
        /* Mirrors Python: test_get_positions_short_position_direction */
        MockIbkrBroker b = make_mock();
        b.add_route("/portfolio/U1234567/positions/0", 200, R"([
            {"conid":8314,"ticker":"IBM","position":-50.0,
             "avgCost":130.0,"mktPrice":128.0,"unrealizedPnl":100.0}
        ])");

        auto positions = b.get_positions();
        CHK(positions.size() == 1);
        CHK(positions[0].side == AF_DIR_SHORT);
        CHK_NEAR(positions[0].lots, 50.0, 1e-6);  /* abs value */
    });

    T("get_positions_filters_zero_quantity", []{
        /* Mirrors Python: test_get_positions_filters_zero_quantity */
        MockIbkrBroker b = make_mock();
        b.add_route("/portfolio/U1234567/positions/0", 200, R"([
            {"conid":1,"ticker":"A","position":0.0},
            {"conid":2,"ticker":"B","position":10.0,
             "avgCost":50.0,"mktPrice":55.0,"unrealizedPnl":50.0}
        ])");

        auto positions = b.get_positions();
        CHK(positions.size() == 1);
        CHK(positions[0].ticket == 2ULL);
    });

    T("get_position_by_ticket", []{
        /* Mirrors Python: test_get_position_by_ticket */
        MockIbkrBroker b = make_mock();
        b.add_route("/portfolio/U1234567/positions/0", 200, R"([
            {"conid":265598,"ticker":"AAPL","position":100.0,
             "avgCost":175.0,"mktPrice":180.0,"unrealizedPnl":500.0}
        ])");

        auto pos = b.get_position(265598ULL);
        CHK(pos.has_value());
        CHK(pos->ticket == 265598ULL);
    });

    T("get_position_returns_nullopt_for_missing", []{
        /* Mirrors Python: test_get_position_returns_none_for_missing */
        MockIbkrBroker b = make_mock();
        b.add_route("/portfolio/U1234567/positions/0", 200, R"([])");

        auto pos = b.get_position(99999ULL);
        CHK(!pos.has_value());
    });

    T("get_positions_contractDesc_fallback_for_symbol", []{
        /* Uses "contractDesc" when "ticker" absent */
        MockIbkrBroker b = make_mock();
        b.add_route("/portfolio/U1234567/positions/0", 200, R"([
            {"conid":12345,"contractDesc":"SPY","position":50.0,
             "avgCost":420.0,"mktPrice":430.0,"unrealizedPnl":500.0}
        ])");

        auto positions = b.get_positions();
        CHK(positions.size() == 1);
        CHK(std::string(positions[0].symbol) == "SPY");
    });

    /* =====================================================================
     * TestErrorPath — mirrors Python :: TestErrorPath
     * ===================================================================== */
    section("ERROR PATHS");

    T("cancel_order_returns_AF_OK_on_success", []{
        /* Mirrors Python: test_cancel_order_returns_zero_on_success */
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/account/U1234567/order/12345", 200, R"({})");

        AF_Error result = b.cancel_order(12345ULL);
        CHK(result == AF_OK);
    });

    T("cancel_order_returns_nonzero_on_http_error", []{
        /* Mirrors Python: test_cancel_order_returns_nonzero_on_broker_error */
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/account/U1234567/order/99999", 404, R"({"error":"not found"})");

        AF_Error result = b.cancel_order(99999ULL);
        CHK(result != AF_OK);
    });

    T("get_account_http_error_propagates", []{
        /* Mirrors Python: test_get_account_http_error_propagates */
        MockIbkrBroker b = make_mock();
        b.add_route("/portfolio/U1234567/summary", 500, R"({"error":"internal server error"})");

        AF_AccountInfo info{};
        bool threw = false;
        try {
            b.get_account(info);
        } catch (const BrokerError&) {
            threw = true;
        }
        CHK(threw);
    });

    T("modify_position_returns_AF_OK_on_success", []{
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/account/U1234567/order/11111", 200, R"({})");

        AF_Error result = b.modify_position(11111ULL, 150.0, 200.0);
        CHK(result == AF_OK);
    });

    T("modify_position_no_sl_tp_returns_AF_OK_immediately", []{
        /* If both sl and tp are 0.0, returns immediately without HTTP call. */
        MockIbkrBroker b = make_mock();

        AF_Error result = b.modify_position(99999ULL, 0.0, 0.0);
        CHK(result == AF_OK);
        /* No HTTP requests should have been made */
        CHK(b.calls.empty());
    });

    T("close_position_returns_nonzero_when_not_found", []{
        MockIbkrBroker b = make_mock();
        b.add_route("/portfolio/U1234567/positions/0", 200, R"([])");

        AF_Error result = b.close_position(99999ULL, 0.0);
        CHK(result == AF_ERR_POSITION_NOT_FOUND);
    });

    T("close_position_success", []{
        /* Finds position, places opposite order */
        MockIbkrBroker b = make_mock();
        b.add_route("/portfolio/U1234567/positions/0", 200, R"([
            {"conid":265598,"ticker":"AAPL","position":100.0,
             "avgCost":175.0,"mktPrice":180.0,"unrealizedPnl":500.0}
        ])");
        b.add_route("/iserver/account/U1234567/orders", 200,
            R"([{"order_id":543210,"order_status":"Submitted"}])");

        AF_Error result = b.close_position(265598ULL, 0.0);
        CHK(result == AF_OK);
    });

    /* =====================================================================
     * _safe_float edge cases — mirrors Python: _safe_float
     * ===================================================================== */
    section("_SAFE_FLOAT EDGE CASES");

    T("safe_float_null_returns_default", []{
        JsonVal v;  /* Null */
        double result = MockIbkrBroker::_safe_float(v, 0.0);
        CHK_NEAR(result, 0.0, 1e-12);
    });

    T("safe_float_number_returns_value", []{
        JsonVal v; v.type = JsonVal::Type::Number; v.num = 123.45;
        double result = MockIbkrBroker::_safe_float(v, 0.0);
        CHK_NEAR(result, 123.45, 1e-9);
    });

    T("safe_float_string_number_parses", []{
        JsonVal v; v.type = JsonVal::Type::String; v.str = "182.50";
        double result = MockIbkrBroker::_safe_float(v, 0.0);
        CHK_NEAR(result, 182.50, 1e-9);
    });

    T("safe_float_string_with_commas_parses", []{
        /* Mirrors Python: float(str(val).replace(",", "").strip()) */
        JsonVal v; v.type = JsonVal::Type::String; v.str = "1,234,567.89";
        double result = MockIbkrBroker::_safe_float(v, 0.0);
        CHK_NEAR(result, 1234567.89, 1e-6);
    });

    T("safe_float_invalid_string_returns_default", []{
        JsonVal v; v.type = JsonVal::Type::String; v.str = "N/A";
        double result = MockIbkrBroker::_safe_float(v, -1.0);
        CHK_NEAR(result, -1.0, 1e-12);
    });

    T("safe_float_empty_string_returns_default", []{
        JsonVal v; v.type = JsonVal::Type::String; v.str = "";
        double result = MockIbkrBroker::_safe_float(v, 99.0);
        CHK_NEAR(result, 99.0, 1e-12);
    });

    T("safe_float_whitespace_string_returns_default", []{
        JsonVal v; v.type = JsonVal::Type::String; v.str = "   ";
        double result = MockIbkrBroker::_safe_float(v, 42.0);
        CHK_NEAR(result, 42.0, 1e-12);
    });

    T("safe_float_zero_string_returns_zero", []{
        JsonVal v; v.type = JsonVal::Type::String; v.str = "0";
        double result = MockIbkrBroker::_safe_float(v, 99.0);
        CHK_NEAR(result, 0.0, 1e-12);
    });

    T("safe_float_negative_number", []{
        JsonVal v; v.type = JsonVal::Type::Number; v.num = -500.0;
        double result = MockIbkrBroker::_safe_float(v, 0.0);
        CHK_NEAR(result, -500.0, 1e-9);
    });

    /* =====================================================================
     * _summary_amount edge cases — mirrors Python: _summary_amount
     * ===================================================================== */
    section("_SUMMARY_AMOUNT EDGE CASES");

    T("summary_amount_nested_dict_with_amount", []{
        /* Standard IBKR portfolio summary shape: {"amount": 100000.0} */
        JsonVal data;
        data.type = JsonVal::Type::Object;
        JsonVal nested;
        nested.type = JsonVal::Type::Object;
        JsonVal amt; amt.type = JsonVal::Type::Number; amt.num = 100000.0;
        nested.obj["amount"] = amt;
        data.obj["netliquidation"] = nested;

        double result = MockIbkrBroker::_summary_amount(data, "netliquidation");
        CHK_NEAR(result, 100000.0, 1e-6);
    });

    T("summary_amount_flat_number", []{
        /* Flat number at top level */
        JsonVal data;
        data.type = JsonVal::Type::Object;
        JsonVal v; v.type = JsonVal::Type::Number; v.num = 55000.0;
        data.obj["availablefunds"] = v;

        double result = MockIbkrBroker::_summary_amount(data, "availablefunds");
        CHK_NEAR(result, 55000.0, 1e-6);
    });

    T("summary_amount_missing_key_returns_zero", []{
        JsonVal data;
        data.type = JsonVal::Type::Object;

        double result = MockIbkrBroker::_summary_amount(data, "nonexistent");
        CHK_NEAR(result, 0.0, 1e-12);
    });

    T("summary_amount_non_object_data_returns_zero", []{
        JsonVal data;  /* Null */

        double result = MockIbkrBroker::_summary_amount(data, "netliquidation");
        CHK_NEAR(result, 0.0, 1e-12);
    });

    /* =====================================================================
     * TestParseOrder — mirrors Python fields of _parse_order
     * ===================================================================== */
    section("_PARSE_ORDER");

    T("parse_order_buy_order_snake_case", []{
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/account/orders", 200, R"({
            "orders": [
                {
                    "orderId":101010,
                    "ticker":"AAPL",
                    "side":"BUY",
                    "remainingQuantity":100.0,
                    "price":180.0,
                    "avgPrice":0.0,
                    "description":"Test BUY"
                }
            ]
        })");

        auto orders = b.get_orders();
        CHK(orders.size() == 1);
        CHK(orders[0].ticket == 101010ULL);
        CHK(std::string(orders[0].symbol) == "AAPL");
        CHK(orders[0].type == AF_ORDER_BUY);
        CHK_NEAR(orders[0].lots, 100.0, 1e-6);
        CHK_NEAR(orders[0].price, 180.0, 1e-6);
        CHK(std::string(orders[0].comment) == "Test BUY");
    });

    T("parse_order_sell_order", []{
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/account/orders", 200, R"([
            {
                "orderId":202020,
                "ticker":"TSLA",
                "side":"SELL",
                "totalSize":50.0,
                "price":250.0
            }
        ])");

        auto orders = b.get_orders();
        CHK(orders.size() == 1);
        CHK(orders[0].type == AF_ORDER_SELL);
        CHK_NEAR(orders[0].lots, 50.0, 1e-6);
    });

    T("parse_order_order_id_fallback", []{
        /* order_id (snake_case) when orderId absent */
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/account/orders", 200, R"([
            {"order_id":333333,"ticker":"IBM","side":"BUY","remainingQuantity":10.0}
        ])");

        auto orders = b.get_orders();
        CHK(orders.size() == 1);
        CHK(orders[0].ticket == 333333ULL);
    });

    /* =====================================================================
     * TestGetOrders — dict vs array shapes
     * ===================================================================== */
    section("GET ORDERS");

    T("get_orders_dict_with_orders_key", []{
        /* Mirrors Python: data.get("orders", []) when data is dict */
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/account/orders", 200, R"({
            "orders": [
                {"orderId":1,"ticker":"A","side":"BUY","remainingQuantity":1.0}
            ]
        })");

        auto orders = b.get_orders();
        CHK(orders.size() == 1);
    });

    T("get_orders_direct_array", []{
        /* Mirrors Python: elif isinstance(data, list): raw_orders = data */
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/account/orders", 200, R"([
            {"orderId":2,"ticker":"B","side":"SELL","remainingQuantity":2.0}
        ])");

        auto orders = b.get_orders();
        CHK(orders.size() == 1);
        CHK(orders[0].type == AF_ORDER_SELL);
    });

    T("get_orders_empty_list", []{
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/account/orders", 200, R"([])");

        auto orders = b.get_orders();
        CHK(orders.empty());
    });

    /* =====================================================================
     * TestGetBars — timeframe mapping + tail trimming
     * ===================================================================== */
    section("GET BARS");

    T("get_bars_returns_correct_count", []{
        MockIbkrBroker b = make_mock();
        b.set_conid("AAPL", 265598);
        b.add_route("/iserver/marketdata/history", 200, R"({
            "data": [
                {"t":1700000000000,"o":175.0,"h":176.0,"l":174.0,"c":175.5,"v":1000000},
                {"t":1700086400000,"o":175.5,"h":177.0,"l":175.0,"c":176.0,"v":900000},
                {"t":1700172800000,"o":176.0,"h":178.0,"l":175.5,"c":177.5,"v":1100000}
            ]
        })");

        AF_Bar bars[10];
        int filled = 0;
        AF_Error err = b.get_bars("AAPL", AF_TF_D1, 3, bars, &filled);
        CHK(err == AF_OK);
        CHK(filled == 3);
        CHK_NEAR(bars[0].open, 175.0, 1e-6);
        CHK_NEAR(bars[2].close, 177.5, 1e-6);
    });

    T("get_bars_tail_trim_to_count", []{
        /* Python: bars[-count:] — trim to most recent */
        MockIbkrBroker b = make_mock();
        b.set_conid("MSFT", 272093);
        /* 5 bars returned, request only 3 */
        b.add_route("/iserver/marketdata/history", 200, R"({
            "data": [
                {"t":1700000000000,"o":1.0,"h":1.1,"l":0.9,"c":1.05,"v":100},
                {"t":1700086400000,"o":2.0,"h":2.1,"l":1.9,"c":2.05,"v":100},
                {"t":1700172800000,"o":3.0,"h":3.1,"l":2.9,"c":3.05,"v":100},
                {"t":1700259200000,"o":4.0,"h":4.1,"l":3.9,"c":4.05,"v":100},
                {"t":1700345600000,"o":5.0,"h":5.1,"l":4.9,"c":5.05,"v":100}
            ]
        })");

        AF_Bar bars[5];
        int filled = 0;
        b.get_bars("MSFT", AF_TF_D1, 3, bars, &filled);
        CHK(filled == 3);
        /* Most recent 3: open 3.0, 4.0, 5.0 */
        CHK_NEAR(bars[0].open, 3.0, 1e-6);
        CHK_NEAR(bars[2].open, 5.0, 1e-6);
    });

    T("get_bars_timestamp_ms_to_seconds", []{
        /* Mirrors Python: int(rb.get("t", 0)) // 1000 */
        MockIbkrBroker b = make_mock();
        b.set_conid("AAPL", 265598);
        b.add_route("/iserver/marketdata/history", 200, R"({
            "data": [{"t":1700000000000,"o":1.0,"h":1.0,"l":1.0,"c":1.0,"v":0}]
        })");

        AF_Bar bars[1];
        int filled = 0;
        b.get_bars("AAPL", AF_TF_D1, 1, bars, &filled);
        CHK(filled == 1);
        CHK(bars[0].timestamp == 1700000000LL);  /* ms -> s */
    });

    T("get_bars_uses_correct_timeframe_strings", []{
        /* Verify M1 maps to bar=1min, period=1d */
        MockIbkrBroker b = make_mock();
        b.set_conid("AAPL", 265598);
        b.add_route("/iserver/marketdata/history", 200, R"({"data":[]})");

        AF_Bar bars[5];
        int filled = 0;
        b.get_bars("AAPL", AF_TF_M1, 5, bars, &filled);

        bool found = false;
        for (const auto& call : b.calls) {
            if (call.url.find("history") != std::string::npos &&
                call.url.find("1min") != std::string::npos &&
                call.url.find("1d")   != std::string::npos) {
                found = true;
            }
        }
        CHK(found);
    });

    /* =====================================================================
     * TestGetSymbolInfo
     * ===================================================================== */
    section("GET SYMBOL INFO");

    T("get_symbol_info_returns_sensible_defaults", []{
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/secdef/search", 200,
            R"([{"conid":265598,"ticker":"AAPL","companyName":"Apple Inc"}])");

        AF_SymbolInfo info{};
        AF_Error err = b.get_symbol_info("AAPL", info);
        CHK(err == AF_OK);
        CHK(std::string(info.name) == "AAPL");
        CHK(info.digits == 2);
        CHK_NEAR(info.point, 0.01, 1e-12);
        CHK_NEAR(info.contract_size, 1.0, 1e-12);
        CHK_NEAR(info.volume_min, 1.0, 1e-12);
        CHK_NEAR(info.volume_step, 1.0, 1e-12);
    });

    T("get_symbol_info_empty_response_raises", []{
        MockIbkrBroker b = make_mock();
        b.add_route("/iserver/secdef/search", 200, R"([])");

        AF_SymbolInfo info{};
        bool threw = false;
        try {
            b.get_symbol_info("NOPE", info);
        } catch (const BrokerError&) {
            threw = true;
        }
        CHK(threw);
    });

    /* =====================================================================
     * Registration test
     * ===================================================================== */
    section("REGISTRY");

    T("register_ibkr_broker registers ibkr name", []{
        register_ibkr_broker();
        auto brokers = available_brokers();
        bool found = false;
        for (const auto& name : brokers) {
            if (name == "ibkr") { found = true; break; }
        }
        CHK(found);
    });
}
