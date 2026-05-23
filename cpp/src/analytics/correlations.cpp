/**
 * AlgoForge — src/analytics/correlations.cpp
 * Pearson correlation matrix and rolling beta.
 */

#include "core/analytics.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace algoforge::analytics {

double pearson_correlation(
    std::span<const double> x,
    std::span<const double> y
) noexcept {
    const int n = static_cast<int>(std::min(x.size(), y.size()));
    if (n < 2) return 0.0;

    double mx = 0.0, my = 0.0;
    for (int i = 0; i < n; ++i) { mx += x[i]; my += y[i]; }
    mx /= n; my /= n;

    double num = 0.0, sx2 = 0.0, sy2 = 0.0;
    for (int i = 0; i < n; ++i) {
        double dx = x[i] - mx;
        double dy = y[i] - my;
        num += dx * dy;
        sx2 += dx * dx;
        sy2 += dy * dy;
    }

    double denom = std::sqrt(sx2 * sy2);
    return (denom > 1e-15) ? num / denom : 0.0;
}

CorrelationMatrix correlation_matrix(
    const std::unordered_map<std::string, std::vector<double>>& returns_per_symbol
) noexcept {
    CorrelationMatrix cm;
    if (returns_per_symbol.empty()) return cm;

    /* collect symbols in a stable order */
    for (const auto& [sym, _] : returns_per_symbol) {
        cm.symbols.push_back(sym);
    }
    std::sort(cm.symbols.begin(), cm.symbols.end());

    /* find minimum length */
    size_t min_len = SIZE_MAX;
    for (const auto& sym : cm.symbols) {
        min_len = std::min(min_len, returns_per_symbol.at(sym).size());
    }
    if (min_len == SIZE_MAX) min_len = 0;

    const int k = static_cast<int>(cm.symbols.size());
    cm.matrix.assign(k, std::vector<double>(k, 0.0));

    for (int i = 0; i < k; ++i) {
        const auto& vi = returns_per_symbol.at(cm.symbols[i]);
        cm.matrix[i][i] = 1.0;
        for (int j = i + 1; j < k; ++j) {
            const auto& vj = returns_per_symbol.at(cm.symbols[j]);
            std::span<const double> si(vi.data(), min_len);
            std::span<const double> sj(vj.data(), min_len);
            double r = pearson_correlation(si, sj);
            cm.matrix[i][j] = r;
            cm.matrix[j][i] = r;
        }
    }

    return cm;
}

std::vector<double> rolling_beta(
    std::span<const double> target,
    std::span<const double> baseline,
    int                     window
) noexcept {
    const int n = static_cast<int>(std::min(target.size(), baseline.size()));
    if (n < window || window < 2) return {};

    std::vector<double> betas;
    betas.reserve(n - window + 1);

    for (int i = 0; i <= n - window; ++i) {
        std::span<const double> t_win(target.data() + i,   window);
        std::span<const double> b_win(baseline.data() + i, window);

        /* cov(target, baseline) and var(baseline) */
        double mt = 0.0, mb = 0.0;
        for (int j = 0; j < window; ++j) { mt += t_win[j]; mb += b_win[j]; }
        mt /= window; mb /= window;

        double cov = 0.0, var_b = 0.0;
        for (int j = 0; j < window; ++j) {
            cov   += (t_win[j] - mt) * (b_win[j] - mb);
            var_b += (b_win[j] - mb) * (b_win[j] - mb);
        }

        betas.push_back((var_b > 1e-15) ? cov / var_b : 0.0);
    }

    return betas;
}

} /* namespace algoforge::analytics */
