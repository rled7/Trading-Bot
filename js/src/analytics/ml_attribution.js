/**
 * ml_attribution.js — OLS linear regression using linalg.js.
 * Spec §4 (ML Attribution section).
 * No external dependencies.
 */

import { matMul, transpose, matInv, matVec } from './linalg.js';

/**
 * Ordinary Least Squares regression.
 * X must already include an intercept column (column of 1s) if desired.
 *
 * @param {number[][]} X  n x k design matrix (rows = observations)
 * @param {number[]}   y  n-vector of responses
 * @returns {{
 *   beta:      number[],
 *   residuals: number[],
 *   r2:        number,
 *   adjR2:     number,
 *   tStats:    number[],
 *   stdErrors: number[],
 *   yHat:      number[],
 *   ssRes:     number,
 *   ssTot:     number,
 * }|null}  null if X^T X is singular
 */
export function ols(X, y) {
    const n = X.length;
    const k = X[0].length;
    if (n < k) return null;

    const Xt    = transpose(X);
    const XtX   = matMul(Xt, X);
    const XtXinv = matInv(XtX);
    if (XtXinv === null) return null;

    const Xty  = matVec(Xt, y);
    const beta = matVec(XtXinv, Xty);

    // y_hat = X @ beta
    const yHat     = matVec(X, beta);
    const residuals = y.map((yi, i) => yi - yHat[i]);

    // SS
    let ssRes = 0;
    for (const r of residuals) ssRes += r * r;
    const yMean = y.reduce((s, v) => s + v, 0) / n;
    let ssTot = 0;
    for (const yi of y) { const d = yi - yMean; ssTot += d * d; }

    const r2    = ssTot < 1e-14 ? 1 : 1 - ssRes / ssTot;
    const adjR2 = ssTot < 1e-14 ? 1 : 1 - (1 - r2) * (n - 1) / (n - k);

    // Variance of beta: sigma^2 * (X^T X)^-1
    const sigmaSq  = n > k ? ssRes / (n - k) : 0;
    const stdErrors = new Array(k);
    const tStats    = new Array(k);
    for (let j = 0; j < k; j++) {
        const varj = sigmaSq * XtXinv[j][j];
        stdErrors[j] = varj >= 0 ? Math.sqrt(varj) : 0;
        tStats[j]    = stdErrors[j] < 1e-14 ? 0 : beta[j] / stdErrors[j];
    }

    return { beta, residuals, r2, adjR2, tStats, stdErrors, yHat, ssRes, ssTot };
}

/**
 * Convenience: prepend intercept column to a feature matrix.
 * @param {number[][]} X  n x (k-1) raw features
 * @returns {number[][]}  n x k with intercept first
 */
export function addIntercept(X) {
    return X.map(row => [1, ...row]);
}
