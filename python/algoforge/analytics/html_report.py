from __future__ import annotations

"""HTML report generator for AlgoForge backtests.

Spec reference: analytics_spec.md §4 "HTML Report".

Usage::

    html = render_report(
        equity_curve=[(ts, eq), ...],
        trades=[{"net_pnl": ..., "bars_held": ..., "timestamp": ...}, ...],
        metrics={"sharpe": ..., "win_rate": ..., ...},
        drawdown_episodes=[...],
    )
    with open("report.html", "w") as f:
        f.write(html)

The CSS constant ``REPORT_CSS`` and the chart-init JS constant
``CHART_INIT_JS_TEMPLATE`` are factored out at module level so that C/C++/JS
ports can copy them verbatim.
"""

import json
from typing import Any, Optional

# ---------------------------------------------------------------------------
# CSS — dark monochrome, monospace, identical across all 4 language ports
# ---------------------------------------------------------------------------

REPORT_CSS: str = """
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  background: #0d0d0d;
  color: #e0e0e0;
  font-family: 'Courier New', Courier, monospace;
  font-size: 14px;
  line-height: 1.5;
  padding: 24px;
}
h1 {
  font-size: 22px;
  color: #ffffff;
  border-bottom: 1px solid #333;
  padding-bottom: 8px;
  margin-bottom: 24px;
}
h2 {
  font-size: 16px;
  color: #aaaaaa;
  margin: 24px 0 12px;
  text-transform: uppercase;
  letter-spacing: 1px;
}
section {
  margin-bottom: 40px;
}
table {
  width: 100%;
  border-collapse: collapse;
  margin-top: 8px;
}
th, td {
  text-align: left;
  padding: 6px 12px;
  border-bottom: 1px solid #222;
}
th {
  color: #888;
  font-weight: normal;
  text-transform: uppercase;
  font-size: 11px;
  letter-spacing: 0.5px;
}
tr:hover td {
  background: #1a1a1a;
}
canvas {
  max-width: 100%;
  background: #111;
  border: 1px solid #222;
  border-radius: 4px;
  margin-top: 8px;
}
.metric-positive { color: #4caf50; }
.metric-negative { color: #f44336; }
.metric-neutral  { color: #e0e0e0; }
""".strip()

# ---------------------------------------------------------------------------
# Chart init JS template — placeholders replaced at render time.
# The surrounding <script> tags are added by the renderer.
# This string is identical across all 4 language ports.
# Placeholders: {eq_labels}, {eq_data}, {dd_data},
#               {pnl_hist_labels}, {pnl_hist_data},
#               {mc_p5}, {mc_p50}, {mc_p95}
# ---------------------------------------------------------------------------

