# AlgoForge Analytics — Cross-Language Specification (S4)

This document is the **single source of truth** for the S4 analytics layer.
All four language implementations (C, C++, Python, JS) must produce
numerically identical results given identical inputs (within 1e-9 absolute
tolerance for floats, 1e-12 for cumulative sums).

The spec is binding. Implementations consult the spec, not each other.

## 1. Scope

| Module | C / C++ / JS / Py | Notes |
|---|---|---|
| metrics | ✓ all 4 | 14 metrics, deterministic numerical |
| drawdown | ✓ all 4 | Series + summary |
| distributions | ✓ all 4 | Skew/kurtosis/quantiles |
| correlations | ✓ all 4 | Pearson matrix, rolling beta |
| montecarlo | ✓ all 4 | Bootstrap with seeded RNG |
| walkforward | ✓ all 4 | Anchored + rolling harness |
| ml_attribution | ✓ all 4 | OLS linear regression + R² only |
| factors | ✓ all 4 | OLS factor regression (Fama-French style) |
| streaming | ✓ all 4 | Online/incremental metric updates |
| dashboard SSE | Py only | Hangs off existing FastAPI dashboard (S3) |
| html_report | ✓ all 4 | Self-contained HTML; identical template across langs |

## 2. Module layout

### Python
```
python/algoforge/analytics/
  __init__.py          (re-exports)
  types.py             (TradeReturn, EquityPoint, AnalyticsResult dataclasses)
  metrics.py
  drawdown.py
  distributions.py
  correlations.py
  montecarlo.py
  walkforward.py
  ml_attribution.py
  factors.py
  streaming.py         (OnlineMetrics class)
  dashboard.py         (FastAPI router — Python only)
  html_report.py
  __main__.py          (CLI)
```

### C
```
c/include/af_analytics.h   (single header, all structs + function prototypes)
c/src/analytics.c          (implementations of all submodules)
c/tests/test_analytics.c
```
Suffix module names with prefixes: `af_metrics_sharpe`, `af_dd_series`, `af_mc_run`, `af_factors_ols`, etc.

### C++
```
cpp/include/core/analytics.hpp  (namespace algoforge::analytics, all classes)
cpp/src/analytics/metrics.cpp
cpp/src/analytics/drawdown.cpp
cpp/src/analytics/distributions.cpp
cpp/src/analytics/correlations.cpp
cpp/src/analytics/montecarlo.cpp
cpp/src/analytics/walkforward.cpp
cpp/src/analytics/ml_attribution.cpp
cpp/src/analytics/factors.cpp
cpp/src/analytics/streaming.cpp
cpp/src/analytics/html_report.cpp
cpp/tests/test_analytics.cpp
```

### JS
```
js/src/analytics/
  index.js          (re-exports)
  metrics.js
  drawdown.js
  distributions.js
  correlations.js
  montecarlo.js
  walkforward.js
  ml_attribution.js
  factors.js
  streaming.js
  html_report.js
js/test/test_analytics.js
```

## 3. Input data shapes

All metrics consume one or both of:

**TradeReturn series** — array of trade net-PnL values (USD or unit-less).

**EquityPoint series** — array of `(timestamp_unix_seconds, equity_usd)` pairs,
strictly increasing in timestamp. Equity starts at `initial_capital`.

Derived series used by many metrics:
- **bar_returns** = `[(eq[i] - eq[i-1]) / eq[i-1] for i in 1..n]` — fractional
  per-bar returns from the equity curve.
- **trade_returns_pct** = same as TradeReturn but divided by initial capital.

## 4. Metric formulas

All formulas use **sample** standard deviation (N-1 denominator) unless
specified. Annualisation factor `A = sqrt(252)` (assume daily bars; the
caller supplies a different factor via parameter if needed).

