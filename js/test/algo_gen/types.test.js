/**
 * types.test.js — Unit tests for algo_gen/types.js
 */
import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import {
    TierName,
    makeStageResult,
    makeTierReport,
    makeValidationResult,
} from '../../src/algo_gen/types.js';

describe('types: TierName', () => {
    it('has all expected tier names', () => {
        assert.strictEqual(TierName.red,    'red');
        assert.strictEqual(TierName.orange, 'orange');
        assert.strictEqual(TierName.yellow, 'yellow');
        assert.strictEqual(TierName.green,  'green');
        assert.strictEqual(TierName.white,  'white');
    });

    it('is frozen', () => {
        assert.throws(() => { TierName.blue = 'blue'; });
    });
});

describe('types: makeStageResult', () => {
    it('creates a stage result with defaults', () => {
        const r = makeStageResult(1, 'schema_lint', true);
        assert.strictEqual(r.stage, 1);
        assert.strictEqual(r.name, 'schema_lint');
        assert.ok(r.passed);
        assert.strictEqual(r.reason, '');
        assert.deepStrictEqual(r.metrics, {});
    });

    it('creates a failed stage result with reason', () => {
        const r = makeStageResult(2, 'sandbox_backtest', false, 'too few trades: 5 < 20', { trades: 5 });
        assert.strictEqual(r.stage, 2);
        assert.ok(!r.passed);
        assert.match(r.reason, /too few trades/);
        assert.strictEqual(r.metrics.trades, 5);
    });
});

describe('types: makeTierReport', () => {
    it('creates a tier report with expected fields', () => {
        const r = makeTierReport({
            tier:        'green',
            minScore:    90.0,
            walkForward: 91.0,
            mcBootstrap: 90.5,
            robustness:  90.2,
            sizeMult:    1.0,
            experimental: false,
            paperOnly:   false,
        });
        assert.strictEqual(r.tier, 'green');
        assert.strictEqual(r.minScore, 90.0);
        assert.strictEqual(r.walkForward, 91.0);
        assert.strictEqual(r.mcBootstrap, 90.5);
        assert.strictEqual(r.robustness, 90.2);
        assert.strictEqual(r.sizeMult, 1.0);
        assert.ok(!r.experimental);
        assert.ok(!r.paperOnly);
        assert.deepStrictEqual(r.stages, []);
    });
});

describe('types: makeValidationResult', () => {
    it('creates a passed validation result', () => {
        const stages = [makeStageResult(1, 'schema_lint', true)];
        const r = makeValidationResult({ passed: true, stages, manifestName: 'test-algo' });
        assert.ok(r.passed);
        assert.strictEqual(r.stages.length, 1);
        assert.strictEqual(r.report, null);
        assert.strictEqual(r.manifestName, 'test-algo');
    });

    it('creates a failed validation result', () => {
        const stages = [makeStageResult(1, 'schema_lint', false, 'bad schema')];
        const r = makeValidationResult({ passed: false, stages });
        assert.ok(!r.passed);
        assert.strictEqual(r.report, null);
    });
});