CHART_INIT_JS_TEMPLATE: str = r"""
(function() {
  var eqLabels  = {eq_labels};
  var eqData    = {eq_data};
  var ddData    = {dd_data};
  var histLabels= {pnl_hist_labels};
  var histData  = {pnl_hist_data};
  var mcP5      = {mc_p5};
  var mcP50     = {mc_p50};
  var mcP95     = {mc_p95};

  var chartDefaults = {
    responsive: true,
    animation: false,
    plugins: { legend: { labels: { color: '#aaa' } } },
    scales: {
      x: { ticks: { color: '#666' }, grid: { color: '#1a1a1a' } },
      y: { ticks: { color: '#666' }, grid: { color: '#1a1a1a' } }
    }
  };

  function mergeOpts(extra) {
    return Object.assign({}, chartDefaults, extra || {});
  }

  // Equity curve chart
  var eqEl = document.getElementById('eq');
  if (eqEl) {
    new Chart(eqEl, {
      type: 'line',
      data: {
        labels: eqLabels,
        datasets: [{
          label: 'Equity',
          data: eqData,
          borderColor: '#4caf50',
          backgroundColor: 'rgba(76,175,80,0.1)',
          borderWidth: 1.5,
          pointRadius: 0,
          fill: true
        }]
      },
      options: mergeOpts()
    });
  }

  // Drawdown chart
  var ddEl = document.getElementById('dd');
  if (ddEl) {
    new Chart(ddEl, {
      type: 'line',
      data: {
        labels: eqLabels,
        datasets: [{
          label: 'Drawdown %',
          data: ddData,
          borderColor: '#f44336',
          backgroundColor: 'rgba(244,67,54,0.15)',
          borderWidth: 1.5,
          pointRadius: 0,
          fill: true
        }]
      },
      options: mergeOpts()
    });
  }

  // PnL histogram
  var histEl = document.getElementById('pnl-hist');
  if (histEl) {
    new Chart(histEl, {
      type: 'bar',
      data: {
        labels: histLabels,
        datasets: [{
          label: 'Trade PnL frequency',
          data: histData,
          backgroundColor: 'rgba(100,181,246,0.7)',
          borderColor: '#64b5f6',
          borderWidth: 1
        }]
      },
      options: mergeOpts()
    });
  }

  // Monte Carlo fan chart
  var mcEl = document.getElementById('mc');
  if (mcEl && mcP5 && mcP5.length > 0) {
    var mcLabels = mcP50.map(function(_, i) { return i; });
    new Chart(mcEl, {
      type: 'line',
      data: {
        labels: mcLabels,
        datasets: [
          {
            label: 'p95',
            data: mcP95,
            borderColor: '#4caf50',
            backgroundColor: 'rgba(76,175,80,0.05)',
            borderWidth: 1,
            pointRadius: 0,
            fill: false
          },
          {
            label: 'p50',
            data: mcP50,
            borderColor: '#ffb300',
            backgroundColor: 'rgba(255,179,0,0.08)',
            borderWidth: 1.5,
            pointRadius: 0,
            fill: false
          },
          {
            label: 'p5',
            data: mcP5,
            borderColor: '#f44336',
            backgroundColor: 'rgba(244,67,54,0.05)',
            borderWidth: 1,
            pointRadius: 0,
            fill: false
          }
        ]
      },
      options: mergeOpts()
    });
  }
})();
""".strip()


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _pnl_histogram(
    pnls: list[float],
    n_bins: int = 20,
) -> tuple[list[str], list[int]]:
    """Compute a simple equal-width histogram of PnL values.

    Returns (bin_labels, counts).
    """
    if not pnls:
        return [], []
    lo, hi = min(pnls), max(pnls)
    if lo == hi:
        return [f"{lo:.2f}"], [len(pnls)]
    width = (hi - lo) / n_bins
    counts = [0] * n_bins
    labels: list[str] = []
    for i in range(n_bins):
        labels.append(f"{lo + i * width:.2f}")
    for v in pnls:
        idx = int((v - lo) / width)
        if idx >= n_bins:
            idx = n_bins - 1
        counts[idx] += 1
    return labels, counts


def _dd_series(equity_curve: list[tuple[int, float]]) -> list[float]:
    """Compute per-bar drawdown percentage from equity curve pairs.

    Returns list[float] of dd_pct values (each <= 0).
    """
    if not equity_curve:
        return []
    peak = equity_curve[0][1]
    result: list[float] = []
    for _, eq in equity_curve:
        if eq > peak:
            peak = eq
        dd = (eq - peak) / peak * 100.0 if peak != 0.0 else 0.0
        result.append(dd)
    return result


def _fmt_metric(key: str, value: Any) -> str:
    """Format a metric value for the HTML table."""
    if isinstance(value, float):
        if value == float("inf"):
            return "inf"
        if value == float("-inf"):
            return "-inf"
        return f"{value:.4f}"
    return str(value)


def _css_class(key: str, value: Any) -> str:
    """Return a CSS class for coloring metric values."""
    positive_keys = {
        "sharpe", "sortino", "calmar", "omega", "profit_factor",
        "win_rate", "expectancy", "avg_win", "payoff_ratio",
        "recovery_factor", "longest_win_streak",
    }
    negative_keys = {"avg_loss", "longest_loss_streak", "max_consec_losses"}
    if key in positive_keys:
        if isinstance(value, (int, float)) and value > 0:
            return "metric-positive"
        if isinstance(value, (int, float)) and value < 0:
            return "metric-negative"
    if key in negative_keys:
        if isinstance(value, (int, float)) and value < 0:
            return "metric-negative"
    return "metric-neutral"


# ---------------------------------------------------------------------------
# Main render function
# ---------------------------------------------------------------------------

