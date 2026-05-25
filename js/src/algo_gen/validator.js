/**
 * validator.js — 5-stage validation pipeline for AlgoManifest.
 *
 * Stages:
 *   1. Schema + lint
 *   2. Sandbox backtest
 *   3. Walk-forward
 *   4. Monte Carlo bootstrap
 *   5. Parameter robustness
 *
 * Mirrors python/algoforge/algo_gen/validator.py exactly.
 *
 * Public API:
 *   validate(manifestDict, bars, seed, opts?) -> Promise<ValidationResult>
 */

import { BTConfig } from '../backtest.js';
import { mcBootstrap } from '../analytics/montecarlo.js';
import { walkforwardAnchored } from '../analytics/walkforward.js';
import { validateManifest } from './schema.js';
import { compileExpr } from './dsl.js';
import { runSandboxBacktest } from './sandbox.js';
import { parameterRobustness } from './robustness.js';
import { scoreToTier } from './tier.js';
import { makeStageResult, makeValidationResult } from './types.js';
import { ManifestAlgo } from './runtime.js';
import { BacktestEngine } from '../backtest.js';

// ── Gate thresholds ───────────────────────────────────────────────────────────

const MIN_TRADES   = 20;
const SHARPE_GATE  = 0.5;
const MAX_DD_GATE  = 25.0;
const WF_N_WINDOWS = 5;
const MC_N_RUNS    = 1000;

// ── Internal helpers ──────────────────────────────────────────────────────────

function runBacktest(manifest, bars, cfg) {
    const algo   = new ManifestAlgo(manifest);
    const engine = new BacktestEngine(algo, cfg);
    return engine.run(bars, 'EURUSD', 3600);
}

// ── Stage 1: Schema + lint ────────────────────────────────────────────────────

function stage1SchemaLint(manifestDict) {
    let manifest;
    try {
        manifest = validateManifest(manifestDict);
    } catch (err) {
        return {
            manifest: null,
            stageResult: makeStageResult(1, 'schema_lint', false, err.message),
        };
    }

    // DSL path: compile all entry/exit expressions
    if (manifest.code === null || manifest.code === undefined) {
        const indicatorIds = new Set(manifest.indicators.map(ind => ind.id));
        for (const rule of manifest.entries) {
            if (rule.when) {
                try {
                    compileExpr(rule.when, indicatorIds);
                } catch (err) {
                    return {
                        manifest: null,
                        stageResult: makeStageResult(1, 'schema_lint', false, `entry rule DSL error: ${err.message}`),
                    };
                }
            }
        }
        for (const rule of manifest.exits) {
            if (rule.when) {
                try {
                    compileExpr(rule.when, indicatorIds);
                } catch (err) {
                    return {
                        manifest: null,
                        stageResult: makeStageResult(1, 'schema_lint', false, `exit rule DSL error: ${err.message}`),
                    };
                }
            }
        }
    }
    // For escape-hatch: JS does not AST-check code (Python-only)

    return {
        manifest,
        stageResult: makeStageResult(
            1, 'schema_lint', true,
            'Schema and DSL lint passed',
            { indicators: manifest.indicators.length },
        ),
    };
}

// ── Stage 3: Walk-forward ─────────────────────────────────────────────────────