| Metric | Formula |
|---|---|
| **Sharpe** | `A * mean(bar_returns) / std(bar_returns)` — 0 if std == 0 or n < 2 |
| **Sortino** | `A * mean(bar_returns) / std(min(r, 0) for r in bar_returns)` — 0 if downside std == 0 |
| **Calmar** | `total_return_pct / max_drawdown_pct` — 0 if mdd == 0 |
| **Omega** | `sum(max(r-thr,0)) / sum(max(thr-r,0))` over bar_returns; threshold = 0 |
| **profit_factor** | `sum(winning_trade_pnls) / abs(sum(losing_trade_pnls))` — inf if no losses, 0 if no wins |
| **win_rate** | `count(t.net_pnl > 0) / count(trades)` |
| **expectancy** | `mean(t.net_pnl)` |
| **avg_win** | `mean(t.net_pnl for t where t.net_pnl > 0)` — 0 if no winners |
| **avg_loss** | `mean(t.net_pnl for t where t.net_pnl < 0)` (returned negative) — 0 if no losers |
| **payoff_ratio** | `avg_win / abs(avg_loss)` — inf if no losers |
| **recovery_factor** | `total_pnl / max_drawdown_abs` — 0 if mdd_abs == 0 |
| **time_in_market** | `sum(bars_held over all trades) / total_bars_in_backtest` |
| **longest_win_streak** | longest run of consecutive `net_pnl > 0` trades |
| **longest_loss_streak** | longest run of consecutive `net_pnl <= 0` trades |
| **max_consec_losses** | same as `longest_loss_streak` (alias for ergonomics) |

### Drawdown

Given an equity curve `eq[0..n-1]`:

- `peak[i] = max(eq[0..i])`
- `dd_pct[i] = (eq[i] - peak[i]) / peak[i] * 100` (≤ 0)
- `dd_abs[i] = eq[i] - peak[i]`
- `max_dd_pct = min(dd_pct)` (most negative)
- `max_dd_abs = min(dd_abs)`

**Drawdown episodes**: contiguous runs of `dd_pct < 0`. For each episode
record: `(start_idx, trough_idx, recover_idx, depth_pct, depth_abs,
duration_bars, recovery_bars)`. `recover_idx` is the first index where
`eq[i] >= peak[trough_idx]`; `None`/`-1` if not recovered by end of series.

`top5_drawdowns(eq) -> list[Episode]` — sorted by `depth_pct` ascending
(deepest first), returns up to 5.

### Distributions

Sample skewness (Fisher-Pearson, adjusted):
```
g1 = (1/n) * sum((x-mean)^3) / std_biased^3
G1 = sqrt(n*(n-1)) / (n-2) * g1     # Fisher-Pearson adjusted
```

