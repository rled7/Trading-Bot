/**
 * dsl.test.js — Unit tests for algo_gen/dsl.js (compileExpr, BAR_FIELDS, PATTERN_NAMES)
 */
import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { compileExpr, BAR_FIELDS, PATTERN_NAMES } from '../../src/algo_gen/dsl.js';

// ── Helpers ───────────────────────────────────────────────────────────────────

const IND_IDS = new Set(['ema9', 'ema21', 'rsi14', 'atr14']);

function makeEnv(overrides = {}) {
    return {
        close:  1.1000,
        open:   1.0900,
        high:   1.1050,
        low:    1.0850,
        volume: 1000,
        spread: 0.0001,
        ema9:   1.1010,
        ema21:  1.0980,
        rsi14:  55.0,
        atr14:  0.0012,
        pattern: {
            doji:      false,
            hammer:    true,
            engulfing: false,
            marubozu:  false,
            pin_bar:   false,
            morning_star: false,
            evening_star: false,
            three_white_soldiers: false,
            three_black_crows: false,
            double_top: false,
            double_bottom: false,
            ascending_triangle: false,
            descending_triangle: false,
            bullish_flag: false,
            bearish_flag: false,
            head_and_shoulders: false,
            inverse_head_and_shoulders: false,
            gartley_bull: false,
            gartley_bear: false,
            bat_bull: false,
            butterfly_bull: false,
            crab_bull: false,
        },
        ...overrides,
    };
}

// ── BAR_FIELDS and PATTERN_NAMES ──────────────────────────────────────────────

describe('dsl: BAR_FIELDS', () => {
    it('contains expected fields', () => {
        for (const f of ['close', 'open', 'high', 'low', 'volume', 'spread']) {
            assert.ok(BAR_FIELDS.has(f));
        }
    });
});

describe('dsl: PATTERN_NAMES', () => {
    it('contains hammer', () => {
        assert.ok(PATTERN_NAMES.has('hammer'));
    });
    it('contains engulfing', () => {
        assert.ok(PATTERN_NAMES.has('engulfing'));
    });
    it('does not contain unknown', () => {
        assert.ok(!PATTERN_NAMES.has('shooting_star_xyz'));
    });
});

// ── Basic expressions ─────────────────────────────────────────────────────────

describe('dsl: compileExpr — numeric comparisons', () => {
    it('simple greater-than true', () => {
        const fn = compileExpr('ema9 > ema21', IND_IDS);
        assert.ok(fn(makeEnv()));
    });

    it('simple greater-than false', () => {
        const fn = compileExpr('ema21 > ema9', IND_IDS);
        assert.ok(!fn(makeEnv()));
    });

    it('less-than operator', () => {
        const fn = compileExpr('rsi14 < 60', IND_IDS);
        assert.ok(fn(makeEnv()));
    });

    it('less-equal operator', () => {
        const fn = compileExpr('rsi14 <= 55', IND_IDS);
        assert.ok(fn(makeEnv()));
    });

    it('greater-equal operator', () => {
        const fn = compileExpr('rsi14 >= 55', IND_IDS);
        assert.ok(fn(makeEnv()));
    });

    it('equality operator', () => {
        const fn = compileExpr('rsi14 == 55', IND_IDS);
        assert.ok(fn(makeEnv()));
    });

    it('not-equal operator', () => {
        const fn = compileExpr('rsi14 != 60', IND_IDS);
        assert.ok(fn(makeEnv()));
    });
});

describe('dsl: compileExpr — arithmetic', () => {
    it('addition expression', () => {
        const fn = compileExpr('ema9 + 0.01 > ema21', IND_IDS);
        assert.ok(fn(makeEnv()));  // 1.1010+0.01=1.111 > 1.0980
    });

    it('subtraction expression', () => {
        const fn = compileExpr('ema9 - ema21 > 0', IND_IDS);
        assert.ok(fn(makeEnv()));  // 1.1010-1.0980 > 0
    });

    it('multiplication expression', () => {
        const fn = compileExpr('atr14 * 2 > 0.001', IND_IDS);
        assert.ok(fn(makeEnv()));  // 0.0024 > 0.001
    });

    it('division expression', () => {
        const fn = compileExpr('rsi14 / 2 > 20', IND_IDS);
        assert.ok(fn(makeEnv()));  // 27.5 > 20
    });

    it('unary minus', () => {
        const fn = compileExpr('-rsi14 < -40', IND_IDS);
        assert.ok(fn(makeEnv()));  // -55 < -40
    });
});

