/**
 * AlgoForge — c/src/algo_gen.c
 *
 * S1 Phase 1 — Algorithm Generator: validator + tier scorer (C port).
 *
 * Sections:
 *   1. JSON mini-parser (hand-rolled, no external deps)
 *   2. DSL tokenizer + recursive-descent parser + evaluator
 *   3. Indicator dispatch (compute one indicator → double[] buffer)
 *   4. Runtime algorithm (implements af_algorithm_t vtable)
 *   5. 5-stage validation pipeline
 *   6. Tier scoring + report helpers
 *   7. JSON serialization / promote
 *
 * C17 standard.  Link with -lm.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <assert.h>

#include "../include/af_algo_gen.h"
#include "../include/af_indicators.h"
#include "../include/af_backtest.h"
#include "../include/af_analytics.h"
#include "../include/af_types.h"

/* ============================================================
 * Internal constants (matching Python spec §5)
 * ============================================================ */

#define STAGE_MIN_TRADES       20
#define STAGE_SHARPE_GATE      0.5
#define STAGE_MAX_DD_GATE      25.0
#define WF_N_FOLDS             5
#define WF_TRAIN_MIN_BARS      200
#define MC_N_RUNS              1000
#define MC_INITIAL_CAPITAL     10000.0
#define ROB_N_LEVELS           3          /* val-delta, val, val+delta */
#define ROB_DELTA_FRAC         0.20
#define ROB_TOP_PARAMS         3
#define ROB_QUICK_EXTRA_BARS   20
#define ROB_QUICK_MAX_EXTRA    50

/* ============================================================
 * §1  Minimal JSON parser
 * ============================================================ *
 *
 * Only what we need to parse manifests.  The parser walks a NUL-terminated
 * JSON string and provides a cursor-based API.
 */

typedef struct {
    const char *src;
    size_t      pos;
    size_t      len;
} json_ctx_t;

static void json_init(json_ctx_t *j, const char *s, size_t n) {
    j->src = s;
    j->pos = 0;
    j->len = n ? n : strlen(s);
}

static void json_skip_ws(json_ctx_t *j) {
    while (j->pos < j->len &&
           (j->src[j->pos] == ' ' || j->src[j->pos] == '\t' ||
            j->src[j->pos] == '\n' || j->src[j->pos] == '\r'))
        ++j->pos;
}

static int json_peek(json_ctx_t *j) {
    json_skip_ws(j);
    if (j->pos >= j->len) return -1;
    return (unsigned char)j->src[j->pos];
}

static int json_consume(json_ctx_t *j, char c) {
    json_skip_ws(j);
    if (j->pos < j->len && j->src[j->pos] == c) { ++j->pos; return 1; }
    return 0;
}

/** Parse a JSON string into buf (max buf_cap-1 chars + NUL).
 *  Returns 0 on success, -1 on failure. */
static int json_parse_string(json_ctx_t *j, char *buf, size_t buf_cap) {
    json_skip_ws(j);
    if (j->pos >= j->len || j->src[j->pos] != '"') return -1;
    ++j->pos;
    size_t out = 0;
    while (j->pos < j->len && j->src[j->pos] != '"') {
        char c = j->src[j->pos++];
        if (c == '\\' && j->pos < j->len) {
            char e = j->src[j->pos++];
            switch (e) {
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case '/':  c = '/';  break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                default:   c = e;    break;
            }
        }
        if (buf && out + 1 < buf_cap) buf[out++] = c;
    }
    if (j->pos < j->len) ++j->pos; /* closing quote */
    if (buf) buf[out] = '\0';
    return 0;
}

/** Parse a JSON number into *val.  Returns 0 on success. */
static int json_parse_number(json_ctx_t *j, double *val) {
    json_skip_ws(j);
    char tmp[64];
    size_t i = 0;
    const char *s = j->src + j->pos;
    size_t remain = j->len - j->pos;
    /* copy digits, sign, decimal, exponent */
    while (i < remain && i < sizeof(tmp) - 1 &&
           (isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' ||
            s[i] == '.' || s[i] == 'e' || s[i] == 'E')) {
        tmp[i] = s[i];
        i++;
    }
    tmp[i] = '\0';
    if (i == 0) return -1;
    char *end;
    *val = strtod(tmp, &end);
    j->pos += (size_t)(end - tmp);
    return 0;
}

/** Parse "true"/"false"/"null" as int (1/0/0). Returns 0 on success. */
static int json_parse_literal(json_ctx_t *j, int *val) {
    json_skip_ws(j);
    if (j->pos + 4 <= j->len && strncmp(j->src + j->pos, "true", 4) == 0) {
        *val = 1; j->pos += 4; return 0;
    }
    if (j->pos + 5 <= j->len && strncmp(j->src + j->pos, "false", 5) == 0) {
        *val = 0; j->pos += 5; return 0;
    }
    if (j->pos + 4 <= j->len && strncmp(j->src + j->pos, "null", 4) == 0) {
        *val = 0; j->pos += 4; return 0;
    }
    return -1;
}

/** Skip a full JSON value (object, array, string, number, literal). */
static int json_skip_value(json_ctx_t *j) {
    int c = json_peek(j);
    if (c == -1) return -1;
    if (c == '"') { char tmp[4096]; return json_parse_string(j, tmp, sizeof(tmp)); }
    if (c == '{') {
        ++j->pos;
        if (json_peek(j) == '}') { ++j->pos; return 0; }
        do {
            char k[256]; if (json_parse_string(j, k, sizeof(k))) return -1;
            if (!json_consume(j, ':')) return -1;
            if (json_skip_value(j)) return -1;
        } while (json_consume(j, ','));
        return json_consume(j, '}') ? 0 : -1;
    }
    if (c == '[') {
        ++j->pos;
        if (json_peek(j) == ']') { ++j->pos; return 0; }
        do { if (json_skip_value(j)) return -1; } while (json_consume(j, ','));
        return json_consume(j, ']') ? 0 : -1;
    }
    if (c == 't' || c == 'f' || c == 'n') { int v; return json_parse_literal(j, &v); }
    if (c == '-' || isdigit(c)) { double v; return json_parse_number(j, &v); }
    return -1;
}

/* ── parse indicator params object ──────────────────────────────────────── */
static int parse_indicator_params(json_ctx_t *j, af_algo_indicator_decl_t *ind) {
    if (!json_consume(j, '{')) return -1;
    if (json_peek(j) == '}') { ++j->pos; return 0; }
    do {
        char key[AF_ALGO_MAX_PARAM_KEY];
        if (json_parse_string(j, key, sizeof(key))) return -1;
        if (!json_consume(j, ':')) return -1;
        int c = json_peek(j);
        double val = 0.0; int is_int = 0;
        if (c == '"') {
            /* string param — store 0 for now, key kept */
            char sv[64]; json_parse_string(j, sv, sizeof(sv));
            val = 0.0; is_int = 0;
        } else if (c == 't' || c == 'f' || c == 'n') {
            int bv; json_parse_literal(j, &bv);
            val = (double)bv; is_int = 1;
        } else {
            if (json_parse_number(j, &val)) return -1;
            /* detect integer: no decimal in original string, compare rounded */
            is_int = (val == floor(val));
        }
        if (ind->n_params < AF_ALGO_MAX_PARAMS) {
            strncpy(ind->params[ind->n_params].key, key, AF_ALGO_MAX_PARAM_KEY - 1);
            ind->params[ind->n_params].key[AF_ALGO_MAX_PARAM_KEY - 1] = '\0';
            ind->params[ind->n_params].value  = val;
            ind->params[ind->n_params].is_int = is_int;
            ++ind->n_params;
        }
    } while (json_consume(j, ','));
    return json_consume(j, '}') ? 0 : -1;
}

/* ── parse one indicator object ─────────────────────────────────────────── */
static int parse_indicator(json_ctx_t *j, af_algo_indicator_decl_t *ind) {
    memset(ind, 0, sizeof(*ind));
    if (!json_consume(j, '{')) return -1;
    if (json_peek(j) == '}') { ++j->pos; return 0; }
    do {
        char key[64];
        if (json_parse_string(j, key, sizeof(key))) return -1;
        if (!json_consume(j, ':')) return -1;
        if (strcmp(key, "id") == 0) {
            json_parse_string(j, ind->id, sizeof(ind->id));
        } else if (strcmp(key, "kind") == 0) {
            json_parse_string(j, ind->kind, sizeof(ind->kind));
        } else if (strcmp(key, "params") == 0) {
            parse_indicator_params(j, ind);
        } else {
            json_skip_value(j);
        }
    } while (json_consume(j, ','));
    return json_consume(j, '}') ? 0 : -1;
}

/* ── parse one entry/exit rule object ───────────────────────────────────── */
static int parse_entry_rule(json_ctx_t *j, af_algo_entry_rule_t *r) {
    memset(r, 0, sizeof(*r));
    if (!json_consume(j, '{')) return -1;
    if (json_peek(j) == '}') { ++j->pos; return 0; }
    do {
        char key[64];
        if (json_parse_string(j, key, sizeof(key))) return -1;
        if (!json_consume(j, ':')) return -1;
        if (strcmp(key, "side") == 0) json_parse_string(j, r->side, sizeof(r->side));
        else if (strcmp(key, "when") == 0) json_parse_string(j, r->when, sizeof(r->when));
        else json_skip_value(j);
    } while (json_consume(j, ','));
    return json_consume(j, '}') ? 0 : -1;
}

static int parse_exit_rule(json_ctx_t *j, af_algo_exit_rule_t *r) {
    memset(r, 0, sizeof(*r));
    if (!json_consume(j, '{')) return -1;
    if (json_peek(j) == '}') { ++j->pos; return 0; }
    do {
        char key[64];
        if (json_parse_string(j, key, sizeof(key))) return -1;
        if (!json_consume(j, ':')) return -1;
        if (strcmp(key, "side") == 0) {
            json_parse_string(j, r->side, sizeof(r->side));
        } else if (strcmp(key, "when") == 0) {
            json_parse_string(j, r->when, sizeof(r->when));
            r->has_when = (r->when[0] != '\0');
        } else if (strcmp(key, "sl_atr") == 0) {
            json_parse_number(j, &r->sl_atr); r->has_sl_atr = 1;
        } else if (strcmp(key, "tp_atr") == 0) {
            json_parse_number(j, &r->tp_atr); r->has_tp_atr = 1;
        } else {
            json_skip_value(j);
        }
    } while (json_consume(j, ','));
    return json_consume(j, '}') ? 0 : -1;
}

/* ── parse risk object ───────────────────────────────────────────────────── */
static int parse_risk(json_ctx_t *j, af_algo_risk_spec_t *r) {
    memset(r, 0, sizeof(*r));
    strncpy(r->size, "fixed", sizeof(r->size) - 1);
    r->max_concurrent = 1;
    if (!json_consume(j, '{')) return -1;
    if (json_peek(j) == '}') { ++j->pos; return 0; }
    do {
        char key[64];
        if (json_parse_string(j, key, sizeof(key))) return -1;
        if (!json_consume(j, ':')) return -1;
        if (strcmp(key, "size") == 0) json_parse_string(j, r->size, sizeof(r->size));
        else if (strcmp(key, "atr_mult") == 0) json_parse_number(j, &r->atr_mult);
        else if (strcmp(key, "fixed_lots") == 0) json_parse_number(j, &r->fixed_lots);
        else if (strcmp(key, "max_concurrent") == 0) {
            double v; json_parse_number(j, &v); r->max_concurrent = (int)v;
        } else if (strcmp(key, "hedge") == 0) {
            int v; json_parse_literal(j, &v); r->hedge = v;
        } else if (strcmp(key, "cool_down_bars") == 0) {
            double v; json_parse_number(j, &v); r->cool_down_bars = (int)v;
        } else {
            json_skip_value(j);
        }
    } while (json_consume(j, ','));
    return json_consume(j, '}') ? 0 : -1;
}

