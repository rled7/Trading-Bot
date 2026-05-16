#include "test_helpers.hpp"
/**
 * AlgoForge — tests/test_main.cpp
 * Self-contained test runner. No external test framework required.
 * Forward-declares test suites from other test files.
 */

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

void af_test_assert_impl(bool cond, const char *expr, const char *file, int line) {
    if (!cond) {
        char msg[512];
        snprintf(msg, sizeof(msg), "%s:%d: ASSERT(%s)", file, line, expr);
        throw std::string(msg);
    }
}

#define AF_ASSERT(expr) af_test_assert_impl((expr), #expr, __FILE__, __LINE__)
#define AF_ASSERT_EQ(a,b) AF_ASSERT((a)==(b))
#define AF_ASSERT_NEAR(a,b,tol) AF_ASSERT(std::abs((a)-(b))<=(tol))
#define AF_ASSERT_GT(a,b) AF_ASSERT((a)>(b))
#define AF_ASSERT_GE(a,b) AF_ASSERT((a)>=(b))
#define AF_ASSERT_LT(a,b) AF_ASSERT((a)<(b))
#define AF_ASSERT_FALSE(expr) AF_ASSERT(!(expr))

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

/* ── Forward declarations of test suites (defined in other files) ── */
void test_indicators(RawTestFn&);
void test_patterns(RawTestFn&);
void test_risk(RawTestFn&);
void test_broker(RawTestFn&);
void test_backtest(RawTestFn&);
void test_learning(RawTestFn&);

int main() {
    printf("\n  %s\n", std::string(75,'=').c_str());
    printf("  AlgoForge C/C++ Test Suite\n");
    printf("  %s\n", std::string(75,'=').c_str());

    test_indicators(run_test);
    test_patterns  (run_test);
    test_risk      (run_test);
    test_broker    (run_test);
    test_backtest  (run_test);
    test_learning  (run_test);

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
