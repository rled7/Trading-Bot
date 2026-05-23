/**
 * AlgoForge — src/analytics/ml_attribution.cpp
 * OLS linear regression with R², adjusted R², t-statistics.
 *
 * Matrix algebra via Gauss-Jordan elimination with partial pivoting.
 * No external libraries.
 *
 * Algorithm:
 *   beta     = (X^T X)^-1 X^T y
 *   y_hat    = X @ beta
 *   ss_res   = sum((y - y_hat)^2)
 *   ss_tot   = sum((y - mean(y))^2)
 *   r2       = 1 - ss_res/ss_tot
 *   adj_r2   = 1 - (1-r2)*(n-1)/(n-k)
 *   sigma_sq = ss_res / (n-k)
 *   var_beta = sigma_sq * inv(X^T X)
 *   std_err  = sqrt(diag(var_beta))
 *   t_stat   = beta / std_err
 */

#include "core/analytics.hpp"

#include <cmath>
#include <span>
#include <vector>

namespace algoforge::analytics {

/* ========================================================================
 * Matrix helpers (square matrices stored as vector<vector<double>>)
 * ======================================================================== */

using Mat = std::vector<std::vector<double>>;

static Mat mat_zeros(int n) {
    return Mat(n, std::vector<double>(n, 0.0));
}

static Mat mat_identity(int n) {
    Mat m = mat_zeros(n);
    for (int i = 0; i < n; ++i) m[i][i] = 1.0;
    return m;
}

/* Gauss-Jordan elimination with partial pivoting.
 * Returns the inverse of A, or identity if A is singular (degenerate). */
static Mat gauss_jordan_inverse(Mat A) {
    const int n = static_cast<int>(A.size());
    Mat inv = mat_identity(n);

    for (int col = 0; col < n; ++col) {
        /* partial pivot: find row with largest abs value in this column */
        int pivot_row = col;
        double max_val = std::abs(A[col][col]);
        for (int row = col + 1; row < n; ++row) {
            if (std::abs(A[row][col]) > max_val) {
                max_val   = std::abs(A[row][col]);
                pivot_row = row;
            }
        }

        if (max_val < 1e-15) {
            /* singular — return identity as fallback */
            return mat_identity(n);
        }

        /* swap rows */
        if (pivot_row != col) {
            std::swap(A[col], A[pivot_row]);
            std::swap(inv[col], inv[pivot_row]);
        }

        /* scale pivot row */
        double diag = A[col][col];
        for (int j = 0; j < n; ++j) {
            A[col][j]   /= diag;
            inv[col][j] /= diag;
        }

        /* eliminate column from all other rows */
        for (int row = 0; row < n; ++row) {
            if (row == col) continue;
            double factor = A[row][col];
            for (int j = 0; j < n; ++j) {
                A[row][j]   -= factor * A[col][j];
                inv[row][j] -= factor * inv[col][j];
            }
        }
    }

    return inv;
}

/* X^T X: (k x n) * (n x k) = (k x k) */
static Mat xtx(const std::vector<std::vector<double>>& X) {
    const int n = static_cast<int>(X.size());
    const int k = static_cast<int>(X[0].size());
    Mat result = mat_zeros(k);
    for (int i = 0; i < k; ++i)
        for (int j = 0; j < k; ++j)
            for (int r = 0; r < n; ++r)
                result[i][j] += X[r][i] * X[r][j];
    return result;
}

/* X^T y: (k x n) * (n) = (k) */
static std::vector<double> xty(const std::vector<std::vector<double>>& X,
                                std::span<const double> y) {
    const int n = static_cast<int>(X.size());
    const int k = static_cast<int>(X[0].size());
    std::vector<double> result(k, 0.0);
    for (int i = 0; i < k; ++i)
        for (int r = 0; r < n; ++r)
            result[i] += X[r][i] * y[r];
    return result;
}

/* Matrix-vector product M * v, square M (k x k) */
static std::vector<double> mat_vec(const Mat& M, const std::vector<double>& v) {
    const int k = static_cast<int>(M.size());
    std::vector<double> result(k, 0.0);
    for (int i = 0; i < k; ++i)
        for (int j = 0; j < k; ++j)
            result[i] += M[i][j] * v[j];
    return result;
}

/* ========================================================================
 * OLS regression
 * ======================================================================== */

OLSResult ols_regression(
    const std::vector<std::vector<double>>& X,
    std::span<const double>                  y
) noexcept {
    OLSResult res;

    const int n = static_cast<int>(X.size());
    if (n == 0 || y.size() == 0) return res;

    const int k = static_cast<int>(X[0].size());
    if (n < k + 1) return res;   /* not enough observations */

    /* ── beta = inv(X^T X) X^T y ── */
    Mat    XtX_mat = xtx(X);
    Mat    XtX_inv = gauss_jordan_inverse(XtX_mat);
    auto   Xty_vec = xty(X, y);
    auto   beta    = mat_vec(XtX_inv, Xty_vec);

    res.beta = beta;

    /* ── residuals ── */
    res.residuals.resize(n);
    for (int i = 0; i < n; ++i) {
        double y_hat = 0.0;
        for (int j = 0; j < k; ++j) y_hat += X[i][j] * beta[j];
        res.residuals[i] = y[i] - y_hat;
    }

    /* ── R² ── */
    double ss_res = 0.0;
    for (double r : res.residuals) ss_res += r * r;

    double mean_y = 0.0;
    for (double v : y) mean_y += v;
    mean_y /= n;

    double ss_tot = 0.0;
    for (double v : y) {
        double d = v - mean_y;
        ss_tot += d * d;
    }

    res.r2     = (ss_tot > 1e-15) ? 1.0 - ss_res / ss_tot : 0.0;
    res.adj_r2 = (n > k) ? 1.0 - (1.0 - res.r2) * (n - 1.0) / (n - k) : 0.0;

    /* ── std errors and t-stats ── */
    double sigma_sq = (n > k) ? ss_res / (n - k) : 0.0;

    res.std_errors.resize(k);
    res.t_stats.resize(k);
    for (int j = 0; j < k; ++j) {
        double var_j = sigma_sq * XtX_inv[j][j];
        res.std_errors[j] = (var_j > 0.0) ? std::sqrt(var_j) : 0.0;
        res.t_stats[j]    = (res.std_errors[j] > 1e-15) ? beta[j] / res.std_errors[j] : 0.0;
    }

    return res;
}

} /* namespace algoforge::analytics */
