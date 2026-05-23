/**
 * AlgoForge — src/analytics/walkforward.cpp
 * Anchored and rolling walk-forward harnesses.
 */

#include "core/analytics.hpp"

#include <vector>

namespace algoforge::analytics {

std::vector<WalkForwardFold> walkforward_anchored(
    int        n_bars,
    int        n_folds,
    int        train_min_bars,
    BacktestFn backtest_fn
) {
    std::vector<WalkForwardFold> results;
    if (n_folds <= 0 || n_bars <= train_min_bars) return results;

    int remaining     = n_bars - train_min_bars;
    int fold_size     = remaining / n_folds;   /* integer division */
    if (fold_size <= 0) return results;

    for (int k = 0; k < n_folds; ++k) {
        int train_end  = train_min_bars + k * fold_size;
        int test_start = train_end;
        int test_end   = test_start + fold_size;
        if (test_end > n_bars) test_end = n_bars;
        if (test_start >= test_end) break;

        WalkForwardFold fold;
        fold.fold_index  = k;
        fold.train_start = 0;
        fold.train_end   = train_end;
        fold.test_start  = test_start;
        fold.test_end    = test_end;

        auto [tm, em] = backtest_fn(0, train_end, test_start, test_end);
        fold.train_metrics = tm;
        fold.test_metrics  = em;

        results.push_back(fold);
    }

    return results;
}

std::vector<WalkForwardFold> walkforward_rolling(
    int        n_bars,
    int        train_window,
    int        test_window,
    int        step,
    BacktestFn backtest_fn
) {
    std::vector<WalkForwardFold> results;
    if (train_window <= 0 || test_window <= 0 || step <= 0) return results;

    int fold_index = 0;
    for (int i = 0; i + train_window + test_window <= n_bars; i += step) {
        int train_start = i;
        int train_end   = i + train_window;
        int test_start  = train_end;
        int test_end    = test_start + test_window;
        if (test_end > n_bars) test_end = n_bars;
        if (test_start >= test_end) break;

        WalkForwardFold fold;
        fold.fold_index  = fold_index++;
        fold.train_start = train_start;
        fold.train_end   = train_end;
        fold.test_start  = test_start;
        fold.test_end    = test_end;

        auto [tm, em] = backtest_fn(train_start, train_end, test_start, test_end);
        fold.train_metrics = tm;
        fold.test_metrics  = em;

        results.push_back(fold);
    }

    return results;
}

} /* namespace algoforge::analytics */
