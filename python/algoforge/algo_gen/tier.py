"""Composite tier scorer for the S1 algorithm generator.

The tier is the MINIMUM of the three component scores:
  - walk_forward  (0-100)
  - mc_bootstrap  (0-100)
  - robustness    (0-100)

Tier thresholds and sizing multipliers are read from algoforge.ini [algo_gen]
with hard-coded defaults if the section is absent.

Defaults (matches §6 of the spec):
  orange_size_mult = 0.25
  yellow_size_mult = 0.50
  green_size_mult  = 1.00
  white_size_mult  = 1.50
  require_manual_review_below = green
  paper_only_below            = yellow

Public API:
    score_to_tier(walk_forward, mc, robustness, cfg=None) -> TierReport
"""
from __future__ import annotations

import configparser
import os
from typing import Optional

from .types import TierName, TierReport

# ── Hard-coded defaults ───────────────────────────────────────────────────────

_DEFAULT_CFG = {
    "orange_size_mult":              "0.25",
    "yellow_size_mult":              "0.50",
    "green_size_mult":               "1.00",
    "white_size_mult":               "1.50",
    "require_manual_review_below":   "green",
    "paper_only_below":              "yellow",
}

# Tier thresholds (min_score → tier)
_TIER_THRESHOLDS = [
    (95.0, TierName.white),
    (90.0, TierName.green),
    (80.0, TierName.yellow),
    (70.0, TierName.orange),
]
# Below 70 → red (not promoted)


def _load_cfg(cfg: Optional[dict] = None) -> dict:
    """Load [algo_gen] config from algoforge.ini or use defaults."""
    resolved = dict(_DEFAULT_CFG)

    if cfg is not None:
        resolved.update(cfg)
        return resolved

    # Try to find algoforge.ini in repo root
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__))
    )))
    ini_path  = os.path.join(repo_root, "algoforge.ini")

    if os.path.exists(ini_path):
        parser = configparser.ConfigParser()
        parser.read(ini_path)
        if parser.has_section("algo_gen"):
            for key, default in _DEFAULT_CFG.items():
                resolved[key] = parser.get("algo_gen", key, fallback=default)

    return resolved


def _tier_from_min_score(min_score: float) -> TierName:
    for threshold, tier in _TIER_THRESHOLDS:
        if min_score >= threshold:
            return tier
    return TierName.red


def score_to_tier(
    walk_forward: float,
    mc: float,
    robustness: float,
    cfg: Optional[dict] = None,
) -> TierReport:
    """Assign a tier from the three component scores.

    The tier is determined by the MINIMUM of the three scores.
    Thresholds and multipliers are read from algoforge.ini [algo_gen].

    Parameters
    ----------
    walk_forward:  Walk-forward component score (0-100).
    mc:            Monte Carlo component score (0-100).
    robustness:    Parameter robustness component score (0-100).
    cfg:           Optional override dict for thresholds/multipliers.

    Returns
    -------
    TierReport with resolved tier, size_mult, experimental, paper_only.
    """
    loaded_cfg  = _load_cfg(cfg)
    min_score   = min(walk_forward, mc, robustness)
    tier        = _tier_from_min_score(min_score)

    # Resolve size multiplier
    size_mult_map = {
        TierName.red:    0.0,
        TierName.orange: float(loaded_cfg["orange_size_mult"]),
        TierName.yellow: float(loaded_cfg["yellow_size_mult"]),
        TierName.green:  float(loaded_cfg["green_size_mult"]),
        TierName.white:  float(loaded_cfg["white_size_mult"]),
    }
    size_mult = size_mult_map[tier]

    # Experimental flag: orange and yellow
    experimental_tiers = {TierName.red, TierName.orange, TierName.yellow}
    experimental = tier in experimental_tiers

    # Paper-only flag: orange and red
    paper_only_below = loaded_cfg["paper_only_below"]
    paper_only_tiers = {TierName.red, TierName.orange}
    if paper_only_below == "yellow":
        paper_only_tiers = {TierName.red, TierName.orange}
    elif paper_only_below == "green":
        paper_only_tiers = {TierName.red, TierName.orange, TierName.yellow}
    paper_only = tier in paper_only_tiers

    return TierReport(
        tier         = tier,
        min_score    = round(min_score, 4),
        walk_forward = round(walk_forward, 4),
        mc_bootstrap = round(mc, 4),
        robustness   = round(robustness, 4),
        size_mult    = size_mult,
        experimental = experimental,
        paper_only   = paper_only,
    )
