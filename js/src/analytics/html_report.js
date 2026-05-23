/**
 * html_report.js — renderReport() returning a self-contained HTML string.
 * Spec §4 (HTML Report section).
 * Chart.js 4 is loaded from CDN; all data is embedded as inline JSON.
 */

// ── Embedded CSS ──────────────────────────────────────────────────────────────
const REPORT_CSS = `
* { box-sizing: border-box; margin: 0; padding: 0; }
body { background: #0d0d0d; color: #e0e0e0; font-family: 'Courier New', Courier, monospace; font-size: 13px; padding: 20px; }
h1 { font-size: 1.4em; color: #88ccff; margin-bottom: 16px; border-bottom: 1px solid #333; padding-bottom: 8px; }
h2 { font-size: 1.1em; color: #aaddff; margin: 20px 0 10px; }
section { margin-bottom: 32px; }
table { border-collapse: collapse; width: 100%; max-width: 700px; }
th, td { border: 1px solid #333; padding: 5px 10px; text-align: left; }
th { background: #1a1a2e; color: #88ccff; }
tr:nth-child(even) td { background: #111; }
.canvas-wrap { max-width: 860px; background: #111; border: 1px solid #333; border-radius: 4px; padding: 12px; margin-top: 8px; }
canvas { max-height: 280px; }
.metric-val { color: #7fff7f; }
.metric-neg { color: #ff7f7f; }
.na { color: #666; }
`;

// ── Helper: escape HTML entities ──────────────────────────────────────────────
function esc(v) {
    if (v === null || v === undefined) return '<span class="na">n/a</span>';
    return String(v)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;');
}

function fmtNum(v, digits = 4) {
    if (v === null || v === undefined || isNaN(v)) return '<span class="na">n/a</span>';
    if (!isFinite(v)) return v > 0 ? 'Inf' : '-Inf';
    const cls = v < 0 ? 'metric-neg' : 'metric-val';
    return `<span class="${cls}">${Number(v).toFixed(digits)}</span>`;
}

// ── Chart init scripts ────────────────────────────────────────────────────────
function equityCurveScript(equityCurve) {
    const labels = equityCurve.map((_, i) => i);
    const data   = JSON.stringify(equityCurve);
    return `
new Chart(document.getElementById('eq'), {
  type: 'line',
  data: { labels: ${JSON.stringify(labels)}, datasets: [{ label: 'Equity', data: ${data}, borderColor: '#4488ff', borderWidth: 1.5, pointRadius: 0, fill: false }] },
  options: { animation: false, plugins: { legend: { display: false } }, scales: { x: { ticks: { color: '#888' } }, y: { ticks: { color: '#888' } } } }
});`;
}

function ddScript(ddPct) {
    const labels = ddPct.map((_, i) => i);
    return `
new Chart(document.getElementById('dd'), {
  type: 'line',
  data: { labels: ${JSON.stringify(labels)}, datasets: [{ label: 'Drawdown %', data: ${JSON.stringify(ddPct)}, borderColor: '#ff4444', borderWidth: 1.5, pointRadius: 0, fill: true, backgroundColor: 'rgba(255,68,68,0.15)' }] },
  options: { animation: false, plugins: { legend: { display: false } }, scales: { x: { ticks: { color: '#888' } }, y: { ticks: { color: '#888' } } } }
});`;
}

function pnlHistScript(tradePnls) {
    // Simple histogram with 20 buckets
    if (!tradePnls || tradePnls.length === 0) return '';
    const mn = Math.min(...tradePnls);
    const mx = Math.max(...tradePnls);
    const buckets = 20;
    const width = (mx - mn) / buckets || 1;
    const counts = new Array(buckets).fill(0);
    for (const p of tradePnls) {
        let b = Math.floor((p - mn) / width);
        if (b >= buckets) b = buckets - 1;
        counts[b]++;
    }
    const labels = counts.map((_, i) => (mn + i * width).toFixed(1));
    const colors = counts.map((_, i) => (mn + i * width >= 0 ? 'rgba(68,204,68,0.7)' : 'rgba(255,68,68,0.7)'));
    return `
new Chart(document.getElementById('pnl-hist'), {
  type: 'bar',
  data: { labels: ${JSON.stringify(labels)}, datasets: [{ label: 'Count', data: ${JSON.stringify(counts)}, backgroundColor: ${JSON.stringify(colors)} }] },
  options: { animation: false, plugins: { legend: { display: false } }, scales: { x: { ticks: { color: '#888', maxRotation: 45 } }, y: { ticks: { color: '#888' } } } }
});`;
}

