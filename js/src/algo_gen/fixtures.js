/**
 * fixtures.js — Fixture loader for algo_gen tests.
 *
 * Mirrors python/algoforge/algo_gen/fixtures.py.
 * Fixtures live in: tests/fixtures/algo_gen/ (relative to repo root).
 *
 * Public API:
 *   loadBars(fixtureName)              -> Bar[]
 *   loadManifest(fixtureName)          -> object (raw dict)
 *   loadExpectedTierReport(fixtureName) -> object
 */

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join, dirname } from 'node:path';

const _thisDir     = dirname(fileURLToPath(import.meta.url));
const FIXTURES_DIR = join(_thisDir, '..', '..', '..', 'tests', 'fixtures', 'algo_gen');

function loadJson(path) {
    try {
        const raw = readFileSync(path, 'utf-8');
        return JSON.parse(raw);
    } catch (err) {
        throw new Error(`fixture not found: ${path} (${err.message})`);
    }
}

/**
 * Load a bar fixture and return an array of bar objects.
 * @param {string} fixtureName  e.g. 'bars_eurusd_h1_2y.json'
 * @returns {object[]}
 */
export function loadBars(fixtureName) {
    const path = join(FIXTURES_DIR, fixtureName);
    const data = loadJson(path);
    const rawBars = (data && data.bars) ? data.bars : data;
    return rawBars.map(b => ({
        timestamp: b.timestamp,
        open:      b.open,
        high:      b.high,
        low:       b.low,
        close:     b.close,
        volume:    b.volume,
        spread:    b.spread !== undefined ? b.spread : 0,
    }));
}

/**
 * Load a manifest fixture and return the raw dict.
 * @param {string} fixtureName  e.g. 'manifest_trend_follow_green.json'
 * @returns {object}
 */
export function loadManifest(fixtureName) {
    const path = join(FIXTURES_DIR, fixtureName);
    return loadJson(path);
}

/**
 * Load an expected TierReport fixture.
 * @param {string} fixtureName  e.g. 'tier_report_trend_follow_green.json'
 * @returns {object}
 */
export function loadExpectedTierReport(fixtureName) {
    const path = join(FIXTURES_DIR, fixtureName);
    return loadJson(path);
}
