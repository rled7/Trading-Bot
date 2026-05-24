"""Tests for algoforge.algo_gen.dsl — whitelist expression compiler."""
from __future__ import annotations

import pytest

from algoforge.algo_gen.dsl import BAR_FIELDS, PATTERN_NAMES, compile_expr


# ── Helper ─────────────────────────────────────────────────────────────────────

def _eval(expr: str, ids: set[str] | None = None, env: dict | None = None) -> bool:
    """Compile and evaluate *expr* against *env*."""
    if ids is None:
        ids = set()
    fn = compile_expr(expr, ids)
    if env is None:
        env = {}
    return fn(env)


def _make_env(**kwargs) -> dict:
    """Build an env dict with pattern stub."""
    class _P:
        def __getattr__(self, name):
            return False
    env = dict(kwargs)
    env.setdefault("pattern", _P())
    return env


# ── BAR_FIELDS and PATTERN_NAMES ──────────────────────────────────────────────

class TestConstants:
    def test_bar_fields_non_empty(self):
        assert len(BAR_FIELDS) > 0

    def test_bar_fields_has_close(self):
        assert "close" in BAR_FIELDS

    def test_bar_fields_has_open(self):
        assert "open" in BAR_FIELDS

    def test_bar_fields_has_high_low(self):
        assert "high" in BAR_FIELDS
        assert "low" in BAR_FIELDS

    def test_pattern_names_non_empty(self):
        assert len(PATTERN_NAMES) > 0

    def test_pattern_names_has_doji(self):
        assert "doji" in PATTERN_NAMES

    def test_pattern_names_has_hammer(self):
        assert "hammer" in PATTERN_NAMES


# ── Accepted expressions ───────────────────────────────────────────────────────

class TestAcceptedExpressions:
    def test_simple_compare(self):
        fn = compile_expr("close > 1.0", set())
        assert fn(_make_env(close=2.0)) is True
        assert fn(_make_env(close=0.5)) is False

    def test_bool_and(self):
        fn = compile_expr("close > 1.0 and close < 2.0", set())
        assert fn(_make_env(close=1.5)) is True
        assert fn(_make_env(close=2.5)) is False

    def test_bool_or(self):
        fn = compile_expr("close < 1.0 or close > 3.0", set())
        assert fn(_make_env(close=0.5)) is True
        assert fn(_make_env(close=5.0)) is True
        assert fn(_make_env(close=2.0)) is False

    def test_unary_not(self):
        fn = compile_expr("not close > 1.0", set())
        assert fn(_make_env(close=0.5)) is True
        assert fn(_make_env(close=1.5)) is False

    def test_indicator_id(self):
        fn = compile_expr("rsi14 < 30", {"rsi14"})
        assert fn(_make_env(rsi14=25)) is True
        assert fn(_make_env(rsi14=50)) is False

    def test_add(self):
        fn = compile_expr("close + 0.1 > 1.0", set())
        assert fn(_make_env(close=1.0)) is True

    def test_subtract(self):
        fn = compile_expr("close - 0.1 > 0.0", set())
        assert fn(_make_env(close=0.5)) is True

    def test_multiply(self):
        fn = compile_expr("close * 2 > 1.0", set())
        assert fn(_make_env(close=0.6)) is True

    def test_divide(self):
        fn = compile_expr("close / 2 < 1.0", set())
        assert fn(_make_env(close=1.5)) is True

    def test_integer_constant(self):
        fn = compile_expr("close > 100", set())
        assert fn(_make_env(close=200)) is True

    def test_float_constant(self):
        fn = compile_expr("close > 1.0035", set())
        assert fn(_make_env(close=1.004)) is True

    def test_all_comparators_lt(self):
        fn = compile_expr("close < 1.0", set())
        assert fn(_make_env(close=0.9)) is True

    def test_all_comparators_lte(self):
        fn = compile_expr("close <= 1.0", set())
        assert fn(_make_env(close=1.0)) is True

    def test_all_comparators_gt(self):
        fn = compile_expr("close > 1.0", set())
        assert fn(_make_env(close=1.1)) is True

    def test_all_comparators_gte(self):
        fn = compile_expr("close >= 1.0", set())
        assert fn(_make_env(close=1.0)) is True

    def test_all_comparators_eq(self):
        fn = compile_expr("close == 1.0", set())
        assert fn(_make_env(close=1.0)) is True

    def test_all_comparators_neq(self):
        fn = compile_expr("close != 1.0", set())
        assert fn(_make_env(close=1.1)) is True

    def test_pattern_attribute(self):
        fn = compile_expr("pattern.doji", set())

        class _P:
            doji = True
        assert fn({"pattern": _P()}) is True

    def test_pattern_attribute_false(self):
        fn = compile_expr("pattern.hammer", set())

        class _P:
            hammer = False
        assert fn({"pattern": _P()}) is False

    def test_pattern_and_indicator(self):
        fn = compile_expr("pattern.doji and rsi14 < 30", {"rsi14"})

        class _P:
            doji = True
        env = {"pattern": _P(), "rsi14": 25}
        assert fn(env) is True

    def test_whitespace_stripped(self):
        fn = compile_expr("  close > 1.0  ", set())
        assert fn(_make_env(close=2.0)) is True

    def test_none_indicator_returns_false(self):
        # When indicator value is None, expression should evaluate to False
        fn = compile_expr("rsi14 < 30", {"rsi14"})
        result = fn({"rsi14": None, "pattern": None})
        assert result is False

    def test_complex_nested(self):
        fn = compile_expr(
            "(ema_fast > ema_slow and rsi14 < 70) or (rsi14 < 20)",
            {"ema_fast", "ema_slow", "rsi14"},
        )
        env1 = {"ema_fast": 1.2, "ema_slow": 1.1, "rsi14": 65}
        env2 = {"ema_fast": 1.0, "ema_slow": 1.1, "rsi14": 15}
        env3 = {"ema_fast": 1.0, "ema_slow": 1.1, "rsi14": 50}
        assert fn(env1) is True   # first clause
        assert fn(env2) is True   # second clause
        assert fn(env3) is False  # neither


