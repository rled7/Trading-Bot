/**
 * AlgoForge — tests/test_health.cpp
 * Parity tests for monitoring::HealthMonitor / HealthSnapshot.
 * Python oracle: python/algoforge/health.py. Standalone runner (own main).
 */
#include "test_helpers.hpp"
#include "monitoring/health.hpp"
#include "broker/broker.hpp"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <utility>

using algoforge::monitoring::HealthMonitor;
using algoforge::monitoring::HealthSnapshot;

namespace af { std::unique_ptr<IBroker> make_paper_broker(double balance); }

static int g_pass = 0, g_fail = 0;
static std::vector<std::pair<std::string, std::string>> g_failures;

void run_test(const char* name, std::function<void()> fn) {
    try {
        fn();
        printf("  \033[92m✓\033[0m %-66s\n", name); g_pass++;
    } catch (std::string& e) {
        printf("  \033[91m✗\033[0m %-66s\n       → %s\n", name, e.c_str());
        g_fail++; g_failures.push_back({name, e});
    } catch (std::exception& ex) {
        printf("  \033[91m✗\033[0m %-66s EXCEPTION\n       → %s\n", name, ex.what());
        g_fail++; g_failures.push_back({name, ex.what()});
    }
}
void section(const char* title) { printf("\n  %s\n  %s\n", title, std::string(72, '-').c_str()); }

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

void run_health_tests() {
    section("health — status derivation (Python thresholds)");
    run_test("guard RED (2) → CRITICAL", [] {
        HealthMonitor m;
        CHK_EQ(m.check(nullptr, 10000, 0.0, 2).status, std::string("CRITICAL"));
    });
    run_test("guard YELLOW (1) → WARN", [] {
        HealthMonitor m;
        CHK_EQ(m.check(nullptr, 10000, 0.0, 1).status, std::string("WARN"));
    });
    run_test("daily loss > 3% → WARN even at GREEN", [] {
        HealthMonitor m;
        CHK_EQ(m.check(nullptr, 10000, -0.05, 0).status, std::string("WARN"));
    });
    run_test("GREEN + small loss → OK", [] {
        HealthMonitor m;
        CHK_EQ(m.check(nullptr, 10000, -0.01, 0).status, std::string("OK"));
        // -0.03 is the boundary: NOT < -0.03, so still OK
        CHK_EQ(m.check(nullptr, 10000, -0.03, 0).status, std::string("OK"));
    });

    section("health — broker fields");
    run_test("null broker → not connected, 0 open positions", [] {
        HealthMonitor m;
        auto s = m.check(nullptr, 10000, 0.0, 0);
        CHK_FALSE(s.broker_connected);
        CHK_EQ(s.open_positions, 0);
    });
    run_test("connected paper broker → connected, counts positions", [] {
        auto broker = af::make_paper_broker(10000);
        broker->connect();
        HealthMonitor m;
        auto s = m.check(broker.get(), 10000, 0.0, 0);
        CHK(s.broker_connected);
        CHK_EQ(s.open_positions, 0);   // no trades placed
    });

    section("health — snapshot fields + JSON");
    run_test("check() passes through equity/counters", [] {
        HealthMonitor m;
        auto s = m.check(nullptr, 12345.67, -0.02, 1, 42, 3);
        CHK_EQ(s.equity, 12345.67);
        CHK_EQ(s.daily_pnl_pct, -0.02);
        CHK_EQ(s.guard_state, 1);
        CHK_EQ(s.signals_processed, 42);
        CHK_EQ(s.signals_rejected, 3);
    });
    run_test("to_json emits all nine fields", [] {
        HealthSnapshot s;
        s.uptime_sec = 5; s.broker_connected = true; s.equity = 10000;
        s.daily_pnl_pct = -0.01; s.guard_state = 0; s.open_positions = 2;
        s.signals_processed = 10; s.signals_rejected = 1; s.status = "OK";
        auto j = s.to_json();
        CHK(contains(j, "\"uptime_sec\":5"));
        CHK(contains(j, "\"broker_connected\":true"));
        CHK(contains(j, "\"equity\":10000"));
        CHK(contains(j, "\"daily_pnl_pct\":-0.01"));
        CHK(contains(j, "\"guard_state\":0"));
        CHK(contains(j, "\"open_positions\":2"));
        CHK(contains(j, "\"signals_processed\":10"));
        CHK(contains(j, "\"signals_rejected\":1"));
        CHK(contains(j, "\"status\":\"OK\""));
    });
}

int main() {
    printf("\n  %s\n", std::string(72, '=').c_str());
    printf("  AlgoForge HealthMonitor Parity Tests\n");
    printf("  %s\n", std::string(72, '=').c_str());
    run_health_tests();
    int total = g_pass + g_fail;
    printf("\n  %s\n", std::string(72, '=').c_str());
    printf("  COMPLETE | Passed: %d | Failed: %d | Total: %d | Score: %.1f%%\n",
           g_pass, g_fail, total, total ? (double)g_pass / total * 100 : 0.0);
    if (!g_failures.empty()) {
        printf("\n  FAILURES:\n");
        for (auto& [n, e] : g_failures) printf("    ✗ %s\n      → %s\n", n.c_str(), e.c_str());
    }
    printf("  %s\n\n", std::string(72, '=').c_str());
    return g_fail == 0 ? 0 : 1;
}
