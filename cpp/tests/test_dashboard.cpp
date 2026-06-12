/**
 * AlgoForge — tests/test_dashboard.cpp
 *
 * Phase 5 dashboard parity tests (slice 1: log_buffer + health/symbols/logs handlers).
 * Python oracle: python/algoforge/dashboard/{log_buffer.py, server.py},
 *                python/tests/dashboard/ . Tests the pure handler logic; the httplib
 *                socket layer (server.cpp) is thin/untested by design.
 */
#include "test_helpers.hpp"
#include "dashboard/handlers.hpp"
#include "dashboard/log_buffer.hpp"

#include <string>
#include <vector>

using namespace algoforge::dashboard;

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

void test_dashboard(RawTestFn& T) {
    section("dashboard — json_escape");
    T("escapes quotes, backslash, and control chars", []{
        CHK_EQ(json_escape("a\"b"), std::string("a\\\"b"));
        CHK_EQ(json_escape("a\\b"), std::string("a\\\\b"));
        CHK_EQ(json_escape("line1\nline2"), std::string("line1\\nline2"));
        CHK_EQ(json_escape("tab\there"), std::string("tab\\there"));
    });

    section("dashboard — /api/health JSON (server.py health())");
    T("emits broker_name, is_connected, version, uptime_seconds", []{
        auto j = health_json("paper", true, "1.0.0", 12.5);
        CHK(contains(j, "\"broker_name\":\"paper\""));
        CHK(contains(j, "\"is_connected\":true"));
        CHK(contains(j, "\"version\":\"1.0.0\""));
        CHK(contains(j, "\"uptime_seconds\":"));
        auto j2 = health_json("oanda", false, "1.0.0", 0.0);
        CHK(contains(j2, "\"is_connected\":false"));
        CHK(contains(j2, "\"broker_name\":\"oanda\""));
    });

    section("dashboard — /api/symbols JSON");
    T("emits a JSON string array", []{
        CHK_EQ(symbols_json({}), std::string("[]"));
        CHK_EQ(symbols_json({"EURUSD"}), std::string("[\"EURUSD\"]"));
        CHK_EQ(symbols_json({"EURUSD", "GBPUSD"}), std::string("[\"EURUSD\",\"GBPUSD\"]"));
    });

    section("dashboard — /api/logs ?n= clamp (server.py logs())");
    T("clamps n to max(1, min(n, 500))", []{
        CHK_EQ(clamp_log_count(0), 1);
        CHK_EQ(clamp_log_count(-7), 1);
        CHK_EQ(clamp_log_count(50), 50);
        CHK_EQ(clamp_log_count(1000), 500);
    });

    section("dashboard — LogRingBuffer (log_buffer.py)");
    T("capacity evicts oldest; tail(n) returns last n chronologically", []{
        LogRingBuffer buf(3);
        for (int i = 0; i < 5; ++i)
            buf.push({"ts", "INFO", "engine", "m" + std::to_string(i)});
        CHK_EQ(buf.size(), (size_t)3);            // capacity enforced
        auto last2 = buf.tail(2);
        CHK_EQ(last2.size(), (size_t)2);
        CHK_EQ(last2[0].msg, std::string("m3"));  // chronological order
        CHK_EQ(last2[1].msg, std::string("m4"));
    });
    T("tail(n >= size) returns all records (Python tail semantics)", []{
        LogRingBuffer buf(10);
        buf.push({"ts", "INFO", "c", "a"});
        buf.push({"ts", "WARN", "c", "b"});
        auto all = buf.tail(50);
        CHK_EQ(all.size(), (size_t)2);
        CHK_EQ(all[0].msg, std::string("a"));
        CHK_EQ(all[1].level, std::string("WARN"));
    });

    section("dashboard — /api/logs JSON shape");
    T("each record serialises ts/level/component/msg", []{
        std::vector<LogRecord> recs = {{"2026-06-09T00:00:00Z", "INFO", "engine", "started"}};
        auto j = logs_json(recs);
        CHK(contains(j, "\"ts\":\"2026-06-09T00:00:00Z\""));
        CHK(contains(j, "\"level\":\"INFO\""));
        CHK(contains(j, "\"component\":\"engine\""));
        CHK(contains(j, "\"msg\":\"started\""));
        CHK_EQ(logs_json({}), std::string("[]"));
    });

    // ── Slice 2: broker-backed routes ──
    section("dashboard — /api/account JSON (server.py account())");
    T("serialises AF_AccountInfo fields", []{
        AF_AccountInfo a{};
        a.balance = 10000; a.equity = 10050; a.margin = 200; a.free_margin = 9850;
        a.profit = 50; a.leverage = 100; a.login = 42;
        std::snprintf(a.currency, sizeof a.currency, "USD");
        auto j = account_json(a);
        CHK(contains(j, "\"balance\":10000"));
        CHK(contains(j, "\"free_margin\":9850"));
        CHK(contains(j, "\"leverage\":100"));
        CHK(contains(j, "\"currency\":\"USD\""));
        CHK(contains(j, "\"login\":42"));
    });

    section("dashboard — /api/positions JSON (side coerced to int)");
    T("position side serialises as int (Python _position_dict)", []{
        AF_Position p{};
        p.ticket = 7; p.side = AF_DIR_LONG; p.lots = 0.5; p.profit = 12.5;
        std::snprintf(p.symbol, sizeof p.symbol, "EURUSD");
        auto j = position_json(p);
        CHK(contains(j, "\"ticket\":7"));
        CHK(contains(j, "\"symbol\":\"EURUSD\""));
        CHK(contains(j, "\"side\":1"));        // int(Direction.LONG)
        CHK(contains(j, "\"profit\":12.5"));
        CHK_EQ(positions_json({}), std::string("[]"));
        CHK(contains(positions_json({p, p}), "},{"));   // two elements
    });

    section("dashboard — /api/orders JSON (type coerced to int)");
    T("order type serialises as int (Python _order_dict)", []{
        AF_Order o{};
        o.ticket = 3; o.type = AF_ORDER_SELL; o.lots = 1.0; o.price = 1.2345;
        std::snprintf(o.symbol, sizeof o.symbol, "GBPUSD");
        auto j = order_json(o);
        CHK(contains(j, "\"type\":1"));        // int(OrderType.SELL)
        CHK(contains(j, "\"symbol\":\"GBPUSD\""));
        CHK(contains(j, "\"price\":1.2345"));
        CHK_EQ(orders_json({}), std::string("[]"));
    });

    section("dashboard — /api/bars JSON + timeframe/count validation");
    T("bar serialises the 7 Python Bar fields", []{
        AF_Bar b{};
        b.timestamp = 1700000000; b.open = 1.1; b.high = 1.2; b.low = 1.0;
        b.close = 1.15; b.volume = 1000; b.spread = 0.0001;
        auto j = bar_json(b);
        CHK(contains(j, "\"timestamp\":1700000000"));
        CHK(contains(j, "\"open\":1.1"));
        CHK(contains(j, "\"close\":1.15"));
        CHK(contains(j, "\"spread\":0.0001"));
        CHK_FALSE(contains(j, "body"));   // derived AF_Bar fields excluded for parity
    });
    T("parse_timeframe accepts valid names, rejects unknown", []{
        AF_Timeframe tf;
        CHK(parse_timeframe("M5", tf));  CHK_EQ((int)tf, (int)AF_TF_M5);
        CHK(parse_timeframe("H1", tf));  CHK_EQ((int)tf, (int)AF_TF_H1);
        CHK(parse_timeframe("W1", tf));  CHK_EQ((int)tf, (int)AF_TF_W1);
        CHK_FALSE(parse_timeframe("Z9", tf));
        CHK_FALSE(parse_timeframe("m5", tf));   // case-sensitive, like Python Timeframe[tf]
        CHK_EQ(timeframe_names().size(), (size_t)9);
    });
    T("clamp_bar_count clamps to [1, 5000]", []{
        CHK_EQ(clamp_bar_count(0), 1);
        CHK_EQ(clamp_bar_count(200), 200);
        CHK_EQ(clamp_bar_count(99999), 5000);
    });

    // ── Slice 3: llm routes ──
    using LK = algoforge::llm::LLMErrorKind;
    section("dashboard — llm error status mapping (_LLM_ERROR_STATUS)");
    T("each error kind maps to the Python status code", []{
        CHK_EQ(llm_error_status(LK::timeout), 504);
        CHK_EQ(llm_error_status(LK::unreachable), 502);
        CHK_EQ(llm_error_status(LK::http), 502);
        CHK_EQ(llm_error_status(LK::decode), 500);
        CHK_EQ(llm_error_status(LK::model_missing), 503);
    });
    T("error kind names + error body shape", []{
        CHK_EQ(llm_error_kind_name(LK::timeout), std::string("timeout"));
        CHK_EQ(llm_error_kind_name(LK::model_missing), std::string("model_missing"));
        CHK_EQ(llm_error_json("timeout", "slow"),
               std::string("{\"error\":\"timeout\",\"detail\":\"slow\"}"));
    });

    section("dashboard — /api/llm/health JSON");
    T("ok body emits status/host/model/model_loaded; host empty => null", []{
        algoforge::llm::HealthStatus s;
        s.ok = true; s.model_loaded = true; s.model = std::string("llama3.1:8b");
        auto j = llm_health_ok_json(s, "127.0.0.1:11434");
        CHK(contains(j, "\"status\":\"ok\""));
        CHK(contains(j, "\"host\":\"127.0.0.1:11434\""));
        CHK(contains(j, "\"model\":\"llama3.1:8b\""));
        CHK(contains(j, "\"model_loaded\":true"));
        auto j2 = llm_health_ok_json(s, "");
        CHK(contains(j2, "\"host\":null"));
    });

    section("dashboard — /api/llm/models JSON");
    T("emits {\"models\":[names]}", []{
        algoforge::llm::ModelInfo a; a.name = "llama3.1:8b";
        algoforge::llm::ModelInfo b; b.name = "qwen2.5:32b";
        CHK_EQ(llm_models_json({}), std::string("{\"models\":[]}"));
        CHK_EQ(llm_models_json({a, b}),
               std::string("{\"models\":[\"llama3.1:8b\",\"qwen2.5:32b\"]}"));
    });

    section("dashboard — /api/llm/chat response JSON");
    T("emits {content,model,tokens}", []{
        algoforge::llm::ChatResponse r;
        r.message.content = "hello"; r.model = "llama3.1:8b"; r.completion_tokens = 7;
        auto j = llm_chat_response_json(r);
        CHK(contains(j, "\"content\":\"hello\""));
        CHK(contains(j, "\"model\":\"llama3.1:8b\""));
        CHK(contains(j, "\"tokens\":7"));
    });

    section("dashboard — SSE event formatting (chat/stream)");
    T("data line + [DONE] terminator", []{
        CHK_EQ(sse_data_line("hi"), std::string("data: hi\n\n"));
        CHK_EQ(sse_data_line("[DONE]"), std::string("data: [DONE]\n\n"));
    });
}
