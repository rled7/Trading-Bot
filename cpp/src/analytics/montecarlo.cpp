/**
 * AlgoForge — src/analytics/montecarlo.cpp
 * Bootstrap Monte Carlo with seeded LCG RNG.
 *
 * LCG spec (canonical, must match Python/C/JS):
 *   state = seed (uint64, unsigned)
 *   next(): state = state * 6364136223846793005 + 1442695040888963407  (mod 2^64)
 *           return state >> 33  (uint32)
 *
 * First 10 outputs from seed=42:
 *   [1220265334, 484179026, 886563538, 1353769503, 1460606294,
 *    56326156, 46730969, 327394710, 1017823166, 53256125]
 */

#include "core/analytics.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace algoforge::analytics {

/* ---- local percentile (unsorted input) ---------------------------------- */
static double local_percentile(std::vector<double>& vals, double pct) {
    if (vals.empty()) return 0.0;
    if (vals.size() == 1) return vals[0];
    std::sort(vals.begin(), vals.end());
    double index = (pct / 100.0) * (static_cast<double>(vals.size()) - 1.0);
    int    lo    = static_cast<int>(index);
    int    hi    = lo + 1;
    if (hi >= static_cast<int>(vals.size())) return vals.back();
    double frac  = index - lo;
    return vals[lo] + frac * (vals[hi] - vals[lo]);
}

/* ---- pointwise percentile across paths ---------------------------------- */
static std::vector<double> pointwise_pct(
    const std::vector<std::vector<double>>& paths,
    double pct
) {
    if (paths.empty()) return {};
    size_t path_len = paths[0].size();
    std::vector<double> result;
    result.reserve(path_len);

    for (size_t step = 0; step < path_len; ++step) {
        std::vector<double> col;
        col.reserve(paths.size());
        for (const auto& path : paths) {
            col.push_back(path[step]);
        }
        result.push_back(local_percentile(col, pct));
    }
    return result;
}

MonteCarloResult mc_bootstrap(
    std::span<const Trade> trades,
    int                    n_runs,
    uint64_t               seed,
    double                 initial_capital
) noexcept {
    MonteCarloResult res;

    if (trades.empty() || n_runs <= 0) {
        res.p5_final   = initial_capital;
        res.p50_final  = initial_capital;
        res.p95_final  = initial_capital;
        res.p5_path    = {initial_capital};
        res.p50_path   = {initial_capital};
        res.p95_path   = {initial_capital};
        res.prob_of_ruin = 0.0;
        return res;
    }

    /* extract PnL values */
    std::vector<double> pnl;
    pnl.reserve(trades.size());
    for (const auto& t : trades) pnl.push_back(t.net_pnl);

    const int n_trades = static_cast<int>(pnl.size());

    uint64_t state = seed;
    std::vector<double>              final_eq;
    std::vector<std::vector<double>> paths;
    final_eq.reserve(n_runs);
    paths.reserve(n_runs);
    int ruin_count = 0;

    for (int run = 0; run < n_runs; ++run) {
        double eq = initial_capital;
        std::vector<double> path;
        path.reserve(n_trades + 1);
        path.push_back(eq);

        bool ruined = (eq <= 0.0);

        for (int i = 0; i < n_trades; ++i) {
            int idx = static_cast<int>(lcg_next(state) % static_cast<uint32_t>(n_trades));
            eq += pnl[idx];
            path.push_back(eq);
            if (eq <= 0.0) ruined = true;
        }

        final_eq.push_back(eq);
        paths.push_back(std::move(path));
        if (ruined) ++ruin_count;
    }

    /* scalar percentiles of final equity */
    {
        std::vector<double> fe_copy = final_eq;
        res.p5_final  = local_percentile(fe_copy, 5.0);
    }
    {
        std::vector<double> fe_copy = final_eq;
        res.p50_final = local_percentile(fe_copy, 50.0);
    }
    {
        std::vector<double> fe_copy = final_eq;
        res.p95_final = local_percentile(fe_copy, 95.0);
    }

    /* path percentiles */
    res.p5_path  = pointwise_pct(paths, 5.0);
    res.p50_path = pointwise_pct(paths, 50.0);
    res.p95_path = pointwise_pct(paths, 95.0);

    res.prob_of_ruin = static_cast<double>(ruin_count) / n_runs;

    return res;
}

} /* namespace algoforge::analytics */
