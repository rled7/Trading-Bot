/**
 * AlgoForge — src/analytics/factors.cpp
 * Fama-French-style multi-factor OLS regression.
 *
 * Builds design matrix X = [1, f1, f2, ...] and calls the OLS solver.
 */

#include "core/analytics.hpp"

#include <cmath>
#include <span>
#include <vector>

namespace algoforge::analytics {

FactorResult factor_regression(
    std::span<const double>                                       strategy_returns,
    const std::unordered_map<std::string, std::vector<double>>&   factor_returns
) noexcept {
    FactorResult fr;

    const int n = static_cast<int>(strategy_returns.size());
    if (n < 2 || factor_returns.empty()) return fr;

    /* ── Collect factor names in sorted order for reproducibility ── */
    std::vector<std::string> names;
    names.reserve(factor_returns.size());
    for (const auto& [name, _] : factor_returns) names.push_back(name);
    std::sort(names.begin(), names.end());

    /* ── Verify all factor series have at least n values ── */
    for (const auto& nm : names) {
        if (static_cast<int>(factor_returns.at(nm).size()) < n) {
            /* Truncate n to the shortest factor series */
        }
    }
    /* Find actual usable length */
    int usable = n;
    for (const auto& nm : names) {
        int sz = static_cast<int>(factor_returns.at(nm).size());
        if (sz < usable) usable = sz;
    }
    if (usable < 2) return fr;

    const int k = static_cast<int>(names.size());

    /* ── Build design matrix X: n x (k+1) with intercept column ── */
    std::vector<std::vector<double>> X(usable, std::vector<double>(k + 1, 0.0));
    for (int i = 0; i < usable; ++i) {
        X[i][0] = 1.0;   /* intercept */
        for (int j = 0; j < k; ++j) {
            X[i][j + 1] = factor_returns.at(names[j])[i];
        }
    }

    /* ── Run OLS ── */
    std::span<const double> y_span(strategy_returns.data(), usable);
    OLSResult ols = ols_regression(X, y_span);

    if (ols.beta.empty()) return fr;

    fr.alpha        = ols.beta[0];
    fr.factor_names = names;
    fr.betas.assign(ols.beta.begin() + 1, ols.beta.end());
    fr.t_stats.assign(ols.t_stats.begin() + 1, ols.t_stats.end());
    fr.r2           = ols.r2;
    fr.adj_r2       = ols.adj_r2;

    return fr;
}

} /* namespace algoforge::analytics */
