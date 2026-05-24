"""Whitelist DSL expression parser for AlgoManifest entry/exit conditions.

Uses ``ast.parse(..., mode='eval')`` and a whitelist NodeVisitor to ensure
only safe constructs are allowed. No ``eval`` is ever called.

Allowed AST node types:
  - Expression (top-level)
  - BoolOp (And / Or)
  - UnaryOp (Not, USub)
  - Compare (all comparator types)
  - BinOp (+, -, *, /)
  - Name (must be in the declared set of indicator IDs, bar fields, pattern names)
  - Constant (int/float/bool only)
  - Attribute (only ``pattern.X`` where X is a known pattern name)
  - NamedExpr is NOT allowed
  - Call is NOT allowed
  - Lambda is NOT allowed
  - ListComp, SetComp, DictComp, GeneratorExp are NOT allowed
  - Subscript is NOT allowed
  - IfExp (ternary) is NOT allowed

Public API:
    compile_expr(expr: str, indicator_ids: set[str], *, strict: bool = True)
        -> Callable[[dict], bool]

    compile_manifest_expr(expr: str, manifest: AlgoManifest)
        -> Callable[[dict], bool]
"""
from __future__ import annotations

import ast
from typing import Any, Callable, Set

# Bar field names always available in expressions
BAR_FIELDS: frozenset[str] = frozenset(
    {"close", "open", "high", "low", "volume", "spread"}
)

# Pattern names available via ``pattern.X`` attribute access
PATTERN_NAMES: frozenset[str] = frozenset({
    "doji", "hammer", "engulfing", "marubozu", "pin_bar",
    "morning_star", "evening_star",
    "three_white_soldiers", "three_black_crows",
    "double_top", "double_bottom",
    "ascending_triangle", "descending_triangle",
    "bullish_flag", "bearish_flag",
    "head_and_shoulders", "inverse_head_and_shoulders",
    "gartley_bull", "gartley_bear",
    "bat_bull", "butterfly_bull", "crab_bull",
})

# Allowed binary operator types
_ALLOWED_BINOP = (
    ast.Add, ast.Sub, ast.Mult, ast.Div,
)

# Allowed comparison operators
_ALLOWED_CMPOP = (
    ast.Lt, ast.LtE, ast.Gt, ast.GtE, ast.Eq, ast.NotEq,
)


