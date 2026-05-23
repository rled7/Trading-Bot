/**
 * linalg.js — matrix operations + Gauss-Jordan inverse with partial pivoting.
 * All inputs/outputs are plain JS arrays (row-major, arrays of rows).
 * No external dependencies.
 */

/**
 * Matrix multiply: A (m x k) @ B (k x n) -> C (m x n)
 * @param {number[][]} A
 * @param {number[][]} B
 * @returns {number[][]}
 */
export function matMul(A, B) {
    const m = A.length;
    const k = A[0].length;
    const n = B[0].length;
    const C = [];
    for (let i = 0; i < m; i++) {
        C.push(new Array(n).fill(0));
        for (let j = 0; j < n; j++) {
            let s = 0;
            for (let p = 0; p < k; p++) s += A[i][p] * B[p][j];
            C[i][j] = s;
        }
    }
    return C;
}

/**
 * Transpose a matrix.
 * @param {number[][]} A  m x n
 * @returns {number[][]}  n x m
 */
export function transpose(A) {
    const m = A.length;
    const n = A[0].length;
    const T = [];
    for (let j = 0; j < n; j++) {
        T.push(new Array(m));
        for (let i = 0; i < m; i++) T[j][i] = A[i][j];
    }
    return T;
}

/**
 * Matrix-vector multiply: A (m x k) @ v (k) -> w (m)
 * @param {number[][]} A
 * @param {number[]}   v
 * @returns {number[]}
 */
export function matVec(A, v) {
    const m = A.length;
    const k = v.length;
    const w = new Array(m).fill(0);
    for (let i = 0; i < m; i++) {
        for (let j = 0; j < k; j++) w[i] += A[i][j] * v[j];
    }
    return w;
}

/**
 * Gauss-Jordan elimination with partial pivoting to compute the inverse of
 * a square matrix A.  Returns null if A is singular (pivot < 1e-14).
 *
 * @param {number[][]} A  n x n matrix (not mutated)
 * @returns {number[][]|null}  n x n inverse, or null if singular
 */
export function matInv(A) {
    const n = A.length;
    // Augment [A | I]
    const aug = A.map((row, i) => {
        const r = row.slice();
        for (let j = 0; j < n; j++) r.push(j === i ? 1 : 0);
        return r;
    });

    for (let col = 0; col < n; col++) {
        // Find pivot row (partial pivoting)
        let maxVal = Math.abs(aug[col][col]);
        let maxRow = col;
        for (let row = col + 1; row < n; row++) {
            const v = Math.abs(aug[row][col]);
            if (v > maxVal) { maxVal = v; maxRow = row; }
        }
        if (maxVal < 1e-14) return null; // singular

        // Swap rows
        if (maxRow !== col) {
            const tmp = aug[col];
            aug[col] = aug[maxRow];
            aug[maxRow] = tmp;
        }

        // Scale pivot row
        const pivot = aug[col][col];
        for (let j = 0; j < 2 * n; j++) aug[col][j] /= pivot;

        // Eliminate column in all other rows
        for (let row = 0; row < n; row++) {
            if (row === col) continue;
            const factor = aug[row][col];
            if (Math.abs(factor) < 1e-18) continue;
            for (let j = 0; j < 2 * n; j++) {
                aug[row][j] -= factor * aug[col][j];
            }
        }
    }

    // Extract right half
    return aug.map(row => row.slice(n));
}

/**
 * Dot product of two equal-length vectors.
 * @param {number[]} a
 * @param {number[]} b
 * @returns {number}
 */
export function dot(a, b) {
    let s = 0;
    for (let i = 0; i < a.length; i++) s += a[i] * b[i];
    return s;
}