/* ── public: af_algo_manifest_parse ─────────────────────────────────────── */
int af_algo_manifest_parse(const char *json, size_t len,
                            af_algo_manifest_t *out,
                            af_algo_error_t *err)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->schema_version, "1.0", sizeof(out->schema_version) - 1);
    out->risk.max_concurrent = 1;
    strncpy(out->risk.size, "fixed", sizeof(out->risk.size) - 1);

    json_ctx_t j;
    json_init(&j, json, len);

    if (!json_consume(&j, '{')) {
        if (err) { strncpy(err->kind, "parse_error", 63); strncpy(err->message, "expected '{'", 511); }
        return -1;
    }
    if (json_peek(&j) == '}') { ++j.pos; return 0; }

    do {
        char key[64];
        if (json_parse_string(&j, key, sizeof(key))) goto fail;
        if (!json_consume(&j, ':')) goto fail;

        if (strcmp(key, "schema_version") == 0) {
            json_parse_string(&j, out->schema_version, sizeof(out->schema_version));
        } else if (strcmp(key, "name") == 0) {
            json_parse_string(&j, out->name, sizeof(out->name));
        } else if (strcmp(key, "description") == 0) {
            json_parse_string(&j, out->description, sizeof(out->description));
        } else if (strcmp(key, "rationale") == 0) {
            json_parse_string(&j, out->rationale, sizeof(out->rationale));
        } else if (strcmp(key, "timeframes") == 0) {
            if (!json_consume(&j, '[')) goto fail;
            while (json_peek(&j) != ']') {
                char tf[AF_ALGO_MAX_TF_LEN] = {0};
                json_parse_string(&j, tf, sizeof(tf));
                if (out->n_timeframes < AF_ALGO_MAX_TIMEFRAMES) {
                    strncpy(out->timeframes[out->n_timeframes++], tf, AF_ALGO_MAX_TF_LEN - 1);
                }
                json_consume(&j, ',');
            }
            json_consume(&j, ']');
        } else if (strcmp(key, "symbols") == 0) {
            int c = json_peek(&j);
            if (c == '"') {
                char sym[32];
                json_parse_string(&j, sym, sizeof(sym));
                if (strcmp(sym, "any") == 0) out->symbols_any = 1;
                else {
                    strncpy(out->symbols[0], sym, AF_ALGO_MAX_SYM_LEN - 1);
                    out->n_symbols = 1;
                }
            } else if (c == '[') {
                json_consume(&j, '[');
                while (json_peek(&j) != ']') {
                    char sym[AF_ALGO_MAX_SYM_LEN] = {0};
                    json_parse_string(&j, sym, sizeof(sym));
                    if (out->n_symbols < AF_ALGO_MAX_SYMBOLS)
                        strncpy(out->symbols[out->n_symbols++], sym, AF_ALGO_MAX_SYM_LEN - 1);
                    json_consume(&j, ',');
                }
                json_consume(&j, ']');
            } else { json_skip_value(&j); }
        } else if (strcmp(key, "indicators") == 0) {
            if (!json_consume(&j, '[')) goto fail;
            while (json_peek(&j) != ']') {
                if (out->n_indicators < AF_ALGO_MAX_INDICATORS) {
                    parse_indicator(&j, &out->indicators[out->n_indicators++]);
                } else { json_skip_value(&j); }
                json_consume(&j, ',');
            }
            json_consume(&j, ']');
        } else if (strcmp(key, "entries") == 0) {
            if (!json_consume(&j, '[')) goto fail;
            while (json_peek(&j) != ']') {
                if (out->n_entries < AF_ALGO_MAX_RULES) {
                    parse_entry_rule(&j, &out->entries[out->n_entries++]);
                } else { json_skip_value(&j); }
                json_consume(&j, ',');
            }
            json_consume(&j, ']');
        } else if (strcmp(key, "exits") == 0) {
            if (!json_consume(&j, '[')) goto fail;
            while (json_peek(&j) != ']') {
                if (out->n_exits < AF_ALGO_MAX_RULES) {
                    parse_exit_rule(&j, &out->exits[out->n_exits++]);
                } else { json_skip_value(&j); }
                json_consume(&j, ',');
            }
            json_consume(&j, ']');
        } else if (strcmp(key, "risk") == 0) {
            parse_risk(&j, &out->risk);
        } else if (strcmp(key, "code") == 0) {
            int c2 = json_peek(&j);
            if (c2 == 'n') {
                int v; json_parse_literal(&j, &v); out->has_code = 0;
            } else {
                json_parse_string(&j, out->code, sizeof(out->code));
                out->has_code = (out->code[0] != '\0');
            }
        } else {
            json_skip_value(&j);
        }
    } while (json_consume(&j, ','));
    json_consume(&j, '}');
    return 0;

fail:
    if (err) { strncpy(err->kind, "parse_error", 63); strncpy(err->message, "JSON parse failed", 511); }
    return -1;
}

/* ============================================================
 * §2  DSL tokenizer + recursive-descent parser + evaluator
 * ============================================================ *
 *
 * Grammar (simplified):
 *   expr     = or_expr
 *   or_expr  = and_expr ('or' and_expr)*
 *   and_expr = not_expr ('and' not_expr)*
 *   not_expr = 'not' not_expr | cmp_expr
 *   cmp_expr = add_expr (cmp_op add_expr)*
 *   add_expr = mul_expr (('+' | '-') mul_expr)*
 *   mul_expr = unary_expr (('*' | '/') unary_expr)*
 *   unary_expr = '-' unary_expr | primary
 *   primary  = NUMBER | TRUE | FALSE | NAME | NAME '.' NAME | '(' expr ')'
 */

typedef enum {
    TOK_EOF = 0,
    TOK_NUM, TOK_NAME,
    TOK_LPAREN, TOK_RPAREN,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH,
    TOK_LT, TOK_LE, TOK_GT, TOK_GE, TOK_EQ, TOK_NEQ,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_TRUE, TOK_FALSE,
    TOK_DOT
} tok_kind_t;

typedef struct {
    tok_kind_t kind;
    double     num_val;
    char       str_val[AF_DSL_ID_LEN];
} token_t;

typedef struct {
    const char *src;
    size_t      pos;
    size_t      len;
    token_t     cur;
    char        err_msg[256];
    int         had_error;
    /* for whitelist validation */
    const char **declared_names;
    size_t       n_names;
} dsl_parser_t;

/* ── tokenizer ─────────────────────────────────────────────────────────── */

static void dsl_set_error(dsl_parser_t *p, const char *msg) {
    if (!p->had_error) {
        strncpy(p->err_msg, msg, sizeof(p->err_msg) - 1);
        p->err_msg[sizeof(p->err_msg) - 1] = '\0';
        p->had_error = 1;
    }
}

static void dsl_skip_ws(dsl_parser_t *p) {
    while (p->pos < p->len &&
           (p->src[p->pos] == ' ' || p->src[p->pos] == '\t' ||
            p->src[p->pos] == '\n' || p->src[p->pos] == '\r'))
        ++p->pos;
}

/* BAR_FIELDS and PATTERN_NAMES whitelists */
static const char *s_bar_fields[] = {
    "close","open","high","low","volume","spread", NULL
};
static const char *s_pattern_names[] = {
    "doji","hammer","engulfing","marubozu","pin_bar",
    "morning_star","evening_star","three_white_soldiers","three_black_crows",
    "double_top","double_bottom","ascending_triangle","descending_triangle",
    "bullish_flag","bearish_flag","head_and_shoulders","inverse_head_and_shoulders",
    "gartley_bull","gartley_bear","bat_bull","butterfly_bull","crab_bull", NULL
};

static int is_bar_field(const char *n) {
    for (int i = 0; s_bar_fields[i]; ++i)
        if (strcmp(n, s_bar_fields[i]) == 0) return 1;
    return 0;
}

static int is_pattern_name(const char *n) {
    for (int i = 0; s_pattern_names[i]; ++i)
        if (strcmp(n, s_pattern_names[i]) == 0) return 1;
    return 0;
}

static int is_declared_name(dsl_parser_t *p, const char *n) {
    for (size_t i = 0; i < p->n_names; ++i)
        if (strcmp(p->declared_names[i], n) == 0) return 1;
    return 0;
}

static void dsl_lex(dsl_parser_t *p) {
    dsl_skip_ws(p);
    if (p->pos >= p->len) { p->cur.kind = TOK_EOF; return; }
    char c = p->src[p->pos];

    /* Punctuation */
    if (c == '(') { p->cur.kind = TOK_LPAREN; ++p->pos; return; }
    if (c == ')') { p->cur.kind = TOK_RPAREN; ++p->pos; return; }
    if (c == '+') { p->cur.kind = TOK_PLUS;   ++p->pos; return; }
    if (c == '-') { p->cur.kind = TOK_MINUS;  ++p->pos; return; }
    if (c == '*') { p->cur.kind = TOK_STAR;   ++p->pos; return; }
    if (c == '/') { p->cur.kind = TOK_SLASH;  ++p->pos; return; }
    if (c == '.') { p->cur.kind = TOK_DOT;    ++p->pos; return; }
    if (c == '<') {
        if (p->pos + 1 < p->len && p->src[p->pos+1] == '=')
            { p->cur.kind = TOK_LE; p->pos += 2; return; }
        p->cur.kind = TOK_LT; ++p->pos; return;
    }
    if (c == '>') {
        if (p->pos + 1 < p->len && p->src[p->pos+1] == '=')
            { p->cur.kind = TOK_GE; p->pos += 2; return; }
        p->cur.kind = TOK_GT; ++p->pos; return;
    }
    if (c == '=') {
        if (p->pos + 1 < p->len && p->src[p->pos+1] == '=')
            { p->cur.kind = TOK_EQ; p->pos += 2; return; }
        dsl_set_error(p, "unexpected '='"); return;
    }
    if (c == '!') {
        if (p->pos + 1 < p->len && p->src[p->pos+1] == '=')
            { p->cur.kind = TOK_NEQ; p->pos += 2; return; }
        dsl_set_error(p, "unexpected '!'"); return;
    }

    /* Number */
    if (isdigit((unsigned char)c) || (c == '.' && p->pos + 1 < p->len &&
                                       isdigit((unsigned char)p->src[p->pos+1]))) {
        char tmp[64]; size_t i = 0;
        while (p->pos < p->len && i < sizeof(tmp) - 1 &&
               (isdigit((unsigned char)p->src[p->pos]) || p->src[p->pos] == '.'))
            tmp[i++] = p->src[p->pos++];
        if (p->pos < p->len && (p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
            tmp[i++] = p->src[p->pos++];
            if (p->pos < p->len && (p->src[p->pos] == '+' || p->src[p->pos] == '-'))
                tmp[i++] = p->src[p->pos++];
            while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos]) && i < sizeof(tmp)-1)
                tmp[i++] = p->src[p->pos++];
        }
        tmp[i] = '\0';
        p->cur.kind    = TOK_NUM;
        p->cur.num_val = strtod(tmp, NULL);
        return;
    }

    /* Identifier / keyword */
    if (isalpha((unsigned char)c) || c == '_') {
        size_t i = 0;
        while (p->pos < p->len && i < AF_DSL_ID_LEN - 1 &&
               (isalnum((unsigned char)p->src[p->pos]) || p->src[p->pos] == '_'))
            p->cur.str_val[i++] = p->src[p->pos++];
        p->cur.str_val[i] = '\0';
        /* keywords */
        if (strcmp(p->cur.str_val, "and")   == 0) { p->cur.kind = TOK_AND;   return; }
        if (strcmp(p->cur.str_val, "or")    == 0) { p->cur.kind = TOK_OR;    return; }
        if (strcmp(p->cur.str_val, "not")   == 0) { p->cur.kind = TOK_NOT;   return; }
        if (strcmp(p->cur.str_val, "True")  == 0) { p->cur.kind = TOK_TRUE;  return; }
        if (strcmp(p->cur.str_val, "False") == 0) { p->cur.kind = TOK_FALSE; return; }
        p->cur.kind = TOK_NAME;
        return;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "unexpected character '%c'", c);
    dsl_set_error(p, msg);
}