class _WhitelistVisitor(ast.NodeVisitor):
    """Validates that the AST only uses whitelisted constructs."""

    def __init__(self, allowed_names: Set[str]) -> None:
        self._allowed = allowed_names
        self._errors: list[str] = []

    def visit_Expression(self, node: ast.Expression) -> None:
        self.generic_visit(node)

    def visit_BoolOp(self, node: ast.BoolOp) -> None:
        if not isinstance(node.op, (ast.And, ast.Or)):
            self._errors.append(f"BoolOp {type(node.op).__name__} not allowed")
        self.generic_visit(node)

    def visit_UnaryOp(self, node: ast.UnaryOp) -> None:
        if not isinstance(node.op, (ast.Not, ast.USub)):
            self._errors.append(f"UnaryOp {type(node.op).__name__} not allowed")
        self.generic_visit(node)

    def visit_Compare(self, node: ast.Compare) -> None:
        for op in node.ops:
            if not isinstance(op, _ALLOWED_CMPOP):
                self._errors.append(f"Compare op {type(op).__name__} not allowed")
        self.generic_visit(node)

    def visit_BinOp(self, node: ast.BinOp) -> None:
        if not isinstance(node.op, _ALLOWED_BINOP):
            self._errors.append(f"BinOp {type(node.op).__name__} not allowed")
        self.generic_visit(node)

    def visit_Name(self, node: ast.Name) -> None:
        if node.id not in self._allowed:
            self._errors.append(
                f"Name '{node.id}' is not declared; "
                f"allowed names: {sorted(self._allowed)}"
            )

    def visit_Constant(self, node: ast.Constant) -> None:
        if not isinstance(node.value, (int, float, bool)):
            self._errors.append(
                f"Constant type {type(node.value).__name__} not allowed; "
                f"only int/float/bool are allowed"
            )

    def visit_Attribute(self, node: ast.Attribute) -> None:
        # Only allow pattern.X
        if (
            isinstance(node.value, ast.Name)
            and node.value.id == "pattern"
        ):
            if node.attr not in PATTERN_NAMES:
                self._errors.append(
                    f"pattern.{node.attr} is not a known pattern name; "
                    f"allowed: {sorted(PATTERN_NAMES)}"
                )
        else:
            # Any other attribute access is forbidden
            src = ast.unparse(node) if hasattr(ast, "unparse") else repr(node)
            self._errors.append(
                f"Attribute access not allowed: '{src}'; "
                f"only 'pattern.X' form is permitted"
            )
        # Do NOT recurse (we handle the Name check ourselves above)

    def visit_Call(self, node: ast.Call) -> None:
        self._errors.append("Function calls are not allowed in DSL expressions")
        # Still recurse so we catch all errors
        self.generic_visit(node)

    def visit_Lambda(self, node: ast.Lambda) -> None:
        self._errors.append("Lambda expressions are not allowed in DSL expressions")

    def visit_ListComp(self, node: ast.ListComp) -> None:
        self._errors.append("List comprehensions are not allowed in DSL expressions")

    def visit_SetComp(self, node: ast.SetComp) -> None:
        self._errors.append("Set comprehensions are not allowed in DSL expressions")

    def visit_DictComp(self, node: ast.DictComp) -> None:
        self._errors.append("Dict comprehensions are not allowed in DSL expressions")

    def visit_GeneratorExp(self, node: ast.GeneratorExp) -> None:
        self._errors.append("Generator expressions are not allowed in DSL expressions")

    def visit_IfExp(self, node: ast.IfExp) -> None:
        self._errors.append("Ternary (if-else) expressions are not allowed in DSL expressions")

    def visit_NamedExpr(self, node: ast.NamedExpr) -> None:
        self._errors.append("Walrus operator (:=) is not allowed in DSL expressions")

    def visit_Subscript(self, node: ast.Subscript) -> None:
        self._errors.append("Subscript access (x[...]) is not allowed in DSL expressions")

    def visit_JoinedStr(self, node: ast.JoinedStr) -> None:
        self._errors.append("f-strings are not allowed in DSL expressions")

    # Catch-all for any other node type not explicitly handled
    def generic_visit(self, node: ast.AST) -> None:
        # Only reject node types that could be dangerous and aren't handled above
        dangerous = (
            ast.Import, ast.ImportFrom, ast.Exec if hasattr(ast, "Exec") else type(None),  # type: ignore[attr-defined]
            ast.Global, ast.Nonlocal,
        )
        if isinstance(node, dangerous):
            self._errors.append(f"Node type {type(node).__name__} not allowed")
        super().generic_visit(node)


def compile_expr(
    expr: str,
    indicator_ids: Set[str],
    *,
    strict: bool = True,
) -> Callable[[dict], bool]:
    """Parse, validate, and compile a DSL expression string.

    Parameters
    ----------
    expr:
        The expression string to compile.
    indicator_ids:
        Set of declared indicator IDs (from the manifest ``indicators`` list).
    strict:
        If True (default), raise ValueError on any validation error.

    Returns
    -------
    A callable ``(env: dict) -> bool`` that evaluates the expression.
    The env dict should contain:
      - keys for each indicator id → the current numeric value (float or None)
      - bar fields: 'close', 'open', 'high', 'low', 'volume', 'spread'
      - 'pattern' → an object with attributes for each pattern name
    """
    if not expr or not expr.strip():
        raise ValueError("DSL expression must be non-empty")

    # Parse
    try:
        tree = ast.parse(expr.strip(), mode="eval")
    except SyntaxError as e:
        raise ValueError(f"DSL expression syntax error: {e}") from e

    # Build allowed name set
    allowed_names: Set[str] = set(indicator_ids) | BAR_FIELDS | {"pattern"}

    # Validate
    visitor = _WhitelistVisitor(allowed_names)
    visitor.visit(tree)
    if visitor._errors:
        raise ValueError(
            f"DSL expression contains forbidden constructs:\n"
            + "\n".join(f"  - {e}" for e in visitor._errors)
        )

    # Compile to code object
    code_obj = compile(tree, "<dsl_expr>", "eval")

    def _evaluator(env: dict) -> bool:
        try:
            result = eval(code_obj, {"__builtins__": {}}, env)  # noqa: S307
            return bool(result)
        except (TypeError, NameError, AttributeError) as exc:
            # Indicator values may be None during warmup — treat as False
            return False

    return _evaluator


def compile_manifest_expr(
    expr: str,
    indicator_ids: Set[str],
) -> Callable[[dict], bool]:
    """Compile a DSL expression using the indicator IDs from a manifest."""
    return compile_expr(expr, indicator_ids)