function mcScript(mcResult) {
    if (!mcResult || !mcResult.p5Path) return '';
    const L = mcResult.p50Path.length;
    const labels = Array.from({ length: L }, (_, i) => i);
    return `
new Chart(document.getElementById('mc'), {
  type: 'line',
  data: { labels: ${JSON.stringify(labels)}, datasets: [
    { label: 'P5',  data: ${JSON.stringify(mcResult.p5Path)},  borderColor: '#ff8800', borderWidth: 1, pointRadius: 0, fill: false },
    { label: 'P50', data: ${JSON.stringify(mcResult.p50Path)}, borderColor: '#88ccff', borderWidth: 1.5, pointRadius: 0, fill: false },
    { label: 'P95', data: ${JSON.stringify(mcResult.p95Path)}, borderColor: '#44cc44', borderWidth: 1, pointRadius: 0, fill: false },
  ]},
  options: { animation: false, scales: { x: { ticks: { color: '#888' } }, y: { ticks: { color: '#888' } } } }
});`;
}

// ── Metrics table ─────────────────────────────────────────────────────────────
function metricsTable(metrics, drawdownSummary) {
    const rows = [
        ['Sharpe Ratio',        fmtNum(metrics?.sharpe)],
        ['Sortino Ratio',       fmtNum(metrics?.sortino)],
        ['Calmar Ratio',        fmtNum(metrics?.calmar)],
        ['Omega Ratio',         fmtNum(metrics?.omega)],
        ['Profit Factor',       fmtNum(metrics?.profitFactor)],
        ['Win Rate',            fmtNum(metrics?.winRate)],
        ['Expectancy',          fmtNum(metrics?.expectancy)],
        ['Avg Win',             fmtNum(metrics?.avgWin)],
        ['Avg Loss',            fmtNum(metrics?.avgLoss)],
        ['Payoff Ratio',        fmtNum(metrics?.payoffRatio)],
        ['Recovery Factor',     fmtNum(metrics?.recoveryFactor)],
        ['Time In Market',      fmtNum(metrics?.timeInMarket)],
        ['Longest Win Streak',  fmtNum(metrics?.longestWinStreak, 0)],
        ['Longest Loss Streak', fmtNum(metrics?.longestLossStreak, 0)],
        ['Max Drawdown %',      fmtNum(drawdownSummary?.maxDdPct)],
        ['Max Drawdown Abs',    fmtNum(drawdownSummary?.maxDdAbs)],
    ];
    return `<table><tr><th>Metric</th><th>Value</th></tr>
${rows.map(([k, v]) => `<tr><td>${esc(k)}</td><td>${v}</td></tr>`).join('\n')}
</table>`;
}

// ── Top drawdowns table ───────────────────────────────────────────────────────
function topDrawdownsTable(top5) {
    if (!top5 || top5.length === 0) return '<p class="na">No drawdown episodes.</p>';
    return `<table>
<tr><th>#</th><th>Start</th><th>Trough</th><th>Recover</th><th>Depth %</th><th>Depth Abs</th><th>Duration</th><th>Recovery Bars</th></tr>
${top5.map((ep, i) => `<tr>
  <td>${i + 1}</td>
  <td>${ep.startIdx}</td>
  <td>${ep.troughIdx}</td>
  <td>${ep.recoverIdx < 0 ? '<span class="na">n/a</span>' : ep.recoverIdx}</td>
  <td>${fmtNum(ep.depthPct)}</td>
  <td>${fmtNum(ep.depthAbs)}</td>
  <td>${ep.durationBars}</td>
  <td>${ep.recoveryBars < 0 ? '<span class="na">n/a</span>' : ep.recoveryBars}</td>
</tr>`).join('\n')}
</table>`;
}

// ── Walk-forward table ────────────────────────────────────────────────────────
function walkforwardTable(wfResults) {
    if (!wfResults || wfResults.length === 0) return '';
    return `<section id="walkforward"><h2>Walk-Forward Results</h2><table>
<tr><th>Fold</th><th>Train Bars</th><th>Test Bars</th><th>Train Result</th><th>Test Result</th></tr>
${wfResults.map(f => `<tr>
  <td>${f.fold}</td>
  <td>${f.trainBars?.length ?? 0}</td>
  <td>${f.testBars?.length ?? 0}</td>
  <td>${esc(JSON.stringify(f.result?.train))}</td>
  <td>${esc(JSON.stringify(f.result?.test))}</td>
</tr>`).join('\n')}
</table></section>`;
}

// ── Attribution table ─────────────────────────────────────────────────────────
function attributionTable(olsResult) {
    if (!olsResult) return '';
    const rows = olsResult.beta.map((b, i) => [
        i === 0 ? 'intercept' : `feature_${i}`,
        b, olsResult.tStats[i], olsResult.stdErrors[i],
    ]);
    return `<section id="attribution"><h2>OLS Attribution</h2>
<p>R&sup2;: ${fmtNum(olsResult.r2)} &nbsp; Adj R&sup2;: ${fmtNum(olsResult.adjR2)}</p>
<table><tr><th>Feature</th><th>Beta</th><th>t-stat</th><th>Std Err</th></tr>
${rows.map(([name, b, t, se]) => `<tr><td>${esc(name)}</td><td>${fmtNum(b)}</td><td>${fmtNum(t)}</td><td>${fmtNum(se)}</td></tr>`).join('\n')}
</table></section>`;
}