/* ── expression tree ─────────────────────────────────────────────────────── */

typedef enum {
    EXPR_NUM, EXPR_BOOL, EXPR_NAME, EXPR_ATTR,
    EXPR_UNEG, EXPR_NOT,
    EXPR_ADD, EXPR_SUB, EXPR_MUL, EXPR_DIV,
    EXPR_LT, EXPR_LE, EXPR_GT, EXPR_GE, EXPR_EQ, EXPR_NEQ,
    EXPR_AND, EXPR_OR
} expr_kind_t;

struct af_algo_expr {
    expr_kind_t kind;
    double      num_val;
    int         bool_val;
    char        name[AF_DSL_ID_LEN];
    char        attr[AF_DSL_ID_LEN]; /* for EXPR_ATTR: "pattern.X" */
    struct af_algo_expr *left;
    struct af_algo_expr *right;
};

static af_algo_expr_t *expr_alloc(expr_kind_t k) {
    af_algo_expr_t *e = (af_algo_expr_t *)calloc(1, sizeof(af_algo_expr_t));
    if (e) e->kind = k;
    return e;
}

void af_algo_expr_free(af_algo_expr_t *e) {
    if (!e) return;
    af_algo_expr_free(e->left);
    af_algo_expr_free(e->right);
    free(e);
}

/* forward declarations */
static af_algo_expr_t *parse_or(dsl_parser_t *p);

static af_algo_expr_t *parse_primary(dsl_parser_t *p) {
    if (p->had_error) return NULL;
    token_t t = p->cur;
    dsl_lex(p);

    if (t.kind == TOK_NUM) {
        af_algo_expr_t *e = expr_alloc(EXPR_NUM);
        if (e) e->num_val = t.num_val;
        return e;
    }
    if (t.kind == TOK_TRUE) {
        af_algo_expr_t *e = expr_alloc(EXPR_BOOL);
        if (e) e->bool_val = 1;
        return e;
    }
    if (t.kind == TOK_FALSE) {
        af_algo_expr_t *e = expr_alloc(EXPR_BOOL);
        if (e) e->bool_val = 0;
        return e;
    }
    if (t.kind == TOK_NAME) {
        /* Check if next is '.' (attribute access, e.g. pattern.doji) */
        if (p->cur.kind == TOK_DOT) {
            dsl_lex(p); /* consume '.' */
            if (p->cur.kind != TOK_NAME) {
                dsl_set_error(p, "expected attribute name after '.'");
                return NULL;
            }
            /* only "pattern" object allowed */
            if (strcmp(t.str_val, "pattern") != 0) {
                char msg[128];
                snprintf(msg, sizeof(msg), "attribute access only allowed on 'pattern', not '%s'", t.str_val);
                dsl_set_error(p, msg);
                return NULL;
            }
            if (!is_pattern_name(p->cur.str_val)) {
                char msg[128];
                snprintf(msg, sizeof(msg), "unknown pattern name '%s'", p->cur.str_val);
                dsl_set_error(p, msg);
                return NULL;
            }
            af_algo_expr_t *e = expr_alloc(EXPR_ATTR);
            if (e) {
                strncpy(e->name, t.str_val, AF_DSL_ID_LEN - 1);
                strncpy(e->attr, p->cur.str_val, AF_DSL_ID_LEN - 1);
            }
            dsl_lex(p); /* consume attr name */
            return e;
        }
        /* Plain name — must be bar field or declared indicator */
        if (!is_bar_field(t.str_val) && !is_declared_name(p, t.str_val)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "unknown name '%s'", t.str_val);
            dsl_set_error(p, msg);
            return NULL;
        }
        af_algo_expr_t *e = expr_alloc(EXPR_NAME);
        if (e) strncpy(e->name, t.str_val, AF_DSL_ID_LEN - 1);
        return e;
    }
    if (t.kind == TOK_LPAREN) {
        af_algo_expr_t *e = parse_or(p);
        if (!json_consume(NULL, 0)) { /* dummy */ }
        if (p->cur.kind != TOK_RPAREN) {
            dsl_set_error(p, "expected ')'");
            af_algo_expr_free(e);
            return NULL;
        }
        dsl_lex(p); /* consume ')' */
        return e;
    }
    dsl_set_error(p, "expected primary expression");
    return NULL;
}

static af_algo_expr_t *parse_unary(dsl_parser_t *p) {
    if (p->had_error) return NULL;
    if (p->cur.kind == TOK_MINUS) {
        dsl_lex(p);
        af_algo_expr_t *operand = parse_unary(p);
        if (!operand) return NULL;
        af_algo_expr_t *e = expr_alloc(EXPR_UNEG);
        if (e) e->left = operand;
        return e;
    }
    return parse_primary(p);
}

static af_algo_expr_t *parse_mul(dsl_parser_t *p) {
    af_algo_expr_t *left = parse_unary(p);
    if (!left) return NULL;
    while (!p->had_error && (p->cur.kind == TOK_STAR || p->cur.kind == TOK_SLASH)) {
        expr_kind_t k = (p->cur.kind == TOK_STAR) ? EXPR_MUL : EXPR_DIV;
        dsl_lex(p);
        af_algo_expr_t *right = parse_unary(p);
        if (!right) { af_algo_expr_free(left); return NULL; }
        af_algo_expr_t *e = expr_alloc(k);
        if (e) { e->left = left; e->right = right; }
        left = e;
    }
    return left;
}

static af_algo_expr_t *parse_add(dsl_parser_t *p) {
    af_algo_expr_t *left = parse_mul(p);
    if (!left) return NULL;
    while (!p->had_error && (p->cur.kind == TOK_PLUS || p->cur.kind == TOK_MINUS)) {
        expr_kind_t k = (p->cur.kind == TOK_PLUS) ? EXPR_ADD : EXPR_SUB;
        dsl_lex(p);
        af_algo_expr_t *right = parse_mul(p);
        if (!right) { af_algo_expr_free(left); return NULL; }
        af_algo_expr_t *e = expr_alloc(k);
        if (e) { e->left = left; e->right = right; }
        left = e;
    }
    return left;
}

static af_algo_expr_t *parse_cmp(dsl_parser_t *p) {
    af_algo_expr_t *left = parse_add(p);
    if (!left) return NULL;
    while (!p->had_error) {
        expr_kind_t k;
        switch (p->cur.kind) {
            case TOK_LT:  k = EXPR_LT;  break;
            case TOK_LE:  k = EXPR_LE;  break;
            case TOK_GT:  k = EXPR_GT;  break;
            case TOK_GE:  k = EXPR_GE;  break;
            case TOK_EQ:  k = EXPR_EQ;  break;
            case TOK_NEQ: k = EXPR_NEQ; break;
            default: return left;
        }
        dsl_lex(p);
        af_algo_expr_t *right = parse_add(p);
        if (!right) { af_algo_expr_free(left); return NULL; }
        af_algo_expr_t *e = expr_alloc(k);
        if (e) { e->left = left; e->right = right; }
        left = e;
    }
    return left;
}

static af_algo_expr_t *parse_not(dsl_parser_t *p) {
    if (p->had_error) return NULL;
    if (p->cur.kind == TOK_NOT) {
        dsl_lex(p);
        af_algo_expr_t *operand = parse_not(p);
        if (!operand) return NULL;
        af_algo_expr_t *e = expr_alloc(EXPR_NOT);
        if (e) e->left = operand;
        return e;
    }
    return parse_cmp(p);
}

static af_algo_expr_t *parse_and(dsl_parser_t *p) {
    af_algo_expr_t *left = parse_not(p);
    if (!left) return NULL;
    while (!p->had_error && p->cur.kind == TOK_AND) {
        dsl_lex(p);
        af_algo_expr_t *right = parse_not(p);
        if (!right) { af_algo_expr_free(left); return NULL; }
        af_algo_expr_t *e = expr_alloc(EXPR_AND);
        if (e) { e->left = left; e->right = right; }
        left = e;
    }
    return left;
}

static af_algo_expr_t *parse_or(dsl_parser_t *p) {
    af_algo_expr_t *left = parse_and(p);
    if (!left) return NULL;
    while (!p->had_error && p->cur.kind == TOK_OR) {
        dsl_lex(p);
        af_algo_expr_t *right = parse_and(p);
        if (!right) { af_algo_expr_free(left); return NULL; }
        af_algo_expr_t *e = expr_alloc(EXPR_OR);
        if (e) { e->left = left; e->right = right; }
        left = e;
    }
    return left;
}

/* ── public compile ─────────────────────────────────────────────────────── */
int af_algo_dsl_compile(const char *src,
                         const char **declared_names, size_t n_names,
                         af_algo_expr_t **out,
                         af_algo_error_t *err)
{
    dsl_parser_t p;
    memset(&p, 0, sizeof(p));
    p.src = src;
    p.len = strlen(src);
    p.declared_names = declared_names;
    p.n_names = n_names;
    dsl_lex(&p); /* prime first token */

    af_algo_expr_t *tree = parse_or(&p);
    if (p.had_error || !tree) {
        af_algo_expr_free(tree);
        if (err) {
            strncpy(err->kind, "dsl_error", 63);
            snprintf(err->message, 511, "DSL parse error: %s", p.err_msg);
        }
        return -1;
    }
    if (p.cur.kind != TOK_EOF) {
        af_algo_expr_free(tree);
        if (err) {
            strncpy(err->kind, "dsl_error", 63);
            strncpy(err->message, "unexpected tokens after expression", 511);
        }
        return -1;
    }
    *out = tree;
    return 0;
}

/* ── evaluator ──────────────────────────────────────────────────────────── */

static double eval_numeric(const af_algo_expr_t *e, const af_algo_dsl_env_t *env, int *ok);
static int    eval_bool(const af_algo_expr_t *e, const af_algo_dsl_env_t *env);

