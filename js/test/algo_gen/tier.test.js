/**
 * tier.test.js — Unit tests for algo_gen/tier.js (scoreToTier, tierFromMinScore)
 */
import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { scoreToTier, tierFromMinScore } from '../../src/algo_gen/tier.js';
import { TierName } from '../../src/algo_gen/types.js';

// ── tierFromMinScore ──────────────────────────────────────────────────────────

describe('tier: tierFromMinScore', () => {
    it('returns white for score >= 95', () => {
        assert.strictEqual(tierFromMinScore(95.0), TierName.white);
        assert.strictEqual(tierFromMinScore(99.0), TierName.white);
        assert.strictEqual(tierFromMinScore(100.0), TierName.white);
    });

    it('returns green for score in [90, 95)', () => {
        assert.strictEqual(tierFromMinScore(90.0), TierName.green);
        assert.strictEqual(tierFromMinScore(94.9), TierName.green);
    });

    it('returns yellow for score in [80, 90)', () => {
        assert.strictEqual(tierFromMinScore(80.0), TierName.yellow);
        assert.strictEqual(tierFromMinScore(89.9), TierName.yellow);
    });

    it('returns orange for score in [70, 80)', () => {
        assert.strictEqual(tierFromMinScore(70.0), TierName.orange);
        assert.strictEqual(tierFromMinScore(79.9), TierName.orange);
    });

    it('returns red for score < 70', () => {
        assert.strictEqual(tierFromMinScore(69.9), TierName.red);
        assert.strictEqual(tierFromMinScore(0.0), TierName.red);
        assert.strictEqual(tierFromMinScore(-5.0), TierName.red);
    });
});

// ── scoreToTier ───────────────────────────────────────────────────────────────

describe('tier: scoreToTier — basic tier assignment', () => {
    it('white tier: all scores >= 95', () => {
        const r = scoreToTier(96.0, 95.5, 95.0);
        assert.strictEqual(r.tier, TierName.white);
        assert.strictEqual(r.minScore, 95.0);
        assert.strictEqual(r.sizeMult, 1.5);
        assert.ok(!r.experimental);
        assert.ok(!r.paperOnly);
    });

    it('green tier: min >= 90 < 95', () => {
        const r = scoreToTier(91.0, 90.5, 90.2);
        assert.strictEqual(r.tier, TierName.green);
        assert.strictEqual(r.minScore, 90.2);
        assert.strictEqual(r.sizeMult, 1.0);
        assert.ok(!r.experimental);
        assert.ok(!r.paperOnly);
    });

    it('yellow tier: min >= 80 < 90', () => {
        const r = scoreToTier(85.0, 83.0, 80.5);
        assert.strictEqual(r.tier, TierName.yellow);
        assert.ok(Math.abs(r.minScore - 80.5) <= 0.001);
        assert.strictEqual(r.sizeMult, 0.5);
        assert.ok(r.experimental);
        assert.ok(!r.paperOnly);
    });

    it('orange tier: min >= 70 < 80', () => {
        const r = scoreToTier(79.0, 74.0, 72.0);
        assert.strictEqual(r.tier, TierName.orange);
        assert.ok(Math.abs(r.minScore - 72.0) <= 0.001);
        assert.strictEqual(r.sizeMult, 0.25);
        assert.ok(r.experimental);
        assert.ok(r.paperOnly);
    });

    it('red tier: min < 70', () => {
        const r = scoreToTier(65.0, 60.0, 55.0);
        assert.strictEqual(r.tier, TierName.red);
        assert.strictEqual(r.sizeMult, 0.0);
        assert.ok(r.experimental);
        assert.ok(r.paperOnly);
    });
});

describe('tier: scoreToTier — min-of-three rule', () => {
    it('uses minimum of wf/mc/rob as minScore', () => {
        const r = scoreToTier(99.0, 91.0, 95.0);
        assert.strictEqual(r.minScore, 91.0);
        assert.strictEqual(r.tier, TierName.green);
    });

    it('mc can be the bottleneck', () => {
        const r = scoreToTier(95.0, 71.0, 95.0);
        assert.strictEqual(r.tier, TierName.orange);
    });

    it('robustness can be the bottleneck', () => {
        const r = scoreToTier(95.0, 95.0, 72.0);
        assert.strictEqual(r.tier, TierName.orange);
    });
});

describe('tier: scoreToTier — rounding', () => {
    it('rounds scores to 4 dp', () => {
        const r = scoreToTier(90.12345, 90.12345, 90.12345);
        assert.strictEqual(r.minScore, 90.1235);
        assert.strictEqual(r.walkForward, 90.1235);
        assert.strictEqual(r.mcBootstrap, 90.1235);
        assert.strictEqual(r.robustness, 90.1235);
    });
});

describe('tier: scoreToTier — fixture values', () => {
    it('trend-follow-green fixture: WF=91 MC=90.5 Rob=90.2 → green', () => {
        const r = scoreToTier(91.0, 90.5, 90.2);
        assert.strictEqual(r.tier, 'green');
        assert.ok(Math.abs(r.minScore - 90.2) <= 0.01);
    });

    it('breakout-white fixture: WF=96 MC=95.5 Rob=95.0 → white', () => {
        const r = scoreToTier(96.0, 95.5, 95.0);
        assert.strictEqual(r.tier, 'white');
        assert.ok(Math.abs(r.minScore - 95.0) <= 0.01);
    });

    it('mean-revert-yellow fixture: WF=85 MC=83 Rob=80.5 → yellow', () => {
        const r = scoreToTier(85.0, 83.0, 80.5);
        assert.strictEqual(r.tier, 'yellow');
        assert.ok(Math.abs(r.minScore - 80.5) <= 0.01);
    });

    it('trend-follow-orange fixture: WF=79 MC=74 Rob=72 → orange', () => {
        const r = scoreToTier(79.0, 74.0, 72.0);
        assert.strictEqual(r.tier, 'orange');
        assert.ok(Math.abs(r.minScore - 72.0) <= 0.01);
    });
});
