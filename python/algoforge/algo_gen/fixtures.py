"""Canonical fixture loader for algo_gen tests.

Mirrors algoforge.llm.fixtures patterns.

Fixtures live in: tests/fixtures/algo_gen/ (relative to repo root).

Public API:
    load_bars(fixture_name: str) -> list[Bar]
    load_manifest(fixture_name: str) -> dict
    load_expected_tier_report(fixture_name: str) -> dict
"""
from __future__ import annotations

import json
import os
from typing import Any

from ..types import Bar

# Path to fixtures directory (relative to repo root)
def _fixture_dir() -> str:
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__))
    )))
    return os.path.join(repo_root, "tests", "fixtures", "algo_gen")


def _load_json(path: str) -> Any:
    """Load a JSON file and return its content."""
    with open(path, "r") as f:
        return json.load(f)


def load_bars(fixture_name: str) -> list[Bar]:
    """Load a bar fixture and return a list of Bar objects.

    Parameters
    ----------
    fixture_name:
        Fixture file name without path, e.g. ``"bars_eurusd_h1_2y.json"``.
    """
    path = os.path.join(_fixture_dir(), fixture_name)
    data = _load_json(path)

    if isinstance(data, dict) and "bars" in data:
        raw_bars = data["bars"]
    else:
        raw_bars = data

    return [
        Bar(
            timestamp = b["timestamp"],
            open      = b["open"],
            high      = b["high"],
            low       = b["low"],
            close     = b["close"],
            volume    = b["volume"],
            spread    = b.get("spread", 0.0),
        )
        for b in raw_bars
    ]


def load_manifest(fixture_name: str) -> dict:
    """Load a manifest fixture and return the raw dict.

    Parameters
    ----------
    fixture_name:
        Fixture file name, e.g. ``"manifest_trend_follow_green.json"``.
    """
    path = os.path.join(_fixture_dir(), fixture_name)
    return _load_json(path)


def load_expected_tier_report(fixture_name: str) -> dict:
    """Load an expected TierReport fixture.

    Parameters
    ----------
    fixture_name:
        Fixture file name, e.g. ``"tier_report_trend_follow_green.json"``.
    """
    path = os.path.join(_fixture_dir(), fixture_name)
    return _load_json(path)
