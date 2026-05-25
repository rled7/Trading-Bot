#include <stdio.h>

void test_indicators_run(int *total_passed, int *total_failed);
void test_patterns_run  (int *total_passed, int *total_failed);
void test_analytics_run (int *total_passed, int *total_failed);
void test_llm_run       (int *total_passed, int *total_failed);
void test_algo_gen_run  (int *total_passed, int *total_failed);

int main(void) {
    int passed = 0, failed = 0;
    test_indicators_run(&passed, &failed);
    test_patterns_run  (&passed, &failed);
    test_analytics_run (&passed, &failed);
    test_llm_run       (&passed, &failed);
    test_algo_gen_run  (&passed, &failed);
    printf("  COMPLETE | Passed: %d | Failed: %d\n", passed, failed);
    return failed ? 1 : 0;
}
