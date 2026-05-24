"""Tests for algoforge.algo_gen.promote — registry promotion."""
from __future__ import annotations

import json
import os
import tempfile

import pytest

from algoforge.algo_gen.promote import PromotionError, promote
from algoforge.algo_gen.runtime import ManifestAlgo
from algoforge.algo_gen.schema import validate_manifest
from algoforge.algo_gen.tier import score_to_tier
from algoforge.algo_gen.types import TierName


# ── Helpers ────────────────────────────────────────────────────────────────────

def _minimal_manifest():
    return validate_manifest({
        "schema_version": "1.0",
        "name":           "promo-test",
        "description":    "Promotion test algo.",
        "rationale":      "Test.",
        "timeframes":     ["H1"],
        "symbols":        ["EURUSD"],
        "indicators": [
            {"id": "rsi14", "kind": "rsi", "params": {"period": 14}},
        ],
        "entries": [
            {"side": "long", "when": "rsi14 < 30"},
        ],
        "exits": [
            {"side": "long", "sl_atr": 1.5, "tp_atr": 3.0},
        ],
        "risk": {"size": "atr"},
    })


def _green_report():
    return score_to_tier(92.0, 91.0, 90.0)


def _red_report():
    return score_to_tier(50.0, 50.0, 50.0)


# ── PromotionError tests ───────────────────────────────────────────────────────