static double eval_numeric(const af_algo_expr_t *e, const af_algo_dsl_env_t *env, int *ok) {
    if (!e) { *ok = 0; return NAN; }
    switch (e->kind) {
        case EXPR_NUM: return e->num_val;
        case EXPR_BOOL: return (double)e->bool_val;
        case EXPR_NAME: {
            /* bar field? */
            if (strcmp(e->name, "close")  == 0) return env->close;
            if (strcmp(e->name, "open")   == 0) return env->open;
            if (strcmp(e->name, "high")   == 0) return env->high;
            if (strcmp(e->name, "low")    == 0) return env->low;
            if (strcmp(e->name, "volume") == 0) return env->volume;
            if (strcmp(e->name, "spread") == 0) return env->spread;
            /* indicator */
            for (int i = 0; i < env->n_indicators; ++i)
                if (strcmp(env->indicator_ids[i], e->name) == 0)
                    return env->indicator_vals[i];
            *ok = 0; return NAN;
        }
        case EXPR_ATTR: {
            /* pattern.X */
            for (int i = 0; i < env->n_patterns; ++i)
                if (strcmp(env->pattern_names[i], e->attr) == 0)
                    return (double)env->pattern_vals[i];
            return 0.0;
        }
        case EXPR_UNEG: { double v = eval_numeric(e->left, env, ok); return isnan(v) ? v : -v; }
        case EXPR_ADD: {
            double l = eval_numeric(e->left, env, ok);
            double r = eval_numeric(e->right, env, ok);
            return l + r;
        }
        case EXPR_SUB: {
            double l = eval_numeric(e->left, env, ok);
            double r = eval_numeric(e->right, env, ok);
            return l - r;
        }
        case EXPR_MUL: {
            double l = eval_numeric(e->left, env, ok);
            double r = eval_numeric(e->right, env, ok);
            return l * r;
        }
        case EXPR_DIV: {
            double l = eval_numeric(e->left, env, ok);
            double r = eval_numeric(e->right, env, ok);
            if (fabs(r) < 1e-20) { *ok = 0; return NAN; }
            return l / r;
        }
        default: *ok = 0; return NAN;
    }
}

static int eval_bool(const af_algo_expr_t *e, const af_algo_dsl_env_t *env) {
    if (!e) return 0;
    switch (e->kind) {
        case EXPR_BOOL: return e->bool_val;
        case EXPR_AND:  return eval_bool(e->left, env) && eval_bool(e->right, env);
        case EXPR_OR:   return eval_bool(e->left, env) || eval_bool(e->right, env);
        case EXPR_NOT:  return !eval_bool(e->left, env);
        default: {
            /* Try as numeric — for bare names that might be truthy */
            if (e->kind == EXPR_NAME || e->kind == EXPR_ATTR ||
                e->kind == EXPR_NUM) {
                int ok = 1; double v = eval_numeric(e, env, &ok);
                if (!ok || isnan(v)) return 0;
                return v != 0.0;
            }
            /* Comparison operators */
            int ok_l = 1, ok_r = 1;
            double l = eval_numeric(e->left,  env, &ok_l);
            double r = eval_numeric(e->right, env, &ok_r);
            if (!ok_l || !ok_r || isnan(l) || isnan(r)) return 0;
            switch (e->kind) {
                case EXPR_LT:  return l < r;
                case EXPR_LE:  return l <= r;
                case EXPR_GT:  return l > r;
                case EXPR_GE:  return l >= r;
                case EXPR_EQ:  return l == r;
                case EXPR_NEQ: return l != r;
                default: return 0;
            }
        }
    }
}

int af_algo_dsl_eval(const af_algo_expr_t *e,
                     const af_algo_dsl_env_t *env,
                     int *result_bool) {
    if (!e || !env || !result_bool) return -1;
    *result_bool = eval_bool(e, env);
    return 0;
}

/* ============================================================
 * §3  Indicator dispatch
 * ============================================================ *
 *
 * Given an af_algo_indicator_decl_t and bar arrays, compute the full
 * output buffer.  Returns the "current" (last bar) value.
 */

/* Helper: get a numeric param by key, with default */
static double ind_param(const af_algo_indicator_decl_t *d,
                         const char *key, double def_val) {
    for (int i = 0; i < d->n_params; ++i)
        if (strcmp(d->params[i].key, key) == 0)
            return d->params[i].value;
    return def_val;
}

/* Maximum scratch buffer we can put on stack for an indicator output */
#define IND_MAX_BARS 65536

/**
 * Compute indicator d over bars[0..n-1], write result into out[0..n-1].
 * Returns the value at index (n-1), or NaN on failure/warmup.
 * out must be caller-allocated with capacity n.
 */
static double compute_indicator(const af_algo_indicator_decl_t *d,
                                  const af_bar_t *bars, size_t n,
                                  double *out)
{
    if (n == 0) return NAN;

    /* Extract price arrays — only allocate what we need */
    double *cl = NULL, *op = NULL, *hi = NULL, *lo = NULL, *vo = NULL;
    cl = (double *)malloc(n * sizeof(double));
    if (!cl) return NAN;
    op = (double *)malloc(n * sizeof(double));
    hi = (double *)malloc(n * sizeof(double));
    lo = (double *)malloc(n * sizeof(double));
    vo = (double *)malloc(n * sizeof(double));
    if (!op || !hi || !lo || !vo) {
        free(cl); free(op); free(hi); free(lo); free(vo); return NAN;
    }
    for (size_t i = 0; i < n; ++i) {
        cl[i] = bars[i].close;
        op[i] = bars[i].open;
        hi[i] = bars[i].high;
        lo[i] = bars[i].low;
        vo[i] = bars[i].volume;
    }

    const char *k = d->kind;
    double val = NAN;
    af_error_t rc = AF_OK;

    if (strcmp(k, "sma") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_sma(cl, n, per, out);
    } else if (strcmp(k, "ema") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_ema(cl, n, per, out);
    } else if (strcmp(k, "rsi") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_rsi(cl, n, per, out);
    } else if (strcmp(k, "atr") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_atr(hi, lo, cl, n, per, out);
    } else if (strcmp(k, "macd") == 0) {
        size_t fast = (size_t)ind_param(d, "fast",   12);
        size_t slow = (size_t)ind_param(d, "slow",   26);
        size_t sig  = (size_t)ind_param(d, "signal",  9);
        double *sig_line = (double *)malloc(n * sizeof(double));
        double *hist     = (double *)malloc(n * sizeof(double));
        if (sig_line && hist) {
            rc = af_macd(cl, n, fast, slow, sig, out, sig_line, hist);
        }
        free(sig_line); free(hist);
    } else if (strcmp(k, "bollinger") == 0) {
        size_t per  = (size_t)ind_param(d, "period", 20);
        double mult = ind_param(d, "mult", 2.0);
        double *up  = (double *)malloc(n * sizeof(double));
        double *lo2 = (double *)malloc(n * sizeof(double));
        if (up && lo2) rc = af_bollinger(cl, n, per, mult, up, out, lo2);
        free(up); free(lo2);
        /* out = middle band; caller uses that for threshold comparisons */
    } else if (strcmp(k, "stochastic") == 0) {
        size_t kp = (size_t)ind_param(d, "k_period", 14);
        size_t dp = (size_t)ind_param(d, "d_period",  3);
        double *d_out = (double *)malloc(n * sizeof(double));
        if (d_out) rc = af_stochastic(hi, lo, cl, n, kp, dp, out, d_out);
        free(d_out);
    } else if (strcmp(k, "obv") == 0) {
        rc = af_obv(cl, vo, n, out);
    } else if (strcmp(k, "wma") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_wma(cl, n, per, out);
    } else if (strcmp(k, "cci") == 0) {
        size_t per  = (size_t)ind_param(d, "period",   20);
        double cons = ind_param(d, "constant", 0.015);
        rc = af_cci(hi, lo, cl, n, per, cons, out);
    } else if (strcmp(k, "williams_r") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_williams_r(hi, lo, cl, n, per, out);
    } else if (strcmp(k, "roc") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_roc(cl, n, per, out);
    } else if (strcmp(k, "mfi") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_mfi(hi, lo, cl, vo, n, per, out);
    } else if (strcmp(k, "vwap") == 0) {
        rc = af_vwap(hi, lo, cl, vo, n, out);
    } else if (strcmp(k, "keltner") == 0) {
        size_t ema_p = (size_t)ind_param(d, "ema_period", 20);
        size_t atr_p = (size_t)ind_param(d, "atr_period", 14);
        double mult  = ind_param(d, "mult", 2.0);
        double *up   = (double *)malloc(n * sizeof(double));
        double *lo2  = (double *)malloc(n * sizeof(double));
        if (up && lo2) rc = af_keltner(hi, lo, cl, n, ema_p, atr_p, mult, up, out, lo2);
        free(up); free(lo2);
    } else if (strcmp(k, "adx") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        double *pd = (double *)malloc(n * sizeof(double));
        double *md = (double *)malloc(n * sizeof(double));
        if (pd && md) rc = af_adx(hi, lo, cl, n, per, out, pd, md);
        free(pd); free(md);
    } else if (strcmp(k, "hma") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_hma(cl, n, per, out);
    } else if (strcmp(k, "dema") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_dema(cl, n, per, out);
    } else if (strcmp(k, "tema") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_tema(cl, n, per, out);
    } else if (strcmp(k, "trix") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_trix(cl, n, per, out);
    } else if (strcmp(k, "momentum") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_momentum(cl, n, per, out);
    } else if (strcmp(k, "true_range") == 0) {
        rc = af_true_range(hi, lo, cl, n, out);
    } else if (strcmp(k, "wilder_ema") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_wilder_ema(cl, n, per, out);
    } else if (strcmp(k, "vwma") == 0) {
        size_t per = (size_t)ind_param(d, "period", 14);
        rc = af_vwma(cl, vo, n, per, out);
    } else if (strcmp(k, "hist_vol") == 0) {
        size_t per = (size_t)ind_param(d, "period", 20);
        rc = af_hist_vol(cl, n, per, out);
    } else if (strcmp(k, "cmf") == 0) {
        size_t per = (size_t)ind_param(d, "period", 20);
        rc = af_cmf(hi, lo, cl, vo, n, per, out);
    } else if (strcmp(k, "acc_dist") == 0) {
        rc = af_acc_dist(hi, lo, cl, vo, n, out);
    } else if (strcmp(k, "force_index") == 0) {
        size_t per = (size_t)ind_param(d, "period", 13);
        rc = af_force_index(cl, vo, n, per, out);
    } else if (strcmp(k, "vol_osc") == 0) {
        size_t fast = (size_t)ind_param(d, "fast", 5);
        size_t slow = (size_t)ind_param(d, "slow", 10);
        rc = af_vol_osc(vo, n, fast, slow, out);
    } else if (strcmp(k, "donchian") == 0) {
        size_t per = (size_t)ind_param(d, "period", 20);
        double *up = (double *)malloc(n * sizeof(double));
        double *lo2= (double *)malloc(n * sizeof(double));
        if (up && lo2) rc = af_donchian(hi, lo, n, per, up, out, lo2);
        free(up); free(lo2);
        /* out = middle channel */
    } else {
        /* Unknown kind — fill with NaN */
        for (size_t i = 0; i < n; ++i) out[i] = NAN;
    }

    if (rc == AF_OK && n > 0) val = out[n - 1];
    (void)rc; /* ignore; NaN propagation handles missing data */

    free(cl); free(op); free(hi); free(lo); free(vo);
    return val;
}

/* ============================================================
 * §4  Runtime algorithm (af_algorithm_t vtable)
 * ============================================================ */

static const char *manifest_algo_name(const af_algorithm_t *self) {
    const af_algo_manifest_algo_t *a = (const af_algo_manifest_algo_t *)self;
    return a->manifest->name;
}
static const char *manifest_algo_desc(const af_algorithm_t *self) {
    const af_algo_manifest_algo_t *a = (const af_algo_manifest_algo_t *)self;
    return a->manifest->description;
}
static uint32_t manifest_algo_magic(const af_algorithm_t *self) {
    /* deterministic hash of the name */
    const af_algo_manifest_algo_t *a = (const af_algo_manifest_algo_t *)self;
    uint32_t h = 5381;
    for (const char *c = a->manifest->name; *c; ++c)
        h = h * 33 ^ (unsigned char)*c;
    return h;
}

