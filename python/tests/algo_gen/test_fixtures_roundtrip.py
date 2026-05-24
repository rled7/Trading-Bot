"""Tests for fixture files — schema roundtrip and expected tier reports.

These tests verify:
  1. All bar fixtures load correctly and have the right structure.
  2. All manifest fixtures load correctly and pass validate_manifest.
  3. Invalid manifests fail validate_manifest.
  4. Expected tier report fixtures match the schema (when present).
"""
from __future__ import annotations

import os
import pytest

from algoforge.algo_gen.fixtures import (
    load_bars,
    load_manifest,
    load_expected_tier_report,
)
from algoforge.algo_gen.schema import validate_manifest
from algoforge.types import Bar


FIXTURE_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__))
    ))),
    "tests", "fixtures", "algo_gen",
)


# ── Bar fixture loading ────────────────────────────────────────────────────────

class TestBarFixtures:
    def test_eurusd_h1_loads(self):
        bars = load_bars("bars_eurusd_h1_2y.json")
        assert isinstance(bars, list)
        assert len(bars) > 0

    def test_eurusd_h1_bar_count(self):
        bars = load_bars("bars_eurusd_h1_2y.json")
        assert len(bars) >= 8000  # ~8760

    def test_eurusd_h1_bar_type(self):
        bars = load_bars("bars_eurusd_h1_2y.json")
        assert isinstance(bars[0], Bar)

    def test_eurusd_h1_bar_fields(self):
        bars = load_bars("bars_eurusd_h1_2y.json")
        b = bars[0]
        assert hasattr(b, "open")
        assert hasattr(b, "high")
        assert hasattr(b, "low")
        assert hasattr(b, "close")
        assert hasattr(b, "volume")
        assert hasattr(b, "timestamp")

    def test_eurusd_h1_ohlc_valid(self):
        bars = load_bars("bars_eurusd_h1_2y.json")
        for b in bars[:100]:
            assert b.high >= b.low
            assert b.high >= b.open
            assert b.high >= b.close
            assert b.low  <= b.open
            assert b.low  <= b.close

    def test_gbpjpy_m15_loads(self):
        bars = load_bars("bars_gbpjpy_m15_6mo.json")
        assert isinstance(bars, list)
        assert len(bars) > 0

    def test_gbpjpy_m15_bar_count(self):
        bars = load_bars("bars_gbpjpy_m15_6mo.json")
        assert len(bars) >= 17000

    def test_gbpjpy_start_price_reasonable(self):
        bars = load_bars("bars_gbpjpy_m15_6mo.json")
        assert 100.0 < bars[0].open < 200.0

    def test_missing_fixture_raises(self):
        with pytest.raises(FileNotFoundError):
            load_bars("nonexistent_bars.json")


# ── Manifest fixture loading ───────────────────────────────────────────────────

class TestManifestFixtures:
    VALID_MANIFESTS = [
        "manifest_trend_follow_green.json",
        "manifest_trend_follow_orange.json",
        "manifest_mean_revert_yellow.json",
        "manifest_breakout_white.json",
        "manifest_escape_hatch_green.json",
    ]
    INVALID_MANIFESTS = [
        "manifest_invalid_schema.json",
    ]

    @pytest.mark.parametrize("fname", VALID_MANIFESTS)
    def test_valid_manifest_loads(self, fname):
        d = load_manifest(fname)
        assert isinstance(d, dict)

    @pytest.mark.parametrize("fname", VALID_MANIFESTS)
    def test_valid_manifest_passes_schema(self, fname):
        d = load_manifest(fname)
        m = validate_manifest(d)
        assert m is not None
        assert m.name == d["name"]

    @pytest.mark.parametrize("fname", INVALID_MANIFESTS)
    def test_invalid_manifest_fails_schema(self, fname):
        d = load_manifest(fname)
        with pytest.raises((ValueError, TypeError)):
            validate_manifest(d)

    def test_low_trade_count_manifest_loads(self):
        d = load_manifest("manifest_low_trade_count.json")
        assert isinstance(d, dict)

    def test_wf_fail_manifest_loads(self):
        d = load_manifest("manifest_wf_fail.json")
        assert isinstance(d, dict)

    def test_missing_manifest_raises(self):
        with pytest.raises(FileNotFoundError):
            load_manifest("nonexistent_manifest.json")

    def test_escape_hatch_has_code(self):
        d = load_manifest("manifest_escape_hatch_green.json")
        assert "code" in d
        assert isinstance(d["code"], str)
        assert len(d["code"]) > 0


# ── Tier report fixtures ───────────────────────────────────────────────────────

class TestTierReportFixtures:
    TIER_REPORTS = [
        "tier_report_trend_follow_green.json",
        "tier_report_mean_revert_yellow.json",
        "tier_report_breakout_white.json",
    ]

    @pytest.mark.parametrize("fname", TIER_REPORTS)
    def test_tier_report_loads(self, fname):
        path = os.path.join(FIXTURE_DIR, fname)
        if not os.path.exists(path):
            pytest.skip(f"Tier report fixture not yet generated: {fname}")
        d = load_expected_tier_report(fname)
        assert isinstance(d, dict)

    @pytest.mark.parametrize("fname", TIER_REPORTS)
    def test_tier_report_has_tier_key(self, fname):
        path = os.path.join(FIXTURE_DIR, fname)
        if not os.path.exists(path):
            pytest.skip(f"Tier report fixture not yet generated: {fname}")
        d = load_expected_tier_report(fname)
        assert "tier" in d

    @pytest.mark.parametrize("fname", TIER_REPORTS)
    def test_tier_report_tier_valid(self, fname):
        from algoforge.algo_gen.types import TierName
        path = os.path.join(FIXTURE_DIR, fname)
        if not os.path.exists(path):
            pytest.skip(f"Tier report fixture not yet generated: {fname}")
        d = load_expected_tier_report(fname)
        valid_tiers = {t.value for t in TierName}
        assert d["tier"] in valid_tiers

    def test_missing_tier_report_raises(self):
        with pytest.raises(FileNotFoundError):
            load_expected_tier_report("nonexistent_tier_report.json")
