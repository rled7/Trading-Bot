/**
 * tier.js — Composite tier scorer for the S1 algorithm generator.
 *
 * The tier is the MINIMUM of the three component scores.
 * Mirrors python/algoforge/algo_gen/tier.py exactly.
 *
 * Public API:
 *   scoreToTier(walkForward, mc, robustness, cfg?) -> TierReport
 */

import { TierName, makeTierReport } from './types.js';

// ── Hard-coded defaults (mirrors Python _DEFAULT_CFG) ─────────────────────────

const DEFAULT_CFG = {
    orange_size_mult:              0.25,
    yellow_size_mult:              0.50,
    green_size_mult:               1.00,
    white_size_mult:               1.50,
    require_manual_review_below:   'green',
    paper_only_below:              'yellow',
};

// Tier thresholds (min_score → tier), sorted descending
const TIER_THRESHOLDS = [
    { threshold: 95.0, tier: TierName.white },
    { threshold: 90.0, tier: TierName.green },
    { threshold: 80.0, tier: TierName.yellow },
    { threshold: 70.0, tier: TierName.orange },
];
// Below 70 → red

/**
 * Merge user config overrides into defaults.
 * @param {object|null} cfg
 * @returns {object}
 */
function loadCfg(cfg) {
    const resolved = { ...DEFAULT_CFG };
    if (cfg) Object.assign(resolved, cfg);
    return resolved;
}

/**
 * Determine tier from min score.
 * @param {number} minScore
 * @returns {string}  TierName value
 */
export function tierFromMinScore(minScore) {
    for (const { threshold, tier } of TIER_THRESHOLDS) {
        if (minScore >= threshold) return tier;
    }
    return TierName.red;
}

/**
 * Assign a tier from three component scores.
 *
 * @param {number}      walkForward
 * @param {number}      mc
 * @param {number}      robustness
 * @param {object|null} [cfg]  optional overrides
 * @returns {import('./types.js').TierReport}
 */
export function scoreToTier(walkForward, mc, robustness, cfg) {
    const loadedCfg = loadCfg(cfg || null);
    const minScore  = Math.min(walkForward, mc, robustness);
    const tier      = tierFromMinScore(minScore);

    // Resolve size multiplier
    const sizeMult = {
        [TierName.red]:    0.0,
        [TierName.orange]: Number(loadedCfg.orange_size_mult),
        [TierName.yellow]: Number(loadedCfg.yellow_size_mult),
        [TierName.green]:  Number(loadedCfg.green_size_mult),
        [TierName.white]:  Number(loadedCfg.white_size_mult),
    }[tier];

    // Experimental flag: orange and yellow (and red)
    const experimental = tier === TierName.red || tier === TierName.orange || tier === TierName.yellow;

    // Paper-only flag: depends on paper_only_below config
    const paperOnlyBelow = loadedCfg.paper_only_below;
    let paperOnly;
    if (paperOnlyBelow === 'yellow') {
        paperOnly = tier === TierName.red || tier === TierName.orange;
    } else if (paperOnlyBelow === 'green') {
        paperOnly = tier === TierName.red || tier === TierName.orange || tier === TierName.yellow;
    } else {
        paperOnly = tier === TierName.red || tier === TierName.orange;
    }

    // Round to 4 decimal places (mirrors Python round(x, 4))
    const round4 = x => Math.round(x * 10000) / 10000;

    return makeTierReport({
        tier,
        minScore:    round4(minScore),
        walkForward: round4(walkForward),
        mcBootstrap: round4(mc),
        robustness:  round4(robustness),
        sizeMult,
        experimental,
        paperOnly,
        stages: [],
    });
}
