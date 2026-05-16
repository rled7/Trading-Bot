#include <stdio.h>
#include <stdlib.h>
#include "af_types.h"

static int g_passed = 0;
static int g_failed = 0;

#define CHK(cond) do {                                              \
    if (cond) { ++g_passed; }                                       \
    else {                                                          \
        ++g_failed;                                                 \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
} while (0)

int main(void) {
    CHK(AF_TF_H1 == 3600);
    CHK(AF_DIR_LONG == 1);
    CHK(AF_DIR_SHORT == -1);
    CHK(sizeof(af_bar_t) >= 6 * sizeof(double) + sizeof(int64_t));

    printf("  COMPLETE | Passed: %d | Failed: %d\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
