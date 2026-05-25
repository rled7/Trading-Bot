/**
 * dsl.js — Whitelist DSL expression parser for AlgoManifest entry/exit conditions.
 *
 * Recursive-descent parser (no eval, no AST stdlib). Mirrors Python dsl.py.
 *
 * Grammar:
 *   expr     ::= or_expr
 *   or_expr  ::= and_expr ('or' and_expr)*
 *   and_expr ::= not_expr ('and' not_expr)*
 *   not_expr ::= 'not' not_expr | cmp_expr
 *   cmp_expr ::= add_expr (('<'|'<='|'>'|'>='|'=='|'!=') add_expr)*
 *   add_expr ::= mul_expr (('+'|'-') mul_expr)*
 *   mul_expr ::= unary (('*'|'/') unary)*
 *   unary    ::= '-' unary | atom
 *   atom     ::= number | 'true' | 'false' | name | 'pattern' '.' name | '(' expr ')'
 *
 * Public API:
 *   compileExpr(expr, indicatorIds) -> (env) => boolean
 */

// ── Whitelist sets ────────────────────────────────────────────────────────────

export const BAR_FIELDS = new Set([
    'close', 'open', 'high', 'low', 'volume', 'spread',
]);

export const PATTERN_NAMES = new Set([
    'doji', 'hammer', 'engulfing', 'marubozu', 'pin_bar',
    'morning_star', 'evening_star',
    'three_white_soldiers', 'three_black_crows',
    'double_top', 'double_bottom',
    'ascending_triangle', 'descending_triangle',
    'bullish_flag', 'bearish_flag',
    'head_and_shoulders', 'inverse_head_and_shoulders',
    'gartley_bull', 'gartley_bear',
    'bat_bull', 'butterfly_bull', 'crab_bull',
]);

// ── Tokenizer ─────────────────────────────────────────────────────────────────

const TOKEN_NUM   = 'NUM';
const TOKEN_NAME  = 'NAME';
const TOKEN_OP    = 'OP';
const TOKEN_EOF   = 'EOF';

function tokenize(src) {
    const tokens = [];
    let i = 0;
    while (i < src.length) {
        // Skip whitespace
        if (/\s/.test(src[i])) { i++; continue; }
        // Numbers (int or float)
        if (/[0-9]/.test(src[i]) || (src[i] === '.' && /[0-9]/.test(src[i + 1] || ''))) {
            let j = i;
            while (j < src.length && /[0-9]/.test(src[j])) j++;
            if (j < src.length && src[j] === '.') {
                j++;
                while (j < src.length && /[0-9]/.test(src[j])) j++;
            }
            // Scientific notation
            if (j < src.length && (src[j] === 'e' || src[j] === 'E')) {
                j++;
                if (j < src.length && (src[j] === '+' || src[j] === '-')) j++;
                while (j < src.length && /[0-9]/.test(src[j])) j++;
            }
            tokens.push({ type: TOKEN_NUM, value: parseFloat(src.slice(i, j)), pos: i });
            i = j;
            continue;
        }
        // Identifiers / keywords
        if (/[a-zA-Z_]/.test(src[i])) {
            let j = i;
            while (j < src.length && /[a-zA-Z0-9_]/.test(src[j])) j++;
            tokens.push({ type: TOKEN_NAME, value: src.slice(i, j), pos: i });
            i = j;
            continue;
        }
        // Two-char operators
        const two = src.slice(i, i + 2);
        if (['<=', '>=', '==', '!='].includes(two)) {
            tokens.push({ type: TOKEN_OP, value: two, pos: i });
            i += 2;
            continue;
        }
        // Single-char operators / punctuation
        if ('<>+-*/().'.includes(src[i])) {
            tokens.push({ type: TOKEN_OP, value: src[i], pos: i });
            i++;
            continue;
        }
        throw new Error(`DSL: unexpected character '${src[i]}' at position ${i}`);
    }
    tokens.push({ type: TOKEN_EOF, value: null, pos: src.length });
    return tokens;
}

// ── Parser ────────────────────────────────────────────────────────────────────

class Parser {
    constructor(tokens, allowedNames) {
        this._tokens = tokens;
        this._pos    = 0;
        this._allowed = allowedNames;
    }

    _peek() {
        return this._tokens[this._pos];
    }

    _consume() {
        return this._tokens[this._pos++];
    }

    _expect(type, value) {
        const tok = this._consume();
        if (tok.type !== type || (value !== undefined && tok.value !== value)) {
            throw new Error(
                `DSL parse error: expected ${value !== undefined ? `'${value}'` : type} ` +
                `at pos ${tok.pos}, got '${tok.value}'`
            );
        }
        return tok;
    }

    parseExpr() {
        const node = this.parseOrExpr();
        if (this._peek().type !== TOKEN_EOF) {
            const tok = this._peek();
            throw new Error(`DSL parse error: unexpected token '${tok.value}' at pos ${tok.pos}`);
        }
        return node;
    }

    parseOrExpr() {
        let left = this.parseAndExpr();
        while (this._peek().type === TOKEN_NAME && this._peek().value === 'or') {
            this._consume();
            const right = this.parseAndExpr();
            const l = left, r = right;
            left = env => l(env) || r(env);
        }
        return left;
    }

    parseAndExpr() {
        let left = this.parseNotExpr();
        while (this._peek().type === TOKEN_NAME && this._peek().value === 'and') {
            this._consume();
            const right = this.parseNotExpr();
            const l = left, r = right;
            left = env => l(env) && r(env);
        }
        return left;
    }

