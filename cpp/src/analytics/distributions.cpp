/**
 * AlgoForge — src/analytics/distributions.cpp
 * Descriptive statistics: skewness, excess kurtosis, quantiles.
 */

#include "core/analytics.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace algoforge::analytics {

double percentile_linear(std::span<const double> sorted_values, double pct) noexcept {
    const int n = static_cast<int>(sorted_values.size());
    if (n == 0) return 0.0;
    if (n == 1) return sorted_values[0];

    /* R-7 / numpy 'linear' method */
    double index = (pct / 100.0) * (n - 1);
    int    lo    = static_cast<int>(index);
    int    hi    = lo + 1;
    if (hi >= n) return sorted_values[n - 1];
    double frac  = index - lo;
    return sorted_values[lo] + frac * (sorted_values[hi] - sorted_values[lo]);
}

DistributionStats compute_distribution(std::span<const double> values) noexcept {
    DistributionStats ds;
    const int n = static_cast<int>(values.size());
    if (n == 0) return ds;

    ds.n = n;

    /* mean */
    double sum = 0.0;
    for (double v : values) sum += v;
    double mean = sum / n;
    ds.mean = mean;

    /* sample std dev, biased std dev, min, max */
    double ss = 0.0;
    double m3 = 0.0;
    double m4 = 0.0;
    ds.min_val = values[0];
    ds.max_val = values[0];

    for (double v : values) {
        double d = v - mean;
        ss += d * d;
        m3 += d * d * d;
        m4 += d * d * d * d;
        if (v < ds.min_val) ds.min_val = v;
        if (v > ds.max_val) ds.max_val = v;
    }

    double var_sample = (n > 1) ? ss / (n - 1) : 0.0;
    ds.std_dev = std::sqrt(var_sample);

    /* biased std dev (for skewness/kurtosis formulas) */
    double std_biased = std::sqrt(ss / n);

    if (n >= 4 && std_biased > 1e-15) {
        /* g1 = (1/n)*sum((x-mean)^3) / std_biased^3 */
        double g1 = (m3 / n) / (std_biased * std_biased * std_biased);
        /* Fisher-Pearson adjusted G1 */
        double G1 = (std::sqrt(static_cast<double>(n) * (n - 1)) / (n - 2)) * g1;
        ds.skewness = G1;

        /* g2 = (1/n)*sum((x-mean)^4) / std_biased^4 - 3 */
        double g2 = (m4 / n) / (std_biased * std_biased * std_biased * std_biased) - 3.0;
        /* Fisher adjusted G2 */
        double G2 = (static_cast<double>(n - 1) / (static_cast<double>(n - 2) * (n - 3)))
                    * ((n + 1) * g2 + 6.0);
        ds.excess_kurtosis = G2;
    }
    /* else skewness = 0, excess_kurtosis = 0 (default) */

    /* Quantiles — requires sorted copy */
    std::vector<double> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());

    ds.p25 = percentile_linear(sorted, 25.0);
    ds.p50 = percentile_linear(sorted, 50.0);
    ds.p75 = percentile_linear(sorted, 75.0);

    return ds;
}

} /* namespace algoforge::analytics */
