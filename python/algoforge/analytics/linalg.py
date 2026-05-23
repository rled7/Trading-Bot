from __future__ import annotations

"""Pure-Python linear algebra helpers for the analytics layer.

All matrices are represented as list[list[float]] (row-major).
No external dependencies.
"""


def identity(n: int) -> list[list[float]]:
    """Return the n×n identity matrix."""
    return [[1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]


def transpose(M: list[list[float]]) -> list[list[float]]:
    """Return the transpose of matrix M (rows become columns)."""
    if not M or not M[0]:
        return []
    rows = len(M)
    cols = len(M[0])
    return [[M[r][c] for r in range(rows)] for c in range(cols)]


def matmul(A: list[list[float]], B: list[list[float]]) -> list[list[float]]:
    """Return the matrix product A @ B.

    A must be m×k and B must be k×n; result is m×n.
    """
    m = len(A)
    k = len(A[0])
    n = len(B[0])
    result: list[list[float]] = [[0.0] * n for _ in range(m)]
    for i in range(m):
        for j in range(n):
            s = 0.0
            for p in range(k):
                s += A[i][p] * B[p][j]
            result[i][j] = s
    return result


def dot(a: list[float], b: list[float]) -> float:
    """Return the dot product of two 1-D vectors."""
    return sum(x * y for x, y in zip(a, b))


def inverse(M: list[list[float]]) -> list[list[float]]:
    """Return the inverse of square matrix M via Gauss-Jordan elimination
    with partial pivoting (swap rows to put largest absolute value on the
    diagonal).

    Raises ValueError if M is singular (pivot falls below 1e-12).
    """
    n = len(M)
    # Build augmented matrix [M | I] using copies so caller is unaffected.
    aug: list[list[float]] = [list(M[i]) + list(identity(n)[i]) for i in range(n)]

    for col in range(n):
        # --- Partial pivoting: find row with largest abs value in this column
        pivot_row = col
        max_val = abs(aug[col][col])
        for row in range(col + 1, n):
            val = abs(aug[row][col])
            if val > max_val:
                max_val = val
                pivot_row = row
        if max_val < 1e-12:
            raise ValueError(
                "Matrix is singular or near-singular; cannot compute inverse."
            )
        # Swap rows
        if pivot_row != col:
            aug[col], aug[pivot_row] = aug[pivot_row], aug[col]

        # --- Scale pivot row so diagonal becomes 1
        pivot = aug[col][col]
        scale = 1.0 / pivot
        aug[col] = [x * scale for x in aug[col]]

        # --- Eliminate all other rows in this column
        for row in range(n):
            if row == col:
                continue
            factor = aug[row][col]
            if factor == 0.0:
                continue
            aug[row] = [aug[row][j] - factor * aug[col][j] for j in range(2 * n)]

    # Extract the right half (the inverse)
    return [aug[i][n:] for i in range(n)]
