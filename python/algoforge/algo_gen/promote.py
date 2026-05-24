"""Registry promotion for validated algorithms.

Public API:
    promote(report, manifest, registry_dir, cfg=None) -> ManifestAlgo
"""
from __future__ import annotations

import json
import os
from datetime import datetime, timezone
from typing import Optional

from .runtime import ManifestAlgo
from .tier import _load_cfg
from .types import AlgoManifest, TierName, TierReport

# Tier color codes
_TIER_COLORS = {
    TierName.red:    "#ff4444",
    TierName.orange: "#ff8800",
    TierName.yellow: "#ffdd00",
    TierName.green:  "#00ff88",
    TierName.white:  "#ffffff",
}


class PromotionError(ValueError):
    """Raised when promotion is refused."""


def promote(
    report:        TierReport,
    manifest:      AlgoManifest,
    registry_dir:  str,
    cfg:           Optional[dict] = None,
    *,
    generator_info: Optional[dict] = None,
    brief:          str = "",
    confirm:        bool = False,
) -> ManifestAlgo:
    """Promote a validated manifest into the registry.

    Refuses red tier. Writes manifest JSON to registry_dir/<name>.json.
    Returns a ManifestAlgo instance with .metadata populated per spec §7.

    Parameters
    ----------
    report:
        TierReport from score_to_tier / validator.
    manifest:
        The AlgoManifest to promote.
    registry_dir:
        Directory path where manifest JSON files are stored.
    cfg:
        Optional config override (same as score_to_tier).
    generator_info:
        Optional dict with generator metadata (mode, model, seed).
    brief:
        Optional brief description used during generation.
    confirm:
        Required True for orange/yellow tiers (manual review gate).

    Returns
    -------
    ManifestAlgo instance with populated metadata.
    """
    loaded_cfg = _load_cfg(cfg)

    # Refuse red tier
    if report.tier == TierName.red:
        raise PromotionError(
            f"Cannot promote algorithm '{manifest.name}': "
            f"tier is red (min_score={report.min_score:.2f} < 70). "
            f"Red-tier algorithms are never promoted."
        )

    # Check manual review gate (orange/yellow require confirm=True)
    require_below = loaded_cfg.get("require_manual_review_below", "green")
    tiers_requiring_confirm: set[TierName] = set()
    if require_below in ("green", "white"):
        tiers_requiring_confirm = {TierName.orange, TierName.yellow}
    elif require_below == "yellow":
        tiers_requiring_confirm = {TierName.orange}

    if report.tier in tiers_requiring_confirm and not confirm:
        raise PromotionError(
            f"Cannot promote '{manifest.name}' (tier={report.tier.value}) "
            f"without confirm=True. This tier requires manual review."
        )

    # Build metadata dict per spec §7
    created_at = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    os.makedirs(registry_dir, exist_ok=True)
    manifest_path = os.path.join(registry_dir, f"{manifest.name}.json")

    metadata = {
        "source":         "algo_gen",
        "schema_version": manifest.schema_version,
        "tier":           report.tier.value,
        "tier_color":     _TIER_COLORS.get(report.tier, "#888888"),
        "scores": {
            "walk_forward": report.walk_forward,
            "mc_bootstrap": report.mc_bootstrap,
            "robustness":   report.robustness,
        },
        "min_score":     report.min_score,
        "size_mult":     report.size_mult,
        "experimental":  report.experimental,
        "paper_only":    report.paper_only,
        "created_at":    created_at,
        "generator":     generator_info or {},
        "brief":         brief,
        "manifest_path": manifest_path,
    }

    # Serialise manifest to JSON
    manifest_dict = _manifest_to_dict(manifest)
    manifest_dict["_metadata"] = metadata

    with open(manifest_path, "w") as f:
        json.dump(manifest_dict, f, indent=2)

    # Create ManifestAlgo with metadata
    algo          = ManifestAlgo(manifest)
    algo.metadata = metadata

    return algo


def _manifest_to_dict(manifest: AlgoManifest) -> dict:
    """Serialise an AlgoManifest to a dict."""
    return {
        "schema_version": manifest.schema_version,
        "name":           manifest.name,
        "description":    manifest.description,
        "rationale":      manifest.rationale,
        "timeframes":     manifest.timeframes,
        "symbols":        manifest.symbols if isinstance(manifest.symbols, str)
                          else list(manifest.symbols),
        "indicators": [
            {"id": i.id, "kind": i.kind, "params": i.params}
            for i in manifest.indicators
        ],
        "entries": [
            {"side": e.side, "when": e.when}
            for e in manifest.entries
        ],
        "exits": [
            {
                "side":   x.side,
                **({"when":   x.when}   if x.when   is not None else {}),
                **({"sl_atr": x.sl_atr} if x.sl_atr is not None else {}),
                **({"tp_atr": x.tp_atr} if x.tp_atr is not None else {}),
            }
            for x in manifest.exits
        ],
        "risk": {
            "size":           manifest.risk.size,
            "atr_mult":       manifest.risk.atr_mult,
            "fixed_lots":     manifest.risk.fixed_lots,
            "max_concurrent": manifest.risk.max_concurrent,
            "hedge":          manifest.risk.hedge,
            "cool_down_bars": manifest.risk.cool_down_bars,
        },
        "code": manifest.code,
    }
