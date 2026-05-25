/**
 * types.js — Type constants and factory functions for algo_gen.
 *
 * Mirrors python/algoforge/algo_gen/types.py.
 * Plain JS objects with JSDoc types (no classes for data structs).
 */

// ── TierName constants ────────────────────────────────────────────────────────

export const TierName = Object.freeze({
    red:    'red',
    orange: 'orange',
    yellow: 'yellow',
    green:  'green',
    white:  'white',
});

// ── Factory helpers ───────────────────────────────────────────────────────────

/**
 * @typedef {object} IndicatorDecl
 * @property {string} id     - local label used in expressions
 * @property {string} kind   - indicator function name
 * @property {object} params - parameter map
 */

/**
 * @typedef {object} EntryRule
 * @property {string} side  - 'long' | 'short'
 * @property {string} when  - DSL expression string
 */

/**
 * @typedef {object} ExitRule
 * @property {string}       side   - 'long' | 'short'
 * @property {string|null}  when   - DSL expression (may be null for bracket-only)
 * @property {number|null}  slAtr  - ATR multiplier for stop-loss
 * @property {number|null}  tpAtr  - ATR multiplier for take-profit
 */

/**
 * @typedef {object} RiskSpec
 * @property {string}  size          - 'atr' | 'fixed'
 * @property {number}  atrMult
 * @property {number}  fixedLots
 * @property {number}  maxConcurrent
 * @property {boolean} hedge
 * @property {number}  coolDownBars
 */

/**
 * @typedef {object} AlgoManifest
 * @property {string}          schemaVersion
 * @property {string}          name
 * @property {string}          description
 * @property {string}          rationale
 * @property {string[]}        timeframes
 * @property {string[]|string} symbols   - list of symbols or 'any'
 * @property {IndicatorDecl[]} indicators
 * @property {EntryRule[]}     entries
 * @property {ExitRule[]}      exits
 * @property {RiskSpec}        risk
 * @property {string|null}     code      - escape-hatch Python code
 */

/**
 * @typedef {object} StageResult
 * @property {number}  stage
 * @property {string}  name
 * @property {boolean} passed
 * @property {string}  reason
 * @property {object}  metrics
 * @property {boolean} [skipped]  - set for escape-hatch skip records
 */

/**
 * @typedef {object} TierReport
 * @property {string}        tier
 * @property {number}        minScore
 * @property {number}        walkForward
 * @property {number}        mcBootstrap
 * @property {number}        robustness
 * @property {number}        sizeMult
 * @property {boolean}       experimental
 * @property {boolean}       paperOnly
 * @property {StageResult[]} stages
 */

/**
 * @typedef {object} ValidationResult
 * @property {boolean}        passed
 * @property {StageResult[]}  stages
 * @property {TierReport|null} report
 * @property {string}         manifestName
 */

/**
 * Create a StageResult object.
 * @param {number}  stage
 * @param {string}  name
 * @param {boolean} passed
 * @param {string}  [reason='']
 * @param {object}  [metrics={}]
 * @returns {StageResult}
 */
export function makeStageResult(stage, name, passed, reason = '', metrics = {}) {
    return { stage, name, passed, reason, metrics };
}

/**
 * Create a TierReport object.
 * @param {object} opts
 * @returns {TierReport}
 */
export function makeTierReport({
    tier,
    minScore,
    walkForward,
    mcBootstrap,
    robustness,
    sizeMult,
    experimental,
    paperOnly,
    stages = [],
}) {
    return { tier, minScore, walkForward, mcBootstrap, robustness, sizeMult, experimental, paperOnly, stages };
}

/**
 * Create a ValidationResult object.
 * @param {object} opts
 * @returns {ValidationResult}
 */
export function makeValidationResult({ passed, stages, report = null, manifestName = '' }) {
    return { passed, stages, report, manifestName };
}
