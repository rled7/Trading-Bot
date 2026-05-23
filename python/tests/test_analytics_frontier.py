from __future__ import annotations

"""Tests for the frontier analytics modules: linalg, ml_attribution, factors.

Run with: pytest python/tests/test_analytics_frontier.py -v
"""

import math
import random

import pytest

from algoforge.analytics.linalg import (
    dot,
    identity,
    inverse,
    matmul,
    transpose,
)
from algoforge.analytics.ml_attribution import OLSResult, attribute, ols
from algoforge.analytics.factors import FactorResult, factor_regression


# ---------------------------------------------------------------------------
# linalg tests
# ---------------------------------------------------------------------------


class TestIdentity:
    def test_2x2(self) -> None:
        I = identity(2)
        assert I == [[1.0, 0.0], [0.0, 1.0]]

    def test_3x3(self) -> None:
        I = identity(3)
        for i in range(3):
            for j in range(3):
                assert I[i][j] == (1.0 if i == j else 0.0)


class TestTranspose:
    def test_square(self) -> None:
        M = [[1.0, 2.0], [3.0, 4.0]]
        Mt = transpose(M)
        assert Mt == [[1.0, 3.0], [2.0, 4.0]]

    def test_rectangular(self) -> None:
        M = [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]  # 2×3
        Mt = transpose(M)
        assert len(Mt) == 3
        assert len(Mt[0]) == 2
        assert Mt[0] == [1.0, 4.0]
        assert Mt[2] == [3.0, 6.0]


class TestMatmul:
    def test_2x2_hand_computed(self) -> None:
        # [[1,2],[3,4]] @ [[5,6],[7,8]] = [[19,22],[43,50]]
        A = [[1.0, 2.0], [3.0, 4.0]]
        B = [[5.0, 6.0], [7.0, 8.0]]
        C = matmul(A, B)
        assert C[0][0] == pytest.approx(19.0)
        assert C[0][1] == pytest.approx(22.0)
        assert C[1][0] == pytest.approx(43.0)
        assert C[1][1] == pytest.approx(50.0)

    def test_identity_multiplication(self) -> None:
        A = [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0], [7.0, 8.0, 9.0]]
        I = identity(3)
        AI = matmul(A, I)
        for i in range(3):
            for j in range(3):
                assert AI[i][j] == pytest.approx(A[i][j])


class TestDot:
    def test_basic(self) -> None:
        assert dot([1.0, 2.0, 3.0], [4.0, 5.0, 6.0]) == pytest.approx(32.0)

    def test_orthogonal(self) -> None:
        assert dot([1.0, 0.0], [0.0, 1.0]) == pytest.approx(0.0)


class TestInverse:
    def test_identity_inverse_is_identity(self) -> None:
        I = identity(3)
        Iinv = inverse(I)
        for i in range(3):
            for j in range(3):
                assert Iinv[i][j] == pytest.approx(1.0 if i == j else 0.0, abs=1e-10)

    def test_2x2_known_inverse(self) -> None:
        # inv([[1,2],[3,4]]) = [[-2,1],[1.5,-0.5]]
        M = [[1.0, 2.0], [3.0, 4.0]]
        Minv = inverse(M)
        assert Minv[0][0] == pytest.approx(-2.0, abs=1e-10)
        assert Minv[0][1] == pytest.approx(1.0, abs=1e-10)
        assert Minv[1][0] == pytest.approx(1.5, abs=1e-10)
        assert Minv[1][1] == pytest.approx(-0.5, abs=1e-10)

    def test_inverse_times_original_is_identity(self) -> None:
        M = [[4.0, 7.0], [2.0, 6.0]]
        Minv = inverse(M)
        I_approx = matmul(M, Minv)
        for i in range(2):
            for j in range(2):
                assert I_approx[i][j] == pytest.approx(1.0 if i == j else 0.0, abs=1e-10)

    def test_3x3_inverse(self) -> None:
        M = [[1.0, 2.0, 3.0], [0.0, 1.0, 4.0], [5.0, 6.0, 0.0]]
        Minv = inverse(M)
        prod = matmul(M, Minv)
        I = identity(3)
        for i in range(3):
            for j in range(3):
                assert prod[i][j] == pytest.approx(I[i][j], abs=1e-10)

    def test_singular_raises_value_error(self) -> None:
        # Rows are linearly dependent.
        M = [[1.0, 2.0], [2.0, 4.0]]
        with pytest.raises(ValueError, match="singular"):
            inverse(M)

    def test_zero_matrix_raises_value_error(self) -> None:
        M = [[0.0, 0.0], [0.0, 0.0]]
        with pytest.raises(ValueError):
            inverse(M)


# ---------------------------------------------------------------------------
# ml_attribution / OLS tests
# ---------------------------------------------------------------------------


