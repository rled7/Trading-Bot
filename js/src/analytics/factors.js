/**
 * factors.js — Fama-French-style multi-factor OLS regression.
 * Spec §4 (Factors section).
 * Uses the same OLS solver from ml_attribution.js.
 */

import { ols } from './ml_attribution.js';

/**
 * Multi-factor regression: strategy_returns = alpha + sum(beta_f * factor_f) + eps
 *
 * @param {number[]}              strategyReturns  n bar returns of the strategy
 * @param {Record<string,number[]>} factorReturns   dict of factor name -> n bar returns
 * @returns {{
 *   alpha:       number,
 *   betas:       Record<string, number>,
 *   tStats:      Record<string, number>,
 *   stdErrors:   Record<string, number>,
 *   r2:          number,
 *   adjR2:       number,
 *   residuals:   number[],
 * }|null}
 */
export function factorRegression(strategyReturns, factorReturns) {
    const factorNames = Object.keys(factorReturns);
    const n = strategyReturns.length;
    if (n === 0) return null;

    // Build design matrix: [1, f1, f2, ...]
    const X = [];
    for (let i = 0; i < n; i++) {
        const row = [1];
        for (const name of factorNames) {
            row.push(factorReturns[name][i] ?? 0);
        }
        X.push(row);
    }

    const result = ols(X, strategyReturns);
    if (result === null) return null;

    const { beta, residuals, r2, adjR2, tStats, stdErrors } = result;

    // beta[0] = alpha (intercept), beta[1..k] = factor betas
    const betas    = {};
    const tStatMap = {};
    const stdErrMap = {};
    for (let f = 0; f < factorNames.length; f++) {
        const name = factorNames[f];
        betas[name]    = beta[f + 1];
        tStatMap[name] = tStats[f + 1];
        stdErrMap[name] = stdErrors[f + 1];
    }

    return {
        alpha:      beta[0],
        betas,
        tStats:     tStatMap,
        stdErrors:  stdErrMap,
        r2,
        adjR2,
        residuals,
    };
}
