/**
 * AlgoForge — tests/test_rest_broker_main.cpp
 * Standalone runner for the REST broker foundation tests (Phase 1 / S5).
 * Mirrors the test_llm_main.cpp / test_analytics_main.cpp pattern.
 */

#include "test_helpers.hpp"

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <cmath>

/* ── Test infrastructure ── */
static int  g_pass = 0, g_fail = 0;
static std::vector<std::pair<std::string,std::string>> g_failures;

void run_test(const char *name, std::function<void()> fn) {
    auto t0 = std::chrono::steady_clock::now();
    try {
        fn();
        auto ms = std::chrono::duration<double,std::milli>(
                      std::chrono::steady_clock::now()-t0).count();
        printf("  \033[92m✓\033[0m %-65s %6.1fms\n", name, ms);
        g_pass++;
    } catch (std::string &e) {
        auto ms = std::chrono::duration<double,std::milli>(
                      std::chrono::steady_clock::now()-t0).count();
        printf("  \033[91m✗\033[0m %-65s %6.1fms\n", name, ms);
        printf("       → %s\n", e.c_str());
        g_fail++;
        g_failures.push_back({name, e});
    } catch (std::exception &ex) {
        printf("  \033[91m✗\033[0m %-65s EXCEPTION\n", name);
        printf("       → %s\n", ex.what());
        g_fail++;
        g_failures.push_back({name, ex.what()});
    }
}

void section(const char *title) {
    printf("\n  %s\n  %s\n", title, std::string(75,'-').c_str());
}

/* Forward declaration of the test suite */
void test_rest_broker(RawTestFn&);

int main() {
    printf("\n  %s\n", std::string(75,'=').c_str());
    printf("  AlgoForge REST Broker Foundation Tests (Phase 1 / S5)\n");
    printf("  %s\n", std::string(75,'=').c_str());

    test_rest_broker(run_test);

    int total = g_pass + g_fail;
    printf("\n  %s\n", std::string(75,'=').c_str());
    printf("  COMPLETE | Passed: %d | Failed: %d | Total: %d | Score: %.1f%%\n",
           g_pass, g_fail, total, total ? (double)g_pass/total*100 : 0.0);
    if (!g_failures.empty()) {
        printf("\n  FAILURES:\n");
        for (auto &[n,e]:g_failures) {
            printf("    ✗ %s\n      → %s\n", n.c_str(), e.c_str());
        }
    }
    printf("  %s\n\n", std::string(75,'=').c_str());
    return g_fail == 0 ? 0 : 1;
}