Excess kurtosis (Fisher's definition, adjusted for sample):
```
g2 = (1/n) * sum((x-mean)^4) / std_biased^4 - 3
G2 = ((n-1)/((n-2)(n-3))) * ((n+1)*g2 + 6)
```

For n < 4, return 0 for both.

Quantiles: linear interpolation (numpy `method='linear'` / R-7).

### Correlations

Pearson correlation between two same-length series:
```
r = sum((x-mx)*(y-my)) / sqrt(sum((x-mx)^2) * sum((y-my)^2))
```

`correlation_matrix(returns_per_symbol: dict[str, list[float]]) -> Matrix`
— pads to common length by truncating to shortest series.

`rolling_beta(target, baseline, window) -> list[float]` — `cov(x,y)/var(y)`
on a sliding window of size `window`.

### Monte Carlo

Bootstrap resampling of trade returns:

```
function mc_bootstrap(trades, n_runs, seed):
    rng = SeededLCG(seed)              # see RNG spec below
    final_eq = []
    paths = []
    for run in 1..n_runs:
        resample = [trades[rng.next() % len(trades)] for _ in 1..len(trades)]
        eq = initial_capital
        path = [eq]
        for t in resample:
            eq += t.net_pnl
            path.append(eq)
        final_eq.append(eq)
        paths.append(path)
    return {
        p5_final: percentile(final_eq, 5),
        p50_final: percentile(final_eq, 50),
        p95_final: percentile(final_eq, 95),
        p5_path: pointwise_percentile(paths, 5),
        p50_path: pointwise_percentile(paths, 50),
        p95_path: pointwise_percentile(paths, 95),
        prob_of_ruin: count(run where min(path) <= 0) / n_runs,
    }
```

**Seeded LCG** (must match across languages):
```
state = seed (uint64)
next(): state = state * 6364136223846793005 + 1442695040888963407 (wrap 64-bit)
        return state >> 33   (uint32)
```

### Walk-forward

`walkforward_anchored(backtest_fn, bars, n_folds, train_min_bars)`:
- Splits bars into n_folds; for each fold k:
  - train slice = `bars[0 : train_min_bars + k * (n - train_min_bars) // n_folds]`
  - test slice  = bars immediately following, length `(n - train_min_bars) // n_folds`
  - call `backtest_fn(train_bars, test_bars) -> {train_metrics, test_metrics}`
- Returns list of fold results.

`walkforward_rolling(backtest_fn, bars, train_window, test_window, step)`:
- Sliding window: train = `bars[i : i+train_window]`, test = `bars[i+train_window : i+train_window+test_window]`, step `i` by `step` each fold.

`backtest_fn` is a callback supplied by caller; signature
`(train_bars, test_bars) -> {train: {metrics...}, test: {metrics...}}`.

### ML Attribution

Single algorithm: **Ordinary Least Squares (OLS) linear regression** with R²,
adjusted R², and per-feature t-statistics. No sklearn dependency in any
language — implement from scratch with manual matrix algebra.

```
ols(X, y) -> {beta, residuals, r2, adj_r2, t_stats, std_errors}
  X: n×k matrix (with intercept column prepended)
  y: n-vector
  beta = (X^T X)^-1 X^T y
  y_hat = X @ beta
  ss_res = sum((y - y_hat)^2)
  ss_tot = sum((y - mean(y))^2)
  r2 = 1 - ss_res/ss_tot
  adj_r2 = 1 - (1-r2) * (n-1)/(n-k)
  sigma_sq = ss_res / (n-k)
  var_beta = sigma_sq * inv(X^T X)
  std_err[j] = sqrt(var_beta[j,j])
  t_stat[j] = beta[j] / std_err[j]
```

Matrix inverse via Gauss-Jordan elimination (no external libs).

Attribution use: feed in feature columns (e.g. indicator values at trade
entry) as X, trade PnL as y. `r2` tells how much of trade variance is
explained by features; `t_stats` rank feature importance.

### Factors

Fama-French-style **multi-factor OLS regression**:
```
strategy_returns = alpha + sum(beta_f * factor_returns_f) + epsilon
```
Uses the same OLS solver. Inputs: strategy bar returns + a dict of
factor return series (same length). Outputs: alpha (intercept), beta per
factor, t-stats, R².

### Streaming (online metrics)

`OnlineMetrics` class maintains running state and accepts trades one at a
time. After each `update(trade)`, snapshot of current metrics is
available via `snapshot()`.

State tracked:
- `n_trades`, `n_wins`, `n_losses`
- `sum_pnl`, `sum_pnl_sq` (for mean + variance via Welford)
- `sum_winning_pnl`, `sum_losing_pnl_abs`
- `current_streak` (signed), `longest_win_streak`, `longest_loss_streak`
- `peak_equity`, `max_dd_abs`, `max_dd_pct`
- `running_equity`

`snapshot() -> dict` returns: all 14 metrics from §4 plus drawdown stats.

Numerical method: **Welford's online algorithm** for mean/variance:
```
update_welford(x):
    n += 1
    delta = x - mean
    mean += delta / n
    m2 += delta * (x - mean)        # use new mean
    # variance = m2 / (n-1)
```

### HTML Report

Each language emits a single self-contained `report.html` file with this
exact structure:

```html
<!DOCTYPE html>
<html><head>
  <meta charset="utf-8">
  <title>AlgoForge Backtest Report — {symbol}</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
  <style>{embedded CSS, ~100 lines, dark monospace, same across all 4 langs}</style>
</head><body>
  <h1>AlgoForge Backtest Report</h1>
  <section id="summary">…14 metrics + drawdown summary as a table…</section>
  <section id="equity-curve"><canvas id="eq"></canvas></section>
  <section id="drawdown"><canvas id="dd"></canvas></section>
  <section id="trade-distribution"><canvas id="pnl-hist"></canvas></section>
  <section id="monte-carlo"><canvas id="mc"></canvas></section>
  <section id="top-drawdowns">…table of top 5 drawdown episodes…</section>
  <section id="walkforward">…fold-by-fold metrics table (if present)…</section>
  <section id="attribution">…OLS coefficients + R² (if present)…</section>
  <section id="factors">…factor loadings + alpha (if present)…</section>
  <script>{Chart.js init scripts referencing embedded JSON data}</script>
</body></html>
```

All chart data is embedded as inline JSON (no external file deps). The
CSS and chart-init JS are identical strings across the 4 language
implementations — pull them from `docs/analytics_report_template.md` once
created (Phase 1 supervisor task).

### Dashboard SSE (Python only)

New endpoints under existing FastAPI app:
- `GET /api/analytics/snapshot` — returns current OnlineMetrics snapshot as JSON.
- `GET /api/analytics/stream` — SSE stream emitting snapshots whenever a
  new trade is recorded. (For v1, poll every 1s and emit if changed.)

C / C++ / JS streaming modules expose the `OnlineMetrics` class but no
HTTP server — they're consumed by application code, not over the network.

## 5. Canonical test vectors

All 4 implementations must produce these outputs to within 1e-9 absolute
tolerance.

### Test series A — 10 trades

`trades_A = [+120, -50, +80, -30, +200, -100, -40, +60, +90, -70]`

Expected:
- `sum_pnl = 260`
- `n_wins = 5`, `n_losses = 5`
- `win_rate = 0.5`
- `expectancy = 26.0`
- `avg_win = 110.0`, `avg_loss = -58.0`
- `payoff_ratio = 110.0 / 58.0 = 1.8965517241379310`
- `profit_factor = 550.0 / 290.0 = 1.8965517241379310`
- `longest_win_streak = 2` (trades 7 and 8: `+60, +90`)
- `longest_loss_streak = 2` (trades 5 and 6: `-100, -40`)

### Test series B — bar_returns (n=8)

`returns_B = [0.01, -0.005, 0.02, -0.01, 0.015, 0.003, -0.008, 0.012]`

- `mean = 0.004625`
- `std (sample, N-1) = 0.011312919...`
- Sharpe (A = sqrt(252)) = `sqrt(252) * 0.004625 / 0.011312919... = 6.489889744758411`
  *(All four implementations must produce this value to 1e-9 tolerance.)*
- Sortino: only negative entries `[-0.005, -0.01, -0.008]`, downside std (sample, demeaned at 0 per common Sortino convention) = compute precisely.
  *Convention*: downside std = `sqrt(sum(min(r,0)^2) / (n-1))` using **n = full sample size**, demeaned at 0 (not at mean). All 4 implementations must follow this convention.

### Test series C — equity curve

`eq_C = [10000, 10100, 10050, 10200, 10150, 9800, 9900, 10300, 10400]`

- `peaks_C = [10000, 10100, 10100, 10200, 10200, 10200, 10200, 10300, 10400]`
- `max_dd_pct = (9800 - 10200) / 10200 * 100 = -3.9215686274509807`
- `max_dd_abs = -400`
- Drawdown episodes: **two** distinct episodes (contiguous runs of `dd_pct < 0`):
  1. Brief dip at idx 2 only (start_idx=2, trough=2, recover_idx=3, depth_abs=-50).
  2. Deeper episode at idx 4–6 (start_idx=4, trough=5, recover_idx=7, depth_abs=-400).
- `top5_drawdowns` returns the deeper episode first (sorted by depth_pct ascending).

### Test series D — Monte Carlo

Inputs: `trades_A`, `n_runs=1000`, `seed=42`, `initial_capital=10000`.

Canonical LCG(seed=42) first 10 uint32 outputs (all four implementations
must reproduce these exactly):
```
[1220265334, 484179026, 886563538, 1353769503, 1460606294,
 56326156,   46730969,  327394710, 1017823166, 53256125]
```

Canonical Monte Carlo result for the above inputs:
- `p5_final  = 9800.0`
- `p50_final = 10250.0`
- `p95_final = 10750.5`
- `prob_of_ruin = 0.0`

## 6. API conventions per language

### Python
- Type hints throughout, `from __future__ import annotations` at top.
- Dataclasses for result types.
- Snake case.
- All functions pure unless explicitly stateful (OnlineMetrics).

### C
- Functions prefixed `af_`.
- All public types in `af_analytics.h`.
- Out-params via pointers, return `af_error_t` for fallible ops.
- Caller-supplied arrays + count + capacity for outputs.
- No malloc in hot paths; caller owns memory.

### C++
- Namespace `algoforge::analytics`.
- `std::span<const double>` for input ranges.
- Return `struct` aggregates by value.
- `noexcept` where applicable.

### JS
- ES module (`import`/`export`).
- camelCase function names.
- Plain objects for results.
- No external deps for math; Chart.js only in HTML report.

## 7. Testing

Each language's test suite must include:
- Unit tests for each metric against the canonical test vectors in §5.
- Edge cases: empty series, single trade, all-wins, all-losses, NaN/inf inputs.
- Reproducibility: same seed → same Monte Carlo output.
- Cross-language parity: a `benchmarks/analytics_parity.sh` script (Phase 3
  supervisor deliverable) runs the canonical vectors through each language
  and diffs outputs to confirm 1e-9 tolerance.

## 8. Non-goals

- No live trading via analytics — analytics observe, never trade.
- No GUI beyond the static HTML report.
- No persistent storage — caller owns data.
- No GPU/SIMD optimizations in v1; correctness first.
