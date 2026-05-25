/**
 * index.js — Public API for js/src/algo_gen/.
 *
 * Exports the validator, tier scorer, schema validator, DSL compiler,
 * ManifestAlgo runtime, promote, and fixtures loader.
 */

export { TierName, makeStageResult, makeTierReport, makeValidationResult } from './types.js';
export { validateManifest, INDICATOR_KINDS } from './schema.js';
export { compileExpr, BAR_FIELDS, PATTERN_NAMES } from './dsl.js';
export { ManifestAlgo } from './runtime.js';
export { runSandboxBacktest } from './sandbox.js';
export { parameterRobustness } from './robustness.js';
export { scoreToTier, tierFromMinScore } from './tier.js';
export { validate } from './validator.js';
export { promote, PromotionError } from './promote.js';
export { loadBars, loadManifest, loadExpectedTierReport } from './fixtures.js';
