"""Tests for algoforge.analytics core modules.

Covers canonical test vectors from spec §5 (series A, B, C) plus edge cases.
Tolerance: 1e-9 absolute for float comparisons.
"""

from __future__ import annotations

import math

import pytest

from algoforge.analytics.correlations import (
    correlation_matrix,
    pearson,
    rolling_beta,
)
from algoforge.analytics.distributions import (
    distribution_stats,
    excess_kurtosis,
    quantile,
    skewness,
)
from algoforge.analytics.drawdown import (
    drawdown_episodes,
    drawdown_series,
    max_drawdown_abs,
    max_drawdown_pct,
    top5_drawdowns,
)
from algoforge.analytics.metrics import (
    avg_loss,
    avg_win,
    calmar,
    compute_metrics,
    expectancy,
    longest_loss_streak,
    longest_win_streak,
    max_consec_losses,
    omega,
    payoff_ratio,
    profit_factor,
    recovery_factor,
    sharpe,
    sortino,
    time_in_market,
    win_rate,
)
from algoforge.analytics.types import (
    CorrelationMatrix,
    DistributionStats,
    DrawdownEpisode,
    MetricsResult,
    TradeReturn,
)

TOL = 1e-9  # absolute tolerance for float assertions


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _trades(pnls: list[float], bars: int = 1) -> list[TradeReturn]:
    return [TradeReturn(net_pnl=p, bars_held=bars) for p in pnls]


def _equity_from_returns(returns: list[float], initial: float = 10_000.0) -> list[float]:
    """Build a compounded equity curve from per-bar fractional returns."""
    eq = [initial]
    for r in returns:
        eq.append(eq[-1] * (1.0 + r))
    return eq


# ---------------------------------------------------------------------------
# Spec §5 — Test series A (trade-based metrics)
# ---------------------------------------------------------------------------

TRADES_A_PNLS = [+120.0, -50.0, +80.0, -30.0, +200.0, -100.0, -40.0, +60.0, +90.0, -70.0]
TRADES_A = _trades(TRADES_A_PNLS)


class TestSeriesA:
    def test_sum_pnl(self):
        assert abs(sum(t.net_pnl for t in TRADES_A) - 260.0) < TOL

    def test_n_wins_and_losses(self):
        wins = [t for t in TRADES_A if t.net_pnl > 0]
        losses = [t for t in TRADES_A if t.net_pnl < 0]
        assert len(wins) == 5
        assert len(losses) == 5

    def test_win_rate(self):
        assert abs(win_rate(TRADES_A) - 0.5) < TOL

    def test_expectancy(self):
        assert abs(expectancy(TRADES_A) - 26.0) < TOL

    def test_avg_win(self):
        assert abs(avg_win(TRADES_A) - 110.0) < TOL

    def test_avg_loss(self):
        # avg_loss returned as negative value
        assert abs(avg_loss(TRADES_A) - (-58.0)) < TOL

    def test_payoff_ratio(self):
        # 110.0 / 58.0 = 1.8965517241379310...
        expected = 110.0 / 58.0
        assert abs(payoff_ratio(TRADES_A) - expected) < TOL

    def test_profit_factor(self):
        # 550.0 / 290.0 = 1.8965517241379310...
        expected = 550.0 / 290.0
        assert abs(profit_factor(TRADES_A) - expected) < TOL

    def test_payoff_ratio_equals_profit_factor(self):
        # For this series they happen to be equal
        assert abs(payoff_ratio(TRADES_A) - profit_factor(TRADES_A)) < TOL

    def test_longest_win_streak(self):
        # trades[7]=+60, trades[8]=+90 are consecutive -> streak=2
        # Spec says 1 but the data shows two consecutive wins at indices 7,8.
        # Our correct implementation gives 2 per the actual data.
        assert longest_win_streak(TRADES_A) == 2

    def test_longest_loss_streak(self):
        # trades[5]=-100, trades[6]=-40 are consecutive -> streak=2
        assert longest_loss_streak(TRADES_A) == 2

    def test_max_consec_losses_alias(self):
        assert max_consec_losses(TRADES_A) == longest_loss_streak(TRADES_A)


# ---------------------------------------------------------------------------
# Spec §5 — Test series B (bar returns / equity-curve metrics)
# ---------------------------------------------------------------------------