/**
 * Build indicator values for bar i (using bars[0..i]).
 * Fills env->indicator_vals from the pre-computed buffers.
 */
static void build_env(af_algo_manifest_algo_t *a,
                       const af_bar_t *bars, size_t i,
                       af_algo_dsl_env_t *env) {
    memset(env, 0, sizeof(*env));
    env->n_indicators = a->manifest->n_indicators;
    env->close  = bars[i].close;
    env->open   = bars[i].open;
    env->high   = bars[i].high;
    env->low    = bars[i].low;
    env->volume = bars[i].volume;
    env->spread = bars[i].spread;

    for (int j = 0; j < a->manifest->n_indicators; ++j) {
        strncpy(env->indicator_ids[j], a->manifest->indicators[j].id,
                AF_DSL_ID_LEN - 1);
        if ((int)i < a->ind_buf_len[j])
            env->indicator_vals[j] = a->ind_buf[j][i];
        else
            env->indicator_vals[j] = NAN;
    }
    /* patterns — not computed here; leave all zero */
    env->n_patterns = 0;
}

static af_algo_decision_t manifest_algo_evaluate(
    af_algorithm_t        *self,
    const char            *symbol,
    af_timeframe_t         tf,
    const af_bar_t        *bars,
    size_t                 count,
    const af_engine_result_t *ind,
    const AF_PatternResult   *pat)
{
    (void)symbol; (void)tf; (void)ind; (void)pat;

    af_algo_manifest_algo_t *a = (af_algo_manifest_algo_t *)self;
    af_algo_decision_t dec = {0};
    dec.signal    = AF_ALGO_NONE;
    dec.direction = 0;

    if (count == 0) return dec;
    size_t i = count - 1;

    /* Ensure indicator buffers are populated up to bar i */
    for (int j = 0; j < a->manifest->n_indicators; ++j) {
        if (a->ind_buf_len[j] != (int)count) {
            /* Recompute */
            compute_indicator(&a->manifest->indicators[j],
                               bars, count,
                               a->ind_buf[j]);
            a->ind_buf_len[j] = (int)count;
        }
    }

    /* Check cool-down */
    if (a->cool_down > 0 && a->bars_since_exit < a->cool_down) {
        ++a->bars_since_exit;
        return dec;
    }

    af_algo_dsl_env_t env;
    build_env(a, bars, i, &env);

    /* Test entry conditions */
    for (int r = 0; r < a->n_entry_exprs; ++r) {
        int result = 0;
        af_algo_dsl_eval(a->entry_exprs[r], &env, &result);
        if (result) {
            dec.signal     = (a->entry_sides[r] > 0) ? AF_ALGO_BUY : AF_ALGO_SELL;
            dec.direction  = a->entry_sides[r];
            dec.confidence = 0.8;

            /* Provide ATR if we have an ATR indicator */
            if (a->atr_ind_idx >= 0 && a->atr_ind_idx < a->manifest->n_indicators) {
                int ai = a->atr_ind_idx;
                if ((int)i < a->ind_buf_len[ai])
                    dec.atr = a->ind_buf[ai][i];
            }
            strncpy(dec.symbol, symbol ? symbol : "", sizeof(dec.symbol) - 1);
            snprintf(dec.reason, sizeof(dec.reason), "entry rule %d", r);
            a->bars_since_exit = 0;
            break;
        }
    }
    return dec;
}

int af_algo_manifest_algo_init(af_algo_manifest_algo_t *a,
                                const af_algo_manifest_t *m,
                                af_algo_error_t *err)
{
    memset(a, 0, sizeof(*a));
    a->manifest = m;

    /* Build declared names list for DSL whitelist */
    const char *names[AF_ALGO_MAX_INDICATORS];
    for (int i = 0; i < m->n_indicators; ++i)
        names[i] = m->indicators[i].id;

    /* Compile entry rules */
    a->n_entry_exprs = m->n_entries;
    for (int i = 0; i < m->n_entries; ++i) {
        if (af_algo_dsl_compile(m->entries[i].when,
                                 names, (size_t)m->n_indicators,
                                 &a->entry_exprs[i], err)) {
            af_algo_manifest_algo_destroy(a);
            return -1;
        }
        a->entry_sides[i] = (strcmp(m->entries[i].side, "long") == 0) ? 1 : -1;
    }

    /* Compile exit rules */
    a->n_exit_exprs = m->n_exits;
    for (int i = 0; i < m->n_exits; ++i) {
        if (m->exits[i].has_when && m->exits[i].when[0] != '\0') {
            if (af_algo_dsl_compile(m->exits[i].when,
                                     names, (size_t)m->n_indicators,
                                     &a->exit_exprs[i], err)) {
                af_algo_manifest_algo_destroy(a);
                return -1;
            }
        }
        a->exit_sides[i] = (strcmp(m->exits[i].side, "long") == 0) ? 1 : -1;
        /* sl/tp brackets */
        if (strcmp(m->exits[i].side, "long") == 0) {
            if (m->exits[i].has_sl_atr) a->sl_atr_long  = m->exits[i].sl_atr;
            if (m->exits[i].has_tp_atr) a->tp_atr_long  = m->exits[i].tp_atr;
        } else {
            if (m->exits[i].has_sl_atr) a->sl_atr_short = m->exits[i].sl_atr;
            if (m->exits[i].has_tp_atr) a->tp_atr_short = m->exits[i].tp_atr;
        }
    }

    /* Find ATR indicator index */
    a->atr_ind_idx = -1;
    for (int i = 0; i < m->n_indicators; ++i) {
        if (strcmp(m->indicators[i].kind, "atr") == 0) { a->atr_ind_idx = i; break; }
    }

    a->cool_down     = m->risk.cool_down_bars;
    a->bars_since_exit = m->risk.cool_down_bars; /* start ready */

    /* Wire up vtable */
    a->base.name        = manifest_algo_name;
    a->base.description = manifest_algo_desc;
    a->base.magic       = manifest_algo_magic;
    a->base.evaluate    = manifest_algo_evaluate;
    a->base.enabled     = 1;
    a->base.impl        = a;
    a->base.metadata    = NULL; /* set after promotion */

    return 0;
}

void af_algo_manifest_algo_destroy(af_algo_manifest_algo_t *a) {
    for (int i = 0; i < AF_ALGO_MAX_RULES; ++i) {
        af_algo_expr_free(a->entry_exprs[i]); a->entry_exprs[i] = NULL;
        af_algo_expr_free(a->exit_exprs[i]);  a->exit_exprs[i]  = NULL;
    }
}

/* ============================================================
 * §5  Validation pipeline (5 stages)
 * ============================================================ */

/* ── Stage 1: Schema lint ────────────────────────────────────────────────── */

/* INDICATOR_KINDS whitelist (spec §3) */
static const char *s_ind_kinds[] = {
    "sma","ema","rsi","atr","macd","bollinger","stochastic","obv","wma","cci",
    "williams_r","roc","mfi","vwap","keltner","adx","hma","dema","tema","trix",
    "momentum","true_range","wilder_ema","vwma","hist_vol","cmf","acc_dist",
    "force_index","vol_osc","donchian", NULL
};

static const char *s_valid_tfs[] = {
    "M1","M5","M15","M30","H1","H4","D1","W1","S15", NULL
};

static int is_valid_kind(const char *k) {
    for (int i = 0; s_ind_kinds[i]; ++i)
        if (strcmp(s_ind_kinds[i], k) == 0) return 1;
    return 0;
}

static int is_valid_tf(const char *t) {
    for (int i = 0; s_valid_tfs[i]; ++i)
        if (strcmp(s_valid_tfs[i], t) == 0) return 1;
    return 0;
}

/* kebab-case: starts with [a-z], followed by [a-z0-9\-]* */
static int is_kebab_case(const char *s) {
    if (!s || !*s) return 0;
    if (!islower((unsigned char)s[0])) return 0;
    for (int i = 1; s[i]; ++i) {
        char c = s[i];
        if (!islower((unsigned char)c) && !isdigit((unsigned char)c) && c != '-')
            return 0;
    }
    return 1;
}

static int stage1_schema_lint(const af_algo_manifest_t *m,
                               af_algo_stage_result_t *stage,
                               af_algo_error_t *err)
{
    stage->stage = 1;
    strncpy(stage->name, "schema_lint", sizeof(stage->name) - 1);
    stage->passed = 0;

    /* Name must be kebab-case */
    if (!is_kebab_case(m->name)) {
        snprintf(stage->reason, sizeof(stage->reason),
                 "name must be kebab-case, got '%s'", m->name);
        if (err) { strncpy(err->kind, "schema_error", 63); strncpy(err->message, stage->reason, 511); }
        return -1;
    }

    /* Timeframe validation */
    for (int i = 0; i < m->n_timeframes; ++i) {
        if (!is_valid_tf(m->timeframes[i])) {
            snprintf(stage->reason, sizeof(stage->reason),
                     "unknown timeframe '%s'", m->timeframes[i]);
            if (err) { strncpy(err->kind, "schema_error", 63); strncpy(err->message, stage->reason, 511); }
            return -1;
        }
    }

    /* Indicator kind validation */
    for (int i = 0; i < m->n_indicators; ++i) {
        if (!is_valid_kind(m->indicators[i].kind)) {
            snprintf(stage->reason, sizeof(stage->reason),
                     "unknown indicator kind '%s'", m->indicators[i].kind);
            if (err) { strncpy(err->kind, "schema_error", 63); strncpy(err->message, stage->reason, 511); }
            return -1;
        }
        if (m->indicators[i].id[0] == '\0') {
            strncpy(stage->reason, "indicator missing 'id'", sizeof(stage->reason) - 1);
            return -1;
        }
    }

    /* At least one entry rule */
    if (m->n_entries == 0) {
        strncpy(stage->reason, "no entry rules defined", sizeof(stage->reason) - 1);
        return -1;
    }

    /* DSL compilation check for all expressions */
    const char *names[AF_ALGO_MAX_INDICATORS];
    for (int i = 0; i < m->n_indicators; ++i) names[i] = m->indicators[i].id;

    for (int i = 0; i < m->n_entries; ++i) {
        af_algo_expr_t *e = NULL;
        af_algo_error_t dsl_err;
        af_algo_error_clear(&dsl_err);
        if (af_algo_dsl_compile(m->entries[i].when,
                                 names, (size_t)m->n_indicators,
                                 &e, &dsl_err)) {
            snprintf(stage->reason, sizeof(stage->reason),
                     "entry %d DSL error: %s", i, dsl_err.message);
            if (err) *err = dsl_err;
            return -1;
        }
        af_algo_expr_free(e);
    }
    for (int i = 0; i < m->n_exits; ++i) {
        if (!m->exits[i].has_when || m->exits[i].when[0] == '\0') continue;
        af_algo_expr_t *e = NULL;
        af_algo_error_t dsl_err;
        af_algo_error_clear(&dsl_err);
        if (af_algo_dsl_compile(m->exits[i].when,
                                 names, (size_t)m->n_indicators,
                                 &e, &dsl_err)) {
            snprintf(stage->reason, sizeof(stage->reason),
                     "exit %d DSL error: %s", i, dsl_err.message);
            if (err) *err = dsl_err;
            return -1;
        }
        af_algo_expr_free(e);
    }

    stage->passed = 1;
    strncpy(stage->reason, "OK", sizeof(stage->reason) - 1);
    return 0;
}

/* ── Stage 2: Sandbox backtest ────────────────────────────────────────────── */