def render_report(
    *,
    equity_curve: list[tuple[int, float]],
    trades: list[dict[str, Any]],
    metrics: dict[str, Any],
    drawdown_episodes: list[Any],
    monte_carlo: Optional[dict[str, Any]] = None,
    walkforward: Optional[list[dict[str, Any]]] = None,
    attribution: Optional[dict[str, Any]] = None,
    factors: Optional[dict[str, Any]] = None,
    title: str = "AlgoForge Backtest Report",
) -> str:
    """Render a self-contained HTML backtest report.

    Parameters
    ----------
    equity_curve:
        List of ``(timestamp_unix_seconds, equity_usd)`` pairs in ascending
        timestamp order.
    trades:
        List of trade dicts, each with at least ``net_pnl``.
    metrics:
        Dict of computed metrics (14 from spec §4 plus any extras).
    drawdown_episodes:
        List of drawdown episode objects (dataclasses or dicts).
    monte_carlo:
        Optional dict with keys ``p5_path``, ``p50_path``, ``p95_path``,
        ``p5_final``, ``p50_final``, ``p95_final``, ``prob_of_ruin``.
    walkforward:
        Optional list of fold result dicts.
    attribution:
        Optional OLS attribution result dict (``beta``, ``r2``, ``t_stats``…).
    factors:
        Optional factor regression result dict (``alpha``, betas, ``r2``…).
    title:
        HTML ``<title>`` and ``<h1>`` content.

    Returns
    -------
    str
        Complete self-contained HTML document.
    """
    # ---- equity curve labels & data ------------------------------------
    eq_labels: list[str] = [str(ts) for ts, _ in equity_curve]
    eq_data: list[float] = [eq for _, eq in equity_curve]
    dd_data: list[float] = _dd_series(equity_curve)

    # ---- PnL histogram --------------------------------------------------
    pnls = [float(t.get("net_pnl", 0.0)) for t in trades]
    hist_labels, hist_data = _pnl_histogram(pnls)

    # ---- Monte Carlo data -----------------------------------------------
    mc_p5: list[float] = []
    mc_p50: list[float] = []
    mc_p95: list[float] = []
    if monte_carlo:
        mc_p5 = monte_carlo.get("p5_path", [])
        mc_p50 = monte_carlo.get("p50_path", [])
        mc_p95 = monte_carlo.get("p95_path", [])

    # ---- Build chart JS -------------------------------------------------
    chart_js = (
        CHART_INIT_JS_TEMPLATE
        .replace("{eq_labels}", json.dumps(eq_labels))
        .replace("{eq_data}", json.dumps(eq_data))
        .replace("{dd_data}", json.dumps(dd_data))
        .replace("{pnl_hist_labels}", json.dumps(hist_labels))
        .replace("{pnl_hist_data}", json.dumps(hist_data))
        .replace("{mc_p5}", json.dumps(mc_p5))
        .replace("{mc_p50}", json.dumps(mc_p50))
        .replace("{mc_p95}", json.dumps(mc_p95))
    )

    # ---- Section: summary metrics table ---------------------------------
    metric_order = [
        "sharpe", "sortino", "calmar", "omega",
        "profit_factor", "win_rate", "expectancy",
        "avg_win", "avg_loss", "payoff_ratio",
        "recovery_factor", "time_in_market",
        "longest_win_streak", "longest_loss_streak",
    ]
    summary_rows = ""
    for key in metric_order:
        val = metrics.get(key, "N/A")
        css = _css_class(key, val)
        formatted = _fmt_metric(key, val)
        summary_rows += (
            f'<tr><td>{key.replace("_", " ").title()}</td>'
            f'<td class="{css}">{formatted}</td></tr>\n'
        )
    # Drawdown summary
    for key in ("max_dd_abs", "max_dd_pct", "current_dd_pct"):
        val = metrics.get(key, "N/A")
        css = "metric-negative" if isinstance(val, float) and val < 0 else "metric-neutral"
        formatted = _fmt_metric(key, val)
        label = key.replace("_", " ").title()
        summary_rows += (
            f'<tr><td>{label}</td>'
            f'<td class="{css}">{formatted}</td></tr>\n'
        )

    # ---- Section: top drawdowns table -----------------------------------
    top_dd_rows = ""
    for ep in drawdown_episodes[:5]:
        if hasattr(ep, "__dict__"):
            d = ep.__dict__
        elif isinstance(ep, dict):
            d = ep
        else:
            continue
        start = d.get("start_idx", "")
        trough = d.get("trough_idx", "")
        recover = d.get("recover_idx", "N/A")
        depth_pct = d.get("depth_pct", 0.0)
        depth_abs = d.get("depth_abs", 0.0)
        duration = d.get("duration_bars", "")
        recovery = d.get("recovery_bars", "N/A")
        top_dd_rows += (
            f"<tr>"
            f"<td>{start}</td><td>{trough}</td><td>{recover}</td>"
            f'<td class="metric-negative">{depth_pct:.4f}%</td>'
            f'<td class="metric-negative">{depth_abs:.2f}</td>'
            f"<td>{duration}</td><td>{recovery}</td>"
            f"</tr>\n"
        )

    # ---- Section: walk-forward folds table ------------------------------
    wf_section = ""
    if walkforward:
        wf_rows = ""
        for i, fold in enumerate(walkforward):
            train_m = fold.get("train", {})
            test_m = fold.get("test", {})
            wf_rows += (
                f"<tr><td>Fold {i + 1}</td>"
                f"<td>{_fmt_metric('sharpe', train_m.get('sharpe', 'N/A'))}</td>"
                f"<td>{_fmt_metric('sharpe', test_m.get('sharpe', 'N/A'))}</td>"
                f"<td>{_fmt_metric('win_rate', train_m.get('win_rate', 'N/A'))}</td>"
                f"<td>{_fmt_metric('win_rate', test_m.get('win_rate', 'N/A'))}</td>"
                f"</tr>\n"
            )
        wf_section = f"""
<section id="walkforward">
  <h2>Walk-Forward Analysis</h2>
  <table>
    <thead>
      <tr>
        <th>Fold</th>
        <th>Train Sharpe</th><th>Test Sharpe</th>
        <th>Train Win Rate</th><th>Test Win Rate</th>
      </tr>
    </thead>
    <tbody>{wf_rows}</tbody>
  </table>
</section>
"""

    # ---- Section: OLS attribution ---------------------------------------
    attr_section = ""
    if attribution:
        r2 = attribution.get("r2", "N/A")
        adj_r2 = attribution.get("adj_r2", "N/A")
        beta = attribution.get("beta", [])
        t_stats = attribution.get("t_stats", [])
        attr_rows = ""
        for i, (b, t) in enumerate(zip(beta, t_stats)):
            attr_rows += (
                f"<tr><td>Feature {i}</td>"
                f"<td>{b:.6f}</td><td>{t:.4f}</td></tr>\n"
            )
        attr_section = f"""
<section id="attribution">
  <h2>ML Attribution (OLS)</h2>
  <table>
    <thead>
      <tr><th>Feature</th><th>Beta</th><th>t-stat</th></tr>
    </thead>
    <tbody>{attr_rows}</tbody>
  </table>
  <p>R&sup2; = {_fmt_metric('r2', r2)} &nbsp; Adj-R&sup2; = {_fmt_metric('adj_r2', adj_r2)}</p>
</section>
"""

    # ---- Section: factor loadings ---------------------------------------
    factors_section = ""
    if factors:
        alpha = factors.get("alpha", "N/A")
        r2 = factors.get("r2", "N/A")
        betas = factors.get("betas", {})
        fac_rows = ""
        for fname, bval in betas.items():
            fac_rows += f"<tr><td>{fname}</td><td>{_fmt_metric('beta', bval)}</td></tr>\n"
        factors_section = f"""
<section id="factors">
  <h2>Factor Loadings (Fama-French Style)</h2>
  <table>
    <thead>
      <tr><th>Factor</th><th>Beta</th></tr>
    </thead>
    <tbody>{fac_rows}</tbody>
  </table>
  <p>Alpha = {_fmt_metric('alpha', alpha)} &nbsp; R&sup2; = {_fmt_metric('r2', r2)}</p>
</section>
"""

    # ---- Assemble HTML -------------------------------------------------
    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{title}</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
  <style>{REPORT_CSS}</style>
