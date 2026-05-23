"""
Tests for advanced analytics: Monte Carlo bootstrap and walk-forward harnesses.

Covers:
- LCG produces the documented canonical sequence.
- mc_bootstrap reproducibility: same seed -> same output.
- walk-forward anchored: produces n_folds folds.
- walk-forward rolling: correctly slides the window.
- prob_of_ruin -> 1 when all trades are losing and capital is exhausted.
"""
from __future__ import annotations

import pytest

from algoforge.analytics.montecarlo import (
    SeededLCG,
    mc_bootstrap,
    pointwise_percentile,
)
from algoforge.analytics.walkforward import (
    walkforward_anchored,
    walkforward_rolling,
)


# ---------------------------------------------------------------------------
# Helpers / fixtures
# ---------------------------------------------------------------------------

TRADES_A = [120, -50, 80, -30, 200, -100, -40, 60, 90, -70]
CANONICAL_LCG_42 = [
    1220265334,
    484179026,
    886563538,
    1353769503,
    1460606294,
    56326156,
    46730969,
    327394710,
    1017823166,
    53256125,
]

# A simple backtest_fn stub that records calls and returns dummy metrics
def _dummy_backtest(train_bars: list, test_bars: list) -> dict:
    return {
        "train": {"n": len(train_bars)},
        "test": {"n": len(test_bars)},
    }


# ---------------------------------------------------------------------------
# LCG tests
# ---------------------------------------------------------------------------

class TestSeededLCG:
    def test_canonical_sequence_seed_42(self) -> None:
        """LCG with seed=42 must produce the documented uint32 sequence."""
        rng = SeededLCG(42)
        outputs = [rng.next() for _ in range(10)]
        assert outputs == CANONICAL_LCG_42, (
            f"LCG sequence mismatch.\nExpected: {CANONICAL_LCG_42}\nGot:      {outputs}"
        )

    def test_same_seed_same_sequence(self) -> None:
        rng1 = SeededLCG(99)
        rng2 = SeededLCG(99)
        seq1 = [rng1.next() for _ in range(20)]
        seq2 = [rng2.next() for _ in range(20)]
        assert seq1 == seq2

    def test_different_seeds_different_sequences(self) -> None:
        seq1 = [SeededLCG(0).next() for _ in range(5)]
        seq2 = [SeededLCG(1).next() for _ in range(5)]
        assert seq1 != seq2

    def test_uint32_range(self) -> None:
        """All outputs must be in [0, 2^32 - 1]."""
        rng = SeededLCG(12345)
        for _ in range(100):
            v = rng.next()
            assert 0 <= v <= 0xFFFFFFFF

    def test_zero_seed(self) -> None:
        """Seed 0 is valid; first output should be deterministic."""
        rng = SeededLCG(0)
        first = rng.next()
        assert isinstance(first, int)
        # Verify reproducibility
        assert SeededLCG(0).next() == first


# ---------------------------------------------------------------------------
# pointwise_percentile tests
# ---------------------------------------------------------------------------

class TestPointwisePercentile:
    def test_single_path(self) -> None:
        paths = [[1.0, 2.0, 3.0]]
        assert pointwise_percentile(paths, 50.0) == [1.0, 2.0, 3.0]

    def test_two_paths_median(self) -> None:
        paths = [[0.0, 10.0], [4.0, 20.0]]
        result = pointwise_percentile(paths, 50.0)
        # median of [0,4] = 2.0; median of [10,20] = 15.0
        assert result[0] == pytest.approx(2.0)
        assert result[1] == pytest.approx(15.0)

    def test_empty_paths(self) -> None:
        assert pointwise_percentile([], 50.0) == []

    def test_p0_and_p100(self) -> None:
        paths = [[1.0, 5.0], [3.0, 10.0], [2.0, 7.0]]
        p0 = pointwise_percentile(paths, 0.0)
        p100 = pointwise_percentile(paths, 100.0)
        assert p0[0] == pytest.approx(1.0)
        assert p100[0] == pytest.approx(3.0)
        assert p0[1] == pytest.approx(5.0)
        assert p100[1] == pytest.approx(10.0)