describe('dsl: compileExpr — logical operators', () => {
    it('and — both true', () => {
        const fn = compileExpr('ema9 > ema21 and rsi14 > 50', IND_IDS);
        assert.ok(fn(makeEnv()));
    });

    it('and — one false', () => {
        const fn = compileExpr('ema9 > ema21 and rsi14 > 90', IND_IDS);
        assert.ok(!fn(makeEnv()));
    });

    it('or — one true', () => {
        const fn = compileExpr('ema9 < ema21 or rsi14 > 50', IND_IDS);
        assert.ok(fn(makeEnv()));
    });

    it('or — both false', () => {
        const fn = compileExpr('ema9 < ema21 or rsi14 > 90', IND_IDS);
        assert.ok(!fn(makeEnv()));
    });

    it('not — negates true', () => {
        const fn = compileExpr('not ema9 < ema21', IND_IDS);
        assert.ok(fn(makeEnv()));
    });

    it('not — negates false', () => {
        const fn = compileExpr('not ema9 > ema21', IND_IDS);
        assert.ok(!fn(makeEnv()));
    });
});

describe('dsl: compileExpr — parentheses', () => {
    it('grouping changes precedence', () => {
        const fn = compileExpr('(ema9 > ema21) and (rsi14 > 50)', IND_IDS);
        assert.ok(fn(makeEnv()));
    });

    it('nested parens', () => {
        const fn = compileExpr('((ema9 > 0) and (rsi14 > 50)) or (ema21 < 0)', IND_IDS);
        assert.ok(fn(makeEnv()));
    });
});

describe('dsl: compileExpr — bar fields', () => {
    it('close field', () => {
        const fn = compileExpr('close > 1.0', new Set());
        assert.ok(fn(makeEnv()));
    });

    it('volume field', () => {
        const fn = compileExpr('volume > 500', new Set());
        assert.ok(fn(makeEnv()));
    });
});

describe('dsl: compileExpr — pattern access', () => {
    it('pattern.hammer true', () => {
        const fn = compileExpr('pattern.hammer', IND_IDS);
        assert.ok(fn(makeEnv()));
    });

    it('pattern.doji false', () => {
        const fn = compileExpr('pattern.doji', IND_IDS);
        assert.ok(!fn(makeEnv()));
    });

    it('unknown pattern name throws at compile time', () => {
        assert.throws(() => compileExpr('pattern.xyz_unknown', IND_IDS), /not a known pattern/);
    });
});

describe('dsl: compileExpr — boolean literals', () => {
    it('true literal', () => {
        const fn = compileExpr('true', IND_IDS);
        assert.ok(fn(makeEnv()));
    });

    it('false literal', () => {
        const fn = compileExpr('false', IND_IDS);
        assert.ok(!fn(makeEnv()));
    });
});

describe('dsl: compileExpr — null propagation', () => {
    it('null indicator returns false (not error)', () => {
        const fn = compileExpr('ema9 > 0', IND_IDS);
        assert.ok(!fn(makeEnv({ ema9: null })));
    });

    it('undefined indicator returns false', () => {
        const fn = compileExpr('ema9 > 0', IND_IDS);
        assert.ok(!fn(makeEnv({ ema9: undefined })));
    });

    it('NaN indicator returns false', () => {
        const fn = compileExpr('ema9 > 0', IND_IDS);
        assert.ok(!fn(makeEnv({ ema9: NaN })));
    });
});

describe('dsl: compileExpr — error cases', () => {
    it('empty expression throws', () => {
        assert.throws(() => compileExpr('', IND_IDS), /non-empty/);
    });

    it('whitespace-only expression throws', () => {
        assert.throws(() => compileExpr('   ', IND_IDS), /non-empty/);
    });

    it('undeclared name throws', () => {
        assert.throws(() => compileExpr('unknown_var > 0', IND_IDS), /not declared/);
    });

    it('syntax error throws', () => {
        assert.throws(() => compileExpr('ema9 > > ema21', IND_IDS));
    });

    it('unexpected char throws', () => {
        assert.throws(() => compileExpr('ema9 @ ema21', IND_IDS), /unexpected character/);
    });

    it('unclosed paren throws', () => {
        assert.throws(() => compileExpr('(ema9 > ema21', IND_IDS));
    });
});