RETURNS_B = [0.01, -0.005, 0.02, -0.01, 0.015, 0.003, -0.008, 0.012]
EQ_B = _equity_from_returns(RETURNS_B)


class TestSeriesB:
    def test_mean_bar_returns(self):
        # spec: mean = 0.004625
        # Due to compounding, bar returns from EQ_B are negligibly close to RETURNS_B.
        bar_rets = [(EQ_B[i] - EQ_B[i - 1]) / EQ_B[i - 1] for i in range(1, len(EQ_B))]
        mean = sum(bar_rets) / len(bar_rets)
        assert abs(mean - 0.004625) < TOL

    def test_sharpe_positive(self):
        sh = sharpe(EQ_B)
        # Implementations must agree; our value is ~6.4899
        assert sh > 0.0

    def test_sharpe_precise(self):
        # Verify to 1e-9 against our reference computation
        sh = sharpe(EQ_B)
        # Reference: sqrt(252) * mean / sample_std = 6.48988974475...
        assert abs(sh - 6.489889744758411) < TOL

    def test_sortino_positive(self):
        so = sortino(EQ_B)
        assert so > 0.0

    def test_sortino_greater_than_sharpe(self):
        # Sortino >= Sharpe when there are fewer negative returns
        assert sortino(EQ_B) >= sharpe(EQ_B)

    def test_sortino_precise(self):
        # Downside std demeaned at 0 using full n-1 denominator: ~14.1296
        so = sortino(EQ_B)
        assert abs(so - 14.129608392780348) < TOL

    def test_sharpe_zero_variance(self):
        # Flat equity -> std = 0 -> Sharpe = 0
        flat = [10000.0] * 5
        assert sharpe(flat) == 0.0

    def test_sharpe_single_point(self):
        assert sharpe([10000.0]) == 0.0

    def test_sortino_no_negative_returns(self):
        # All positive returns -> downside std = 0 -> returns 0.0
        eq_up = _equity_from_returns([0.01, 0.02, 0.005])
        assert sortino(eq_up) == 0.0


# ---------------------------------------------------------------------------
# Spec §5 — Test series C (equity curve / drawdown)
# ---------------------------------------------------------------------------

EQ_C = [10000.0, 10100.0, 10050.0, 10200.0, 10150.0, 9800.0, 9900.0, 10300.0, 10400.0]
PEAKS_C = [10000.0, 10100.0, 10100.0, 10200.0, 10200.0, 10200.0, 10200.0, 10300.0, 10400.0]


class TestSeriesC:
    def test_peaks(self):
        dd_pct, _ = drawdown_series(EQ_C)
        # Reconstruct peaks from eq and dd_pct
        # peak[i] = eq[i] / (1 + dd_pct[i]/100) when dd_pct < 0
        for i, (e, p) in enumerate(zip(EQ_C, PEAKS_C)):
            if dd_pct[i] < 0.0:
                reconstructed_peak = e / (1.0 + dd_pct[i] / 100.0)
                assert abs(reconstructed_peak - p) < 1e-6, f"peak mismatch at i={i}"

    def test_max_dd_pct(self):
        # (9800 - 10200) / 10200 * 100 = -3.9215686274509807
        expected = (9800.0 - 10200.0) / 10200.0 * 100.0
        assert abs(max_drawdown_pct(EQ_C) - expected) < TOL

    def test_max_dd_pct_value(self):
        assert abs(max_drawdown_pct(EQ_C) - (-3.9215686274509802)) < TOL

    def test_max_dd_abs(self):
        assert abs(max_drawdown_abs(EQ_C) - (-400.0)) < TOL

    def test_drawdown_episodes_count(self):
        # Two distinct episodes: one at i=2 (brief) and one at i=4-6
        eps = drawdown_episodes(EQ_C)
        assert len(eps) == 2

    def test_episode_2_matches_spec(self):
        # Spec: trough=5, recover=7 (the main episode)
        eps = drawdown_episodes(EQ_C)
        # Sorted by start_idx; episode 1 (index 1) is the major one
        major = eps[1]
        assert major.start_idx == 4
        assert major.trough_idx == 5
        assert major.recover_idx == 7
        assert abs(major.depth_pct - (-3.9215686274509802)) < TOL
        assert abs(major.depth_abs - (-400.0)) < TOL
        assert major.duration_bars == 3
        assert major.recovery_bars == 2

    def test_episode_1_small_dip(self):
        eps = drawdown_episodes(EQ_C)
        small = eps[0]
        assert small.start_idx == 2
        assert small.trough_idx == 2
        assert small.recover_idx == 3
        assert abs(small.depth_pct - (-0.49504950495049505)) < TOL
        assert abs(small.depth_abs - (-50.0)) < TOL

    def test_top5_deepest_first(self):
        top5 = top5_drawdowns(EQ_C)
        assert len(top5) == 2
        # Deepest first
        assert top5[0].depth_pct <= top5[1].depth_pct
        assert abs(top5[0].depth_pct - (-3.9215686274509802)) < TOL

    def test_max_dd_monotone_equity(self):
        # Strictly increasing -> no drawdown
        eq_up = [100.0, 101.0, 102.0, 103.0]
        assert max_drawdown_pct(eq_up) == 0.0
        assert max_drawdown_abs(eq_up) == 0.0
        assert len(drawdown_episodes(eq_up)) == 0

    def test_unrecovered_episode(self):
        # Series ends in drawdown -> recover_idx = None
        eq = [100.0, 110.0, 90.0]
        eps = drawdown_episodes(eq)
        assert len(eps) == 1
        assert eps[0].recover_idx is None
        assert eps[0].recovery_bars is None

    def test_drawdown_series_empty(self):
        dd_pct, dd_abs = drawdown_series([])
        assert dd_pct == []
        assert dd_abs == []

    def test_max_dd_single_point(self):
        assert max_drawdown_pct([10000.0]) == 0.0
        assert max_drawdown_abs([10000.0]) == 0.0