static int stage2_sandbox_backtest(const af_algo_manifest_t *m,
                                    const af_bar_t *bars, size_t n_bars,
                                    af_algo_manifest_algo_t *algo,
                                    af_bt_result_t *bt_out,
                                    af_algo_stage_result_t *stage,
                                    af_algo_error_t *err)
{
    stage->stage = 2;
    strncpy(stage->name, "sandbox_backtest", sizeof(stage->name) - 1);
    stage->passed = 0;

    af_bt_config_t cfg;
    af_bt_config_default(&cfg);
    if (m->risk.atr_mult > 0.0) cfg.atr_sl_mult = m->risk.atr_mult;

    /* Pick first symbol, or "EURUSD" */
    const char *sym = (m->n_symbols > 0 && !m->symbols_any) ? m->symbols[0] : "EURUSD";
    af_timeframe_t tf = AF_TF_H1; /* default */
    if (m->n_timeframes > 0) {
        const char *ts = m->timeframes[0];
        if (strcmp(ts, "M1") == 0)  tf = AF_TF_M1;
        else if (strcmp(ts, "M5") == 0)  tf = AF_TF_M5;
        else if (strcmp(ts, "M15") == 0) tf = AF_TF_M15;
        else if (strcmp(ts, "M30") == 0) tf = AF_TF_M30;
        else if (strcmp(ts, "H1") == 0)  tf = AF_TF_H1;
        else if (strcmp(ts, "H4") == 0)  tf = AF_TF_H4;
        else if (strcmp(ts, "D1") == 0)  tf = AF_TF_D1;
        else if (strcmp(ts, "W1") == 0)  tf = AF_TF_W1;
    }

    *bt_out = af_backtest_run(&algo->base, &cfg, bars, n_bars, sym, tf);
    stage->trades = bt_out->trade_count;

    if (bt_out->trade_count < STAGE_MIN_TRADES) {
        snprintf(stage->reason, sizeof(stage->reason),
                 "too few trades: %d < %d", bt_out->trade_count, STAGE_MIN_TRADES);
        if (err) {
            strncpy(err->kind, "stage_failed", 63);
            strncpy(err->message, stage->reason, 511);
        }
        return -1;
    }

    double sharpe = af_bt_sharpe_ratio(bt_out);
    double mdd    = af_bt_max_drawdown_pct(bt_out);

    if (sharpe < STAGE_SHARPE_GATE) {
        snprintf(stage->reason, sizeof(stage->reason),
                 "sharpe %.3f < %.1f gate", sharpe, STAGE_SHARPE_GATE);
        return -1;
    }
    if (mdd > STAGE_MAX_DD_GATE) {
        snprintf(stage->reason, sizeof(stage->reason),
                 "max drawdown %.1f%% > %.0f%% gate", mdd, STAGE_MAX_DD_GATE);
        return -1;
    }

    stage->passed = 1;
    strncpy(stage->reason, "OK", sizeof(stage->reason) - 1);
    return 0;
}

/* ── Walk-forward context ────────────────────────────────────────────────── */

typedef struct {
    const af_algo_manifest_t    *manifest;
    const af_bar_t              *bars;
    size_t                       n_bars;
    af_algo_manifest_algo_t     *algo; /* reused across folds */
    /* per-fold pass/fail */
    int                          fold_passed[AF_WF_MAX_FOLDS];
    int                          n_folds_run;
} wf_ctx_t;

static af_error_t wf_backtest_fn(int train_start, int train_end,
                                  int test_start, int test_end,
                                  void *ctx,
                                  af_metrics_t *train_out,
                                  af_metrics_t *test_out)
{
    wf_ctx_t *wc = (wf_ctx_t *)ctx;
    int fold_idx = wc->n_folds_run;

    /* Reset algorithm state */
    af_algo_manifest_algo_t algo;
    af_algo_error_t err;
    af_algo_error_clear(&err);
    if (af_algo_manifest_algo_init(&algo, wc->manifest, &err) != 0) {
        return AF_ERR_INVALID_PARAM;
    }

    af_bt_config_t cfg;
    af_bt_config_default(&cfg);
    if (wc->manifest->risk.atr_mult > 0.0)
        cfg.atr_sl_mult = wc->manifest->risk.atr_mult;

    const char *sym = (wc->manifest->n_symbols > 0 && !wc->manifest->symbols_any)
                      ? wc->manifest->symbols[0] : "EURUSD";

    /* Train fold */
    af_bt_result_t tr = af_backtest_run(&algo.base, &cfg,
                                         wc->bars + train_start,
                                         (size_t)(train_end - train_start),
                                         sym, AF_TF_H1);

    /* Test fold */
    /* Reset indicator state for test */
    af_algo_manifest_algo_destroy(&algo);
    af_algo_manifest_algo_init(&algo, wc->manifest, &err);

    af_bt_result_t te = af_backtest_run(&algo.base, &cfg,
                                         wc->bars + test_start,
                                         (size_t)(test_end - test_start),
                                         sym, AF_TF_H1);
    af_algo_manifest_algo_destroy(&algo);

    /* Populate metrics structs */
    memset(train_out, 0, sizeof(*train_out));
    memset(test_out,  0, sizeof(*test_out));

    /* Simple pass gate for test fold: must have positive net profit */
    int passed = (te.trade_count >= 1 && af_bt_net_profit(&te) > 0.0);
    /* Also gate on sharpe */
    if (af_bt_sharpe_ratio(&te) < STAGE_SHARPE_GATE) passed = 0;

    if (fold_idx < AF_WF_MAX_FOLDS)
        wc->fold_passed[fold_idx] = passed;
    ++wc->n_folds_run;

    train_out->n_trades = tr.trade_count;
    train_out->total_pnl = af_bt_net_profit(&tr);

    test_out->n_trades = te.trade_count;
    test_out->total_pnl = af_bt_net_profit(&te);
    test_out->sharpe    = af_bt_sharpe_ratio(&te);

    return AF_OK;
}

/* ── Stage 3: Walk-forward ───────────────────────────────────────────────── */

static int stage3_walk_forward(const af_algo_manifest_t *m,
                                const af_bar_t *bars, size_t n_bars,
                                af_algo_stage_result_t *stage,
                                af_algo_error_t *err)
{
    stage->stage = 3;
    strncpy(stage->name, "walk_forward", sizeof(stage->name) - 1);
    stage->passed = 0;

    wf_ctx_t wctx;
    memset(&wctx, 0, sizeof(wctx));
    wctx.manifest = m;
    wctx.bars     = bars;
    wctx.n_bars   = n_bars;

    af_wf_result_t wf_result;
    af_error_t rc = af_wf_anchored((int)n_bars, WF_N_FOLDS, WF_TRAIN_MIN_BARS,
                                    wf_backtest_fn, &wctx, &wf_result);
    if (rc != AF_OK) {
        strncpy(stage->reason, "walk-forward engine error", sizeof(stage->reason) - 1);
        if (err) strncpy(err->kind, "stage_failed", 63);
        return -1;
    }

    int n_run    = wctx.n_folds_run;
    int n_passed = 0;
    for (int i = 0; i < n_run; ++i)
        if (wctx.fold_passed[i]) ++n_passed;

    double score = (n_run > 0) ? ((double)n_passed / (double)n_run * 100.0) : 0.0;
    stage->score = score;

    /* Pass only if ALL windows pass */
    if (n_passed < n_run) {
        snprintf(stage->reason, sizeof(stage->reason),
                 "%d/%d windows passed (stage failed)", n_passed, n_run);
        if (err) { strncpy(err->kind, "stage_failed", 63); strncpy(err->message, stage->reason, 511); }
        return -1;
    }

    snprintf(stage->reason, sizeof(stage->reason), "%d/%d windows passed", n_passed, n_run);
    stage->passed = 1;
    return 0;
}

/* ── Stage 4: MC bootstrap ───────────────────────────────────────────────── */

static double mc_score_from_result(const af_mc_result_t *mc, double initial_capital) {
    double p5      = mc->p5_final;
    double ruin    = mc->prob_of_ruin;
    double score;
    if (p5 < 0.0) {
        score = 0.0;
    } else if (ruin > 0.5) {
        score = (1.0 - ruin) * 100.0;
    } else {
        double ret_p5 = (p5 - initial_capital) / initial_capital;
        if (ret_p5 >= 0.0) {
            score = fmin(100.0, 70.0 + ret_p5 * 60.0);
        } else {
            score = fmax(0.0, 70.0 + ret_p5 * 140.0);
        }
        score *= (1.0 - ruin);
    }
    return score;
}

static int stage4_mc_bootstrap(const af_bt_result_t *bt,
                                 uint64_t seed,
                                 af_algo_stage_result_t *stage,
                                 af_algo_error_t *err)
{
    (void)err;
    stage->stage = 4;
    strncpy(stage->name, "mc_bootstrap", sizeof(stage->name) - 1);
    stage->passed = 0;

    int n_trades = bt->trade_count;
    if (n_trades < 1) {
        strncpy(stage->reason, "no trades for MC bootstrap", sizeof(stage->reason) - 1);
        return -1;
    }

    /* Build PnL array */
    double *pnl = (double *)malloc((size_t)n_trades * sizeof(double));
    if (!pnl) return -1;
    for (int i = 0; i < n_trades; ++i)
        pnl[i] = af_bt_trade_net_pnl(&bt->trades[i]);

    double initial_capital = bt->initial_capital;

    /* Scratch space for MC paths */
    int scratch_cap = MC_N_RUNS * (n_trades + 1);
    double *scratch = (double *)malloc((size_t)scratch_cap * sizeof(double));
    double *p5_path  = (double *)malloc((size_t)(n_trades + 1) * sizeof(double));
    double *p50_path = (double *)malloc((size_t)(n_trades + 1) * sizeof(double));
    double *p95_path = (double *)malloc((size_t)(n_trades + 1) * sizeof(double));

    if (!scratch || !p5_path || !p50_path || !p95_path) {
        free(pnl); free(scratch); free(p5_path); free(p50_path); free(p95_path);
        return -1;
    }

    af_mc_result_t mc;
    mc.p5_path  = p5_path;
    mc.p50_path = p50_path;
    mc.p95_path = p95_path;

    af_error_t rc = af_mc_run(pnl, n_trades, MC_N_RUNS, seed,
                               initial_capital, scratch, scratch_cap, &mc);
    free(pnl); free(scratch);

    if (rc != AF_OK) {
        free(p5_path); free(p50_path); free(p95_path);
        strncpy(stage->reason, "MC bootstrap engine error", sizeof(stage->reason) - 1);
        return -1;
    }

    double score = mc_score_from_result(&mc, initial_capital);
    stage->score  = score;
    free(p5_path); free(p50_path); free(p95_path);

    snprintf(stage->reason, sizeof(stage->reason), "MC score=%.1f%%", score);
    stage->passed = 1; /* always passes — score is informational */
    return 0;
}

/* ── Stage 5: Parameter robustness ──────────────────────────────────────── */

/* Collects the "top numeric params" for perturbation */
typedef struct {
    const char *param_key;
    int         indicator_idx;   /* -1 = risk param */
    double      val;
    double      delta;
} rob_param_t;

