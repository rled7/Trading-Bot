from __future__ import annotations

"""CLI entry point for the AlgoForge analytics module.

Usage::

    python -m algoforge.analytics --backtest path.json --out report.html

The JSON file should have the schema exported by ``python/algoforge/backtest.py``::

    {
        "equity_curve": [[timestamp, equity], ...],
        "trades": [{"net_pnl": ..., "bars_held": ..., "timestamp": ...}, ...],
        "config": {...},
        ...
    }

Optional top-level keys that are used when present:
    "metrics"             -- pre-computed metric dict
    "drawdown_episodes"   -- list of drawdown episode dicts
    "monte_carlo"         -- Monte Carlo result dict
    "walkforward"         -- walk-forward fold list
    "attribution"         -- OLS attribution result
    "factors"             -- factor regression result
"""

import argparse
import json
import sys
from pathlib import Path


def _load_json(path: str) -> dict:
    p = Path(path)
    if not p.exists():
        print(f"error: file not found: {path}", file=sys.stderr)
        sys.exit(1)
    with p.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        prog="python -m algoforge.analytics",
        description="Render an AlgoForge backtest HTML report from a JSON file.",
    )
    parser.add_argument(
        "--backtest",
        required=True,
        metavar="PATH",
        help="Path to the backtest result JSON file.",
    )
    parser.add_argument(
        "--out",
        required=True,
        metavar="PATH",
        help="Output path for the HTML report.",
    )
    parser.add_argument(
        "--title",
        default="AlgoForge Backtest Report",
        metavar="TITLE",
        help="Report title embedded in the HTML (default: 'AlgoForge Backtest Report').",
    )

    args = parser.parse_args(argv)

    # ---- Load backtest data ---------------------------------------------
    data = _load_json(args.backtest)

    equity_curve_raw = data.get("equity_curve", [])
    # Accept [[ts, eq], ...] or [{"timestamp": ts, "equity": eq}, ...]
    equity_curve: list[tuple[int, float]] = []
    for item in equity_curve_raw:
        if isinstance(item, (list, tuple)) and len(item) >= 2:
            equity_curve.append((int(item[0]), float(item[1])))
        elif isinstance(item, dict):
            equity_curve.append((int(item["timestamp"]), float(item["equity"])))

    trades: list[dict] = data.get("trades", [])
    metrics: dict = data.get("metrics", {})
    drawdown_episodes: list = data.get("drawdown_episodes", [])
    monte_carlo = data.get("monte_carlo", None)
    walkforward = data.get("walkforward", None)
    attribution = data.get("attribution", None)
    factors = data.get("factors", None)

    # ---- Lazy import html_report ----------------------------------------
    try:
        from algoforge.analytics.html_report import render_report
    except ImportError as exc:
        print(f"error: could not import html_report: {exc}", file=sys.stderr)
        sys.exit(1)

    # ---- If metrics not provided, try to compute from the data ----------
    if not metrics and equity_curve and trades:
        try:
            from algoforge.analytics.streaming import OnlineMetrics as _OM

            om = _OM(
                initial_capital=float(
                    data.get("config", {}).get("initial_capital", 10_000.0)
                )
            )
            for t in trades:
                om.update(t)
            metrics = om.snapshot()
        except Exception:  # noqa: BLE001
            pass

    # ---- Render ----------------------------------------------------------
    html = render_report(
        equity_curve=equity_curve,
        trades=trades,
        metrics=metrics,
        drawdown_episodes=drawdown_episodes,
        monte_carlo=monte_carlo,
        walkforward=walkforward,
        attribution=attribution,
        factors=factors,
        title=args.title,
    )

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as fh:
        fh.write(html)

    print(f"report written to {out_path}")


if __name__ == "__main__":
    main()