class TestOLS:
    def _make_dataset(
        self,
        n: int = 100,
        seed: int = 42,
        noise_scale: float = 0.0,
    ) -> tuple[list[list[float]], list[float]]:
        """y = 2 + 3*x1 + 4*x2 (+ optional noise)."""
        rng = random.Random(seed)
        X: list[list[float]] = []
        y: list[float] = []
        for _ in range(n):
            x1 = rng.uniform(-5.0, 5.0)
            x2 = rng.uniform(-5.0, 5.0)
            noise = rng.gauss(0.0, noise_scale) if noise_scale > 0 else 0.0
            X.append([x1, x2])
            y.append(2.0 + 3.0 * x1 + 4.0 * x2 + noise)
        return X, y

    def test_perfect_fit_recovers_true_beta(self) -> None:
        X, y = self._make_dataset(noise_scale=0.0)
        result = ols(X, y, add_intercept=True)
        assert result.beta[0] == pytest.approx(2.0, abs=1e-6)
        assert result.beta[1] == pytest.approx(3.0, abs=1e-6)
        assert result.beta[2] == pytest.approx(4.0, abs=1e-6)

    def test_perfect_fit_r2_is_one(self) -> None:
        X, y = self._make_dataset(noise_scale=0.0)
        result = ols(X, y, add_intercept=True)
        assert result.r2 == pytest.approx(1.0, abs=1e-8)

    def test_noisy_fit_beta_approximate(self) -> None:
        """With tiny noise the betas should still be close."""
        X, y = self._make_dataset(n=500, noise_scale=0.01, seed=7)
        result = ols(X, y, add_intercept=True)
        assert result.beta[0] == pytest.approx(2.0, abs=0.05)
        assert result.beta[1] == pytest.approx(3.0, abs=0.05)
        assert result.beta[2] == pytest.approx(4.0, abs=0.05)
        assert result.r2 > 0.999

    def test_r2_close_to_one_with_small_noise(self) -> None:
        X, y = self._make_dataset(n=200, noise_scale=0.001, seed=13)
        result = ols(X, y, add_intercept=True)
        assert result.r2 > 0.9999

    def test_result_has_correct_n_and_k(self) -> None:
        # n=5 > k=3 (intercept + 2 features); columns are not collinear.
        X = [[1.0, 2.0], [3.0, 1.0], [5.0, 4.0], [2.0, 6.0], [4.0, 3.0]]
        y = [7.0, 10.0, 17.0, 13.0, 14.0]
        result = ols(X, y, add_intercept=True)
        assert result.n == 5
        assert result.k == 3  # intercept + 2 features

    def test_residuals_length(self) -> None:
        X, y = self._make_dataset(n=50)
        result = ols(X, y)
        assert len(result.residuals) == 50

    def test_residuals_sum_near_zero(self) -> None:
        """OLS residuals are orthogonal to the intercept column, so they sum to ~0."""
        X, y = self._make_dataset(n=100, noise_scale=0.5)
        result = ols(X, y, add_intercept=True)
        assert sum(result.residuals) == pytest.approx(0.0, abs=1e-8)

    def test_t_stats_length_matches_k(self) -> None:
        X, y = self._make_dataset(n=50)
        result = ols(X, y, add_intercept=True)
        assert len(result.t_stats) == result.k
        assert len(result.std_errors) == result.k

    def test_singular_X_raises_value_error(self) -> None:
        """Duplicate columns => X^T X singular."""
        X = [[1.0, 1.0], [2.0, 2.0], [3.0, 3.0]]
        y = [1.0, 2.0, 3.0]
        with pytest.raises(ValueError):
            ols(X, y, add_intercept=True)

    def test_no_intercept_mode(self) -> None:
        """With add_intercept=False, beta length equals number of feature columns."""
        X = [[1.0, 0.0], [0.0, 1.0], [1.0, 1.0]]
        y = [3.0, 4.0, 7.0]  # y = 3*x1 + 4*x2 exactly
        result = ols(X, y, add_intercept=False)
        assert result.k == 2
        assert result.beta[0] == pytest.approx(3.0, abs=1e-9)
        assert result.beta[1] == pytest.approx(4.0, abs=1e-9)


class TestAttribute:
    def test_returns_dict_with_expected_keys(self) -> None:
        # Use linearly independent features (not collinear with each other or intercept).
        features = {"x1": [1.0, 3.0, 5.0, 2.0, 4.0], "x2": [2.0, 1.0, 4.0, 6.0, 3.0]}
        pnls = [5.0, 10.0, 13.0, 11.0, 9.0]
        out = attribute(features, pnls)
        assert "ols_result" in out
        assert "feature_importance" in out
        assert "r2" in out
        assert "adj_r2" in out

    def test_feature_importance_sorted_by_abs_t_stat(self) -> None:
        rng = random.Random(99)
        n = 100
        x1 = [rng.uniform(0, 10) for _ in range(n)]
        x2 = [rng.uniform(0, 10) for _ in range(n)]
        # x1 has a much larger coefficient, so should rank first.
        pnls = [10.0 * x1[i] + 0.1 * x2[i] + rng.gauss(0, 0.01) for i in range(n)]
        out = attribute({"x1": x1, "x2": x2}, pnls)
        importance = out["feature_importance"]
        assert importance[0][0] == "x1"

    def test_ols_result_is_correct_type(self) -> None:
        features = {"a": [1.0, 2.0, 3.0]}
        pnls = [1.0, 2.0, 3.0]
        out = attribute(features, pnls)
        assert isinstance(out["ols_result"], OLSResult)