static int collect_rob_params(const af_algo_manifest_t *m,
                               rob_param_t params[], int max_params)
{
    int n = 0;
    /* Indicator params */
    for (int i = 0; i < m->n_indicators && n < max_params; ++i) {
        for (int p = 0; p < m->indicators[i].n_params && n < max_params; ++p) {
            const af_algo_param_t *ap = &m->indicators[i].params[p];
            if (ap->value == 0.0) continue;
            double delta = fabs(ap->value) * ROB_DELTA_FRAC;
            if (delta < 0.1) delta = 0.1;
            params[n].param_key      = ap->key;
            params[n].indicator_idx  = i;
            params[n].val            = ap->value;
            params[n].delta          = delta;
            ++n;
        }
    }
    /* Risk params */
    if (strcmp(m->risk.size, "atr") == 0 && m->risk.atr_mult > 0.0 && n < max_params) {
        double delta = fabs(m->risk.atr_mult) * ROB_DELTA_FRAC;
        if (delta < 0.1) delta = 0.1;
        params[n].param_key     = "atr_mult";
        params[n].indicator_idx = -1;
        params[n].val           = m->risk.atr_mult;
        params[n].delta         = delta;
        ++n;
    }
    if (m->risk.fixed_lots > 0.0 && strcmp(m->risk.size, "fixed") == 0 && n < max_params) {
        double delta = fabs(m->risk.fixed_lots) * ROB_DELTA_FRAC;
        if (delta < 0.1) delta = 0.1;
        params[n].param_key     = "fixed_lots";
        params[n].indicator_idx = -1;
        params[n].val           = m->risk.fixed_lots;
        params[n].delta         = delta;
        ++n;
    }
    if (m->risk.cool_down_bars > 0 && n < max_params) {
        params[n].param_key     = "cool_down_bars";
        params[n].indicator_idx = -2; /* special: risk.cool_down */
        params[n].val           = (double)m->risk.cool_down_bars;
        params[n].delta         = fabs(params[n].val) * ROB_DELTA_FRAC > 0.1
                                    ? fabs(params[n].val) * ROB_DELTA_FRAC : 0.1;
        ++n;
    }
    return n;
}

/* Apply perturbed value to a manifest copy */
static void apply_param(af_algo_manifest_t *m_copy,
                         const rob_param_t *rp, double new_val)
{
    if (rp->indicator_idx >= 0) {
        for (int p = 0; p < m_copy->indicators[rp->indicator_idx].n_params; ++p) {
            if (strcmp(m_copy->indicators[rp->indicator_idx].params[p].key,
                       rp->param_key) == 0) {
                double v = new_val;
                if (m_copy->indicators[rp->indicator_idx].params[p].is_int) {
                    v = (double)( (int)round(new_val) > 0 ? (int)round(new_val) : 1 );
                }
                m_copy->indicators[rp->indicator_idx].params[p].value = v;
                return;
            }
        }
    } else if (rp->indicator_idx == -1) {
        if (strcmp(rp->param_key, "atr_mult") == 0)    m_copy->risk.atr_mult   = new_val;
        if (strcmp(rp->param_key, "fixed_lots") == 0)  m_copy->risk.fixed_lots = new_val;
    } else if (rp->indicator_idx == -2) {
        int v = (int)round(new_val); if (v < 0) v = 0;
        m_copy->risk.cool_down_bars = v;
    }
}

static double run_quick_backtest(const af_algo_manifest_t *m,
                                  const af_bar_t *bars, size_t n_bars,
                                  int warmup)
{
    /* Quick bars: max(warmup+ROB_QUICK_EXTRA_BARS, min(ROB_QUICK_MAX_EXTRA+warmup, n_bars)) */
    size_t quick_n = (size_t)(warmup + ROB_QUICK_EXTRA_BARS);
    size_t max_n   = (size_t)(warmup + ROB_QUICK_MAX_EXTRA);
    if (quick_n > max_n)    quick_n = max_n;
    if (quick_n > n_bars)   quick_n = n_bars;

    af_algo_manifest_algo_t algo;
    af_algo_error_t err; af_algo_error_clear(&err);
    if (af_algo_manifest_algo_init(&algo, m, &err) != 0) return NAN;

    af_bt_config_t cfg;
    af_bt_config_default(&cfg);
    if (m->risk.atr_mult > 0.0) cfg.atr_sl_mult = m->risk.atr_mult;

    const char *sym = (m->n_symbols > 0 && !m->symbols_any) ? m->symbols[0] : "EURUSD";
    af_bt_result_t res = af_backtest_run(&algo.base, &cfg, bars, quick_n, sym, AF_TF_H1);
    af_algo_manifest_algo_destroy(&algo);

    if (res.trade_count < 1) return NAN;
    return af_bt_net_profit(&res);
}

static int stage5_parameter_robustness(const af_algo_manifest_t *m,
                                        const af_bar_t *bars, size_t n_bars,
                                        af_algo_stage_result_t *stage,
                                        af_algo_error_t *err)
{
    stage->stage = 5;
    strncpy(stage->name, "parameter_robustness", sizeof(stage->name) - 1);
    stage->passed = 0;

    /* Collect all numeric params */
#define MAX_ROB_PARAMS 32
    rob_param_t all_params[MAX_ROB_PARAMS];
    int n_all = collect_rob_params(m, all_params, MAX_ROB_PARAMS);

    if (n_all == 0) {
        /* No params to perturb — trivially robust */
        stage->score  = 100.0;
        stage->passed = 1;
        strncpy(stage->reason, "Robustness score=100.0%", sizeof(stage->reason) - 1);
        return 0;
    }

    /* Compute base score */
    double base_score = run_quick_backtest(m, bars, n_bars, 200);

    /* Compute gradient for each param */
    double gradients[MAX_ROB_PARAMS] = {0};
    for (int i = 0; i < n_all; ++i) {
        af_algo_manifest_t m_plus  = *m;
        af_algo_manifest_t m_minus = *m;
        apply_param(&m_plus,  &all_params[i], all_params[i].val + all_params[i].delta);
        apply_param(&m_minus, &all_params[i], all_params[i].val - all_params[i].delta);

        double s_plus  = run_quick_backtest(&m_plus,  bars, n_bars, 200);
        double s_minus = run_quick_backtest(&m_minus, bars, n_bars, 200);

        if (!isnan(s_plus) && !isnan(s_minus) && all_params[i].delta > 1e-10) {
            gradients[i] = fabs(s_plus - s_minus) / (2.0 * all_params[i].delta);
        } else {
            gradients[i] = 0.0;
        }
    }

    /* Select top-3 by gradient */
    int top_idx[ROB_TOP_PARAMS];
    int n_top = n_all < ROB_TOP_PARAMS ? n_all : ROB_TOP_PARAMS;
    /* Simple selection sort */
    int used[MAX_ROB_PARAMS] = {0};
    for (int t = 0; t < n_top; ++t) {
        int best = -1; double best_g = -1.0;
        for (int i = 0; i < n_all; ++i) {
            if (!used[i] && gradients[i] > best_g) { best_g = gradients[i]; best = i; }
        }
        if (best == -1) { n_top = t; break; }
        top_idx[t] = best;
        used[best] = 1;
    }

    if (n_top == 0) {
        stage->score  = 100.0;
        stage->passed = 1;
        strncpy(stage->reason, "Robustness score=100.0%", sizeof(stage->reason) - 1);
        return 0;
    }

    /* Cartesian product: for each top param, test [val-delta, val, val+delta] */
    double levels[ROB_TOP_PARAMS][ROB_N_LEVELS];
    for (int t = 0; t < n_top; ++t) {
        int idx = top_idx[t];
        double v = all_params[idx].val, d = all_params[idx].delta;
        levels[t][0] = v - d;
        levels[t][1] = v;
        levels[t][2] = v + d;
    }

    /* Enumerate all combinations */
    int n_cells = 1;
    for (int t = 0; t < n_top; ++t) n_cells *= ROB_N_LEVELS;

    int n_pass = 0, n_total = 0;
    int combo[ROB_TOP_PARAMS] = {0};

    for (int cell = 0; cell < n_cells; ++cell) {
        af_algo_manifest_t m_copy = *m;
        for (int t = 0; t < n_top; ++t)
            apply_param(&m_copy, &all_params[top_idx[t]], levels[t][combo[t]]);

        double s = run_quick_backtest(&m_copy, bars, n_bars, 200);
        ++n_total;
        if (!isnan(s) && s > 0.0) ++n_pass;

        /* Increment combo counter (little-endian) */
        for (int t = 0; t < n_top; ++t) {
            combo[t]++;
            if (combo[t] < ROB_N_LEVELS) break;
            combo[t] = 0;
        }
    }

    double score = (n_total > 0) ? ((double)n_pass / (double)n_total * 100.0) : 100.0;
    stage->score  = score;
    stage->passed = 1; /* informational */
    snprintf(stage->reason, sizeof(stage->reason), "Robustness score=%.1f%%", score);
    (void)base_score; (void)err;
    return 0;
}

/* ── Public: af_algo_validate ──────────────────────────────────────────── */

int af_algo_validate(const af_algo_manifest_t *m,
                     const af_bar_t *bars, size_t n_bars,
                     uint64_t seed,
                     af_algo_tier_report_t *out,
                     af_algo_error_t *err)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->manifest_name, m->name, AF_ALGO_GEN_MAX_NAME - 1);
    out->tier = AF_TIER_RED;

    /* Escape hatch */
    if (m->has_code) {
        out->is_escape_hatch = 1;
        out->passed = 0;
        out->n_stages = 0;
        return 0;
    }

    af_algo_error_t local_err;
    if (!err) err = &local_err;
    af_algo_error_clear(err);

    /* Stage 1: schema lint */
    af_algo_stage_result_t *s1 = &out->stages[0];
    if (stage1_schema_lint(m, s1, err)) {
        out->n_stages = 1;
        out->passed = 0;
        return -1;
    }
    out->n_stages = 1;

    /* Stage 2: sandbox backtest */
    af_algo_manifest_algo_t algo;
    if (af_algo_manifest_algo_init(&algo, m, err) != 0) {
        out->passed = 0;
        return -1;
    }

    af_bt_result_t bt;
    af_algo_stage_result_t *s2 = &out->stages[1];
    int rc2 = stage2_sandbox_backtest(m, bars, n_bars, &algo, &bt, s2, err);
    af_algo_manifest_algo_destroy(&algo);
    out->n_stages = 2;
    if (rc2 != 0) {
        out->passed = 0;
        return -1;
    }

    /* Stage 3: walk-forward */
    af_algo_stage_result_t *s3 = &out->stages[2];
    if (stage3_walk_forward(m, bars, n_bars, s3, err)) {
        out->n_stages = 3;
        out->passed = 0;
        return -1;
    }
    out->n_stages = 3;

    /* Stage 4: MC bootstrap */
    af_algo_stage_result_t *s4 = &out->stages[3];
    stage4_mc_bootstrap(&bt, seed, s4, err);
    out->n_stages = 4;
    /* MC never gates */

    /* Stage 5: parameter robustness */
    af_algo_stage_result_t *s5 = &out->stages[4];
    stage5_parameter_robustness(m, bars, n_bars, s5, err);
    out->n_stages = 5;

    /* Compute tier */
    double wf_score  = s3->score;
    double mc_score  = s4->score;
    double rob_score = s5->score;

    af_algo_tier_cfg_t cfg;
    af_algo_tier_cfg_default(&cfg);
    out->tier       = af_algo_score_to_tier(wf_score, mc_score, rob_score, &cfg);
    out->walk_forward   = wf_score;
    out->mc_bootstrap   = mc_score;
    out->robustness     = rob_score;

    double min_score = wf_score;
    if (mc_score  < min_score) min_score = mc_score;
    if (rob_score < min_score) min_score = rob_score;
    out->min_score = min_score;

    /* Tier flags and size multiplier */
    out->experimental = (out->tier <= AF_TIER_YELLOW);
    out->paper_only   = (out->tier <= AF_TIER_ORANGE);
    switch (out->tier) {
        case AF_TIER_RED:    out->size_mult = 0.0;                       break;
        case AF_TIER_ORANGE: out->size_mult = cfg.orange_size_mult;      break;
        case AF_TIER_YELLOW: out->size_mult = cfg.yellow_size_mult;      break;
        case AF_TIER_GREEN:  out->size_mult = cfg.green_size_mult;       break;
        case AF_TIER_WHITE:  out->size_mult = cfg.white_size_mult;       break;
    }

    out->passed = (out->tier >= AF_TIER_ORANGE); /* at least some tier required */
    return (out->tier >= AF_TIER_ORANGE) ? 0 : -1;
}

