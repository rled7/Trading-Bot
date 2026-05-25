/**
 * schema.test.js — Unit tests for algo_gen/schema.js (validateManifest, INDICATOR_KINDS)
 */
import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { validateManifest, INDICATOR_KINDS } from '../../src/algo_gen/schema.js';

// ── Minimal valid manifest ────────────────────────────────────────────────────

const VALID = {
    schema_version: '1.0',
    name:           'my-algo',
    description:    'A test algorithm',
    rationale:      'Testing schema validation',
    timeframes:     ['H1'],
    symbols:        ['EURUSD'],
    indicators: [
        { id: 'ema9',  kind: 'ema',  params: { period: 9  } },
        { id: 'rsi14', kind: 'rsi',  params: { period: 14 } },
    ],
    entries: [
        { side: 'long',  when: 'ema9 > 0 and rsi14 > 50' },
    ],
    exits: [
        { side: 'long', sl_atr: 1.5, tp_atr: 3.0 },
    ],
    risk: {
        size:           'atr',
        atr_mult:       1.5,
        fixed_lots:     0.01,
        max_concurrent: 1,
        hedge:          false,
        cool_down_bars: 3,
    },
};

function clone(obj) { return JSON.parse(JSON.stringify(obj)); }

// ── Tests ─────────────────────────────────────────────────────────────────────

describe('schema: INDICATOR_KINDS', () => {
    it('contains expected kinds', () => {
        for (const k of ['sma', 'ema', 'rsi', 'atr', 'macd', 'bollinger', 'williams_r', 'adx', 'donchian']) {
            assert.ok(INDICATOR_KINDS.has(k), `missing kind: ${k}`);
        }
    });

    it('does not contain invalid kinds', () => {
        assert.ok(!INDICATOR_KINDS.has('nonexistent_indicator'));
        assert.ok(!INDICATOR_KINDS.has(''));
    });
});

describe('schema: validateManifest — valid manifests', () => {
    it('accepts a minimal valid manifest', () => {
        const result = validateManifest(VALID);
        assert.strictEqual(result.name, 'my-algo');
        assert.strictEqual(result.schemaVersion, '1.0');
        assert.strictEqual(result.indicators.length, 2);
        assert.strictEqual(result.entries.length, 1);
        assert.strictEqual(result.exits.length, 1);
    });

    it('returns camelCase fields', () => {
        const result = validateManifest(VALID);
        assert.ok('schemaVersion' in result);
        assert.ok('risk' in result);
        assert.ok('atrMult' in result.risk);
        assert.ok('fixedLots' in result.risk);
        assert.ok('maxConcurrent' in result.risk);
        assert.ok('coolDownBars' in result.risk);
    });

    it('accepts symbols = "any"', () => {
        const d = clone(VALID);
        d.symbols = 'any';
        const result = validateManifest(d);
        assert.strictEqual(result.symbols, 'any');
    });

    it('accepts all valid timeframes', () => {
        for (const tf of ['M1', 'M5', 'M15', 'M30', 'H1', 'H4', 'D1', 'W1', 'S15']) {
            const d = clone(VALID);
            d.timeframes = [tf];
            const result = validateManifest(d);
            assert.deepStrictEqual(result.timeframes, [tf]);
        }
    });

    it('exit rule with only when (no sl/tp)', () => {
        const d = clone(VALID);
        d.exits = [{ side: 'long', when: 'rsi14 > 70' }];
        const result = validateManifest(d);
        assert.strictEqual(result.exits[0].slAtr, null);
        assert.strictEqual(result.exits[0].tpAtr, null);
        assert.strictEqual(result.exits[0].when, 'rsi14 > 70');
    });

    it('accepts code field (escape hatch)', () => {
        const d = clone(VALID);
        d.code = 'def evaluate(): pass';
        const result = validateManifest(d);
        assert.strictEqual(result.code, 'def evaluate(): pass');
    });

    it('null code results in null', () => {
        const result = validateManifest(VALID);
        assert.strictEqual(result.code, null);
    });

    it('accepts all 30+ indicator kinds', () => {
        for (const kind of INDICATOR_KINDS) {
            const d = clone(VALID);
            d.indicators = [{ id: 'test_ind', kind, params: {} }];
            d.entries = [{ side: 'long', when: 'test_ind > 0' }];
            assert.doesNotThrow(() => validateManifest(d), `should accept kind: ${kind}`);
        }
    });
});

describe('schema: validateManifest — error cases', () => {
    it('rejects null input', () => {
        assert.throws(() => validateManifest(null), /manifest must be an object/);
    });

    it('rejects non-object', () => {
        assert.throws(() => validateManifest('foo'));
        assert.throws(() => validateManifest(42));
    });

    it('rejects unknown schema_version', () => {
        const d = clone(VALID);
        d.schema_version = '9.9';
        assert.throws(() => validateManifest(d), /unsupported_schema_version/);
    });

    it('rejects name with invalid chars (spaces)', () => {
        const d = clone(VALID);
        d.name = 'my algo';
        assert.throws(() => validateManifest(d), /kebab-case/);
    });

    it('rejects name starting with digit', () => {
        const d = clone(VALID);
        d.name = '1algo';
        assert.throws(() => validateManifest(d), /kebab-case/);
    });

    it('rejects unknown timeframe', () => {
        const d = clone(VALID);
        d.timeframes = ['X9'];
        assert.throws(() => validateManifest(d), /unknown timeframe/);
    });

    it('rejects unknown indicator kind', () => {
        const d = clone(VALID);
        d.indicators = [{ id: 'x', kind: 'nonexistent_indicator', params: {} }];
        assert.throws(() => validateManifest(d), /unknown indicator kind/);
    });

    it('rejects duplicate indicator IDs', () => {
        const d = clone(VALID);
        d.indicators = [
            { id: 'dup', kind: 'ema', params: { period: 9 } },
            { id: 'dup', kind: 'rsi', params: { period: 14 } },
        ];
        assert.throws(() => validateManifest(d), /duplicate indicator id/);
    });

    it('rejects invalid indicator id (starts with digit)', () => {
        const d = clone(VALID);
        d.indicators = [{ id: '9bad', kind: 'ema', params: { period: 9 } }];
        assert.throws(() => validateManifest(d), /valid identifier/);
    });

    it('rejects unknown side in entry', () => {
        const d = clone(VALID);
        d.entries = [{ side: 'both', when: 'ema9 > 0' }];
        assert.throws(() => validateManifest(d), /side/);
    });

    it('rejects exit rule with no when/sl_atr/tp_atr', () => {
        const d = clone(VALID);
        d.exits = [{ side: 'long' }];
        assert.throws(() => validateManifest(d), /must have/);
    });

    it('rejects invalid risk size method', () => {
        const d = clone(VALID);
        d.risk = { ...d.risk, size: 'kelly' };
        assert.throws(() => validateManifest(d), /size/);
    });

    it('rejects cool_down_bars < 0', () => {
        const d = clone(VALID);
        d.risk = { ...d.risk, cool_down_bars: -1 };
        assert.throws(() => validateManifest(d), /cool_down_bars/);
    });

    it('rejects missing required field name', () => {
        const d = clone(VALID);
        delete d.name;
        assert.throws(() => validateManifest(d), /missing required field 'name'/);
    });
});