# ---------------------------------------------------------------------------
# Additional metric tests
# ---------------------------------------------------------------------------

class TestProfitFactor:
    def test_no_losses(self):
        t = _trades([100.0, 50.0])
        assert profit_factor(t) == math.inf

    def test_no_wins(self):
        t = _trades([-100.0, -50.0])
        assert profit_factor(t) == 0.0

    def test_empty(self):
        assert profit_factor([]) == 0.0

    def test_mixed(self):
        t = _trades([100.0, -50.0])
        assert abs(profit_factor(t) - 2.0) < TOL


class TestWinRate:
    def test_all_wins(self):
        assert abs(win_rate(_trades([1.0, 2.0, 3.0])) - 1.0) < TOL

    def test_all_losses(self):
        assert abs(win_rate(_trades([-1.0, -2.0])) - 0.0) < TOL

    def test_empty(self):
        assert win_rate([]) == 0.0

    def test_zero_pnl_is_not_a_win(self):
        t = _trades([0.0, 1.0])
        assert abs(win_rate(t) - 0.5) < TOL


class TestLossStreak:
    def test_all_losses(self):
        t = _trades([-1.0, -2.0, -3.0])
        assert longest_loss_streak(t) == 3

    def test_all_wins(self):
        t = _trades([1.0, 2.0, 3.0])
        assert longest_loss_streak(t) == 0

    def test_zero_pnl_counts_as_loss(self):
        t = _trades([1.0, 0.0, 0.0, 1.0])
        assert longest_loss_streak(t) == 2

    def test_empty(self):
        assert longest_loss_streak([]) == 0


class TestWinStreak:
    def test_all_wins(self):
        t = _trades([1.0, 2.0, 3.0])
        assert longest_win_streak(t) == 3

    def test_empty(self):
        assert longest_win_streak([]) == 0


class TestRecoveryFactor:
    def test_basic(self):
        # Cumulative equity from trades_A: max_dd_abs = -140 (at index 7),
        # total_pnl = 260 -> recovery_factor = 260/140
        trades = TRADES_A
        eq = [10000.0 + sum(t.net_pnl for t in trades[:i]) for i in range(len(trades) + 1)]
        rf = recovery_factor(trades, eq)
        expected = 260.0 / 140.0
        assert abs(rf - expected) < TOL

    def test_no_drawdown(self):
        t = _trades([100.0, 200.0])
        eq = [10000.0, 10100.0, 10300.0]
        assert recovery_factor(t, eq) == 0.0  # no drawdown -> 0

    def test_empty(self):
        assert recovery_factor([], []) == 0.0


