/**
 * AlgoForge — src/analytics/html_report.cpp
 * Self-contained HTML report generator.
 *
 * Emits a single string containing a valid HTML5 document with Chart.js
 * visualizations.  All chart data is inlined as JSON.
 */

#include "core/analytics.hpp"

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace algoforge::analytics {

/* ========================================================================
 * Helper: append a JSON number array to an ostringstream
 * ======================================================================== */
static void json_array(std::ostringstream& ss,
                        const std::vector<double>& v) {
    ss << '[';
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) ss << ',';
        if (std::isnan(v[i]) || std::isinf(v[i]))
            ss << "null";
        else
            ss << v[i];
    }
    ss << ']';
}

static void json_iarray(std::ostringstream& ss, const std::vector<int>& v) {
    ss << '[';
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) ss << ',';
        ss << v[i];
    }
    ss << ']';
}

static std::string esc(const std::string& s) {
    /* minimal HTML escaping */
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        if      (c == '<')  r += "&lt;";
        else if (c == '>')  r += "&gt;";
        else if (c == '&')  r += "&amp;";
        else if (c == '"')  r += "&quot;";
        else                r += c;
    }
    return r;
}

static std::string fmt(double v, int prec = 4) {
    char buf[64];
    if (std::isinf(v))  return v > 0 ? "Inf" : "-Inf";
    if (std::isnan(v))  return "NaN";
    snprintf(buf, sizeof(buf), "%.*f", prec, v);
    return buf;
}

/* ========================================================================
 * Embedded CSS (~100 lines, dark monospace)
 * ======================================================================== */
