/**
 * promote.test.js — Unit tests for algo_gen/promote.js
 */
import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, existsSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { promote, PromotionError } from '../../src/algo_gen/promote.js';
import { validateManifest } from '../../src/algo_gen/schema.js';
import { scoreToTier } from '../../src/algo_gen/tier.js';
import { ManifestAlgo } from '../../src/algo_gen/runtime.js';

// ── Fixtures ──────────────────────────────────────────────────────────────────

const MANIFEST_DICT = {
    schema_version: '1.0',
    name:           'test-promote-algo',
    description:    'Algorithm for promotion tests',
    rationale:      'Testing promote function',
    timeframes:     ['H1'],
    symbols:        ['EURUSD'],
    indicators: [
        { id: 'ema9',  kind: 'ema', params: { period: 9  } },
        { id: 'rsi14', kind: 'rsi', params: { period: 14 } },
    ],
    entries: [{ side: 'long', when: 'ema9 > 0 and rsi14 > 50' }],
    exits:   [{ side: 'long', sl_atr: 1.5, tp_atr: 3.0 }],
    risk: {
        size: 'atr', atr_mult: 1.5, fixed_lots: 0.01,
        max_concurrent: 1, hedge: false, cool_down_bars: 0,
    },
};

function makeTmpDir() {
    return mkdtempSync(join(tmpdir(), 'algo_promote_test_'));
}

function makeGreenReport() {
    return scoreToTier(91.0, 90.5, 90.2);
}

function makeOrangeReport() {
    return scoreToTier(79.0, 74.0, 72.0);
}

function makeRedReport() {
    return scoreToTier(60.0, 55.0, 50.0);
}

// ── Tests ─────────────────────────────────────────────────────────────────────

describe('promote: PromotionError', () => {
    it('is an Error subclass', () => {
        const err = new PromotionError('test');
        assert.ok(err instanceof Error);
        assert.ok(err instanceof PromotionError);
        assert.strictEqual(err.name, 'PromotionError');
        assert.strictEqual(err.message, 'test');
    });
});

describe('promote: green tier — happy path', () => {
    it('promotes a green tier algo and returns ManifestAlgo', () => {
        const tmpDir = makeTmpDir();
        try {
            const manifest = validateManifest(MANIFEST_DICT);
            const report   = makeGreenReport();
            const result   = promote(report, manifest, tmpDir);
            assert.ok(result instanceof ManifestAlgo);
        } finally {
            rmSync(tmpDir, { recursive: true, force: true });
        }
    });

    it('writes manifest JSON to registry dir', () => {
        const tmpDir = makeTmpDir();
        try {
            const manifest = validateManifest(MANIFEST_DICT);
            const report   = makeGreenReport();
            promote(report, manifest, tmpDir);
            const outPath = join(tmpDir, `${manifest.name}.json`);
            assert.ok(existsSync(outPath), `manifest file should exist at ${outPath}`);
        } finally {
            rmSync(tmpDir, { recursive: true, force: true });
        }
    });

    it('written JSON contains _metadata field', () => {
        const tmpDir = makeTmpDir();
        try {
            const manifest = validateManifest(MANIFEST_DICT);
            const report   = makeGreenReport();
            promote(report, manifest, tmpDir);
            const outPath = join(tmpDir, `${manifest.name}.json`);
            const content = JSON.parse(readFileSync(outPath, 'utf-8'));
            assert.ok('_metadata' in content);
            assert.strictEqual(content._metadata.tier, 'green');
            assert.strictEqual(content._metadata.source, 'algo_gen');
        } finally {
            rmSync(tmpDir, { recursive: true, force: true });
        }
    });

    it('returned algo has metadata populated', () => {
        const tmpDir = makeTmpDir();
        try {
            const manifest = validateManifest(MANIFEST_DICT);
            const report   = makeGreenReport();
            const algo     = promote(report, manifest, tmpDir);
            assert.ok(algo.metadata !== null);
            assert.strictEqual(algo.metadata.tier, 'green');
            assert.strictEqual(algo.metadata.size_mult, 1.0);
            assert.ok('scores' in algo.metadata);
            assert.ok('created_at' in algo.metadata);
        } finally {
            rmSync(tmpDir, { recursive: true, force: true });
        }
    });

    it('creates registry dir if it does not exist', () => {
        const tmpDir = makeTmpDir();
        const subDir = join(tmpDir, 'subdir', 'registry');
        try {
            const manifest = validateManifest(MANIFEST_DICT);
            const report   = makeGreenReport();
            promote(report, manifest, subDir);
            assert.ok(existsSync(subDir));
        } finally {
            rmSync(tmpDir, { recursive: true, force: true });
        }
    });
});

describe('promote: red tier refusal', () => {
    it('throws PromotionError for red tier', () => {
        const tmpDir = makeTmpDir();
        try {
            const manifest = validateManifest(MANIFEST_DICT);
            const report   = makeRedReport();
            assert.throws(
                () => promote(report, manifest, tmpDir),
                err => err instanceof PromotionError && /red/i.test(err.message)
            );
        } finally {
            rmSync(tmpDir, { recursive: true, force: true });
        }
    });
});

describe('promote: orange tier — requires confirm', () => {
    it('throws PromotionError without confirm=true', () => {
        const tmpDir = makeTmpDir();
        try {
            const manifest = validateManifest(MANIFEST_DICT);
            const report   = makeOrangeReport();
            assert.throws(
                () => promote(report, manifest, tmpDir, { confirm: false }),
                err => err instanceof PromotionError && /confirm/i.test(err.message)
            );
        } finally {
            rmSync(tmpDir, { recursive: true, force: true });
        }
    });

    it('succeeds with confirm=true', () => {
        const tmpDir = makeTmpDir();
        try {
            const manifest = validateManifest(MANIFEST_DICT);
            const report   = makeOrangeReport();
            const algo = promote(report, manifest, tmpDir, { confirm: true });
            assert.ok(algo instanceof ManifestAlgo);
            assert.strictEqual(algo.metadata.tier, 'orange');
        } finally {
            rmSync(tmpDir, { recursive: true, force: true });
        }
    });
});

describe('promote: metadata content', () => {
    it('metadata includes all required spec §7 fields', () => {
        const tmpDir = makeTmpDir();
        try {
            const manifest = validateManifest(MANIFEST_DICT);
            const report   = makeGreenReport();
            const algo = promote(report, manifest, tmpDir, { brief: 'test brief' });
            const m = algo.metadata;
            for (const field of ['source', 'schema_version', 'tier', 'tier_color',
                                  'scores', 'min_score', 'size_mult', 'experimental',
                                  'paper_only', 'created_at', 'generator', 'brief',
                                  'manifest_path']) {
                assert.ok(field in m, `missing metadata field: ${field}`);
            }
            assert.strictEqual(m.brief, 'test brief');
        } finally {
            rmSync(tmpDir, { recursive: true, force: true });
        }
    });

    it('tier_color is a valid hex color', () => {
        const tmpDir = makeTmpDir();
        try {
            const manifest = validateManifest(MANIFEST_DICT);
            const report   = makeGreenReport();
            const algo = promote(report, manifest, tmpDir);
            assert.match(algo.metadata.tier_color, /^#[0-9a-fA-F]{6}$/);
        } finally {
            rmSync(tmpDir, { recursive: true, force: true });
        }
    });
});