</head>
<body>
  <h1>{title}</h1>

  <section id="summary">
    <h2>Performance Summary</h2>
    <table>
      <thead>
        <tr><th>Metric</th><th>Value</th></tr>
      </thead>
      <tbody>
        {summary_rows}
      </tbody>
    </table>
  </section>

  <section id="equity-curve">
    <h2>Equity Curve</h2>
    <canvas id="eq" height="80"></canvas>
  </section>

  <section id="drawdown">
    <h2>Drawdown</h2>
    <canvas id="dd" height="60"></canvas>
  </section>

  <section id="trade-distribution">
    <h2>Trade PnL Distribution</h2>
    <canvas id="pnl-hist" height="60"></canvas>
  </section>

  <section id="monte-carlo">
    <h2>Monte Carlo Simulation</h2>
    <canvas id="mc" height="80"></canvas>
  </section>

  <section id="top-drawdowns">
    <h2>Top 5 Drawdown Episodes</h2>
    <table>
      <thead>
        <tr>
          <th>Start</th><th>Trough</th><th>Recover</th>
          <th>Depth %</th><th>Depth Abs</th>
          <th>Duration</th><th>Recovery Bars</th>
        </tr>
      </thead>
      <tbody>
        {top_dd_rows}
      </tbody>
    </table>
  </section>

  {wf_section}

  {attr_section}

  {factors_section}

  <script>
    {chart_js}
  </script>
</body>
</html>"""

    return html