// ── Factors table ─────────────────────────────────────────────────────────────
function factorsTable(factorResult) {
    if (!factorResult) return '';
    const names = Object.keys(factorResult.betas);
    return `<section id="factors"><h2>Factor Loadings</h2>
<p>Alpha: ${fmtNum(factorResult.alpha)} &nbsp; R&sup2;: ${fmtNum(factorResult.r2)} &nbsp; Adj R&sup2;: ${fmtNum(factorResult.adjR2)}</p>
<table><tr><th>Factor</th><th>Beta</th><th>t-stat</th><th>Std Err</th></tr>
${names.map(name => `<tr><td>${esc(name)}</td><td>${fmtNum(factorResult.betas[name])}</td><td>${fmtNum(factorResult.tStats[name])}</td><td>${fmtNum(factorResult.stdErrors[name])}</td></tr>`).join('\n')}
</table></section>`;
}

// ── Main render function ──────────────────────────────────────────────────────

/**
 * Render a self-contained HTML backtest report.
 *
 * @param {{
 *   symbol?:          string,
 *   equityCurve?:     number[],
 *   trades?:          Array<{netPnl:number}>,
 *   metrics?:         object,
 *   drawdownSummary?: { maxDdPct:number, maxDdAbs:number, ddPct:number[] },
 *   top5Drawdowns?:   Array,
 *   mcResult?:        object,
 *   wfResults?:       Array,
 *   olsResult?:       object,
 *   factorResult?:    object,
 * }} opts
 * @returns {string}  HTML string
 */
export function renderReport({
    symbol          = 'Unknown',
    equityCurve     = [],
    trades          = [],
    metrics         = null,
    drawdownSummary = null,
    top5Drawdowns   = null,
    mcResult        = null,
    wfResults       = null,
    olsResult       = null,
    factorResult    = null,
} = {}) {
    const tradePnls = trades.map(t => typeof t.netPnl === 'function' ? t.netPnl() : (t.netPnl ?? 0));
    const ddPct = drawdownSummary?.ddPct ?? [];

    const chartScripts = [
        equityCurve.length > 0 ? equityCurveScript(equityCurve) : '',
        ddPct.length > 0       ? ddScript(ddPct) : '',
        tradePnls.length > 0   ? pnlHistScript(tradePnls) : '',
        mcResult               ? mcScript(mcResult) : '',
    ].filter(Boolean).join('\n');

    return `<!DOCTYPE html>
<html><head>
  <meta charset="utf-8">
  <title>AlgoForge Backtest Report — ${esc(symbol)}</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
  <style>${REPORT_CSS}</style>
</head><body>
  <h1>AlgoForge Backtest Report</h1>
  <p>Symbol: <strong>${esc(symbol)}</strong> &nbsp; Generated: ${new Date().toISOString()}</p>

  <section id="summary">
    <h2>Performance Summary</h2>
    ${metricsTable(metrics, drawdownSummary)}
  </section>

  <section id="equity-curve">
    <h2>Equity Curve</h2>
    <div class="canvas-wrap"><canvas id="eq"></canvas></div>
  </section>

  <section id="drawdown">
    <h2>Drawdown</h2>
    <div class="canvas-wrap"><canvas id="dd"></canvas></div>
  </section>

  <section id="trade-distribution">
    <h2>Trade PnL Distribution</h2>
    <div class="canvas-wrap"><canvas id="pnl-hist"></canvas></div>
  </section>

  <section id="monte-carlo">
    <h2>Monte Carlo Simulation</h2>
    <div class="canvas-wrap"><canvas id="mc"></canvas></div>
    ${mcResult ? `<p>P5 Final: ${fmtNum(mcResult.p5Final)} &nbsp; P50: ${fmtNum(mcResult.p50Final)} &nbsp; P95: ${fmtNum(mcResult.p95Final)} &nbsp; Prob of Ruin: ${fmtNum(mcResult.probOfRuin)}</p>` : ''}
  </section>

  <section id="top-drawdowns">
    <h2>Top 5 Drawdown Episodes</h2>
    ${topDrawdownsTable(top5Drawdowns)}
  </section>

  ${walkforwardTable(wfResults)}
  ${attributionTable(olsResult)}
  ${factorsTable(factorResult)}

  <script>
  window.addEventListener('load', function() {
    ${chartScripts}
  });
  </script>
</body></html>`;
}