# ---------------------------------------------------------------------------
# factor_regression tests
# ---------------------------------------------------------------------------


class TestFactorRegression:
    def _make_factor_data(
        self,
        n: int = 200,
        true_alpha: float = 0.002,
        true_betas: dict[str, float] | None = None,
        noise_std: float = 0.0,
        seed: int = 0,
    ) -> tuple[list[float], dict[str, list[float]]]:
        """Synthetic factor returns and strategy returns."""
        if true_betas is None:
            true_betas = {"market": 1.1, "hml": 0.3, "smb": -0.2}
        rng = random.Random(seed)
        factor_returns: dict[str, list[float]] = {
            name: [rng.gauss(0.0, 0.01) for _ in range(n)]
            for name in true_betas
        }
        strategy_returns: list[float] = []
        for i in range(n):
            r = true_alpha
            for name, beta in true_betas.items():
                r += beta * factor_returns[name][i]
            r += rng.gauss(0.0, noise_std) if noise_std > 0 else 0.0
            strategy_returns.append(r)
        return strategy_returns, factor_returns

    def test_recovers_alpha_and_betas_no_noise(self) -> None:
        true_alpha = 0.002
        true_betas = {"market": 1.1, "hml": 0.3, "smb": -0.2}
        strat, factors = self._make_factor_data(
            true_alpha=true_alpha, true_betas=true_betas, noise_std=0.0
        )
        result = factor_regression(strat, factors)
        assert result.alpha == pytest.approx(true_alpha, abs=1e-6)
        for name, b in true_betas.items():
            assert result.betas[name] == pytest.approx(b, abs=1e-6)

    def test_recovers_values_to_1e_6(self) -> None:
        """Spec requires factor recovery to within 1e-6 for the no-noise case."""
        true_alpha = 0.0015
        true_betas = {"f1": 0.8, "f2": -0.4}
        strat, factors = self._make_factor_data(
            n=300, true_alpha=true_alpha, true_betas=true_betas, noise_std=0.0, seed=5
        )
        result = factor_regression(strat, factors)
        assert abs(result.alpha - true_alpha) < 1e-6
        for name, b in true_betas.items():
            assert abs(result.betas[name] - b) < 1e-6

    def test_r2_near_one_no_noise(self) -> None:
        strat, factors = self._make_factor_data(noise_std=0.0)
        result = factor_regression(strat, factors)
        assert result.r2 == pytest.approx(1.0, abs=1e-8)

    def test_returns_factor_result_dataclass(self) -> None:
        strat, factors = self._make_factor_data()
        result = factor_regression(strat, factors)
        assert isinstance(result, FactorResult)

    def test_betas_dict_keys_match_factor_names(self) -> None:
        factor_names = ["momentum", "value", "quality"]
        true_betas = {name: 0.5 for name in factor_names}
        strat, factors = self._make_factor_data(true_betas=true_betas)
        result = factor_regression(strat, factors)
        assert set(result.betas.keys()) == set(factor_names)
        assert set(result.beta_t_stats.keys()) == set(factor_names)

    def test_alpha_t_stat_and_beta_t_stats_present(self) -> None:
        strat, factors = self._make_factor_data(noise_std=0.0001, seed=3)
        result = factor_regression(strat, factors)
        assert math.isfinite(result.alpha_t_stat)
        for name in factors:
            assert math.isfinite(result.beta_t_stats[name])

    def test_single_factor_model(self) -> None:
        """Regression against a single factor should give simple OLS."""
        n = 50
        rng = random.Random(42)
        f = [rng.gauss(0, 0.01) for _ in range(n)]
        alpha, beta = 0.001, 1.5
        strat = [alpha + beta * fi for fi in f]
        result = factor_regression(strat, {"market": f})
        assert result.alpha == pytest.approx(alpha, abs=1e-9)
        assert result.betas["market"] == pytest.approx(beta, abs=1e-9)
        assert result.r2 == pytest.approx(1.0, abs=1e-8)

    def test_collinear_factors_raise_value_error(self) -> None:
        """Two perfectly collinear factors make X^T X singular."""
        n = 50
        rng = random.Random(7)
        f = [rng.gauss(0, 0.01) for _ in range(n)]
        strat = [0.001 + 1.0 * fi for fi in f]
        # f2 is identical to f1 — collinear.
        with pytest.raises(ValueError):
            factor_regression(strat, {"f1": f, "f2": list(f)})
