/**
 * promote.js — Registry promotion for validated algorithms.
 *
 * Mirrors python/algoforge/algo_gen/promote.py.
 *
 * Public API:
 *   promote(report, manifest, registryDir, opts?) -> ManifestAlgo
 */

import { writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { ManifestAlgo } from './runtime.js';
import { TierName } from './types.js';

// Tier color codes (mirrors Python _TIER_COLORS)
const TIER_COLORS = {
    [TierName.red]:    '#ff4444',
    [TierName.orange]: '#ff8800',
    [TierName.yellow]: '#ffdd00',
    [TierName.green]:  '#00ff88',
    [TierName.white]:  '#ffffff',
};

export class PromotionError extends Error {
    constructor(msg) { super(msg); this.name = 'PromotionError'; }
}

/**
 * Promote a validated manifest into the registry.
 * Refuses red tier. Returns a ManifestAlgo with .metadata populated.
 *
 * @param {import('./types.js').TierReport} report
 * @param {import('./types.js').AlgoManifest} manifest
 * @param {string} registryDir
 * @param {object} [opts]
 * @param {object|null} [opts.cfg]              tier config overrides
 * @param {object|null} [opts.generatorInfo]    generator metadata
 * @param {string}      [opts.brief]            brief description
 * @param {boolean}     [opts.confirm]          required for orange/yellow
 * @returns {ManifestAlgo}
 */
export function promote(report, manifest, registryDir, opts) {
    const cfg           = (opts && opts.cfg)           ? opts.cfg           : null;
    const generatorInfo = (opts && opts.generatorInfo)  ? opts.generatorInfo  : {};
    const brief         = (opts && opts.brief)          ? opts.brief          : '';
    const confirm       = (opts && opts.confirm)        ? opts.confirm        : false;

    // Refuse red tier
    if (report.tier === TierName.red) {
        throw new PromotionError(
            `Cannot promote algorithm '${manifest.name}': ` +
            `tier is red (min_score=${report.minScore.toFixed(2)} < 70). ` +
            `Red-tier algorithms are never promoted.`
        );
    }

    // Check manual review gate (orange/yellow require confirm=true)
    const requireBelow = (cfg && cfg.require_manual_review_below) || 'green';
    const tiersRequiringConfirm = new Set();
    if (requireBelow === 'green' || requireBelow === 'white') {
        tiersRequiringConfirm.add(TierName.orange);
        tiersRequiringConfirm.add(TierName.yellow);
    } else if (requireBelow === 'yellow') {
        tiersRequiringConfirm.add(TierName.orange);
    }

    if (tiersRequiringConfirm.has(report.tier) && !confirm) {
        throw new PromotionError(
            `Cannot promote '${manifest.name}' (tier=${report.tier}) ` +
            `without confirm=true. This tier requires manual review.`
        );
    }

    // Build metadata dict per spec §7
    const createdAt    = new Date().toISOString().replace(/\.\d{3}Z$/, 'Z');
    mkdirSync(registryDir, { recursive: true });
    const manifestPath = join(registryDir, `${manifest.name}.json`);

    const metadata = {
        source:         'algo_gen',
        schema_version: manifest.schemaVersion,
        tier:           report.tier,
        tier_color:     TIER_COLORS[report.tier] || '#888888',
        scores: {
            walk_forward: report.walkForward,
            mc_bootstrap: report.mcBootstrap,
            robustness:   report.robustness,
        },
        min_score:     report.minScore,
        size_mult:     report.sizeMult,
        experimental:  report.experimental,
        paper_only:    report.paperOnly,
        created_at:    createdAt,
        generator:     generatorInfo,
        brief,
        manifest_path: manifestPath,
    };

    // Serialise manifest to JSON
    const manifestDict = manifestToDict(manifest);
    manifestDict._metadata = metadata;

    writeFileSync(manifestPath, JSON.stringify(manifestDict, null, 2));

    const algo      = new ManifestAlgo(manifest);
    algo.metadata   = metadata;
    return algo;
}

/**
 * Serialise an AlgoManifest to a plain dict.
 */
function manifestToDict(manifest) {
    return {
        schema_version: manifest.schemaVersion,
        name:           manifest.name,
        description:    manifest.description,
        rationale:      manifest.rationale,
        timeframes:     manifest.timeframes,
        symbols:        manifest.symbols,
        indicators: manifest.indicators.map(i => ({
            id:     i.id,
            kind:   i.kind,
            params: i.params,
        })),
        entries: manifest.entries.map(e => ({
            side: e.side,
            when: e.when,
        })),
        exits: manifest.exits.map(x => {
            const obj = { side: x.side };
            if (x.when   !== null) obj.when   = x.when;
            if (x.slAtr  !== null) obj.sl_atr = x.slAtr;
            if (x.tpAtr  !== null) obj.tp_atr = x.tpAtr;
            return obj;
        }),
        risk: {
            size:           manifest.risk.size,
            atr_mult:       manifest.risk.atrMult,
            fixed_lots:     manifest.risk.fixedLots,
            max_concurrent: manifest.risk.maxConcurrent,
            hedge:          manifest.risk.hedge,
            cool_down_bars: manifest.risk.coolDownBars,
        },
        code: manifest.code,
    };
}
