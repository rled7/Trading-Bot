/**
 * robustness.js — Parameter robustness sweep.
 *
 * Same algorithm as Python robustness.py:
 *   1. Collect all numeric parameters.
 *   2. Finite-difference Sharpe gradient over 50-bar quick replay.
 *   3. Select top-3 most-impactful parameters.
 *   4. 27-cell Cartesian product ±20% perturbations.
 *   5. Return % of cells passing the gate.
 *
 * Must produce ±0.01 of Python's output given identical bars + manifest.
 *
 * Public API:
 *   parameterRobustness(manifest, bars, seed, cfg?) -> number (0-100)
 */

import { BTConfig, BacktestEngine } from '../backtest.js';
import { ManifestAlgo } from './runtime.js';

// ── Gate thresholds ───────────────────────────────────────────────────────────

const SHARPE_GATE   = 0.5;
const MAX_DD_GATE   = 25.0;
const MIN_TRADES    = 20;
const PERTURB_FRAC  = 0.20;
const MAX_PARAMS    = 3;
const QUICK_BARS    = 50;

// ── Internal helpers ──────────────────────────────────────────────────────────

function runBacktest(manifest, bars, cfg) {
    const algo   = new ManifestAlgo(manifest);
    const engine = new BacktestEngine(algo, cfg);
    return engine.run(bars, 'EURUSD', 3600);
}

function passesGate(result) {
    if (result.trades.length < MIN_TRADES) return false;
    const eq = result.equityCurve;
    if (eq.length === 0) return false;
    for (const v of eq) {
        if (!isFinite(v)) return false;
    }
    if (result.sharpeRatio() < SHARPE_GATE) return false;
    if (result.maxDrawdownPct() > MAX_DD_GATE) return false;
    return true;
}

/**
 * Collect all numeric parameters with their (path, value) pairs.
 * Paths mirror Python: "ind.{id}.{key}" or "risk.atr_mult" etc.
 */
function collectNumericParams(manifest) {
    const params = [];
    // Indicator params
    for (const ind of manifest.indicators) {
        for (const [k, v] of Object.entries(ind.params)) {
            if (typeof v === 'number' && typeof v !== 'boolean') {
                params.push({ path: `ind.${ind.id}.${k}`, value: v });
            }
        }
    }
    // Risk params
    const risk = manifest.risk;
    if (risk.size === 'atr') {
        params.push({ path: 'risk.atr_mult', value: risk.atrMult });
    } else {
        params.push({ path: 'risk.fixed_lots', value: risk.fixedLots });
    }
    if (risk.coolDownBars > 0) {
        params.push({ path: 'risk.cool_down_bars', value: risk.coolDownBars });
    }
    return params;
}

/**
 * Return a copy of manifest with one parameter set to a new value.
 * Mirrors Python _apply_param.
 */
function applyParam(manifest, path, newVal) {
    // Deep-copy indicators
    const newIndicators = manifest.indicators.map(ind => {
        const prefix = `ind.${ind.id}.`;
        if (path.startsWith(prefix)) {
            const paramName = path.slice(prefix.length);
            const newParams = { ...ind.params };
            const origVal   = ind.params[paramName];
            if (typeof origVal === 'number' && Number.isInteger(origVal)) {
                newParams[paramName] = Math.max(1, Math.round(newVal));
            } else {
                newParams[paramName] = newVal;
            }
            return { id: ind.id, kind: ind.kind, params: newParams };
        }
        return ind;
    });

    // Risk copy
    let risk = { ...manifest.risk };
    if (path === 'risk.atr_mult') {
        risk = { ...risk, atrMult: Math.max(0.1, newVal) };
    } else if (path === 'risk.fixed_lots') {
        risk = { ...risk, fixedLots: Math.max(0.01, newVal) };
    } else if (path === 'risk.cool_down_bars') {
        risk = { ...risk, coolDownBars: Math.max(0, Math.round(newVal)) };
    }

    return {
        ...manifest,
        indicators: newIndicators,
        risk,
    };
}

/**
 * Compute parameter robustness score (0-100).
 *
 * @param {import('./types.js').AlgoManifest} manifest
 * @param {object[]} bars
 * @param {number}   seed   (unused in robustness — kept for API parity)
 * @param {BTConfig} [cfg]
 * @returns {number}
 */
export function parameterRobustness(manifest, bars, seed, cfg) {
    if (!cfg) {
        cfg = new BTConfig({ warmupBars: Math.min(200, Math.max(20, Math.floor(bars.length / 4))) });
    }

    const allParams = collectNumericParams(manifest);

    if (allParams.length === 0) {
        // No numeric parameters — perfect robustness
        return 100.0;
    }

    // Quick-replay bars
    const quickBarsN = Math.max(
        cfg.warmupBars + 20,
        Math.min(QUICK_BARS + cfg.warmupBars, bars.length),
    );
    const quickBars = bars.slice(0, quickBarsN);

    // Baseline Sharpe
    let baseSharpe = 0;
    try {
        const baseResult = runBacktest(manifest, quickBars, cfg);
        baseSharpe = baseResult.sharpeRatio();
    } catch (_) {
        baseSharpe = 0;
    }

    // Finite-difference gradients
    const gradients = [];
    for (const { path, value } of allParams) {
        const delta = Math.abs(value) > 1e-6 ? Math.abs(value) * PERTURB_FRAC : 0.1;

        let sPlus = baseSharpe;
        try {
            const mPlus  = applyParam(manifest, path, value + delta);
            const rPlus  = runBacktest(mPlus, quickBars, cfg);
            sPlus = rPlus.sharpeRatio();
        } catch (_) {}

        let sMinus = baseSharpe;
        try {
            const mMinus = applyParam(manifest, path, value - delta);
            const rMinus = runBacktest(mMinus, quickBars, cfg);
            sMinus = rMinus.sharpeRatio();
        } catch (_) {}

        const grad = delta > 1e-12 ? Math.abs(sPlus - sMinus) / (2 * delta) : 0;
        gradients.push({ grad, path, value });
    }

    // Sort descending by gradient, pick top MAX_PARAMS
    gradients.sort((a, b) => b.grad - a.grad);
    const topParams = gradients.slice(0, MAX_PARAMS);

    if (topParams.length === 0) return 100.0;

    // Build perturbation values [-20%, 0%, +20%] for each selected param
    const perturbationValues = topParams.map(({ value }) => {
        const delta = Math.abs(value) > 1e-6 ? Math.abs(value) * PERTURB_FRAC : 0.1;
        return [value - delta, value, value + delta];
    });

    // Cartesian product over all perturbed combos
    let passCount = 0;
    let total     = 0;

    for (const combo of cartesian(perturbationValues)) {
        let perturbedManifest = manifest;
        for (let i = 0; i < topParams.length; i++) {
            perturbedManifest = applyParam(perturbedManifest, topParams[i].path, combo[i]);
        }

        try {
            const result = runBacktest(perturbedManifest, bars, cfg);
            if (passesGate(result)) passCount++;
        } catch (_) {
            // treat as failure
        }
        total++;
    }

    if (total === 0) return 0;
    return passCount / total * 100;
}

/**
 * Cartesian product of arrays.
 * @param {any[][]} arrays
 * @yields {any[]}
 */
function* cartesian(arrays) {
    if (arrays.length === 0) { yield []; return; }
    const [first, ...rest] = arrays;
    for (const v of first) {
        for (const tail of cartesian(rest)) {
            yield [v, ...tail];
        }
    }
}
