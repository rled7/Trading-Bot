/**
 * AlgoForge — c/include/af_algorithm.h
 * C vtable interface mirroring the C++ IAlgorithm / AlgoDecision types.
 *
 * Each concrete algorithm is represented as an af_algorithm_t whose function
 * pointers implement the strategy.  A "stub" algorithm (always returns NONE)
 * is provided for testing the backtest harness in isolation.
 */
#ifndef AF_ALGORITHM_H
#define AF_ALGORITHM_H

#include <stddef.h>
#include <stdint.h>
#include "af_types.h"
#include "af_indicators.h"
#include "af_patterns.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Algorithm signal values ────────────────────────────────────────────── */
#define AF_ALGO_NONE  0
#define AF_ALGO_BUY   1
#define AF_ALGO_SELL (-1)

/* ── Decision produced by an algorithm ─────────────────────────────────── */
typedef struct {
    int    signal;       /* AF_ALGO_NONE / AF_ALGO_BUY / AF_ALGO_SELL */
    char   symbol[12];
    int    direction;    /* AF_DIR_NONE / AF_DIR_LONG / AF_DIR_SHORT */
    double confidence;   /* 0.0 – 1.0 */
    char   reason[64];
    double atr;          /* ATR at decision time (may be 0 if unavailable) */
} af_algo_decision_t;

/**
 * Returns 1 (true) if the decision has a non-NONE signal AND confidence ≥ 0.55.
 * Mirrors IAlgorithm::is_actionable().
 */
static inline int af_algo_decision_is_actionable(const af_algo_decision_t *d) {
    return (d->signal != AF_ALGO_NONE) && (d->confidence >= 0.55);
}

/* ── Algorithm vtable ───────────────────────────────────────────────────── */
typedef struct af_algorithm_s af_algorithm_t;

struct af_algorithm_s {
    /** Return the algorithm's human-readable name (static string). */
    const char *(*name)       (const af_algorithm_t *self);

    /** Return a one-line description (static string). */
    const char *(*description)(const af_algorithm_t *self);

    /** Return the MT4/5-compatible magic number. */
    uint32_t    (*magic)      (const af_algorithm_t *self);

    /**
     * Evaluate bars and return a trading decision.
     * bars[0] = oldest, bars[count-1] = newest.
     * ind  — pre-computed indicator engine result for this bar window.
     * pat  — pre-computed pattern engine result for this bar window.
     */
    af_algo_decision_t (*evaluate)(
        af_algorithm_t        *self,
        const char            *symbol,
        af_timeframe_t         tf,
        const af_bar_t        *bars,
        size_t                 count,
        const af_engine_result_t *ind,
        const AF_PatternResult   *pat
    );

    int   enabled; /**< Non-zero = algorithm participates in evaluation. */
    void *impl;    /**< Private implementation state (algorithm-defined). */

    /**
     * Opaque metadata pointer — populated by af_algo_promote() after a
     * manifest algorithm is promoted.  NULL for all other algorithms.
     * Caller owns the lifetime; cast to af_algo_metadata_t * when needed.
     */
    void *metadata;
};

/**
 * af_algo_safe_evaluate — wrapper that returns a NONE decision if the
 * algorithm is disabled (enabled == 0) or if evaluate() returns an
 * invalid decision.  Mirrors IAlgorithm::safe_evaluate().
 */
af_algo_decision_t af_algo_safe_evaluate(
    af_algorithm_t        *algo,
    const char            *symbol,
    af_timeframe_t         tf,
    const af_bar_t        *bars,
    size_t                 count,
    const af_engine_result_t *ind,
    const AF_PatternResult   *pat
);

/* ── Stub algorithm ─────────────────────────────────────────────────────── */

/**
 * af_make_stub_algo — allocates and returns an af_algorithm_t whose
 * evaluate() always returns AF_ALGO_NONE with confidence 0.  Useful for
 * unit-testing the backtest harness without a real strategy.
 *
 * The returned pointer must be freed with af_stub_algo_destroy().
 */
af_algorithm_t *af_make_stub_algo(void);

/** Free a stub algorithm created by af_make_stub_algo(). */
void af_stub_algo_destroy(af_algorithm_t *algo);

#ifdef __cplusplus
}
#endif

#endif /* AF_ALGORITHM_H */