class TestTimeInMarket:
    def test_basic(self):
        trades = [TradeReturn(net_pnl=100.0, bars_held=5),
                  TradeReturn(net_pnl=-50.0, bars_held=3)]
        assert abs(time_in_market(trades, 20) - 8.0 / 20.0) < TOL

    def test_zero_total_bars(self):
        assert time_in_market(_trades([1.0]), 0) == 0.0

    def test_empty_trades(self):
        assert time_in_market([], 100) == 0.0


class TestCalmar:
    def test_series_c(self):
        # total_return = (10400 - 10000) / 10000 * 100 = 4%
        # max_dd_pct = -3.9215686...
        c = calmar(EQ_C)
        expected = (4.0) / abs(-3.9215686274509802)
        assert abs(c - expected) < TOL

    def test_no_drawdown(self):
        eq_up = [10000.0, 10100.0, 10200.0]
        assert calmar(eq_up) == 0.0

    def test_empty(self):
        assert calmar([]) == 0.0


class TestOmega:
    def test_all_positive_returns(self):
        # All gains -> losses == 0 -> return 0.0
        eq = [100.0, 101.0, 102.0]
        assert omega(eq) == 0.0

    def test_mixed(self):
        eq = _equity_from_returns([0.01, -0.005, 0.02])
        o = omega(eq)
        assert o > 0.0

    def test_empty(self):
        assert omega([]) == 0.0


# ---------------------------------------------------------------------------
# Distributions
# ---------------------------------------------------------------------------

class TestSkewness:
    def test_symmetric(self):
        xs = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0]
        assert abs(skewness(xs) - 0.0) < TOL

    def test_right_skewed(self):
        xs = [1.0, 1.0, 1.0, 1.0, 10.0]
        sk = skewness(xs)
        assert sk > 0.0  # right skew -> positive

    def test_known_value(self):
        xs = [1.0, 1.0, 1.0, 1.0, 10.0]
        expected = 2.2360679774997898  # computed reference
        assert abs(skewness(xs) - expected) < TOL

    def test_n_less_than_4(self):
        assert skewness([1.0, 2.0, 3.0]) == 0.0
        assert skewness([]) == 0.0

    def test_constant_series(self):
        assert skewness([5.0, 5.0, 5.0, 5.0, 5.0]) == 0.0


class TestExcessKurtosis:
    def test_uniform_1_to_10(self):
        xs = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0]
        ek = excess_kurtosis(xs)
        assert abs(ek - (-1.2)) < TOL

    def test_n_less_than_4(self):
        assert excess_kurtosis([1.0, 2.0, 3.0]) == 0.0
        assert excess_kurtosis([]) == 0.0


class TestQuantile:
    def test_median_of_1_to_10(self):
        xs = list(range(1, 11))
        assert abs(quantile(xs, 0.5) - 5.5) < TOL

    def test_p25_of_1_to_10(self):
        xs = list(range(1, 11))
        assert abs(quantile(xs, 0.25) - 3.25) < TOL

    def test_p75_of_1_to_10(self):
        xs = list(range(1, 11))
        assert abs(quantile(xs, 0.75) - 7.75) < TOL

    def test_p0_and_p1(self):
        xs = [1.0, 2.0, 3.0, 4.0, 5.0]
        assert abs(quantile(xs, 0.0) - 1.0) < TOL
        assert abs(quantile(xs, 1.0) - 5.0) < TOL

    def test_single_element(self):
        assert quantile([42.0], 0.5) == 42.0

    def test_empty(self):
        assert quantile([], 0.5) == 0.0

    def test_unsorted_input(self):
        xs = [5.0, 1.0, 3.0, 2.0, 4.0]
        assert abs(quantile(xs, 0.5) - 3.0) < TOL


class TestDistributionStats:
    def test_empty(self):
        ds = distribution_stats([])
        assert ds.n == 0

    def test_basic_fields(self):
        xs = [1.0, 2.0, 3.0, 4.0, 5.0]
        ds = distribution_stats(xs)
        assert ds.n == 5
        assert abs(ds.mean - 3.0) < TOL
        assert abs(ds.min - 1.0) < TOL
        assert abs(ds.max - 5.0) < TOL

    def test_std_is_sample(self):
        # Sample std of [1,2,3,4,5] = sqrt(10/4) = sqrt(2.5)
        xs = [1.0, 2.0, 3.0, 4.0, 5.0]
        ds = distribution_stats(xs)
        expected_std = math.sqrt(2.5)
        assert abs(ds.std - expected_std) < TOL


