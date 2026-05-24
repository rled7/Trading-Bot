"""Parameter robustness sweep for AlgoManifest.

Picks the top-3 most-impactful numeric parameters via finite-difference
Sharpe gradient on a quick replay, then runs the 27-cell Cartesian product.

Returns pass percentage: fraction of perturbed runs where the backtest
passes the gate (Sharpe >= 0.5, max DD <= 25%, trades >= 20).

Public API:
    parameter_robustness(manifest, bars, seed) -> float
"""
from __future__ import annotations

import copy
import math
from itertools import product
from typing import Any

from ..backtest import BTConfig, BTResult, BacktestEngine
from ..types import Bar
from .runtime import ManifestAlgo
from .types import AlgoManifest, IndicatorDecl, RiskSpec

_SHARPE_GATE    = 0.5
_MAX_DD_GATE    = 25.0
_MIN_TRADES     = 20
_PERTURB_FRAC   = 0.20   # ±20%
_MAX_PARAMS     = 3      # top-3 most-impactful
_QUICK_BARS     = 50     # bars for finite-difference replay


def _run_backtest(manifest: AlgoManifest, bars: list[Bar], cfg: BTConfig) -> BTResult:
    """Run a backtest using ManifestAlgo."""
    algo   = ManifestAlgo(manifest)
    engine = BacktestEngine(algo, cfg)
    return engine.run(bars, symbol="EURUSD", tf=3600)


def _passes_gate(result: BTResult) -> bool:
    """Return True if result meets the gate criteria."""
    if result.total_trades < _MIN_TRADES:
        return False
    curve = result.equity_curve
    if not curve:
        return False
    # Check for NaN/inf in equity curve
    for v in curve:
        if not math.isfinite(v):
            return False
    if result.sharpe_ratio < _SHARPE_GATE:
        return False
    if result.max_drawdown_pct > _MAX_DD_GATE:
        return False
    return True


def _collect_numeric_params(manifest: AlgoManifest) -> list[tuple[str, float]]:
    """Return (path, value) pairs for all numeric parameters in the manifest.

    Paths use a dotted notation to identify the parameter location, e.g.
    ``"ind.rsi14.period"`` or ``"risk.atr_mult"``.
    """
    params: list[tuple[str, float]] = []

    # Indicator params
    for ind in manifest.indicators:
        for k, v in ind.params.items():
            if isinstance(v, (int, float)) and not isinstance(v, bool):
                params.append((f"ind.{ind.id}.{k}", float(v)))

    # Risk params
    risk = manifest.risk
    if risk.size == "atr":
        params.append(("risk.atr_mult", float(risk.atr_mult)))
    else:
        params.append(("risk.fixed_lots", float(risk.fixed_lots)))
    if risk.cool_down_bars > 0:
        params.append(("risk.cool_down_bars", float(risk.cool_down_bars)))

    return params


def _apply_param(manifest: AlgoManifest, path: str, value: float) -> AlgoManifest:
    """Return a shallow copy of the manifest with the named parameter set to value."""
    import dataclasses

    # Deep copy indicators list
    new_indicators = []
    for ind in manifest.indicators:
        new_params = dict(ind.params)
        key = f"ind.{ind.id}."
        if path.startswith(key):
            param_name = path[len(key):]
            orig_val = ind.params.get(param_name)
            if isinstance(orig_val, int):
                new_params[param_name] = max(1, int(round(value)))
            else:
                new_params[param_name] = value
        new_indicators.append(IndicatorDecl(id=ind.id, kind=ind.kind, params=new_params))

    # Risk
    risk = manifest.risk
    if path == "risk.atr_mult":
        risk = RiskSpec(
            size=risk.size, atr_mult=max(0.1, value),
            fixed_lots=risk.fixed_lots, max_concurrent=risk.max_concurrent,
            hedge=risk.hedge, cool_down_bars=risk.cool_down_bars,
        )
    elif path == "risk.fixed_lots":
        risk = RiskSpec(
            size=risk.size, atr_mult=risk.atr_mult,
            fixed_lots=max(0.01, value), max_concurrent=risk.max_concurrent,
            hedge=risk.hedge, cool_down_bars=risk.cool_down_bars,
        )
    elif path == "risk.cool_down_bars":
        risk = RiskSpec(
            size=risk.size, atr_mult=risk.atr_mult,
            fixed_lots=risk.fixed_lots, max_concurrent=risk.max_concurrent,
            hedge=risk.hedge, cool_down_bars=max(0, int(round(value))),
        )

    return AlgoManifest(
        schema_version = manifest.schema_version,
        name           = manifest.name,
        description    = manifest.description,
        rationale      = manifest.rationale,
        timeframes     = manifest.timeframes,
        symbols        = manifest.symbols,
        indicators     = new_indicators,
        entries        = manifest.entries,
        exits          = manifest.exits,
        risk           = risk,
        code           = manifest.code,
    )


