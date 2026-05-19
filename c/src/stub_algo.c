/**
 * AlgoForge — c/src/stub_algo.c
 * Stub algorithm — always returns AF_ALGO_NONE (confidence = 0).
 *
 * Used to unit-test the backtest harness without a real strategy:
 * the engine should execute zero trades and return a clean result.
 *
 * Compile with: -std=c11 -Wall -Wextra
 */

#include "../include/af_algorithm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── vtable implementations ─────────────────────────────────────────── */

static const char *stub_name(const af_algorithm_t *self) {
    (void)self;
    return "StubAlgo";
}

static const char *stub_description(const af_algorithm_t *self) {
    (void)self;
    return "Stub algorithm — always returns NONE for backtest harness testing";
}

static uint32_t stub_magic(const af_algorithm_t *self) {
    (void)self;
    return 0xDEADBEEFU;
}

static af_algo_decision_t stub_evaluate(
    af_algorithm_t           *self,
    const char               *symbol,
    af_timeframe_t            tf,
    const af_bar_t           *bars,
    size_t                    count,
    const af_engine_result_t *ind,
    const AF_PatternResult   *pat)
{
    (void)self;
    (void)tf;
    (void)bars;
    (void)count;
    (void)ind;
    (void)pat;

    af_algo_decision_t d;
    memset(&d, 0, sizeof(d));
    d.signal    = AF_ALGO_NONE;
    d.direction = AF_DIR_NONE;
    d.confidence = 0.0;
    if (symbol)
        snprintf(d.symbol, sizeof(d.symbol), "%s", symbol);
    snprintf(d.reason, sizeof(d.reason), "stub: always NONE");
    return d;
}

/* ── af_algo_safe_evaluate ──────────────────────────────────────────── */
/*
 * Placed here (rather than in backtest_engine.c) so that stub_algo.c can
 * be compiled without linking the full backtest engine.  This is the only
 * file that needs to provide this symbol.
 */
af_algo_decision_t af_algo_safe_evaluate(
    af_algorithm_t           *algo,
    const char               *symbol,
    af_timeframe_t            tf,
    const af_bar_t           *bars,
    size_t                    count,
    const af_engine_result_t *ind,
    const AF_PatternResult   *pat)
{
    af_algo_decision_t none;
    memset(&none, 0, sizeof(none));
    none.signal    = AF_ALGO_NONE;
    none.direction = AF_DIR_NONE;
    if (symbol)
        snprintf(none.symbol, sizeof(none.symbol), "%s", symbol);

    if (!algo || !algo->enabled || !algo->evaluate)
        return none;

    return algo->evaluate(algo, symbol, tf, bars, count, ind, pat);
}

/* ── Factory / destructor ───────────────────────────────────────────── */

af_algorithm_t *af_make_stub_algo(void) {
    af_algorithm_t *a = (af_algorithm_t *)malloc(sizeof(af_algorithm_t));
    if (!a) return NULL;

    a->name        = stub_name;
    a->description = stub_description;
    a->magic       = stub_magic;
    a->evaluate    = stub_evaluate;
    a->enabled     = 1;
    a->impl        = NULL;

    return a;
}

void af_stub_algo_destroy(af_algorithm_t *algo) {
    free(algo);
}