async function stage3WalkForward(manifest, bars, cfg, nWindows) {
    const trainMin = Math.max(cfg.warmupBars + 50, Math.floor(bars.length / 3));

    function backtestFn(trainBars, testBars) {
        try {
            const result = runBacktest(manifest, testBars, cfg);
            return {
                train: {},
                test: {
                    trades:     result.trades.length,
                    sharpe:     result.sharpeRatio(),
                    max_dd_pct: result.maxDrawdownPct(),
                },
            };
        } catch (err) {
            return {
                train: {},
                test: { error: err.message, trades: 0, sharpe: 0, max_dd_pct: 100 },
            };
        }
    }

    let folds;
    try {
        folds = await walkforwardAnchored(backtestFn, bars, nWindows, trainMin);
    } catch (err) {
        return {
            score:       0,
            stageResult: makeStageResult(3, 'walk_forward', false, `walk-forward error: ${err.message}`),
        };
    }

    if (!folds || folds.length === 0) {
        return {
            score:       0,
            stageResult: makeStageResult(3, 'walk_forward', false, 'no folds produced (bars too few for walk-forward split)'),
        };
    }

    let passing    = 0;
    const foldDetails = [];

    for (const foldInfo of folds) {
        const testRes  = foldInfo.result && foldInfo.result.test ? foldInfo.result.test : {};
        const sharpe   = testRes.sharpe     !== undefined ? testRes.sharpe     : 0;
        const maxDd    = testRes.max_dd_pct !== undefined ? testRes.max_dd_pct : 100;
        const trades   = testRes.trades     !== undefined ? testRes.trades     : 0;
        const passed   = sharpe >= SHARPE_GATE && maxDd <= MAX_DD_GATE && trades >= MIN_TRADES;
        if (passed) passing++;
        foldDetails.push({
            fold:    foldInfo.fold,
            sharpe:  Math.round(sharpe * 10000) / 10000,
            max_dd:  Math.round(maxDd  * 10000) / 10000,
            trades,
            passed,
        });
    }

    const nFoldsRun = folds.length;
    const score     = nFoldsRun > 0 ? passing / nFoldsRun * 100 : 0;
    const passed    = passing === nFoldsRun;

    return {
        score,
        stageResult: makeStageResult(
            3, 'walk_forward', passed,
            `${passing}/${nFoldsRun} windows passed` + (passed ? '' : ' (stage failed)'),
            {
                score:   Math.round(score * 100) / 100,
                passing,
                total:   nFoldsRun,
                folds:   foldDetails,
            },
        ),
    };
}

// ── Stage 4: Monte Carlo bootstrap ───────────────────────────────────────────

function stage4MonteCarlo(btResult, seed) {
    if (!btResult || btResult.trades.length === 0) {
        return {
            score:       0,
            stageResult: makeStageResult(4, 'mc_bootstrap', false, 'no trades to resample'),
        };
    }

    let mcRes;
    try {
        mcRes = mcBootstrap({
            trades:         btResult.trades,
            nRuns:          MC_N_RUNS,
            seed,
            initialCapital: btResult.initialCapital,
        });
    } catch (err) {
        return {
            score:       0,
            stageResult: makeStageResult(4, 'mc_bootstrap', false, `MC bootstrap error: ${err.message}`),
        };
    }

    const p5Final  = mcRes.p5Final;
    const probRuin = mcRes.probOfRuin;
    const initial  = btResult.initialCapital;

    // Mirror Python scoring logic exactly
    let score;
    if (initial > 0 && p5Final < 0) {
        score = 0;
    } else if (probRuin > 0.5) {
        score = Math.max(0, (1 - probRuin) * 100);
    } else {
        const retP5 = initial > 1e-6 ? (p5Final - initial) / initial : 0;
        if (retP5 >= 0) {
            score = Math.min(100, 70 + retP5 * 60);
        } else {
            score = Math.max(0, 70 + retP5 * 140);
        }
        score = score * (1 - probRuin);
    }

    const passed = score >= 70;

    return {
        score,
        stageResult: makeStageResult(
            4, 'mc_bootstrap', passed,
            `MC score=${score.toFixed(1)}%` + (passed ? '' : ' (stage failed)'),
            {
                score:        Math.round(score   * 100) / 100,
                p5_final:     Math.round(p5Final * 100) / 100,
                p50_final:    Math.round(mcRes.p50Final * 100) / 100,
                p95_final:    Math.round(mcRes.p95Final * 100) / 100,
                prob_of_ruin: Math.round(probRuin * 10000) / 10000,
            },
        ),
    };
}

