"""Tests for algoforge.algo_gen.tier — score_to_tier and tier thresholds."""
from __future__ import annotations

import pytest

from algoforge.algo_gen.tier import score_to_tier
from algoforge.algo_gen.types import TierName, TierReport


class TestScoreToTier:
    """Verify tier boundaries and TierReport fields."""

    def _score(self, s: float) -> TierReport:
        return score_to_tier(s, s, s)

    # --- Tier name boundaries ---

    def test_score_100_is_white(self):
        assert self._score(100.0).tier == TierName.white

    def test_score_95_is_white(self):
        assert self._score(95.0).tier == TierName.white

    def test_score_94_99_is_green(self):
        r = score_to_tier(94.99, 94.99, 94.99)
        assert r.tier == TierName.green

    def test_score_90_is_green(self):
        assert self._score(90.0).tier == TierName.green

    def test_score_89_99_is_yellow(self):
        r = score_to_tier(89.99, 89.99, 89.99)
        assert r.tier == TierName.yellow

    def test_score_80_is_yellow(self):
        assert self._score(80.0).tier == TierName.yellow

    def test_score_79_99_is_orange(self):
        r = score_to_tier(79.99, 79.99, 79.99)
        assert r.tier == TierName.orange

    def test_score_70_is_orange(self):
        assert self._score(70.0).tier == TierName.orange

    def test_score_69_99_is_red(self):
        r = score_to_tier(69.99, 69.99, 69.99)
        assert r.tier == TierName.red

    def test_score_0_is_red(self):
        assert self._score(0.0).tier == TierName.red

    # --- min_score is the minimum of the three ---

    def test_min_score_is_minimum(self):
        r = score_to_tier(95.0, 80.0, 70.0)
        assert r.min_score == pytest.approx(70.0, abs=0.001)

    def test_min_score_one_dragging_down(self):
        r = score_to_tier(99.0, 99.0, 65.0)
        assert r.tier == TierName.red  # 65 < 70

    def test_individual_scores_stored(self):
        r = score_to_tier(91.0, 82.0, 71.0)
        assert r.walk_forward == pytest.approx(91.0, abs=0.001)
        assert r.mc_bootstrap == pytest.approx(82.0, abs=0.001)
        assert r.robustness   == pytest.approx(71.0, abs=0.001)

    # --- size_mult defaults ---

    def test_red_size_mult_zero(self):
        r = self._score(50.0)
        assert r.size_mult == 0.0

    def test_orange_size_mult_default(self):
        r = self._score(75.0)
        assert r.size_mult == pytest.approx(0.25)

    def test_yellow_size_mult_default(self):
        r = self._score(85.0)
        assert r.size_mult == pytest.approx(0.50)

    def test_green_size_mult_default(self):
        r = self._score(90.0)
        assert r.size_mult == pytest.approx(1.00)

    def test_white_size_mult_default(self):
        r = self._score(95.0)
        assert r.size_mult == pytest.approx(1.50)

    # --- experimental flag ---

    def test_red_is_experimental(self):
        assert self._score(50.0).experimental is True

    def test_orange_is_experimental(self):
        assert self._score(75.0).experimental is True

    def test_yellow_is_experimental(self):
        assert self._score(85.0).experimental is True

    def test_green_not_experimental(self):
        assert self._score(90.0).experimental is False

    def test_white_not_experimental(self):
        assert self._score(95.0).experimental is False

    # --- paper_only flag ---

    def test_red_is_paper_only(self):
        assert self._score(50.0).paper_only is True

    def test_orange_is_paper_only(self):
        assert self._score(75.0).paper_only is True

    def test_yellow_not_paper_only_by_default(self):
        # Default paper_only_below = yellow → orange and red are paper-only
        assert self._score(85.0).paper_only is False

    def test_green_not_paper_only(self):
        assert self._score(90.0).paper_only is False

    # --- cfg overrides ---

    def test_cfg_overrides_size_mult(self):
        r = score_to_tier(90.0, 90.0, 90.0, cfg={"green_size_mult": "2.00"})
        assert r.size_mult == pytest.approx(2.00)

    def test_cfg_overrides_white_mult(self):
        r = score_to_tier(95.0, 95.0, 95.0, cfg={"white_size_mult": "3.00"})
        assert r.size_mult == pytest.approx(3.00)

    def test_returns_tier_report_type(self):
        assert isinstance(self._score(90.0), TierReport)

    # --- TierName enum ---

    def test_tier_name_str(self):
        assert str(TierName.green) == "green"
        assert str(TierName.white) == "white"
        assert str(TierName.red) == "red"

    def test_tier_name_values(self):
        assert TierName.green.value == "green"
        assert TierName.orange.value == "orange"
        assert TierName.yellow.value == "yellow"
