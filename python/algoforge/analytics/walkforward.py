"""
Walk-forward validation harnesses: anchored and rolling.

Both functions accept a user-supplied ``backtest_fn`` callable with signature::

    backtest_fn(train_bars, test_bars) -> {"train": {...}, "test": {...}}

and return a list of fold result dicts, each containing the indices used and
the result returned by ``backtest_fn``.

No intra-package imports: inputs and outputs use plain Python primitives /
lists of any objects (bars are opaque to this module).
"""
from __future__ import annotations

from typing import Any, Callable

__all__ = [
    "walkforward_anchored",
    "walkforward_rolling",
]

# Type alias: bars are opaque sequences
Bars = list[Any]
BacktestResult = dict[str, Any]
BacktestFn = Callable[[Bars, Bars], BacktestResult]


def walkforward_anchored(
    backtest_fn: BacktestFn,
    bars: Bars,
    n_folds: int,
    train_min_bars: int,
) -> list[dict[str, Any]]:
    """Anchored (expanding-window) walk-forward validation.

    The training window always starts at bar 0 and grows with each fold;
    the test window immediately follows.

    For fold k in 0..n_folds-1:
        test_size  = (n - train_min_bars) // n_folds
        train_end  = train_min_bars + k * test_size
        test_start = train_end
        test_end   = test_start + test_size

    Folds with zero-length train or test windows are skipped.

    Args:
        backtest_fn:    Callable ``(train_bars, test_bars) -> {train, test}``.
        bars:           Full list of bars in chronological order.
        n_folds:        Number of folds to generate.
        train_min_bars: Minimum number of bars in the first training window.

    Returns:
        List of fold dicts, each containing:
            fold           -- 0-based fold index
            train_start    -- inclusive start index of training window (always 0)
            train_end      -- exclusive end index of training window
            test_start     -- inclusive start index of test window
            test_end       -- exclusive end index of test window
            result         -- return value of backtest_fn
    """
    n = len(bars)
    results: list[dict[str, Any]] = []

    if n_folds <= 0 or train_min_bars >= n:
        return results

    remainder = n - train_min_bars
    test_size = remainder // n_folds
    if test_size <= 0:
        return results

    for k in range(n_folds):
        train_end = train_min_bars + k * test_size
        test_start = train_end
        test_end = test_start + test_size

        if train_end <= 0 or test_end > n:
            continue

        train_bars = bars[0:train_end]
        test_bars = bars[test_start:test_end]

        if not train_bars or not test_bars:
            continue

        fold_result = backtest_fn(train_bars, test_bars)
        results.append(
            {
                "fold": k,
                "train_start": 0,
                "train_end": train_end,
                "test_start": test_start,
                "test_end": test_end,
                "result": fold_result,
            }
        )

    return results


def walkforward_rolling(
    backtest_fn: BacktestFn,
    bars: Bars,
    train_window: int,
    test_window: int,
    step: int,
) -> list[dict[str, Any]]:
    """Rolling (sliding-window) walk-forward validation.

    The training window has a fixed size ``train_window`` and slides forward
    by ``step`` bars each fold; the test window of size ``test_window``
    immediately follows the training window.

    For position i = 0, step, 2*step, ...:
        train = bars[i : i + train_window]
        test  = bars[i + train_window : i + train_window + test_window]

    Stops when there are not enough bars to form a full test window.

    Args:
        backtest_fn:    Callable ``(train_bars, test_bars) -> {train, test}``.
        bars:           Full list of bars in chronological order.
        train_window:   Fixed size of each training window (number of bars).
        test_window:    Fixed size of each test window (number of bars).
        step:           Number of bars to advance between folds.

    Returns:
        List of fold dicts, each containing:
            fold           -- 0-based fold index
            train_start    -- inclusive start index of training window
            train_end      -- exclusive end index of training window
            test_start     -- inclusive start index of test window
            test_end       -- exclusive end index of test window
            result         -- return value of backtest_fn
    """
    n = len(bars)
    results: list[dict[str, Any]] = []

    if train_window <= 0 or test_window <= 0 or step <= 0:
        return results

    fold_index = 0
    i = 0
    while i + train_window + test_window <= n:
        train_start = i
        train_end = i + train_window
        test_start = train_end
        test_end = test_start + test_window

        train_bars = bars[train_start:train_end]
        test_bars = bars[test_start:test_end]

        fold_result = backtest_fn(train_bars, test_bars)
        results.append(
            {
                "fold": fold_index,
                "train_start": train_start,
                "train_end": train_end,
                "test_start": test_start,
                "test_end": test_end,
                "result": fold_result,
            }
        )

        fold_index += 1
        i += step

    return results