# ── Rejected expressions ───────────────────────────────────────────────────────

class TestRejectedExpressions:
    def test_empty_expression_raises(self):
        with pytest.raises(ValueError):
            compile_expr("", set())

    def test_whitespace_only_raises(self):
        with pytest.raises(ValueError):
            compile_expr("   ", set())

    def test_call_not_allowed(self):
        with pytest.raises(ValueError, match="[Cc]all"):
            compile_expr("abs(close)", set())

    def test_lambda_not_allowed(self):
        with pytest.raises(ValueError, match="[Ll]ambda"):
            compile_expr("lambda x: x > 0", set())

    def test_list_comp_not_allowed(self):
        with pytest.raises(ValueError):
            compile_expr("[x for x in [1, 2]]", set())

    def test_subscript_not_allowed(self):
        with pytest.raises(ValueError, match="[Ss]ubscript"):
            compile_expr("close[0] > 1.0", set())

    def test_walrus_not_allowed(self):
        with pytest.raises((ValueError, SyntaxError)):
            compile_expr("(x := close) > 1.0", set())

    def test_undeclared_name(self):
        with pytest.raises(ValueError, match="not declared"):
            compile_expr("undeclared_var > 0.5", set())

    def test_string_constant_not_allowed(self):
        with pytest.raises(ValueError, match="[Ss]tring|Constant"):
            compile_expr("'hello' == 'hello'", set())

    def test_attribute_on_non_pattern(self):
        with pytest.raises(ValueError, match="[Aa]ttribute"):
            compile_expr("rsi14.value > 0", {"rsi14"})

    def test_syntax_error(self):
        with pytest.raises(ValueError, match="syntax"):
            compile_expr("close >>>> 1.0", set())

    def test_unknown_pattern_name(self):
        with pytest.raises(ValueError):
            compile_expr("pattern.unknown_pattern", set())

    def test_ternary_not_allowed(self):
        with pytest.raises(ValueError):
            compile_expr("1 if close > 1.0 else 0", set())

    def test_fstring_not_allowed(self):
        with pytest.raises(ValueError):
            compile_expr('f"hello {close}"', set())