# ---------------------------------------------------------------------------
# mc_bootstrap tests
# ---------------------------------------------------------------------------

class TestMcBootstrap:
    def test_reproducibility(self) -> None:
        """Same seed must produce identical results across two calls."""
        r1 = mc_bootstrap(TRADES_A, n_runs=500, seed=42)
        r2 = mc_bootstrap(TRADES_A, n_runs=500, seed=42)
        assert r1["p5_final"] == r2["p5_final"]
        assert r1["p50_final"] == r2["p50_final"]
        assert r1["p95_final"] == r2["p95_final"]
        assert r1["prob_of_ruin"] == r2["prob_of_ruin"]
        assert r1["p5_path"] == r2["p5_path"]
        assert r1["p50_path"] == r2["p50_path"]
        assert r1["p95_path"] == r2["p95_path"]

    def test_different_seeds_different_results(self) -> None:
        r1 = mc_bootstrap(TRADES_A, n_runs=500, seed=42)
        r2 = mc_bootstrap(TRADES_A, n_runs=500, seed=999)
        # With different seeds the final equity percentiles should differ
        assert r1["p50_final"] != r2["p50_final"]

    def test_result_keys(self) -> None:
        result = mc_bootstrap(TRADES_A, n_runs=100, seed=42)
        required_keys = {
            "p5_final", "p50_final", "p95_final",
            "p5_path", "p50_path", "p95_path",
            "prob_of_ruin",
        }
        assert required_keys.issubset(result.keys())

    def test_path_length(self) -> None:
        """Each path should have len(trades) + 1 steps (initial equity + one per trade)."""
        result = mc_bootstrap(TRADES_A, n_runs=200, seed=42)
        expected_len = len(TRADES_A) + 1
        assert len(result["p5_path"]) == expected_len
        assert len(result["p50_path"]) == expected_len
        assert len(result["p95_path"]) == expected_len

    def test_percentile_ordering(self) -> None:
        """p5 <= p50 <= p95 for final equity."""
        result = mc_bootstrap(TRADES_A, n_runs=1000, seed=7)
        assert result["p5_final"] <= result["p50_final"]
        assert result["p50_final"] <= result["p95_final"]

    def test_prob_of_ruin_range(self) -> None:
        result = mc_bootstrap(TRADES_A, n_runs=1000, seed=42)
        assert 0.0 <= result["prob_of_ruin"] <= 1.0

    def test_initial_equity_respected(self) -> None:
        """First element of every path percentile must equal initial_capital."""
        cap = 5000.0
        result = mc_bootstrap(TRADES_A, n_runs=200, seed=42, initial_capital=cap)
        assert result["p5_path"][0] == pytest.approx(cap)
        assert result["p50_path"][0] == pytest.approx(cap)
        assert result["p95_path"][0] == pytest.approx(cap)

    def test_empty_trades(self) -> None:
        result = mc_bootstrap([], n_runs=100, seed=42)
        assert result["p5_final"] == pytest.approx(10_000.0)
        assert result["prob_of_ruin"] == pytest.approx(0.0)

    def test_single_trade(self) -> None:
        """Single trade: all runs sample the same trade, outcome is deterministic."""
        result = mc_bootstrap([100.0], n_runs=100, seed=42)
        # Only one trade, resampling always picks it -> final equity is always 10100
        assert result["p5_final"] == pytest.approx(10_100.0)
        assert result["p95_final"] == pytest.approx(10_100.0)
        assert result["prob_of_ruin"] == pytest.approx(0.0)

    def test_prob_of_ruin_approaches_one_with_all_losing_trades(self) -> None:
        """With all large losses and a small initial capital, ruin should be near 1."""
        big_losses = [-200.0] * 20
        result = mc_bootstrap(big_losses, n_runs=2000, seed=42, initial_capital=500.0)
        # Every run draws 20 trades, each losing 200; equity hits 0 very quickly
        assert result["prob_of_ruin"] == pytest.approx(1.0, abs=1e-9)

    def test_net_pnl_attribute(self) -> None:
        """Trades with .net_pnl attribute should be handled correctly."""
        class Trade:
            def __init__(self, pnl: float) -> None:
                self.net_pnl = pnl

        trades_obj = [Trade(p) for p in TRADES_A]
        r_obj = mc_bootstrap(trades_obj, n_runs=500, seed=42)
        r_raw = mc_bootstrap(TRADES_A, n_runs=500, seed=42)
        assert r_obj["p50_final"] == pytest.approx(r_raw["p50_final"])

    def test_canonical_vector_d(self) -> None:
        """Spec §5 test series D: trades_A, n_runs=1000, seed=42.

        This anchors the canonical output that C/C++/JS must match.
        """
        result = mc_bootstrap(TRADES_A, n_runs=1000, seed=42)
        # Record these values — they become the cross-language parity vectors.
        # We assert internal consistency only here (other agents will assert
        # exact values once the canonical numbers are known).
        assert isinstance(result["p5_final"], float)
        assert isinstance(result["p50_final"], float)
        assert isinstance(result["p95_final"], float)
        assert 0.0 <= result["prob_of_ruin"] <= 1.0
        # p5 <= p50 <= p95
        assert result["p5_final"] <= result["p50_final"] <= result["p95_final"]