def parameter_robustness(
    manifest: AlgoManifest,
    bars: list[Bar],
    seed: int,
    *,
    cfg: BTConfig | None = None,
) -> float:
    """Compute parameter robustness score (0–100).

    Algorithm:
    1. Collect all numeric parameters.
    2. Run a short 50-bar finite-difference Sharpe gradient to rank parameters
       by impact.
    3. Select the top-3 most-impactful parameters.
    4. Form a 3×3×3 = 27-cell Cartesian product with -20%/0%/+20% perturbations.
    5. Run each cell through a backtest.
    6. Return percentage of cells that pass the gate.

    If the manifest has fewer than 3 numeric parameters, uses all of them
    (resulting in 3^N cells).
    """
    if cfg is None:
        cfg = BTConfig(warmup_bars=min(200, max(20, len(bars) // 4)))

    all_params = _collect_numeric_params(manifest)

    if not all_params:
        # No numeric parameters — perfect robustness by definition
        return 100.0

    # Quick-replay bars (use min of QUICK_BARS, len(bars))
    quick_bars_n = max(cfg.warmup_bars + 20, min(_QUICK_BARS + cfg.warmup_bars, len(bars)))
    quick_bars   = bars[:quick_bars_n]

    # Baseline Sharpe on quick bars
    try:
        baseline_result = _run_backtest(manifest, quick_bars, cfg)
        baseline_sharpe = baseline_result.sharpe_ratio
    except Exception:
        baseline_sharpe = 0.0

    # Finite-difference: compute |dSharpe/dparam| for each parameter
    gradients: list[tuple[float, str, float]] = []  # (|grad|, path, value)

    for path, val in all_params:
        delta = abs(val) * _PERTURB_FRAC if abs(val) > 1e-6 else 0.1

        # Forward perturbation
        try:
            m_plus   = _apply_param(manifest, path, val + delta)
            r_plus   = _run_backtest(m_plus, quick_bars, cfg)
            s_plus   = r_plus.sharpe_ratio
        except Exception:
            s_plus   = baseline_sharpe

        # Backward perturbation
        try:
            m_minus  = _apply_param(manifest, path, val - delta)
            r_minus  = _run_backtest(m_minus, quick_bars, cfg)
            s_minus  = r_minus.sharpe_ratio
        except Exception:
            s_minus  = baseline_sharpe

        grad = abs(s_plus - s_minus) / (2.0 * delta) if delta > 1e-12 else 0.0
        gradients.append((grad, path, val))

    # Sort by gradient descending, pick top-3
    gradients.sort(key=lambda x: x[0], reverse=True)
    top_params = gradients[:_MAX_PARAMS]

    if not top_params:
        return 100.0

    # Cartesian product: -20%, 0%, +20% for each selected parameter
    perturbation_values: list[list[float]] = []
    for _, path, val in top_params:
        delta = abs(val) * _PERTURB_FRAC if abs(val) > 1e-6 else 0.1
        perturbation_values.append([val - delta, val, val + delta])

    # Run all cells on the full bar set
    pass_count = 0
    total      = 0

    for combo in product(*perturbation_values):
        perturbed_manifest = manifest
        for i, (_, path, _) in enumerate(top_params):
            perturbed_manifest = _apply_param(perturbed_manifest, path, combo[i])

        try:
            result = _run_backtest(perturbed_manifest, bars, cfg)
            if _passes_gate(result):
                pass_count += 1
        except Exception:
            pass  # treat as failure

        total += 1

    if total == 0:
        return 0.0

    return pass_count / total * 100.0