    parseNotExpr() {
        if (this._peek().type === TOKEN_NAME && this._peek().value === 'not') {
            this._consume();
            const inner = this.parseNotExpr();
            return env => !inner(env);
        }
        return this.parseCmpExpr();
    }

    parseCmpExpr() {
        let left = this.parseAddExpr();
        const CMP_OPS = new Set(['<', '<=', '>', '>=', '==', '!=']);
        while (this._peek().type === TOKEN_OP && CMP_OPS.has(this._peek().value)) {
            const op    = this._consume().value;
            const right = this.parseAddExpr();
            const l = left, r = right;
            switch (op) {
                case '<':  left = env => l(env) <  r(env); break;
                case '<=': left = env => l(env) <= r(env); break;
                case '>':  left = env => l(env) >  r(env); break;
                case '>=': left = env => l(env) >= r(env); break;
                case '==': left = env => l(env) === r(env); break;
                case '!=': left = env => l(env) !== r(env); break;
            }
        }
        return left;
    }

    parseAddExpr() {
        let left = this.parseMulExpr();
        while (
            this._peek().type === TOKEN_OP &&
            (this._peek().value === '+' || this._peek().value === '-')
        ) {
            const op    = this._consume().value;
            const right = this.parseMulExpr();
            const l = left, r = right;
            if (op === '+') left = env => l(env) + r(env);
            else            left = env => l(env) - r(env);
        }
        return left;
    }

    parseMulExpr() {
        let left = this.parseUnary();
        while (
            this._peek().type === TOKEN_OP &&
            (this._peek().value === '*' || this._peek().value === '/')
        ) {
            const op    = this._consume().value;
            const right = this.parseUnary();
            const l = left, r = right;
            if (op === '*') left = env => l(env) * r(env);
            else            left = env => r(env) !== 0 ? l(env) / r(env) : NaN;
        }
        return left;
    }

    parseUnary() {
        if (this._peek().type === TOKEN_OP && this._peek().value === '-') {
            this._consume();
            const inner = this.parseUnary();
            return env => -inner(env);
        }
        return this.parseAtom();
    }

    parseAtom() {
        const tok = this._peek();

        // Parenthesised expression
        if (tok.type === TOKEN_OP && tok.value === '(') {
            this._consume();
            const inner = this.parseOrExpr();
            this._expect(TOKEN_OP, ')');
            return inner;
        }

        // Number literal
        if (tok.type === TOKEN_NUM) {
            this._consume();
            const v = tok.value;
            return () => v;
        }

        // Identifier
        if (tok.type === TOKEN_NAME) {
            const name = tok.value;

            // Boolean literals
            if (name === 'true')  { this._consume(); return () => true; }
            if (name === 'false') { this._consume(); return () => false; }

            // Keywords not valid as identifiers here
            if (name === 'and' || name === 'or' || name === 'not') {
                throw new Error(`DSL parse error: unexpected keyword '${name}' at pos ${tok.pos}`);
            }

            // pattern.X attribute access
            if (name === 'pattern') {
                this._consume();
                this._expect(TOKEN_OP, '.');
                const attrTok = this._consume();
                if (attrTok.type !== TOKEN_NAME) {
                    throw new Error(`DSL parse error: expected pattern attribute name at pos ${attrTok.pos}`);
                }
                const attr = attrTok.value;
                if (!PATTERN_NAMES.has(attr)) {
                    throw new Error(
                        `DSL: pattern.${attr} is not a known pattern name; ` +
                        `allowed: ${[...PATTERN_NAMES].sort().join(', ')}`
                    );
                }
                return env => {
                    const p = env['pattern'];
                    if (!p) return false;
                    const v = typeof p[attr] !== 'undefined' ? p[attr] : false;
                    return Boolean(v);
                };
            }

            // Must be in allowed names
            if (!this._allowed.has(name)) {
                throw new Error(
                    `DSL: Name '${name}' is not declared; ` +
                    `allowed names: ${[...this._allowed].sort().join(', ')}`
                );
            }
            this._consume();
            return env => {
                const v = env[name];
                // null/undefined during warmup → treat as 0 (falsy)
                if (v === null || v === undefined || (typeof v === 'number' && isNaN(v))) return null;
                return v;
            };
        }

        throw new Error(`DSL parse error: unexpected token '${tok.value}' at pos ${tok.pos}`);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

/**
 * Compile a DSL expression string into an evaluator function.
 *
 * @param {string}      expr          expression string
 * @param {Set<string>} indicatorIds  declared indicator IDs from the manifest
 * @returns {(env: object) => boolean}  evaluator function
 * @throws {Error}  on syntax or whitelist error
 */
export function compileExpr(expr, indicatorIds) {
    if (!expr || !expr.trim()) {
        throw new Error('DSL expression must be non-empty');
    }
    const allowedNames = new Set([...indicatorIds, ...BAR_FIELDS, 'pattern']);
    const tokens = tokenize(expr.trim());
    const parser = new Parser(tokens, allowedNames);
    const fn     = parser.parseExpr();
    // Wrap to ensure boolean return and handle null propagation
    return env => {
        try {
            const v = fn(env);
            if (v === null || v === undefined) return false;
            return Boolean(v);
        } catch (_) {
            return false;
        }
    };
}
