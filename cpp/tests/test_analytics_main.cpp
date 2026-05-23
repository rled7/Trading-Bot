/**
 * AlgoForge — tests/test_analytics_main.cpp
 * Self-contained test runner for the S4 analytics layer.
 *
 * Pattern matches the existing test infrastructure in test_helpers.hpp.
 */

#include "test_helpers.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

/* ── Global counters ── */
static int g_pass = 0, g_fail = 0;
static std::vector<std::pair<std::string, std::string>> g_failures;

void run_test(const char* name, std::function<void()> fn) {
    auto t0 = std::chrono::steady_clock::now();
    try {
        fn();
        auto ms = std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - t0).count();
        printf("  \033[92m+\033[0m %-70s %6.1fms\n", name, ms);
        ++g_pass;
    } catch (std::string& e) {
        auto ms = std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - t0).count();
        printf("  \033[91mF\033[0m %-70s %6.1fms\n", name, ms);
        printf("      -> %s\n", e.c_str());
        ++g_fail;
        g_failures.push_back({name, e});
    } catch (std::exception& ex) {
        printf("  \033[91mF\033[0m %-70s EXCEPTION\n", name);
        printf("      -> %s\n", ex.what());
        ++g_fail;
        g_failures.push_back({name, ex.what()});
    }
}

void section(const char* title) {
    printf("\n  %s\n  %s\n", title,
           "----------------------------------------------------------------------");
}

/* Forward declaration of analytics test suite */
void test_analytics(TestRunner T);

int main() {
    printf("\n  ======================================================================\n");
    printf("  AlgoForge Analytics Test Suite (S4)\n");
    printf("  ======================================================================\n");

    test_analytics(run_test);

    int total = g_pass + g_fail;
    printf("\n  ======================================================================\n");
    printf("  COMPLETE | Passed: %d | Failed: %d | Total: %d | Score: %.1f%%\n",
           g_pass, g_fail, total,
           total ? static_cast<double>(g_pass) / total * 100.0 : 0.0);
    if (!g_failures.empty()) {
        printf("\n  FAILURES:\n");
        for (auto& [n, e] : g_failures) {
            printf("    F %s\n      -> %s\n", n.c_str(), e.c_str());
        }
    }
    printf("  ======================================================================\n\n");
    return (g_fail == 0) ? 0 : 1;
}