// ── Stage 5: Parameter robustness ─────────────────────────────────────────────

function stage5Robustness(manifest, bars, seed, cfg) {
    let score;
    try {
        score = parameterRobustness(manifest, bars, seed, cfg);
    } catch (err) {
        return {
            score:       0,
            stageResult: makeStageResult(5, 'parameter_robustness', false, `robustness error: ${err.message}`),
        };
    }

    const passed = score >= 70;
    return {
        score,
        stageResult: makeStageResult(
            5, 'parameter_robustness', passed,
            `Robustness score=${score.toFixed(1)}%` + (passed ? '' : ' (stage failed)'),
            { score: Math.round(score * 100) / 100 },
        ),
    };
}

// ── Orchestrator ──────────────────────────────────────────────────────────────

/**
 * Run all 5 validation stages. Halts on first failure.
 *
 * @param {object}   manifestDict  raw manifest dict
 * @param {object[]} bars          historical bar data
 * @param {number}   seed          seed for MC bootstrap
 * @param {object}   [opts]
 * @param {BTConfig} [opts.cfg]
 * @param {number}   [opts.nWfWindows=5]
 * @returns {Promise<import('./types.js').ValidationResult>}
 */
export async function validate(manifestDict, bars, seed, opts) {
    const cfg        = (opts && opts.cfg)          ? opts.cfg          : new BTConfig();
    const nWfWindows = (opts && opts.nWfWindows)   ? opts.nWfWindows   : WF_N_WINDOWS;

    const stages = [];

    // ── Stage 1 ───────────────────────────────────────────────────────────────
    const { manifest, stageResult: s1 } = stage1SchemaLint(manifestDict);
    stages.push(s1);
    if (!s1.passed) {
        return makeValidationResult({
            passed:       false,
            stages,
            manifestName: manifestDict.name || '',
        });
    }

    // ── Stage 2 ───────────────────────────────────────────────────────────────
    const { btResult, stageResult: s2 } = runSandboxBacktest(manifest, bars, cfg);

    // Escape-hatch skip: pass through to tier with sentinel scores
    if (s2.skipped) {
        stages.push(s2);
        return makeValidationResult({
            passed:       true,
            stages,
            report: {
                tier: 'skip',
                minScore: 0,
                walkForward: 0,
                mcBootstrap: 0,
                robustness: 0,
                sizeMult: 0,
                experimental: true,
                paperOnly: true,
                stages,
                skipped: true,
                reason: 'escape_hatch_python_only',
            },
            manifestName: manifest.name,
        });
    }

    stages.push(s2);
    if (!s2.passed) {
        return makeValidationResult({
            passed:       false,
            stages,
            manifestName: manifest.name,
        });
    }

    // ── Stage 3 ───────────────────────────────────────────────────────────────
    const { score: wfScore, stageResult: s3 } = await stage3WalkForward(manifest, bars, cfg, nWfWindows);
    stages.push(s3);
    if (!s3.passed) {
        return makeValidationResult({ passed: false, stages, manifestName: manifest.name });
    }

    // ── Stage 4 ───────────────────────────────────────────────────────────────
    const { score: mcScore, stageResult: s4 } = stage4MonteCarlo(btResult, seed);
    stages.push(s4);
    if (!s4.passed) {
        return makeValidationResult({ passed: false, stages, manifestName: manifest.name });
    }

    // ── Stage 5 ───────────────────────────────────────────────────────────────
    const { score: robScore, stageResult: s5 } = stage5Robustness(manifest, bars, seed, cfg);
    stages.push(s5);
    if (!s5.passed) {
        return makeValidationResult({ passed: false, stages, manifestName: manifest.name });
    }

    // All stages passed — compute tier
    const tierReport = scoreToTier(wfScore, mcScore, robScore);
    tierReport.stages = stages;

    return makeValidationResult({
        passed:       true,
        stages,
        report:       tierReport,
        manifestName: manifest.name,
    });
}
