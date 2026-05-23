/**
 * AlgoForge — src/analytics/drawdown.cpp
 * Drawdown series, summary, and episodes.
 */

#include "core/analytics.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace algoforge::analytics {

DrawdownSummary compute_drawdown(std::span<const double> equity) noexcept {
    DrawdownSummary ds;
    const int n = static_cast<int>(equity.size());
    if (n == 0) return ds;

    ds.dd_pct_series.resize(n);
    ds.dd_abs_series.resize(n);

    double peak     = equity[0];
    double max_pct  = 0.0;
    double max_abs  = 0.0;

    for (int i = 0; i < n; ++i) {
        if (equity[i] > peak) peak = equity[i];
        double dd_abs = equity[i] - peak;
        double dd_pct = (peak > 1e-15) ? (dd_abs / peak * 100.0) : 0.0;
        ds.dd_pct_series[i] = dd_pct;
        ds.dd_abs_series[i] = dd_abs;
        if (dd_pct < max_pct) max_pct = dd_pct;
        if (dd_abs < max_abs) max_abs = dd_abs;
    }
    ds.max_dd_pct = max_pct;
    ds.max_dd_abs = max_abs;

    /* ── Drawdown episodes: contiguous runs of dd_pct < 0 ── */
    int i = 0;
    while (i < n) {
        /* find episode start: first bar below prior peak */
        if (ds.dd_pct_series[i] < 0.0) {
            DrawdownEpisode ep;
            ep.start_idx   = i;
            ep.trough_idx  = i;

            /* find trough */
            while (i < n && ds.dd_pct_series[i] < 0.0) {
                if (ds.dd_pct_series[i] < ds.dd_pct_series[ep.trough_idx]) {
                    ep.trough_idx = i;
                }
                ++i;
            }
            /* i now points to recovery or end */

            ep.depth_pct = ds.dd_pct_series[ep.trough_idx];
            ep.depth_abs = ds.dd_abs_series[ep.trough_idx];

            /* find recovery: first index where equity >= peak before episode */
            /* The peak before the episode is the value at (start_idx - 1)
               or equity[0] if start_idx == 0 */
            double pre_peak = (ep.start_idx > 0) ? equity[ep.start_idx - 1] : equity[0];

            ep.recover_idx   = -1;
            ep.recovery_bars = -1;
            for (int j = ep.trough_idx + 1; j < n; ++j) {
                if (equity[j] >= pre_peak) {
                    ep.recover_idx = j;
                    break;
                }
            }

            if (ep.recover_idx >= 0) {
                ep.duration_bars = ep.recover_idx - ep.start_idx;
                ep.recovery_bars = ep.recover_idx - ep.trough_idx;
            } else {
                ep.duration_bars = n - ep.start_idx;
                ep.recovery_bars = -1;
            }

            ds.episodes.push_back(ep);
        } else {
            ++i;
        }
    }

    return ds;
}

std::vector<DrawdownEpisode> top5_drawdowns(std::span<const double> equity) noexcept {
    auto ds = compute_drawdown(equity);
    auto& eps = ds.episodes;

    /* sort by depth_pct ascending (most negative = deepest first) */
    std::sort(eps.begin(), eps.end(), [](const DrawdownEpisode& a, const DrawdownEpisode& b) {
        return a.depth_pct < b.depth_pct;
    });

    if (eps.size() > 5) eps.resize(5);
    return eps;
}

} /* namespace algoforge::analytics */