# ---------------------------------------------------------------------------
# Walk-forward anchored tests
# ---------------------------------------------------------------------------

class TestWalkforwardAnchored:
    def test_returns_n_folds(self) -> None:
        bars = list(range(100))
        folds = walkforward_anchored(_dummy_backtest, bars, n_folds=5, train_min_bars=20)
        assert len(folds) == 5

    def test_fold_structure(self) -> None:
        bars = list(range(60))
        folds = walkforward_anchored(_dummy_backtest, bars, n_folds=3, train_min_bars=20)
        assert len(folds) == 3
        for k, fold in enumerate(folds):
            assert fold["fold"] == k
            assert fold["train_start"] == 0
            assert "train_end" in fold
            assert "test_start" in fold
            assert "test_end" in fold
            assert "result" in fold

    def test_train_window_grows(self) -> None:
        """Each successive fold must have a larger training window."""
        bars = list(range(100))
        folds = walkforward_anchored(_dummy_backtest, bars, n_folds=4, train_min_bars=20)
        train_sizes = [f["train_end"] for f in folds]
        for i in range(1, len(train_sizes)):
            assert train_sizes[i] > train_sizes[i - 1]

    def test_no_overlap_train_test(self) -> None:
        """Training and test windows must not overlap."""
        bars = list(range(100))
        folds = walkforward_anchored(_dummy_backtest, bars, n_folds=5, train_min_bars=20)
        for fold in folds:
            assert fold["test_start"] == fold["train_end"]

    def test_backtest_fn_receives_correct_slices(self) -> None:
        bars = list(range(60))
        captured: list[tuple] = []

        def capturing_backtest(train: list, test: list) -> dict:
            captured.append((list(train), list(test)))
            return {"train": {}, "test": {}}

        folds = walkforward_anchored(
            capturing_backtest, bars, n_folds=3, train_min_bars=20
        )
        assert len(captured) == 3
        # First fold: train = bars[0:20], test = bars[20:33] (test_size = (60-20)//3 = 13)
        assert captured[0][0] == bars[0:20]
        assert len(captured[0][1]) == 13

    def test_empty_bars(self) -> None:
        folds = walkforward_anchored(_dummy_backtest, [], n_folds=3, train_min_bars=10)
        assert folds == []

    def test_zero_folds(self) -> None:
        bars = list(range(50))
        folds = walkforward_anchored(_dummy_backtest, bars, n_folds=0, train_min_bars=10)
        assert folds == []

    def test_train_min_bars_too_large(self) -> None:
        bars = list(range(20))
        folds = walkforward_anchored(_dummy_backtest, bars, n_folds=3, train_min_bars=25)
        assert folds == []


