/**
 * sandbox.js — Stage 2 sandbox backtest.
 *
 * For DSL manifests: run the existing JS backtest engine on canonical bar fixtures.
 * For escape-hatch manifests (code != null): return a deterministic skip record.
 *
 * Mirrors python/algoforge/algo_gen/sandbox.py (escape-hatch path only skips in JS).
 *
 * Public API:
 *   runSandboxBacktest(manifest, bars, cfg) -> { btResult, stageResult }
 */

import { BTConfig, BacktestEngine } from '../backtest.js';
import { ManifestAlgo } from './runtime.js';
import { makeStageResult } from './types.js';

// ── Gate thresholds (mirrors validator.py) ────────────────────────────────────

const MIN_TRADES = 20;

/**
 * Run stage 2 sandbox backtest.
 *
 * @param {import('./types.js').AlgoManifest} manifest
 * @param {object[]} bars
 * @param {BTConfig} [cfg]
 * @returns {{ btResult: object|null, stageResult: import('./types.js').StageResult }}
 */
export function runSandboxBacktest(manifest, bars, cfg) {
    if (!cfg) cfg = new BTConfig();

    // Escape-hatch: skip (per spec §10 — Python-only)
    if (manifest.code !== null && manifest.code !== undefined) {
        const skipResult = makeStageResult(
            2,
            'sandbox_backtest',
            true,   // passed=true (skip is not a failure)
            'escape_hatch_python_only',
            { skipped: true },
        );
        skipResult.skipped = true;
        return { btResult: null, stageResult: skipResult };
    }

    // DSL path: run JS backtest engine
    let btResult;
    try {
        const algo   = new ManifestAlgo(manifest);
        const engine = new BacktestEngine(algo, cfg);
        btResult     = engine.run(bars, 'EURUSD', 3600);
    } catch (err) {
        return {
            btResult: null,
            stageResult: makeStageResult(
                2, 'sandbox_backtest', false,
                `backtest error: ${err.message}`,
            ),
        };
    }

    // Gate: minimum trade count
    if (btResult.trades.length < MIN_TRADES) {
        return {
            btResult,
            stageResult: makeStageResult(
                2, 'sandbox_backtest', false,
                `too few trades: ${btResult.trades.length} < ${MIN_TRADES}`,
                { trades: btResult.trades.length },
            ),
        };
    }

    // Gate: no NaN/inf in equity curve
    for (const v of btResult.equityCurve) {
        if (!isFinite(v)) {
            return {
                btResult,
                stageResult: makeStageResult(
                    2, 'sandbox_backtest', false,
                    'NaN or inf detected in equity curve',
                    { trades: btResult.trades.length },
                ),
            };
        }
    }

    return {
        btResult,
        stageResult: makeStageResult(
            2, 'sandbox_backtest', true,
            'Sandbox backtest passed',
            {
                trades:     btResult.trades.length,
                sharpe:     Math.round(btResult.sharpeRatio()    * 10000) / 10000,
                max_dd_pct: Math.round(btResult.maxDrawdownPct() * 10000) / 10000,
                net_profit: Math.round(btResult.netProfit()      * 10000) / 10000,
            },
        ),
    };
}
