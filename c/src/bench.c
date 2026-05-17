/* Cross-language benchmark — c/ */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "af_indicators.h"
#include "af_patterns.h"

static double *gen_series(size_t n) {
    /* Deterministic walk shared across all four language benches. */
    double *a = (double*)malloc(n * sizeof(double));
    double p = 100.0;
    for (size_t i = 0; i < n; ++i) {
        p += (double)(((int)i * 9301 + 49297) % 233 - 116) * 0.01;
        a[i] = p;
    }
    return a;
}

static int64_t nanos_since(struct timespec t0) {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (int64_t)(t1.tv_sec - t0.tv_sec) * 1000000000LL
         + (int64_t)(t1.tv_nsec - t0.tv_nsec);
}

#define TIMED(label, fn) do {                                       \
    struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);        \
    for (int r = 0; r < iters; ++r) (void)fn;                       \
    int64_t ns = nanos_since(t0);                                   \
    printf("%s\t%lld\t%lld\n", label, (long long)ns,                \
           (long long)(ns / iters));                                \
} while (0)

int main(int argc, char **argv) {
    size_t n     = (argc > 1) ? (size_t)atoi(argv[1]) : 100000;
    int    iters = (argc > 2) ? atoi(argv[2])         : 10;

    double *src = gen_series(n);
    double *hi  = (double*)malloc(n * sizeof(double));
    double *lo  = (double*)malloc(n * sizeof(double));
    double *vol = (double*)malloc(n * sizeof(double));
    for (size_t i = 0; i < n; ++i) {
        hi[i]  = src[i] + 0.5;
        lo[i]  = src[i] - 0.5;
        vol[i] = 1000.0 + (double)(i % 500);
    }
    double *out = (double*)malloc(n * sizeof(double));
    double *aux1 = (double*)malloc(n * sizeof(double));
    double *aux2 = (double*)malloc(n * sizeof(double));

    printf("# language\tc\n");
    printf("# bars\t%zu\n", n);
    printf("# iterations\t%d\n", iters);
    printf("indicator\ttotal_ns\tns_per_iter\n");

    TIMED("sma20",     af_sma(src, n, 20, out));
    TIMED("ema50",     af_ema(src, n, 50, out));
    TIMED("rsi14",     af_rsi(src, n, 14, out));
    TIMED("atr14",     af_atr(hi, lo, src, n, 14, out));
    TIMED("macd",      af_macd(src, n, 12, 26, 9, out, aux1, aux2));
    TIMED("bollinger", af_bollinger(src, n, 20, 2.0, out, aux1, aux2));
    TIMED("stoch",     af_stochastic(hi, lo, src, n, 14, 3, out, aux1));
    TIMED("obv",       af_obv(src, vol, n, out));
    TIMED("adx",       af_adx(hi, lo, src, n, 14, out, aux1, aux2));

    /* Pattern scan over a bar array. */
    af_bar_t *bars = (af_bar_t*)malloc(n * sizeof(af_bar_t));
    for (size_t i = 0; i < n; ++i) {
        bars[i].timestamp = (int64_t)i;
        bars[i].open  = src[i];
        bars[i].close = src[i] + 0.001 * (i % 7 - 3);
        bars[i].high  = hi[i];
        bars[i].low   = lo[i];
        bars[i].volume = 1.0;
        bars[i].spread = 0.0;
    }
    af_pattern_match_t *matches = (af_pattern_match_t*)malloc(n * 5 * sizeof(af_pattern_match_t));
    TIMED("pscan",  af_scan_patterns(bars, n, matches, n * 5));
    free(matches); free(bars);

    free(src); free(hi); free(lo); free(vol); free(out); free(aux1); free(aux2);
    return 0;
}