/* ============================================================
 * §6  Tier scoring
 * ============================================================ */

void af_algo_tier_cfg_default(af_algo_tier_cfg_t *cfg) {
    cfg->orange_size_mult = 0.25;
    cfg->yellow_size_mult = 0.50;
    cfg->green_size_mult  = 1.00;
    cfg->white_size_mult  = 1.50;
}

af_algo_tier_t af_algo_score_to_tier(double wf, double mc, double robustness,
                                      const af_algo_tier_cfg_t *cfg)
{
    (void)cfg; /* thresholds are fixed by spec */
    double min_score = wf;
    if (mc        < min_score) min_score = mc;
    if (robustness < min_score) min_score = robustness;

    if (min_score >= 95.0) return AF_TIER_WHITE;
    if (min_score >= 90.0) return AF_TIER_GREEN;
    if (min_score >= 80.0) return AF_TIER_YELLOW;
    if (min_score >= 70.0) return AF_TIER_ORANGE;
    return AF_TIER_RED;
}

const char *af_algo_tier_str(af_algo_tier_t tier) {
    switch (tier) {
        case AF_TIER_RED:    return "red";
        case AF_TIER_ORANGE: return "orange";
        case AF_TIER_YELLOW: return "yellow";
        case AF_TIER_GREEN:  return "green";
        case AF_TIER_WHITE:  return "white";
        default: return "unknown";
    }
}

const char *af_algo_tier_color(af_algo_tier_t tier) {
    switch (tier) {
        case AF_TIER_RED:    return "#ff4444";
        case AF_TIER_ORANGE: return "#ff8800";
        case AF_TIER_YELLOW: return "#ffcc00";
        case AF_TIER_GREEN:  return "#44cc44";
        case AF_TIER_WHITE:  return "#ffffff";
        default: return "#888888";
    }
}

/* ============================================================
 * §7  JSON serialization / promote
 * ============================================================ */

int af_algo_tier_report_to_json(const af_algo_tier_report_t *r,
                                 char *buf, size_t bufsz)
{
    if (!r || !buf || bufsz < 4) return -1;

    /* Helper macro: append formatted string, fail if truncated */
    size_t used = 0;
#define APPEND(...) do {                                          \
    int _n = snprintf(buf + used, bufsz - used, __VA_ARGS__);   \
    if (_n < 0 || (size_t)_n >= bufsz - used) return -1;        \
    used += (size_t)_n;                                          \
} while (0)

    APPEND("{\n");
    APPEND("  \"manifest_name\": \"%s\",\n", r->manifest_name);
    APPEND("  \"passed\": %s,\n", r->passed ? "true" : "false");

    if (r->passed && !r->is_escape_hatch) {
        APPEND("  \"tier\": \"%s\",\n", af_algo_tier_str(r->tier));
        APPEND("  \"min_score\": %.1f,\n", r->min_score);
        APPEND("  \"walk_forward\": %.1f,\n", r->walk_forward);
        APPEND("  \"mc_bootstrap\": %.1f,\n", r->mc_bootstrap);
        APPEND("  \"robustness\": %.1f,\n", r->robustness);
        APPEND("  \"size_mult\": %.2f,\n", r->size_mult);
        APPEND("  \"experimental\": %s,\n", r->experimental ? "true" : "false");
        APPEND("  \"paper_only\": %s,\n", r->paper_only ? "true" : "false");
    } else {
        APPEND("  \"tier\": null,\n");
    }

    APPEND("  \"stages\": [\n");
    for (int i = 0; i < r->n_stages; ++i) {
        const af_algo_stage_result_t *s = &r->stages[i];
        APPEND("    {\n");
        APPEND("      \"stage\": %d,\n", s->stage);
        APPEND("      \"name\": \"%s\",\n", s->name);
        APPEND("      \"passed\": %s,\n", s->passed ? "true" : "false");
        APPEND("      \"reason\": \"%s\",\n", s->reason);
        APPEND("      \"metrics\": {");
        int wrote_metric = 0;
        if (s->stage == 2) {
            APPEND("\"trades\": %d", s->trades);
            wrote_metric = 1;
        }
        if ((s->stage == 3 || s->stage == 4 || s->stage == 5) && s->score != 0.0) {
            if (wrote_metric) APPEND(", ");
            APPEND("\"score\": %.1f", s->score);
        }
        APPEND("}\n");
        APPEND("    }%s\n", (i + 1 < r->n_stages) ? "," : "");
    }
    APPEND("  ]\n");
    APPEND("}\n");

#undef APPEND
    return (int)used;
}

int af_algo_tier_report_from_json(const char *json, size_t len,
                                   af_algo_tier_report_t *out,
                                   af_algo_error_t *err)
{
    memset(out, 0, sizeof(*out));
    json_ctx_t j;
    json_init(&j, json, len);

    if (!json_consume(&j, '{')) goto fail;
    if (json_peek(&j) == '}') { ++j.pos; return 0; }

    do {
        char key[64];
        if (json_parse_string(&j, key, sizeof(key))) goto fail;
        if (!json_consume(&j, ':')) goto fail;

        if (strcmp(key, "manifest_name") == 0) {
            json_parse_string(&j, out->manifest_name, sizeof(out->manifest_name));
        } else if (strcmp(key, "passed") == 0) {
            int v; json_parse_literal(&j, &v); out->passed = v;
        } else if (strcmp(key, "tier") == 0) {
            int c = json_peek(&j);
            if (c == 'n') { int v; json_parse_literal(&j, &v); out->tier = AF_TIER_RED; }
            else {
                char ts[16]; json_parse_string(&j, ts, sizeof(ts));
                if      (strcmp(ts, "white")  == 0) out->tier = AF_TIER_WHITE;
                else if (strcmp(ts, "green")  == 0) out->tier = AF_TIER_GREEN;
                else if (strcmp(ts, "yellow") == 0) out->tier = AF_TIER_YELLOW;
                else if (strcmp(ts, "orange") == 0) out->tier = AF_TIER_ORANGE;
                else out->tier = AF_TIER_RED;
            }
        } else if (strcmp(key, "min_score") == 0) {
            json_parse_number(&j, &out->min_score);
        } else if (strcmp(key, "walk_forward") == 0) {
            json_parse_number(&j, &out->walk_forward);
        } else if (strcmp(key, "mc_bootstrap") == 0) {
            json_parse_number(&j, &out->mc_bootstrap);
        } else if (strcmp(key, "robustness") == 0) {
            json_parse_number(&j, &out->robustness);
        } else if (strcmp(key, "size_mult") == 0) {
            json_parse_number(&j, &out->size_mult);
        } else if (strcmp(key, "experimental") == 0) {
            int v; json_parse_literal(&j, &v); out->experimental = v;
        } else if (strcmp(key, "paper_only") == 0) {
            int v; json_parse_literal(&j, &v); out->paper_only = v;
        } else if (strcmp(key, "stages") == 0) {
            if (!json_consume(&j, '[')) goto fail;
            while (json_peek(&j) != ']' && out->n_stages < AF_ALGO_GEN_MAX_STAGES) {
                af_algo_stage_result_t *s = &out->stages[out->n_stages];
                if (!json_consume(&j, '{')) goto fail;
                do {
                    char sk[64];
                    if (json_parse_string(&j, sk, sizeof(sk))) goto fail;
                    if (!json_consume(&j, ':')) goto fail;
                    if (strcmp(sk, "stage") == 0) {
                        double v; json_parse_number(&j, &v); s->stage = (int)v;
                    } else if (strcmp(sk, "name") == 0) {
                        json_parse_string(&j, s->name, sizeof(s->name));
                    } else if (strcmp(sk, "passed") == 0) {
                        int v; json_parse_literal(&j, &v); s->passed = v;
                    } else if (strcmp(sk, "reason") == 0) {
                        json_parse_string(&j, s->reason, sizeof(s->reason));
                    } else if (strcmp(sk, "metrics") == 0) {
                        if (!json_consume(&j, '{')) goto fail;
                        if (json_peek(&j) != '}') {
                            do {
                                char mk[32]; json_parse_string(&j, mk, sizeof(mk));
                                if (!json_consume(&j, ':')) goto fail;
                                double mv; json_parse_number(&j, &mv);
                                if (strcmp(mk, "trades") == 0) s->trades = (int)mv;
                                else if (strcmp(mk, "score") == 0) s->score = mv;
                            } while (json_consume(&j, ','));
                        }
                        json_consume(&j, '}');
                    } else { json_skip_value(&j); }
                } while (json_consume(&j, ','));
                json_consume(&j, '}');
                ++out->n_stages;
                json_consume(&j, ',');
            }
            json_consume(&j, ']');
        } else { json_skip_value(&j); }
    } while (json_consume(&j, ','));
    json_consume(&j, '}');
    return 0;

fail:
    if (err) { strncpy(err->kind, "parse_error", 63); strncpy(err->message, "tier report JSON parse failed", 511); }
    return -1;
}

int af_algo_promote(const af_algo_tier_report_t *report,
                    const af_algo_manifest_t *m,
                    const char *output_dir,
                    af_algo_error_t *err)
{
    (void)m; /* reserved for future manifest embedding */
    if (!report || !output_dir) {
        if (err) { strncpy(err->kind, "invalid_param", 63); strncpy(err->message, "NULL argument", 511); }
        return -1;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.json", output_dir, report->manifest_name);

    char buf[8192];
    int n = af_algo_tier_report_to_json(report, buf, sizeof(buf));
    if (n < 0) {
        if (err) { strncpy(err->kind, "serialize_error", 63); strncpy(err->message, "buffer too small", 511); }
        return -1;
    }

    FILE *fp = fopen(path, "w");
    if (!fp) {
        if (err) {
            strncpy(err->kind, "io_error", 63);
            snprintf(err->message, 511, "cannot open '%s' for writing", path);
        }
        return -1;
    }
    fwrite(buf, 1, (size_t)n, fp);
    fclose(fp);
    return 0;
}

/* ── af_algo_metadata_t helpers ─────────────────────────────────────────── */

af_algo_metadata_t *af_algo_metadata_create(const af_algo_tier_report_t *report) {
    if (!report) return NULL;
    af_algo_metadata_t *m = (af_algo_metadata_t *)calloc(1, sizeof(af_algo_metadata_t));
    if (!m) return NULL;
    strncpy(m->manifest_name, report->manifest_name, AF_ALGO_GEN_MAX_NAME - 1);
    strncpy(m->tier, af_algo_tier_str(report->tier), sizeof(m->tier) - 1);
    m->size_mult   = report->size_mult;
    m->experimental = report->experimental;
    m->paper_only  = report->paper_only;
    return m;
}

void af_algo_metadata_destroy(af_algo_metadata_t *m) {
    free(m);
}