static const char* STYLE = R"css(
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: #0d1117; color: #c9d1d9;
    font-family: 'Consolas', 'Menlo', monospace; font-size: 13px;
    padding: 24px;
  }
  h1 { color: #58a6ff; font-size: 22px; margin-bottom: 8px; }
  h2 { color: #79c0ff; font-size: 15px; margin: 24px 0 8px; border-bottom: 1px solid #30363d; padding-bottom: 4px; }
  section { margin-bottom: 32px; }
  table { border-collapse: collapse; width: 100%; max-width: 700px; }
  th, td { padding: 6px 12px; border: 1px solid #30363d; text-align: left; }
  th { background: #161b22; color: #8b949e; font-weight: normal; }
  tr:nth-child(even) td { background: #0d1117; }
  tr:nth-child(odd)  td { background: #161b22; }
  td.num { text-align: right; font-variant-numeric: tabular-nums; }
  .canvas-wrap { max-width: 860px; margin-top: 12px; }
  canvas { width: 100% !important; }
  .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 24px; }
  @media (max-width: 700px) { .grid { grid-template-columns: 1fr; } }
  .badge-pos { color: #3fb950; }
  .badge-neg { color: #f85149; }
  .subtitle  { color: #8b949e; font-size: 11px; margin-bottom: 16px; }
)css";

/* ========================================================================
 * Chart.js init scripts
 * ======================================================================== */
static std::string make_scripts(const ReportData& d) {
    std::ostringstream ss;

    /* Equity curve */
    ss << "new Chart(document.getElementById('eq'),{type:'line',data:{labels:";
    {
        std::vector<int> labels;
        labels.reserve(d.equity_curve.size());
        for (size_t i = 0; i < d.equity_curve.size(); ++i) labels.push_back(static_cast<int>(i));
        json_iarray(ss, labels);
    }
    ss << ",datasets:[{label:'Equity',data:";
    json_array(ss, d.equity_curve);
    ss << ",borderColor:'#58a6ff',borderWidth:1.5,pointRadius:0,fill:false}]},";
    ss << "options:{animation:false,plugins:{legend:{display:false}},scales:{x:{display:false},y:{ticks:{color:'#8b949e'}}}}});\n";

    /* Drawdown */
    ss << "new Chart(document.getElementById('dd'),{type:'line',data:{labels:";
    {
        std::vector<int> labels;
        labels.reserve(d.drawdown.dd_pct_series.size());
        for (size_t i = 0; i < d.drawdown.dd_pct_series.size(); ++i) labels.push_back(static_cast<int>(i));
        json_iarray(ss, labels);
    }
    ss << ",datasets:[{label:'DD%',data:";
    json_array(ss, d.drawdown.dd_pct_series);
    ss << ",borderColor:'#f85149',borderWidth:1.5,pointRadius:0,fill:true,backgroundColor:'rgba(248,81,73,0.12)'}]},";
    ss << "options:{animation:false,plugins:{legend:{display:false}},scales:{x:{display:false},y:{ticks:{color:'#8b949e'}}}}});\n";

    /* PnL histogram (use MC final eq distribution as proxy) */
    ss << "new Chart(document.getElementById('pnl-hist'),{type:'bar',data:{labels:";
    json_array(ss, d.mc_result.p5_path.empty() ? std::vector<double>{} : std::vector<double>(d.mc_result.p5_path.size()));
    ss << ",datasets:[{label:'P5',data:";
    json_array(ss, d.mc_result.p5_path);
    ss << ",backgroundColor:'rgba(248,81,73,0.5)'},{label:'P50',data:";
    json_array(ss, d.mc_result.p50_path);
    ss << ",backgroundColor:'rgba(88,166,255,0.5)'},{label:'P95',data:";
    json_array(ss, d.mc_result.p95_path);
    ss << ",backgroundColor:'rgba(63,185,80,0.5)'}]},";
    ss << "options:{animation:false,plugins:{legend:{labels:{color:'#c9d1d9'}}},scales:{x:{display:false},y:{ticks:{color:'#8b949e'}}}}});\n";

    /* MC paths */
    ss << "new Chart(document.getElementById('mc'),{type:'line',data:{labels:";
    {
        std::vector<int> labels;
        size_t pl = d.mc_result.p50_path.size();
        labels.reserve(pl);
        for (size_t i = 0; i < pl; ++i) labels.push_back(static_cast<int>(i));
        json_iarray(ss, labels);
    }
    ss << ",datasets:[{label:'P5',data:";
    json_array(ss, d.mc_result.p5_path);
    ss << ",borderColor:'#f85149',borderWidth:1,pointRadius:0,fill:false},";
    ss << "{label:'P50',data:";
    json_array(ss, d.mc_result.p50_path);
    ss << ",borderColor:'#58a6ff',borderWidth:1.5,pointRadius:0,fill:false},";
    ss << "{label:'P95',data:";
    json_array(ss, d.mc_result.p95_path);
    ss << ",borderColor:'#3fb950',borderWidth:1,pointRadius:0,fill:false}]},";
    ss << "options:{animation:false,plugins:{legend:{labels:{color:'#c9d1d9'}}},scales:{x:{display:false},y:{ticks:{color:'#8b949e'}}}}});\n";

    return ss.str();
}

/* ========================================================================
 * generate_html_report
 * ======================================================================== */
std::string generate_html_report(const ReportData& d) noexcept {
    std::ostringstream o;
    const Metrics& m = d.metrics;

    o << "<!DOCTYPE html>\n<html><head>\n";
    o << "  <meta charset=\"utf-8\">\n";
    o << "  <title>AlgoForge Backtest Report &mdash; " << esc(d.symbol) << "</title>\n";
    o << "  <script src=\"https://cdn.jsdelivr.net/npm/chart.js@4\"></script>\n";
    o << "  <style>" << STYLE << "  </style>\n";
    o << "</head><body>\n";
    o << "<h1>AlgoForge Backtest Report</h1>\n";
    o << "<p class=\"subtitle\">Symbol: " << esc(d.symbol) << "</p>\n";

    /* ── Summary metrics table ── */
    o << "<section id=\"summary\">\n";
    o << "  <h2>Performance Summary</h2>\n";
    o << "  <table>\n";
    o << "    <tr><th>Metric</th><th>Value</th></tr>\n";

    auto row = [&](const char* name, const std::string& val, bool is_num = true) {
        o << "    <tr><td>" << name << "</td>"
          << "<td class=\"" << (is_num ? "num" : "") << "\">" << val << "</td></tr>\n";
    };

    row("Sharpe Ratio",         fmt(m.sharpe));
    row("Sortino Ratio",        fmt(m.sortino));
    row("Calmar Ratio",         fmt(m.calmar));
    row("Omega Ratio",          fmt(m.omega));
    row("Profit Factor",        fmt(m.profit_factor));
    row("Win Rate",             fmt(m.win_rate * 100.0, 2) + "%");
    row("Expectancy",           fmt(m.expectancy, 2));
    row("Avg Win",              fmt(m.avg_win, 2));
    row("Avg Loss",             fmt(m.avg_loss, 2));
    row("Payoff Ratio",         fmt(m.payoff_ratio));
    row("Recovery Factor",      fmt(m.recovery_factor));
    row("Time in Market",       fmt(m.time_in_market * 100.0, 2) + "%");
    row("Longest Win Streak",   std::to_string(m.longest_win_streak));
    row("Longest Loss Streak",  std::to_string(m.longest_loss_streak));
    row("Max Drawdown %",       fmt(d.drawdown.max_dd_pct, 4) + "%");
    row("Max Drawdown Abs",     fmt(d.drawdown.max_dd_abs, 2));
    o << "  </table>\n</section>\n";

    /* ── Equity curve ── */
    o << "<section id=\"equity-curve\">\n";
    o << "  <h2>Equity Curve</h2>\n";
    o << "  <div class=\"canvas-wrap\"><canvas id=\"eq\"></canvas></div>\n";
    o << "</section>\n";

    /* ── Drawdown ── */
    o << "<section id=\"drawdown\">\n";
    o << "  <h2>Drawdown (%)</h2>\n";
    o << "  <div class=\"canvas-wrap\"><canvas id=\"dd\"></canvas></div>\n";
    o << "</section>\n";

    /* ── Trade distribution / MC paths (side by side) ── */
    o << "<section id=\"trade-distribution\">\n";
    o << "  <h2>Monte Carlo Equity Paths (P5/P50/P95)</h2>\n";
    o << "  <div class=\"grid\">\n";
    o << "    <div class=\"canvas-wrap\"><canvas id=\"pnl-hist\"></canvas></div>\n";
    o << "    <div class=\"canvas-wrap\"><canvas id=\"mc\"></canvas></div>\n";
    o << "  </div>\n";
    o << "</section>\n";

    /* ── Monte Carlo summary ── */
    o << "<section id=\"monte-carlo\">\n";
    o << "  <h2>Monte Carlo Summary</h2>\n";
    o << "  <table>\n";
    o << "    <tr><th>Stat</th><th>Value</th></tr>\n";
    o << "    <tr><td>P5 Final Equity</td><td class=\"num\">" << fmt(d.mc_result.p5_final, 2) << "</td></tr>\n";
    o << "    <tr><td>P50 Final Equity</td><td class=\"num\">" << fmt(d.mc_result.p50_final, 2) << "</td></tr>\n";
    o << "    <tr><td>P95 Final Equity</td><td class=\"num\">" << fmt(d.mc_result.p95_final, 2) << "</td></tr>\n";
    o << "    <tr><td>Prob of Ruin</td><td class=\"num\">" << fmt(d.mc_result.prob_of_ruin * 100.0, 2) << "%</td></tr>\n";
    o << "  </table>\n</section>\n";

    /* ── Top drawdowns ── */
    o << "<section id=\"top-drawdowns\">\n";
    o << "  <h2>Top Drawdown Episodes</h2>\n";
    o << "  <table>\n";
    o << "    <tr><th>#</th><th>Start</th><th>Trough</th><th>Recover</th>"
         "<th>Depth %</th><th>Depth Abs</th><th>Duration</th><th>Recovery Bars</th></tr>\n";
    const auto& eps = d.drawdown.episodes;
    size_t max_ep = std::min(eps.size(), static_cast<size_t>(5));
    /* sort by depth */
    std::vector<size_t> idx;
    idx.reserve(eps.size());
    for (size_t i = 0; i < eps.size(); ++i) idx.push_back(i);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        return eps[a].depth_pct < eps[b].depth_pct;
    });
    for (size_t rank = 0; rank < max_ep; ++rank) {
        const auto& ep = eps[idx[rank]];
        o << "    <tr>"
          << "<td>" << (rank+1) << "</td>"
          << "<td>" << ep.start_idx << "</td>"
          << "<td>" << ep.trough_idx << "</td>"
          << "<td>" << (ep.recover_idx >= 0 ? std::to_string(ep.recover_idx) : "N/A") << "</td>"
          << "<td class=\"num\">" << fmt(ep.depth_pct, 4) << "%</td>"
          << "<td class=\"num\">" << fmt(ep.depth_abs, 2) << "</td>"
          << "<td>" << ep.duration_bars << "</td>"
          << "<td>" << (ep.recovery_bars >= 0 ? std::to_string(ep.recovery_bars) : "N/A") << "</td>"
          << "</tr>\n";
    }
    o << "  </table>\n</section>\n";

    /* ── Walk-forward (optional) ── */
    if (!d.walkforward_folds.empty()) {
        o << "<section id=\"walkforward\">\n";
        o << "  <h2>Walk-Forward Results</h2>\n";
        o << "  <table>\n";
        o << "    <tr><th>Fold</th><th>Train Start</th><th>Train End</th>"
             "<th>Test Start</th><th>Test End</th>"
             "<th>Train Sharpe</th><th>Test Sharpe</th></tr>\n";
        for (const auto& f : d.walkforward_folds) {
            o << "    <tr>"
              << "<td>" << f.fold_index << "</td>"
              << "<td>" << f.train_start << "</td>"
              << "<td>" << f.train_end << "</td>"
              << "<td>" << f.test_start << "</td>"
              << "<td>" << f.test_end << "</td>"
              << "<td class=\"num\">" << fmt(f.train_metrics.sharpe) << "</td>"
              << "<td class=\"num\">" << fmt(f.test_metrics.sharpe) << "</td>"
              << "</tr>\n";
        }
        o << "  </table>\n</section>\n";
    }

    /* ── Attribution (optional) ── */
    if (!d.attribution.beta.empty()) {
        o << "<section id=\"attribution\">\n";
        o << "  <h2>OLS Attribution (R&sup2; = " << fmt(d.attribution.r2) << ")</h2>\n";
        o << "  <table>\n";
        o << "    <tr><th>Feature</th><th>Beta</th><th>Std Err</th><th>t-stat</th></tr>\n";
        const int k = static_cast<int>(d.attribution.beta.size());
        for (int j = 0; j < k; ++j) {
            o << "    <tr>"
              << "<td>" << (j == 0 ? "intercept" : "feature_" + std::to_string(j)) << "</td>"
              << "<td class=\"num\">" << fmt(d.attribution.beta[j]) << "</td>"
              << "<td class=\"num\">" << fmt(j < static_cast<int>(d.attribution.std_errors.size()) ? d.attribution.std_errors[j] : 0.0) << "</td>"
              << "<td class=\"num\">" << fmt(j < static_cast<int>(d.attribution.t_stats.size()) ? d.attribution.t_stats[j] : 0.0) << "</td>"
              << "</tr>\n";
        }
        o << "  </table>\n</section>\n";
    }

    /* ── Factor loadings (optional) ── */
    if (!d.factor_result.betas.empty()) {
        o << "<section id=\"factors\">\n";
        o << "  <h2>Factor Loadings (alpha = " << fmt(d.factor_result.alpha)
          << ", R&sup2; = " << fmt(d.factor_result.r2) << ")</h2>\n";
        o << "  <table>\n";
        o << "    <tr><th>Factor</th><th>Beta</th><th>t-stat</th></tr>\n";
        for (size_t j = 0; j < d.factor_result.betas.size(); ++j) {
            const std::string& nm = (j < d.factor_result.factor_names.size())
                                        ? d.factor_result.factor_names[j]
                                        : "factor_" + std::to_string(j);
            o << "    <tr>"
              << "<td>" << esc(nm) << "</td>"
              << "<td class=\"num\">" << fmt(d.factor_result.betas[j]) << "</td>"
              << "<td class=\"num\">" << fmt(j < d.factor_result.t_stats.size() ? d.factor_result.t_stats[j] : 0.0) << "</td>"
              << "</tr>\n";
        }
        o << "  </table>\n</section>\n";
    }

    /* ── Chart.js init ── */
    o << "<script>\n";
    o << make_scripts(d);
    o << "</script>\n";

    o << "</body></html>\n";
    return o.str();
}

} /* namespace algoforge::analytics */