# ---------------------------------------------------------------------------
# Correlations
# ---------------------------------------------------------------------------

class TestPearson:
    def test_perfect_positive(self):
        x = [1.0, 2.0, 3.0, 4.0, 5.0]
        y = [2.0, 4.0, 6.0, 8.0, 10.0]
        assert abs(pearson(x, y) - 1.0) < TOL

    def test_perfect_negative(self):
        x = [1.0, 2.0, 3.0, 4.0, 5.0]
        y = [-1.0, -2.0, -3.0, -4.0, -5.0]
        assert abs(pearson(x, y) - (-1.0)) < TOL

    def test_zero_correlation(self):
        x = [1.0, 2.0, 3.0, 4.0, 5.0]
        y = [3.0, 3.0, 3.0, 3.0, 3.0]  # constant -> zero variance
        assert pearson(x, y) == 0.0

    def test_empty(self):
        assert pearson([], []) == 0.0

    def test_mismatched_lengths(self):
        assert pearson([1.0, 2.0], [1.0]) == 0.0

    def test_bounds(self):
        x = [1.0, 2.0, 3.0, 4.0, 5.0]
        y = [2.1, 3.9, 6.2, 7.8, 10.1]
        r = pearson(x, y)
        assert -1.0 <= r <= 1.0


class TestCorrelationMatrix:
    def test_single_symbol(self):
        cm = correlation_matrix({"A": [1.0, 2.0, 3.0]})
        assert cm.symbols == ["A"]
        assert abs(cm.matrix[0][0] - 1.0) < TOL

    def test_two_symbols_perfect(self):
        cm = correlation_matrix({
            "A": [1.0, 2.0, 3.0],
            "B": [2.0, 4.0, 6.0],
        })
        assert abs(cm.matrix[0][1] - 1.0) < TOL
        assert abs(cm.matrix[1][0] - 1.0) < TOL
        # Diagonal
        assert abs(cm.matrix[0][0] - 1.0) < TOL
        assert abs(cm.matrix[1][1] - 1.0) < TOL

    def test_symmetric(self):
        cm = correlation_matrix({
            "A": [0.01, -0.005, 0.02],
            "B": [0.005, -0.01, 0.015],
            "C": [-0.01, 0.005, -0.008],
        })
        n = len(cm.symbols)
        for i in range(n):
            for j in range(n):
                assert abs(cm.matrix[i][j] - cm.matrix[j][i]) < TOL

    def test_truncates_to_shortest(self):
        cm = correlation_matrix({
            "A": [1.0, 2.0, 3.0, 4.0, 5.0],
            "B": [2.0, 4.0, 6.0],
        })
        # Both truncated to length 3 -> perfect correlation
        assert abs(cm.matrix[0][1] - 1.0) < TOL

    def test_empty(self):
        cm = correlation_matrix({})
        assert cm.symbols == []
        assert cm.matrix == []


class TestRollingBeta:
    def test_returns_empty_on_too_short(self):
        target = [0.01, 0.02]
        baseline = [0.01, 0.02]
        assert rolling_beta(target, baseline, window=5) == []

    def test_window_less_than_2(self):
        target = [0.01, 0.02, 0.03]
        baseline = [0.01, 0.02, 0.03]
        assert rolling_beta(target, baseline, window=1) == []

    def test_perfect_beta_1(self):
        # target == baseline -> beta = 1
        xs = [0.01, -0.005, 0.02, -0.01, 0.015]
        betas = rolling_beta(xs, xs, window=3)
        for b in betas:
            assert abs(b - 1.0) < TOL

    def test_output_length(self):
        target = [0.01, -0.005, 0.02, -0.01, 0.015, 0.003, -0.008, 0.012]
        baseline = target[:]
        betas = rolling_beta(target, baseline, window=4)
        assert len(betas) == len(target) - 4 + 1

    def test_beta_values(self):
        target = [0.01, -0.005, 0.02, -0.01, 0.015, 0.003, -0.008, 0.012]
        baseline = [0.008, -0.003, 0.015, -0.008, 0.012, 0.001, -0.006, 0.01]
        betas = rolling_beta(target, baseline, window=4)
        assert len(betas) == 5
        # First beta computed from target[0:4] and baseline[0:4]
        # Reference: cov/var_b = 0.00043/0.000326 = 1.3190184049079756
        assert abs(betas[0] - 1.3190184049079756) < TOL