class TestPromotionRefused:
    def test_red_tier_refused(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            with pytest.raises(PromotionError, match="red"):
                promote(_red_report(), _minimal_manifest(), tmpdir)

    def test_orange_without_confirm_refused(self):
        report = score_to_tier(75.0, 75.0, 75.0)
        assert report.tier == TierName.orange
        with tempfile.TemporaryDirectory() as tmpdir:
            with pytest.raises(PromotionError, match="confirm"):
                promote(report, _minimal_manifest(), tmpdir, confirm=False)

    def test_yellow_without_confirm_refused(self):
        report = score_to_tier(85.0, 85.0, 85.0)
        assert report.tier == TierName.yellow
        with tempfile.TemporaryDirectory() as tmpdir:
            with pytest.raises(PromotionError, match="confirm"):
                promote(report, _minimal_manifest(), tmpdir, confirm=False)

    def test_green_without_confirm_succeeds(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            algo = promote(_green_report(), _minimal_manifest(), tmpdir, confirm=False)
            assert algo is not None

    def test_promotion_error_is_value_error(self):
        assert issubclass(PromotionError, ValueError)


# ── Successful promotion tests ─────────────────────────────────────────────────

class TestSuccessfulPromotion:
    def setup_method(self):
        self._tmpdir = tempfile.mkdtemp()

    def teardown_method(self):
        import shutil
        shutil.rmtree(self._tmpdir, ignore_errors=True)

    def test_returns_manifest_algo(self):
        algo = promote(_green_report(), _minimal_manifest(), self._tmpdir)
        assert isinstance(algo, ManifestAlgo)

    def test_writes_json_file(self):
        promote(_green_report(), _minimal_manifest(), self._tmpdir)
        path = os.path.join(self._tmpdir, "promo-test.json")
        assert os.path.exists(path)

    def test_json_file_valid(self):
        promote(_green_report(), _minimal_manifest(), self._tmpdir)
        path = os.path.join(self._tmpdir, "promo-test.json")
        with open(path) as f:
            data = json.load(f)
        assert data["name"] == "promo-test"

    def test_metadata_key_in_json(self):
        promote(_green_report(), _minimal_manifest(), self._tmpdir)
        path = os.path.join(self._tmpdir, "promo-test.json")
        with open(path) as f:
            data = json.load(f)
        assert "_metadata" in data

    def test_metadata_tier(self):
        promote(_green_report(), _minimal_manifest(), self._tmpdir)
        path = os.path.join(self._tmpdir, "promo-test.json")
        with open(path) as f:
            data = json.load(f)
        assert data["_metadata"]["tier"] == "green"

    def test_metadata_size_mult(self):
        promote(_green_report(), _minimal_manifest(), self._tmpdir)
        path = os.path.join(self._tmpdir, "promo-test.json")
        with open(path) as f:
            data = json.load(f)
        assert data["_metadata"]["size_mult"] == pytest.approx(1.0)

    def test_metadata_source_algo_gen(self):
        promote(_green_report(), _minimal_manifest(), self._tmpdir)
        path = os.path.join(self._tmpdir, "promo-test.json")
        with open(path) as f:
            data = json.load(f)
        assert data["_metadata"]["source"] == "algo_gen"

    def test_metadata_scores_stored(self):
        promote(_green_report(), _minimal_manifest(), self._tmpdir)
        path = os.path.join(self._tmpdir, "promo-test.json")
        with open(path) as f:
            data = json.load(f)
        scores = data["_metadata"]["scores"]
        assert "walk_forward" in scores
        assert "mc_bootstrap" in scores
        assert "robustness" in scores

    def test_metadata_created_at_present(self):
        promote(_green_report(), _minimal_manifest(), self._tmpdir)
        path = os.path.join(self._tmpdir, "promo-test.json")
        with open(path) as f:
            data = json.load(f)
        assert "created_at" in data["_metadata"]

    def test_algo_metadata_populated(self):
        algo = promote(_green_report(), _minimal_manifest(), self._tmpdir)
        assert algo.metadata is not None
        assert isinstance(algo.metadata, dict)

    def test_algo_metadata_size_mult(self):
        algo = promote(_green_report(), _minimal_manifest(), self._tmpdir)
        assert "size_mult" in algo.metadata
        assert algo.metadata["size_mult"] == pytest.approx(1.0)

    def test_algo_metadata_tier(self):
        algo = promote(_green_report(), _minimal_manifest(), self._tmpdir)
        assert algo.metadata["tier"] == "green"

    def test_algo_metadata_manifest_path(self):
        algo = promote(_green_report(), _minimal_manifest(), self._tmpdir)
        assert "manifest_path" in algo.metadata
        assert os.path.exists(algo.metadata["manifest_path"])

    def test_orange_with_confirm_succeeds(self):
        report = score_to_tier(75.0, 75.0, 75.0)
        algo = promote(report, _minimal_manifest(), self._tmpdir, confirm=True)
        assert algo.metadata["tier"] == "orange"

    def test_white_report_size_mult(self):
        report = score_to_tier(96.0, 95.0, 96.0)
        algo = promote(report, _minimal_manifest(), self._tmpdir)
        assert algo.metadata["size_mult"] == pytest.approx(1.5)

    def test_registry_dir_created_if_missing(self):
        new_dir = os.path.join(self._tmpdir, "registry", "sub")
        promote(_green_report(), _minimal_manifest(), new_dir)
        assert os.path.isdir(new_dir)

    def test_generator_info_stored(self):
        gen_info = {"model": "claude-3", "mode": "balanced"}
        promote(_green_report(), _minimal_manifest(), self._tmpdir,
                generator_info=gen_info)
        path = os.path.join(self._tmpdir, "promo-test.json")
        with open(path) as f:
            data = json.load(f)
        assert data["_metadata"]["generator"] == gen_info

    def test_brief_stored(self):
        promote(_green_report(), _minimal_manifest(), self._tmpdir,
                brief="A trend-following algo")
        path = os.path.join(self._tmpdir, "promo-test.json")
        with open(path) as f:
            data = json.load(f)
        assert data["_metadata"]["brief"] == "A trend-following algo"

    def test_idempotent_overwrite(self):
        # Promoting the same algo twice should just overwrite
        promote(_green_report(), _minimal_manifest(), self._tmpdir)
        promote(_green_report(), _minimal_manifest(), self._tmpdir)
        path = os.path.join(self._tmpdir, "promo-test.json")
        assert os.path.exists(path)
