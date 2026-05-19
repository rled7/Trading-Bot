/**
 * AlgoForge — c/include/af_algorithms.h
 * Factory declarations for the four concrete trading algorithms plus the
 * algorithm registry.  Mirrors the C++ concrete algorithm and AlgoRegistry
 * classes.
 *
 * All algorithms are allocated on the heap; the caller must eventually invoke
 * the algo's destroy() function pointer (set by each factory) or call
 * af_algo_registry_destroy() for registry-owned algorithms.
 *
 * Compile with: -std=c11 -Wall -Wextra -lm
 */
#ifndef AF_ALGORITHMS_H
#define AF_ALGORITHMS_H

#include <stddef.h>
#include <stdint.h>
#include "af_algorithm.h"
#include "af_types.h"
#include "af_indicators.h"
#include "af_patterns.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Factory functions ──────────────────────────────────────────────────── */

/**
 * af_make_trend_follower — EMA crossover strategy with ADX filter.
 * @param fast     Fast EMA period (default 21).
 * @param slow     Slow EMA period (default 50).
 * @param adx_min  Minimum ADX required for a signal (default 22.0).
 * @param magic    MT4/5 magic number (default 1001).
 * Name: "TrendFollower_EMA{fast}/{slow}"
 */
af_algorithm_t *af_make_trend_follower(int fast, int slow,
                                        double adx_min, uint32_t magic);

/**
 * af_make_mean_reversion — RSI + Bollinger Band mean-reversion strategy.
 * Uses fixed params: rsi_os=30, rsi_ob=70, adx_max=25, magic=1002.
 * Name: "MeanReversion_RSI_BB"
 */
af_algorithm_t *af_make_mean_reversion(void);

/**
 * af_make_breakout_trader — Donchian Channel breakout strategy.
 * @param dc_period  Donchian channel period (default 20).
 * @param vol_mult   Volume surge multiplier (default 1.5).
 * Magic: 1003.  Name: "Breakout_Donchian"
 */
af_algorithm_t *af_make_breakout_trader(int dc_period, double vol_mult);

/**
 * af_make_swing_trader — Pullback-to-EMA21 swing strategy.
 * Only fires on AF_TF_H4 or AF_TF_D1.
 * Magic: 1004.  Name: "SwingTrader_PullbackEMA"
 */
af_algorithm_t *af_make_swing_trader(void);

/* ── Algorithm Registry ─────────────────────────────────────────────────── */

#define AF_ALGO_MAX_ALGOS 16

/**
 * af_algo_registry_t — fixed-size registry of algorithm pointers.
 * No heap allocation in the registry struct itself; algorithm impls are
 * heap-allocated by their factory functions.
 */
typedef struct {
    af_algorithm_t *algos[AF_ALGO_MAX_ALGOS];
    int             count;
} af_algo_registry_t;

/**
 * af_algo_registry_init — populate the registry with the standard suite:
 *   [0] TrendFollower(fast=21, slow=50, adx_min=22.0, magic=1001)
 *   [1] TrendFollower(fast=9,  slow=21, adx_min=20.0, magic=1005)
 *   [2] MeanReversion
 *   [3] BreakoutTrader
 *   [4] SwingTrader
 */
void af_algo_registry_init(af_algo_registry_t *r);

/**
 * af_algo_registry_destroy — call each algo's destroy() function pointer
 * (if non-NULL) and zero the registry.
 */
void af_algo_registry_destroy(af_algo_registry_t *r);

/**
 * af_algo_registry_get — linear search by name; returns NULL if not found.
 */
af_algorithm_t *af_algo_registry_get(const af_algo_registry_t *r,
                                      const char *name);

/**
 * af_algo_registry_evaluate_all — run every enabled algorithm via
 * af_algo_safe_evaluate(), collect decisions with signal != NONE and
 * confidence >= 0.55 into out[0..max_out-1].
 * Returns the number of actionable decisions written.
 */
int af_algo_registry_evaluate_all(const af_algo_registry_t   *r,
                                   const char                 *symbol,
                                   af_timeframe_t              tf,
                                   const af_bar_t             *bars,
                                   size_t                      count,
                                   const af_engine_result_t   *ind,
                                   const AF_PatternResult     *pat,
                                   af_algo_decision_t         *out,
                                   int                         max_out);

#ifdef __cplusplus
}
#endif

#endif /* AF_ALGORITHMS_H */