# ---------------------------------------------------------------------------
# Compute metrics integration
# ---------------------------------------------------------------------------

class TestComputeMetrics:
    def test_series_a_integration(self):
        # Build a simple monotone equity curve then use trade metrics
        equity = [10000.0 + sum(TRADES_A_PNLS[:i]) for i in range(len(TRADES_A_PNLS) + 1)]
        result = compute_metrics(TRADES_A, equity, total_bars=len(equity) - 1)
        assert isinstance(result, MetricsResult)
        assert abs(result.win_rate - 0.5) < TOL
        assert abs(result.expectancy - 26.0) < TOL
        assert abs(result.avg_win - 110.0) < TOL
        assert abs(result.avg_loss - (-58.0)) < TOL
        assert result.longest_win_streak == 2
        assert result.longest_loss_streak == 2

    def test_max_consec_losses_property(self):
        result = MetricsResult(longest_loss_streak=3)
        assert result.max_consec_losses == 3

    def test_empty_trades_returns_zeros(self):
        result = compute_metrics([], [10000.0], total_bars=0)
        assert result.win_rate == 0.0
        assert result.profit_factor == 0.0


# ---------------------------------------------------------------------------
# Edge cases: empty series, single trade, all wins, all losses
# ---------------------------------------------------------------------------

class TestEdgeCases:
    def test_single_win_trade(self):
        t = [TradeReturn(net_pnl=100.0)]
        assert abs(win_rate(t) - 1.0) < TOL
        assert profit_factor(t) == math.inf
        assert longest_win_streak(t) == 1
        assert longest_loss_streak(t) == 0

    def test_single_loss_trade(self):
        t = [TradeReturn(net_pnl=-100.0)]
        assert win_rate(t) == 0.0
        assert profit_factor(t) == 0.0
        assert longest_win_streak(t) == 0
        assert longest_loss_streak(t) == 1

    def test_all_wins(self):
        t = _trades([10.0, 20.0, 30.0])
        assert abs(win_rate(t) - 1.0) < TOL
        assert profit_factor(t) == math.inf
        assert avg_loss(t) == 0.0
        assert payoff_ratio(t) == math.inf

    def test_all_losses(self):
        t = _trades([-10.0, -20.0, -30.0])
        assert win_rate(t) == 0.0
        assert profit_factor(t) == 0.0
        assert avg_win(t) == 0.0
        assert payoff_ratio(t) == 0.0

    def test_equity_length_1(self):
        assert max_drawdown_pct([10000.0]) == 0.0
        assert max_drawdown_abs([10000.0]) == 0.0
        assert drawdown_episodes([10000.0]) == []
        assert top5_drawdowns([10000.0]) == []

    def test_equity_empty(self):
        assert max_drawdown_pct([]) == 0.0
        assert max_drawdown_abs([]) == 0.0

    def test_distributions_small_n(self):
        # n < 4 -> skewness and excess_kurtosis return 0.0
        for n in range(4):
            xs = list(range(n))
            assert skewness(xs) == 0.0
            assert excess_kurtosis(xs) == 0.0

    def test_pearson_single_element(self):
        # std = 0 -> returns 0
        assert pearson([1.0], [2.0]) == 0.0

    def test_rolling_beta_constant_baseline(self):
        # var(baseline) = 0 -> beta = 0
        target = [0.01, 0.02, 0.03, 0.04]
        baseline = [1.0, 1.0, 1.0, 1.0]
        betas = rolling_beta(target, baseline, window=4)
        assert betas == [0.0]

    def test_metrics_inf_handling(self):
        # payoff_ratio and profit_factor return math.inf when no losses
        t = _trades([100.0, 200.0])
        pr = payoff_ratio(t)
        pf = profit_factor(t)
        assert math.isinf(pr) and pr > 0
        assert math.isinf(pf) and pf > 0
