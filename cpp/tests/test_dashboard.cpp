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
}
