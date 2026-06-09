/**
 * AlgoForge — tests/test_generator_main.cpp
 * Standalone runner for algo_gen generator tests (Phase 2).
 */
#include "test_helpers.hpp"
#include <cstdio>
#include <string>
#include <vector>
#include <functional>
#include <chrono>

static int g_pass = 0, g_fail = 0;
static std::vector<std::pair<std::string,std::string>> g_failures;

void run_test(const char *name, std::function<void()> fn) {
    auto t0 = std::chrono::steady_clock::now();
    try {
        fn();
        auto ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
        printf("  \033[92m✓\033[0m %-66s %6.1fms\n", name, ms); g_pass++;
    } catch (std::string &e) {
        printf("  \033[91m✗\033[0m %-66s\n       → %s\n", name, e.c_str()); g_fail++; g_failures.push_back({name,e});
    } catch (std::exception &ex) {
        printf("  \033[91m✗\033[0m %-66s EXCEPTION\n       → %s\n", name, ex.what()); g_fail++; g_failures.push_back({name,ex.what()});
    }
}
void section(const char *title) { printf("\n  %s\n  %s\n", title, std::string(72,'-').c_str()); }

void test_generator(RawTestFn&);

int main() {
    printf("\n  %s\n", std::string(72,'=').c_str());
    printf("  AlgoForge algo_gen Generator Tests (Phase 2)\n");
    printf("  %s\n", std::string(72,'=').c_str());
    test_generator(run_test);
    int total = g_pass + g_fail;
    printf("\n  %s\n", std::string(72,'=').c_str());
    printf("  COMPLETE | Passed: %d | Failed: %d | Total: %d | Score: %.1f%%\n",
           g_pass, g_fail, total, total ? (double)g_pass/total*100 : 0.0);
    if (!g_failures.empty()) { printf("\n  FAILURES:\n"); for (auto &[n,e]:g_failures) printf("    ✗ %s\n      → %s\n", n.c_str(), e.c_str()); }
    printf("  %s\n\n", std::string(72,'=').c_str());
    return g_fail == 0 ? 0 : 1;
}