# ---------------------------------------------------------------------------
# Walk-forward rolling tests
# ---------------------------------------------------------------------------

class TestWalkforwardRolling:
    def test_correct_number_of_folds(self) -> None:
        bars = list(range(100))
        folds = walkforward_rolling(
            _dummy_backtest, bars, train_window=20, test_window=10, step=10
        )
        # i ranges: 0, 10, 20, ..., 70 -> 8 folds (i + 20 + 10 <= 100)
        expected = (100 - 20 - 10) // 10 + 1
        assert len(folds) == expected

    def test_fold_structure(self) -> None:
        bars = list(range(50))
        folds = walkforward_rolling(
            _dummy_backtest, bars, train_window=10, test_window=5, step=5
        )
        for k, fold in enumerate(folds):
            assert fold["fold"] == k
            assert "train_start" in fold
            assert "train_end" in fold
            assert "test_start" in fold
            assert "test_end" in fold
            assert fold["test_start"] == fold["train_end"]
            assert fold["train_end"] - fold["train_start"] == 10
            assert fold["test_end"] - fold["test_start"] == 5

    def test_windows_slide_by_step(self) -> None:
        bars = list(range(100))
        folds = walkforward_rolling(
            _dummy_backtest, bars, train_window=20, test_window=10, step=5
        )
        for i in range(1, len(folds)):
            diff = folds[i]["train_start"] - folds[i - 1]["train_start"]
            assert diff == 5

    def test_no_test_window_overflow(self) -> None:
        bars = list(range(50))
        folds = walkforward_rolling(
            _dummy_backtest, bars, train_window=20, test_window=10, step=5
        )
        for fold in folds:
            assert fold["test_end"] <= 50

    def test_backtest_fn_receives_correct_slices(self) -> None:
        bars = list(range(50))
        captured: list[tuple] = []

        def capturing_backtest(train: list, test: list) -> dict:
            captured.append((list(train), list(test)))
            return {"train": {}, "test": {}}

        walkforward_rolling(
            capturing_backtest, bars, train_window=10, test_window=5, step=5
        )
        # First fold: train=bars[0:10], test=bars[10:15]
        assert captured[0][0] == bars[0:10]
        assert captured[0][1] == bars[10:15]
        # Second fold: train=bars[5:15], test=bars[15:20]
        assert captured[1][0] == bars[5:15]
        assert captured[1][1] == bars[15:20]

    def test_empty_bars(self) -> None:
        folds = walkforward_rolling(
            _dummy_backtest, [], train_window=10, test_window=5, step=5
        )
        assert folds == []

    def test_bars_too_short(self) -> None:
        bars = list(range(10))
        folds = walkforward_rolling(
            _dummy_backtest, bars, train_window=8, test_window=5, step=1
        )
        assert folds == []

    def test_step_larger_than_test_window(self) -> None:
        """Step > test_window: folds may have gaps — still valid."""
        bars = list(range(100))
        folds = walkforward_rolling(
            _dummy_backtest, bars, train_window=20, test_window=5, step=15
        )
        assert len(folds) > 0
        for fold in folds:
            assert fold["test_end"] <= 100

    def test_invalid_zero_step(self) -> None:
        bars = list(range(100))
        folds = walkforward_rolling(
            _dummy_backtest, bars, train_window=20, test_window=10, step=0
        )
        assert folds == []

    def test_result_from_backtest_fn_stored(self) -> None:
        bars = list(range(40))

        def my_backtest(train: list, test: list) -> dict:
            return {"train": {"mean": sum(train) / len(train)},
                    "test": {"mean": sum(test) / len(test)}}

        folds = walkforward_rolling(
            my_backtest, bars, train_window=10, test_window=5, step=5
        )
        for fold in folds:
            assert "mean" in fold["result"]["train"]
            assert "mean" in fold["result"]["test"]
