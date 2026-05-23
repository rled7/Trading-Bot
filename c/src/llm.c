/**
 * AlgoForge — c/src/llm.c
 *
 * Monolithic implementation of the S2 LLM layer:
 *   - Hand-rolled JSON parser/emitter (no external dep)
 *   - libcurl-backed HTTP transport (or stub when AF_NO_LIBCURL)
 *   - OllamaProvider: complete, chat, chat_stream, embed, health, list_models
 *   - Four use cases: trade_rationale, backtest_commentary,
 *                     news_sentiment, strategy_describe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>

#include "af_llm.h"

/* ============================================================
 * Internal helpers — string utilities
 * ============================================================ */

static void set_error(af_llm_error_t *err, const char *kind,
                      int status, const char *fmt, ...)
{
    if (!err) return;
    strncpy(err->kind, kind, sizeof(err->kind) - 1);
    err->kind[sizeof(err->kind) - 1] = '\0';
    err->status = status;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt, ap);
    va_end(ap);
}

/* Safe string copy into fixed buffer */
static void safe_strncpy(char *dst, const char *src, size_t n)
{
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

/* ============================================================
 * Hand-rolled JSON emitter
 * ============================================================ */

/* Write buffer — growable */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} jbuf_t;

static int jbuf_init(jbuf_t *j)
{
    j->cap = 4096;
    j->len = 0;
    j->buf = (char *)malloc(j->cap);
    if (!j->buf) return -1;
    j->buf[0] = '\0';
    return 0;
}

static void jbuf_free(jbuf_t *j)
{
    free(j->buf);
    j->buf = NULL;
    j->len = j->cap = 0;
}

static int jbuf_ensure(jbuf_t *j, size_t extra)
{
    if (j->len + extra + 1 < j->cap) return 0;
    size_t newcap = j->cap;
    while (newcap < j->len + extra + 1) newcap *= 2;
    char *nb = (char *)realloc(j->buf, newcap);
    if (!nb) return -1;
    j->buf = nb;
    j->cap = newcap;
    return 0;
}

static int jbuf_append(jbuf_t *j, const char *s)
{
    size_t slen = strlen(s);
    if (jbuf_ensure(j, slen) != 0) return -1;
    memcpy(j->buf + j->len, s, slen + 1);
    j->len += slen;
    return 0;
}

static int jbuf_appendc(jbuf_t *j, char c)
{
    if (jbuf_ensure(j, 1) != 0) return -1;
    j->buf[j->len++] = c;
    j->buf[j->len]   = '\0';
    return 0;
}

/* Append a JSON-escaped string (including surrounding quotes) */
static int jbuf_append_str(jbuf_t *j, const char *s)
{
    if (jbuf_appendc(j, '"') != 0) return -1;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"') {
            if (jbuf_append(j, "\\\"") != 0) return -1;
        } else if (c == '\\') {
            if (jbuf_append(j, "\\\\") != 0) return -1;
        } else if (c == '\n') {
            if (jbuf_append(j, "\\n") != 0) return -1;
        } else if (c == '\r') {
            if (jbuf_append(j, "\\r") != 0) return -1;
        } else if (c == '\t') {
            if (jbuf_append(j, "\\t") != 0) return -1;
        } else if (c < 0x20) {
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            if (jbuf_append(j, esc) != 0) return -1;
        } else {
            if (jbuf_appendc(j, (char)c) != 0) return -1;
        }
    }
    return jbuf_appendc(j, '"');
}

static int jbuf_append_int(jbuf_t *j, int v)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", v);
    return jbuf_append(j, tmp);
}

static int jbuf_append_double(jbuf_t *j, double v)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%g", v);
    return jbuf_append(j, tmp);
}

static int jbuf_append_bool(jbuf_t *j, int v)
{
    return jbuf_append(j, v ? "true" : "false");
}

/* ============================================================
 * Hand-rolled JSON parser
 * ============================================================ */

typedef enum {
    JT_NULL,
    JT_BOOL,
    JT_NUMBER,
    JT_STRING,
    JT_ARRAY,
    JT_OBJECT,
    JT_EOF,
    JT_ERROR
} jtype_t;

typedef struct {
    const char *src;
    size_t      pos;
    size_t      len;
} jparser_t;

static void jp_init(jparser_t *jp, const char *src)
{
    jp->src = src;
    jp->pos = 0;
    jp->len = src ? strlen(src) : 0;
}

static void jp_skip_ws(jparser_t *jp)
{
    while (jp->pos < jp->len &&
           (jp->src[jp->pos] == ' ' || jp->src[jp->pos] == '\t' ||
            jp->src[jp->pos] == '\n' || jp->src[jp->pos] == '\r'))
        jp->pos++;
}

__attribute__((unused))
static char jp_peek(jparser_t *jp)
{
    jp_skip_ws(jp);
    if (jp->pos >= jp->len) return '\0';
    return jp->src[jp->pos];
}

__attribute__((unused))
static char jp_cur(jparser_t *jp)
{
    if (jp->pos >= jp->len) return '\0';
    return jp->src[jp->pos];
}

/* Parse a JSON string — writes unescaped content into buf (up to bufsz-1) */
static int jp_parse_string(jparser_t *jp, char *buf, size_t bufsz)
{
    jp_skip_ws(jp);
    if (jp->pos >= jp->len || jp->src[jp->pos] != '"') return -1;
    jp->pos++; /* skip opening quote */
    size_t out = 0;
    while (jp->pos < jp->len) {
        char c = jp->src[jp->pos++];
        if (c == '"') {
            if (buf && out < bufsz) buf[out] = '\0';
            return 0;
        }
        if (c == '\\') {
            if (jp->pos >= jp->len) return -1;
            char esc = jp->src[jp->pos++];
            char decoded;
            switch (esc) {
                case '"':  decoded = '"';  break;
                case '\\': decoded = '\\'; break;
                case '/':  decoded = '/';  break;
                case 'n':  decoded = '\n'; break;
                case 'r':  decoded = '\r'; break;
                case 't':  decoded = '\t'; break;
                case 'b':  decoded = '\b'; break;
                case 'f':  decoded = '\f'; break;
                case 'u':
                    /* skip 4 hex digits */
                    if (jp->pos + 4 > jp->len) return -1;
                    jp->pos += 4;
                    decoded = '?'; /* simplified: not full Unicode */
                    break;
                default:   decoded = esc; break;
            }
            if (buf && out + 1 < bufsz) buf[out++] = decoded;
        } else {
            if (buf && out + 1 < bufsz) buf[out++] = c;
        }
    }
    return -1; /* unterminated string */
}

/* Skip over any JSON value */
static int jp_skip_value(jparser_t *jp);

static int jp_skip_string(jparser_t *jp)
{
    jp_skip_ws(jp);
    if (jp->pos >= jp->len || jp->src[jp->pos] != '"') return -1;
    jp->pos++;
    while (jp->pos < jp->len) {
        char c = jp->src[jp->pos++];
        if (c == '\\') { jp->pos++; continue; }
        if (c == '"') return 0;
    }
    return -1;
}

static int jp_skip_value(jparser_t *jp)
{
    jp_skip_ws(jp);
    if (jp->pos >= jp->len) return -1;
    char c = jp->src[jp->pos];
    if (c == '"') return jp_skip_string(jp);
    if (c == '{') {
        jp->pos++;
        jp_skip_ws(jp);
        if (jp->pos < jp->len && jp->src[jp->pos] == '}') { jp->pos++; return 0; }
        for (;;) {
            if (jp_skip_string(jp) != 0) return -1; /* key */
            jp_skip_ws(jp);
            if (jp->pos >= jp->len || jp->src[jp->pos] != ':') return -1;
            jp->pos++;
            if (jp_skip_value(jp) != 0) return -1;
            jp_skip_ws(jp);
            if (jp->pos >= jp->len) return -1;
            if (jp->src[jp->pos] == '}') { jp->pos++; return 0; }
            if (jp->src[jp->pos] == ',') { jp->pos++; continue; }
            return -1;
        }
    }
    if (c == '[') {
        jp->pos++;
        jp_skip_ws(jp);
        if (jp->pos < jp->len && jp->src[jp->pos] == ']') { jp->pos++; return 0; }
        for (;;) {
            if (jp_skip_value(jp) != 0) return -1;
            jp_skip_ws(jp);
            if (jp->pos >= jp->len) return -1;
            if (jp->src[jp->pos] == ']') { jp->pos++; return 0; }
            if (jp->src[jp->pos] == ',') { jp->pos++; continue; }
            return -1;
        }
    }
    /* null / true / false */
    if (strncmp(jp->src + jp->pos, "null",  4) == 0) { jp->pos += 4; return 0; }
    if (strncmp(jp->src + jp->pos, "true",  4) == 0) { jp->pos += 4; return 0; }
    if (strncmp(jp->src + jp->pos, "false", 5) == 0) { jp->pos += 5; return 0; }
    /* number */
    if (c == '-' || isdigit((unsigned char)c)) {
        if (c == '-') jp->pos++;
        while (jp->pos < jp->len && isdigit((unsigned char)jp->src[jp->pos])) jp->pos++;
        if (jp->pos < jp->len && jp->src[jp->pos] == '.') {
            jp->pos++;
            while (jp->pos < jp->len && isdigit((unsigned char)jp->src[jp->pos])) jp->pos++;
        }
        if (jp->pos < jp->len &&
            (jp->src[jp->pos] == 'e' || jp->src[jp->pos] == 'E')) {
            jp->pos++;
            if (jp->pos < jp->len &&
                (jp->src[jp->pos] == '+' || jp->src[jp->pos] == '-')) jp->pos++;
            while (jp->pos < jp->len && isdigit((unsigned char)jp->src[jp->pos])) jp->pos++;
        }
        return 0;
    }
    return -1;
}

/* Parse a JSON number into a double */
static int jp_parse_number(jparser_t *jp, double *out)
{
    jp_skip_ws(jp);
    char *end = NULL;
    double v = strtod(jp->src + jp->pos, &end);
    if (end == jp->src + jp->pos) return -1;
    jp->pos = (size_t)(end - jp->src);
    if (out) *out = v;
    return 0;
}

/* Parse a JSON integer */
static int jp_parse_int(jparser_t *jp, int *out)
{
    double d;
    if (jp_parse_number(jp, &d) != 0) return -1;
    if (out) *out = (int)d;
    return 0;
}

/* Parse a JSON int64 */
static int jp_parse_int64(jparser_t *jp, int64_t *out)
{
    jp_skip_ws(jp);
    char *end = NULL;
    long long v = strtoll(jp->src + jp->pos, &end, 10);
    if (end == jp->src + jp->pos) {
        /* might be a float in scientific notation */
        double d;
        if (jp_parse_number(jp, &d) != 0) return -1;
        if (out) *out = (int64_t)d;
        return 0;
    }
    jp->pos = (size_t)(end - jp->src);
    if (out) *out = (int64_t)v;
    return 0;
}

/* Parse a JSON boolean */
static int jp_parse_bool(jparser_t *jp, int *out)
{
    jp_skip_ws(jp);
    if (strncmp(jp->src + jp->pos, "true", 4) == 0) {
        jp->pos += 4; if (out) *out = 1; return 0;
    }
    if (strncmp(jp->src + jp->pos, "false", 5) == 0) {
        jp->pos += 5; if (out) *out = 0; return 0;
    }
    return -1;
}

/* Parse null literal */
__attribute__((unused))
static int jp_parse_null(jparser_t *jp)
{
    jp_skip_ws(jp);
    if (strncmp(jp->src + jp->pos, "null", 4) == 0) {
        jp->pos += 4; return 0;
    }
    return -1;
}

/* Check if the next token is a null */
__attribute__((unused))
static int jp_is_null(jparser_t *jp)
{
    jp_skip_ws(jp);
    return (strncmp(jp->src + jp->pos, "null", 4) == 0);
}

/*
 * Object iteration helpers.
 * Usage:
 *   if (jp_enter_object(jp) != 0) error;
 *   char key[64];
 *   while (jp_next_key(jp, key, sizeof(key)) == 0) {
 *       if (strcmp(key, "foo") == 0) { parse value ... }
 *       else { jp_skip_value(jp); }
 *   }
 *   // after loop, jp_leave_object unnecessary (consumed by jp_next_key returning -1)
 */
static int jp_enter_object(jparser_t *jp)
{
    jp_skip_ws(jp);
    if (jp->pos >= jp->len || jp->src[jp->pos] != '{') return -1;
    jp->pos++;
    return 0;
}

/* Returns 0 and fills key if there is a next key; returns -1 at '}'  */
static int jp_next_key(jparser_t *jp, char *key, size_t keysz)
{
    jp_skip_ws(jp);
    if (jp->pos >= jp->len) return -1;
    if (jp->src[jp->pos] == '}') { jp->pos++; return -1; }
    if (jp->src[jp->pos] == ',') { jp->pos++; jp_skip_ws(jp); }
    if (jp->pos >= jp->len || jp->src[jp->pos] == '}') {
        if (jp->pos < jp->len) jp->pos++;
        return -1;
    }
    if (jp_parse_string(jp, key, keysz) != 0) return -1;
    jp_skip_ws(jp);
    if (jp->pos >= jp->len || jp->src[jp->pos] != ':') return -1;
    jp->pos++;
    return 0;
}

static int jp_enter_array(jparser_t *jp)
{
    jp_skip_ws(jp);
    if (jp->pos >= jp->len || jp->src[jp->pos] != '[') return -1;
    jp->pos++;
    return 0;
}

/* Returns 0 if there is a next element; returns -1 at ']' */
static int jp_next_element(jparser_t *jp)
{
    jp_skip_ws(jp);
    if (jp->pos >= jp->len) return -1;
    if (jp->src[jp->pos] == ']') { jp->pos++; return -1; }
    if (jp->src[jp->pos] == ',') { jp->pos++; jp_skip_ws(jp); }
    if (jp->pos >= jp->len || jp->src[jp->pos] == ']') {
        if (jp->pos < jp->len) jp->pos++;
        return -1;
    }
    return 0;
}

/* ============================================================
 * JSON emitters for Ollama request bodies
 * ============================================================ */

/* Emit options object (temperature, num_predict, seed) */
static int emit_options(jbuf_t *j, double temperature, int max_tokens, int seed,
                        const char stop[][AF_LLM_MAX_STOP_LEN], int n_stop)
{
    if (jbuf_append(j, "{") != 0) return -1;
    /* temperature */
    if (jbuf_append_str(j, "temperature") != 0) return -1;
    if (jbuf_appendc(j, ':') != 0) return -1;
    if (jbuf_append_double(j, temperature) != 0) return -1;
    /* num_predict */
    if (max_tokens > 0) {
        if (jbuf_append(j, ",") != 0) return -1;
        if (jbuf_append_str(j, "num_predict") != 0) return -1;
        if (jbuf_appendc(j, ':') != 0) return -1;
        if (jbuf_append_int(j, max_tokens) != 0) return -1;
    }
    /* seed */
    if (seed >= 0) {
        if (jbuf_append(j, ",") != 0) return -1;
        if (jbuf_append_str(j, "seed") != 0) return -1;
        if (jbuf_appendc(j, ':') != 0) return -1;
        if (jbuf_append_int(j, seed) != 0) return -1;
    }
    /* stop sequences */
    if (n_stop > 0 && stop) {
        if (jbuf_append(j, ",") != 0) return -1;
        if (jbuf_append_str(j, "stop") != 0) return -1;
        if (jbuf_append(j, ":[") != 0) return -1;
        for (int i = 0; i < n_stop; i++) {
            if (i > 0 && jbuf_appendc(j, ',') != 0) return -1;
            if (jbuf_append_str(j, stop[i]) != 0) return -1;
        }
        if (jbuf_appendc(j, ']') != 0) return -1;
    }
    return jbuf_appendc(j, '}');
}

/* Build POST /api/generate body */
static char *build_complete_body(const af_llm_completion_request_t *req)
{
    jbuf_t j;
    if (jbuf_init(&j) != 0) return NULL;

    if (jbuf_append(&j, "{") != 0) goto fail;
    if (jbuf_append_str(&j, "model") != 0) goto fail;
    if (jbuf_append(&j, ":") != 0) goto fail;
    if (jbuf_append_str(&j, req->model) != 0) goto fail;
    if (jbuf_append(&j, ",") != 0) goto fail;
    if (jbuf_append_str(&j, "options") != 0) goto fail;
    if (jbuf_append(&j, ":") != 0) goto fail;
    {
        char stop_tmp[AF_LLM_MAX_STOP_SEQS][AF_LLM_MAX_STOP_LEN];
        int n_stop = req->n_stop;
        for (int i = 0; i < n_stop; i++)
            safe_strncpy(stop_tmp[i], req->stop[i], AF_LLM_MAX_STOP_LEN);
        if (emit_options(&j, req->temperature, req->max_tokens, req->seed,
                         (const char(*)[AF_LLM_MAX_STOP_LEN])stop_tmp, n_stop) != 0)
            goto fail;
    }
    if (jbuf_append(&j, ",") != 0) goto fail;
    if (jbuf_append_str(&j, "prompt") != 0) goto fail;
    if (jbuf_append(&j, ":") != 0) goto fail;
    if (jbuf_append_str(&j, req->prompt) != 0) goto fail;
    if (jbuf_append(&j, ",") != 0) goto fail;
    if (jbuf_append_str(&j, "stream") != 0) goto fail;
    if (jbuf_append(&j, ":") != 0) goto fail;
    if (jbuf_append_bool(&j, 0) != 0) goto fail;
    if (jbuf_appendc(&j, '}') != 0) goto fail;

    return j.buf; /* caller owns */
fail:
    jbuf_free(&j);
    return NULL;
}

/* Build POST /api/chat body */
static char *build_chat_body(const af_llm_chat_request_t *req)
{
    jbuf_t j;
    if (jbuf_init(&j) != 0) return NULL;

    if (jbuf_append(&j, "{") != 0) goto fail;
    if (jbuf_append_str(&j, "messages") != 0) goto fail;
    if (jbuf_append(&j, ":[") != 0) goto fail;
    for (int i = 0; i < req->n_messages; i++) {
        if (i > 0 && jbuf_appendc(&j, ',') != 0) goto fail;
        if (jbuf_appendc(&j, '{') != 0) goto fail;
        if (jbuf_append_str(&j, "content") != 0) goto fail;
        if (jbuf_appendc(&j, ':') != 0) goto fail;
        if (jbuf_append_str(&j, req->messages[i].content) != 0) goto fail;
        if (jbuf_append(&j, ",") != 0) goto fail;
        if (jbuf_append_str(&j, "role") != 0) goto fail;
        if (jbuf_appendc(&j, ':') != 0) goto fail;
        if (jbuf_append_str(&j, req->messages[i].role) != 0) goto fail;
        if (jbuf_appendc(&j, '}') != 0) goto fail;
    }
    if (jbuf_append(&j, "],") != 0) goto fail;
    if (jbuf_append_str(&j, "model") != 0) goto fail;
    if (jbuf_append(&j, ":") != 0) goto fail;
    if (jbuf_append_str(&j, req->model) != 0) goto fail;
    if (jbuf_append(&j, ",") != 0) goto fail;
    if (jbuf_append_str(&j, "options") != 0) goto fail;
    if (jbuf_append(&j, ":") != 0) goto fail;
    if (emit_options(&j, req->temperature, req->max_tokens, req->seed,
                     NULL, 0) != 0) goto fail;
    if (jbuf_append(&j, ",") != 0) goto fail;
    if (jbuf_append_str(&j, "stream") != 0) goto fail;
    if (jbuf_append(&j, ":") != 0) goto fail;
    if (jbuf_append_bool(&j, req->stream) != 0) goto fail;
    if (jbuf_appendc(&j, '}') != 0) goto fail;

    return j.buf;
fail:
    jbuf_free(&j);
    return NULL;
}

/* Build POST /api/embeddings body (single prompt) */
static char *build_embed_body(const char *model, const char *prompt)
{
    jbuf_t j;
    if (jbuf_init(&j) != 0) return NULL;

    if (jbuf_append(&j, "{") != 0) goto fail;
    if (jbuf_append_str(&j, "model") != 0) goto fail;
    if (jbuf_append(&j, ":") != 0) goto fail;
    if (jbuf_append_str(&j, model) != 0) goto fail;
    if (jbuf_append(&j, ",") != 0) goto fail;
    if (jbuf_append_str(&j, "prompt") != 0) goto fail;
    if (jbuf_append(&j, ":") != 0) goto fail;
    if (jbuf_append_str(&j, prompt) != 0) goto fail;
    if (jbuf_appendc(&j, '}') != 0) goto fail;

    return j.buf;
fail:
    jbuf_free(&j);
    return NULL;
}

/* ============================================================
 * JSON response parsers
 * ============================================================ */

/* Parse /api/generate response into af_llm_completion_response_t */
static int parse_complete_response(const char *body,
                                   af_llm_completion_response_t *resp,
                                   af_llm_error_t *err)
{
    memset(resp, 0, sizeof(*resp));
    jparser_t jp;
    jp_init(&jp, body);

    if (jp_enter_object(&jp) != 0) {
        set_error(err, AF_LLM_ERR_DECODE, 0, "invalid JSON object in response");
        return -1;
    }
    char key[64];
    while (jp_next_key(&jp, key, sizeof(key)) == 0) {
        if (strcmp(key, "response") == 0) {
            jp_parse_string(&jp, resp->text, sizeof(resp->text));
        } else if (strcmp(key, "model") == 0) {
            jp_parse_string(&jp, resp->model, sizeof(resp->model));
        } else if (strcmp(key, "prompt_eval_count") == 0) {
            jp_parse_int(&jp, &resp->prompt_tokens);
        } else if (strcmp(key, "eval_count") == 0) {
            jp_parse_int(&jp, &resp->completion_tokens);
        } else if (strcmp(key, "total_duration") == 0) {
            jp_parse_int64(&jp, &resp->total_duration_ns);
        } else if (strcmp(key, "done_reason") == 0) {
            jp_parse_string(&jp, resp->finish_reason, sizeof(resp->finish_reason));
        } else {
            jp_skip_value(&jp);
        }
    }
    if (resp->finish_reason[0] == '\0')
        safe_strncpy(resp->finish_reason, "stop", sizeof(resp->finish_reason));
    return 0;
}

/* Parse message object { "role":..., "content":... } */
static int parse_message_obj(jparser_t *jp, af_llm_chat_message_t *msg)
{
    if (jp_enter_object(jp) != 0) return -1;
    char key[32];
    while (jp_next_key(jp, key, sizeof(key)) == 0) {
        if (strcmp(key, "role") == 0) {
            jp_parse_string(jp, msg->role, sizeof(msg->role));
        } else if (strcmp(key, "content") == 0) {
            jp_parse_string(jp, msg->content, sizeof(msg->content));
        } else {
            jp_skip_value(jp);
        }
    }
    return 0;
}

/* Parse /api/chat non-streaming response */
static int parse_chat_response(const char *body,
                                af_llm_chat_response_t *resp,
                                af_llm_error_t *err)
{
    memset(resp, 0, sizeof(*resp));
    jparser_t jp;
    jp_init(&jp, body);

    if (jp_enter_object(&jp) != 0) {
        set_error(err, AF_LLM_ERR_DECODE, 0, "invalid JSON object in chat response");
        return -1;
    }
    char key[64];
    while (jp_next_key(&jp, key, sizeof(key)) == 0) {
        if (strcmp(key, "message") == 0) {
            parse_message_obj(&jp, &resp->message);
        } else if (strcmp(key, "model") == 0) {
            jp_parse_string(&jp, resp->model, sizeof(resp->model));
        } else if (strcmp(key, "prompt_eval_count") == 0) {
            jp_parse_int(&jp, &resp->prompt_tokens);
        } else if (strcmp(key, "eval_count") == 0) {
            jp_parse_int(&jp, &resp->completion_tokens);
        } else if (strcmp(key, "total_duration") == 0) {
            jp_parse_int64(&jp, &resp->total_duration_ns);
        } else if (strcmp(key, "done_reason") == 0) {
            jp_parse_string(&jp, resp->finish_reason, sizeof(resp->finish_reason));
        } else {
            jp_skip_value(&jp);
        }
    }
    if (resp->finish_reason[0] == '\0')
        safe_strncpy(resp->finish_reason, "stop", sizeof(resp->finish_reason));
    return 0;
}

/* Parse one NDJSON streaming chunk */
static int parse_chat_chunk(const char *line, af_llm_chat_chunk_t *chunk)
{
    memset(chunk, 0, sizeof(*chunk));
    jparser_t jp;
    jp_init(&jp, line);

    if (jp_enter_object(&jp) != 0) return -1;
    char key[64];
    while (jp_next_key(&jp, key, sizeof(key)) == 0) {
        if (strcmp(key, "message") == 0) {
            jparser_t save = jp;
            char mkey[32];
            if (jp_enter_object(&jp) == 0) {
                while (jp_next_key(&jp, mkey, sizeof(mkey)) == 0) {
                    if (strcmp(mkey, "content") == 0) {
                        jp_parse_string(&jp, chunk->delta, sizeof(chunk->delta));
                    } else {
                        jp_skip_value(&jp);
                    }
                }
            } else {
                jp = save;
                jp_skip_value(&jp);
            }
        } else if (strcmp(key, "done") == 0) {
            jp_parse_bool(&jp, &chunk->done);
        } else if (strcmp(key, "done_reason") == 0) {
            jp_parse_string(&jp, chunk->finish_reason, sizeof(chunk->finish_reason));
        } else {
            jp_skip_value(&jp);
        }
    }
    return 0;
}

/* Parse /api/embeddings response — returns heap-allocated array */
static int parse_embed_response(const char *body,
                                 double **embed_out, int *dim_out,
                                 af_llm_error_t *err)
{
    *embed_out = NULL;
    *dim_out = 0;

    jparser_t jp;
    jp_init(&jp, body);

    if (jp_enter_object(&jp) != 0) {
        set_error(err, AF_LLM_ERR_DECODE, 0, "invalid JSON in embed response");
        return -1;
    }

    char key[32];
    int found = 0;
    while (jp_next_key(&jp, key, sizeof(key)) == 0) {
        if (strcmp(key, "embedding") == 0) {
            found = 1;
            /* first pass: count elements */
            jparser_t scan = jp;
            int count = 0;
            if (jp_enter_array(&scan) == 0) {
                while (jp_next_element(&scan) == 0) {
                    jp_skip_value(&scan);
                    count++;
                }
            }
            /* allocate */
            double *arr = (double *)malloc((size_t)count * sizeof(double));
            if (!arr) {
                set_error(err, AF_LLM_ERR_DECODE, 0, "OOM parsing embedding");
                return -1;
            }
            /* second pass: fill */
            if (jp_enter_array(&jp) == 0) {
                int i = 0;
                while (jp_next_element(&jp) == 0 && i < count) {
                    jp_parse_number(&jp, &arr[i++]);
                }
            }
            *embed_out = arr;
            *dim_out = count;
        } else {
            jp_skip_value(&jp);
        }
    }

    if (!found) {
        set_error(err, AF_LLM_ERR_DECODE, 0, "no 'embedding' field in response");
        return -1;
    }
    return 0;
}

/* Parse /api/tags response into model_info array */
static int parse_list_models(const char *body,
                              af_llm_model_info_t *out, int cap, int *n_out,
                              af_llm_error_t *err)
{
    *n_out = 0;
    jparser_t jp;
    jp_init(&jp, body);

    if (jp_enter_object(&jp) != 0) {
        set_error(err, AF_LLM_ERR_DECODE, 0, "invalid JSON in list_models");
        return -1;
    }
    char key[64];
    while (jp_next_key(&jp, key, sizeof(key)) == 0) {
        if (strcmp(key, "models") == 0) {
            if (jp_enter_array(&jp) != 0) {
                set_error(err, AF_LLM_ERR_DECODE, 0, "models not an array");
                return -1;
            }
            while (jp_next_element(&jp) == 0 && *n_out < cap) {
                af_llm_model_info_t *m = &out[*n_out];
                memset(m, 0, sizeof(*m));
                if (jp_enter_object(&jp) == 0) {
                    char mkey[32];
                    while (jp_next_key(&jp, mkey, sizeof(mkey)) == 0) {
                        if (strcmp(mkey, "name") == 0) {
                            jp_parse_string(&jp, m->name, sizeof(m->name));
                        } else if (strcmp(mkey, "size") == 0) {
                            jp_parse_int64(&jp, &m->size_bytes);
                        } else if (strcmp(mkey, "modified_at") == 0) {
                            jp_parse_string(&jp, m->modified_at, sizeof(m->modified_at));
                        } else {
                            jp_skip_value(&jp);
                        }
                    }
                    (*n_out)++;
                } else {
                    jp_skip_value(&jp);
                }
            }
        } else {
            jp_skip_value(&jp);
        }
    }
    return 0;
}

/* ============================================================
 * libcurl transport
 * ============================================================ */

#ifndef AF_NO_LIBCURL

#include <curl/curl.h>

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} curl_buf_t;

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    curl_buf_t *b = (curl_buf_t *)userdata;
    size_t total = size * nmemb;
    if (b->len + total + 1 > b->cap) {
        size_t newcap = b->cap ? b->cap * 2 : 4096;
        while (newcap < b->len + total + 1) newcap *= 2;
        char *nb = (char *)realloc(b->buf, newcap);
        if (!nb) return 0;
        b->buf = nb;
        b->cap = newcap;
    }
    memcpy(b->buf + b->len, ptr, total);
    b->len += total;
    b->buf[b->len] = '\0';
    return total;
}

typedef struct {
    char host[AF_LLM_MAX_HOST_LEN];
} curl_ctx_t;

static int curl_transport_request(void *ctx, const char *method, const char *path,
                                   const char *body, char **resp_body,
                                   int *resp_status, af_llm_error_t *err)
{
    curl_ctx_t *c = (curl_ctx_t *)ctx;
    char url[AF_LLM_MAX_HOST_LEN + 256];
    snprintf(url, sizeof(url), "%s%s", c->host, path);

    CURL *curl = curl_easy_init();
    if (!curl) {
        set_error(err, AF_LLM_ERR_UNREACHABLE, 0, "curl_easy_init failed");
        return -1;
    }

    curl_buf_t rbuf = { NULL, 0, 0 };
    struct curl_slist *headers = NULL;
    if (body) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rbuf);
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        } else {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        }
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        const char *cerr = curl_easy_strerror(res);
        if (res == CURLE_OPERATION_TIMEDOUT)
            set_error(err, AF_LLM_ERR_TIMEOUT, 0, "request timed out: %s", cerr);
        else
            set_error(err, AF_LLM_ERR_UNREACHABLE, 0, "curl error: %s", cerr);
        free(rbuf.buf);
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return -1;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    *resp_status = (int)http_code;

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    *resp_body = rbuf.buf ? rbuf.buf : strdup("");
    return 0;
}

static void curl_transport_destroy(void *ctx)
{
    free(ctx);
}

static af_llm_transport_t make_curl_transport(const char *host)
{
    curl_ctx_t *ctx = (curl_ctx_t *)malloc(sizeof(curl_ctx_t));
    if (ctx) safe_strncpy(ctx->host, host, sizeof(ctx->host));
    af_llm_transport_t t;
    t.request = curl_transport_request;
    t.destroy = curl_transport_destroy;
    t.ctx     = ctx;
    return t;
}

#else /* AF_NO_LIBCURL */

static int stub_transport_request(void *ctx,
                                   const char *method, const char *path,
                                   const char *body, char **resp_body,
                                   int *resp_status, af_llm_error_t *err)
{
    (void)ctx; (void)method; (void)path; (void)body;
    *resp_body  = NULL;
    *resp_status = 0;
    set_error(err, AF_LLM_ERR_UNREACHABLE, 0, "no http backend");
    return -1;
}

static af_llm_transport_t make_curl_transport(const char *host)
{
    (void)host;
    af_llm_transport_t t;
    t.request = stub_transport_request;
    t.destroy = NULL;
    t.ctx     = NULL;
    return t;
}

#endif /* AF_NO_LIBCURL */

/* ============================================================
 * Provider init / free
 * ============================================================ */

/* Read env var with fallback */
static const char *getenv_or(const char *name, const char *fallback)
{
    const char *v = getenv(name);
    return (v && v[0]) ? v : fallback;
}

static int getenv_int_or(const char *name, int fallback)
{
    const char *v = getenv(name);
    if (!v || !v[0]) return fallback;
    return (int)strtol(v, NULL, 10);
}

static double getenv_double_or(const char *name, double fallback)
{
    const char *v = getenv(name);
    if (!v || !v[0]) return fallback;
    return strtod(v, NULL);
}

int af_llm_provider_init(af_llm_provider_t *p,
                          const char *host, const char *model,
                          const char *embed_model,
                          double temperature, int max_tokens,
                          int seed, double timeout_s,
                          af_llm_transport_t transport)
{
    if (!p) return -1;
    memset(p, 0, sizeof(*p));
    safe_strncpy(p->host,        host        ? host        : "http://localhost:11434",
                 sizeof(p->host));
    safe_strncpy(p->model,       model       ? model       : "llama3.1:8b",
                 sizeof(p->model));
    safe_strncpy(p->embed_model, embed_model ? embed_model : "nomic-embed-text",
                 sizeof(p->embed_model));
    p->temperature = temperature;
    p->max_tokens  = max_tokens;
    p->seed        = seed;
    p->timeout_s   = timeout_s;
    p->transport   = transport;
    return 0;
}

int af_llm_provider_from_env(af_llm_provider_t *p)
{
    if (!p) return -1;
    const char *host        = getenv_or("AF_LLM_HOST",        "http://localhost:11434");
    const char *model       = getenv_or("AF_LLM_MODEL",       "llama3.1:8b");
    const char *embed_model = getenv_or("AF_LLM_EMBED_MODEL", "nomic-embed-text");
    double temperature = getenv_double_or("AF_LLM_TEMPERATURE", 0.2);
    int    max_tokens  = getenv_int_or("AF_LLM_MAX_TOKENS", 512);
    int    seed        = getenv_int_or("AF_LLM_SEED", 42);
    double timeout_s   = getenv_double_or("AF_LLM_TIMEOUT", 60.0);
    af_llm_transport_t t = make_curl_transport(host);
    return af_llm_provider_init(p, host, model, embed_model,
                                temperature, max_tokens, seed, timeout_s, t);
}

int af_llm_provider_with_transport(af_llm_provider_t *p, af_llm_transport_t transport)
{
    if (!p) return -1;
    const char *host        = getenv_or("AF_LLM_HOST",        "http://localhost:11434");
    const char *model       = getenv_or("AF_LLM_MODEL",       "llama3.1:8b");
    const char *embed_model = getenv_or("AF_LLM_EMBED_MODEL", "nomic-embed-text");
    double temperature = getenv_double_or("AF_LLM_TEMPERATURE", 0.2);
    int    max_tokens  = getenv_int_or("AF_LLM_MAX_TOKENS", 512);
    int    seed        = getenv_int_or("AF_LLM_SEED", 42);
    double timeout_s   = getenv_double_or("AF_LLM_TIMEOUT", 60.0);
    return af_llm_provider_init(p, host, model, embed_model,
                                temperature, max_tokens, seed, timeout_s, transport);
}

void af_llm_provider_free(af_llm_provider_t *p)
{
    if (!p) return;
    if (p->transport.destroy)
        p->transport.destroy(p->transport.ctx);
    memset(p, 0, sizeof(*p));
}

/* ============================================================
 * Internal: do a transport request
 * ============================================================ */

static int do_request(af_llm_provider_t *p,
                       const char *method, const char *path, const char *body,
                       char **resp_body, int *resp_status, af_llm_error_t *err)
{
    af_llm_error_clear(err);
    int rc = p->transport.request(p->transport.ctx, method, path, body,
                                   resp_body, resp_status, err);
    if (rc != 0) return rc;
    /* Check for HTTP errors */
    if (*resp_status >= 400) {
        set_error(err, AF_LLM_ERR_HTTP, *resp_status,
                  "HTTP %d from %s %s", *resp_status, method, path);
        return -1;
    }
    return 0;
}

/* ============================================================
 * Provider interface implementations
 * ============================================================ */

int af_llm_complete(af_llm_provider_t *p,
                     const af_llm_completion_request_t *req,
                     af_llm_completion_response_t *resp,
                     af_llm_error_t *err)
{
    if (!p || !req || !resp || !err) return -1;
    af_llm_error_clear(err);

    char *body = build_complete_body(req);
    if (!body) {
        set_error(err, AF_LLM_ERR_DECODE, 0, "OOM building request body");
        return -1;
    }

    char *rbody = NULL;
    int   status = 0;
    int rc = do_request(p, "POST", "/api/generate", body, &rbody, &status, err);
    free(body);
    if (rc != 0) { free(rbody); return rc; }

    rc = parse_complete_response(rbody, resp, err);
    free(rbody);
    return rc;
}

int af_llm_chat(af_llm_provider_t *p,
                 const af_llm_chat_request_t *req,
                 af_llm_chat_response_t *resp,
                 af_llm_error_t *err)
{
    if (!p || !req || !resp || !err) return -1;
    af_llm_error_clear(err);

    /* Force stream=false for non-streaming chat */
    af_llm_chat_request_t req2 = *req;
    req2.stream = 0;

    char *body = build_chat_body(&req2);
    if (!body) {
        set_error(err, AF_LLM_ERR_DECODE, 0, "OOM building chat request body");
        return -1;
    }

    char *rbody = NULL;
    int   status = 0;
    int rc = do_request(p, "POST", "/api/chat", body, &rbody, &status, err);
    free(body);
    if (rc != 0) { free(rbody); return rc; }

    rc = parse_chat_response(rbody, resp, err);
    free(rbody);
    return rc;
}

int af_llm_chat_stream(af_llm_provider_t *p,
                        const af_llm_chat_request_t *req,
                        int (*chunk_cb)(const af_llm_chat_chunk_t *chunk, void *ud),
                        void *userdata,
                        af_llm_error_t *err)
{
    if (!p || !req || !chunk_cb || !err) return -1;
    af_llm_error_clear(err);

    /* Force stream=true */
    af_llm_chat_request_t req2 = *req;
    req2.stream = 1;

    char *body = build_chat_body(&req2);
    if (!body) {
        set_error(err, AF_LLM_ERR_DECODE, 0, "OOM building stream request body");
        return -1;
    }

    char *rbody = NULL;
    int   status = 0;
    int rc = do_request(p, "POST", "/api/chat", body, &rbody, &status, err);
    free(body);
    if (rc != 0) { free(rbody); return rc; }

    /* Split NDJSON on newlines */
    char *line = rbody;
    char *end  = rbody + strlen(rbody);
    while (line < end) {
        char *nl = memchr(line, '\n', (size_t)(end - line));
        size_t linelen = nl ? (size_t)(nl - line) : (size_t)(end - line);
        if (linelen > 0) {
            /* NUL-terminate the line temporarily */
            char saved = line[linelen];
            line[linelen] = '\0';
            af_llm_chat_chunk_t chunk;
            if (parse_chat_chunk(line, &chunk) == 0) {
                int cbrc = chunk_cb(&chunk, userdata);
                line[linelen] = saved;
                if (cbrc != 0) break;
            } else {
                line[linelen] = saved;
            }
        }
        line = nl ? nl + 1 : end;
    }

    free(rbody);
    return 0;
}

void af_llm_embed_response_free(af_llm_embed_response_t *resp)
{
    if (!resp) return;
    if (resp->embeddings) {
        for (int i = 0; i < resp->n_inputs; i++)
            free(resp->embeddings[i]);
        free(resp->embeddings);
    }
    memset(resp, 0, sizeof(*resp));
}

int af_llm_embed(af_llm_provider_t *p,
                  const af_llm_embed_request_t *req,
                  af_llm_embed_response_t *resp,
                  af_llm_error_t *err)
{
    if (!p || !req || !resp || !err) return -1;
    af_llm_error_clear(err);

    memset(resp, 0, sizeof(*resp));
    safe_strncpy(resp->model, req->model, sizeof(resp->model));

    if (req->n_inputs == 0) return 0;

    resp->embeddings = (double **)calloc((size_t)req->n_inputs, sizeof(double *));
    if (!resp->embeddings) {
        set_error(err, AF_LLM_ERR_DECODE, 0, "OOM allocating embeddings");
        return -1;
    }
    resp->n_inputs = req->n_inputs;

    for (int i = 0; i < req->n_inputs; i++) {
        char *body = build_embed_body(req->model, req->inputs[i]);
        if (!body) {
            set_error(err, AF_LLM_ERR_DECODE, 0, "OOM building embed body");
            af_llm_embed_response_free(resp);
            return -1;
        }

        char *rbody = NULL;
        int   status = 0;
        int rc = do_request(p, "POST", "/api/embeddings", body, &rbody, &status, err);
        free(body);
        if (rc != 0) { free(rbody); af_llm_embed_response_free(resp); return rc; }

        int dim = 0;
        rc = parse_embed_response(rbody, &resp->embeddings[i], &dim, err);
        free(rbody);
        if (rc != 0) { af_llm_embed_response_free(resp); return rc; }

        if (i == 0) resp->embed_dim = dim;
    }
    return 0;
}

int af_llm_health(af_llm_provider_t *p,
                   af_llm_health_status_t *status,
                   af_llm_error_t *err)
{
    if (!p || !status || !err) return -1;
    af_llm_error_clear(err);
    memset(status, 0, sizeof(*status));

    /* GET /api/tags — if this succeeds, Ollama is reachable */
    char *rbody = NULL;
    int   http_status = 0;
    int rc = p->transport.request(p->transport.ctx, "GET", "/api/tags", NULL,
                                   &rbody, &http_status, err);
    if (rc != 0 || http_status >= 400) {
        status->ok = 0;
        if (rc == 0) {
            /* HTTP error */
            safe_strncpy(status->error, err->message, sizeof(status->error));
        }
        free(rbody);
        return 0; /* health check itself succeeded */
    }

    /* Parse model list */
    af_llm_model_info_t models[AF_LLM_MAX_MODELS];
    int n_models = 0;
    af_llm_error_t parse_err;
    af_llm_error_clear(&parse_err);
    parse_list_models(rbody, models, AF_LLM_MAX_MODELS, &n_models, &parse_err);
    free(rbody);

    status->ok = 1;
    safe_strncpy(status->model, p->model, sizeof(status->model));

    /* Check if default model is loaded */
    status->model_loaded = 0;
    for (int i = 0; i < n_models; i++) {
        if (strcmp(models[i].name, p->model) == 0) {
            status->model_loaded = 1;
            break;
        }
    }
    return 0;
}

int af_llm_list_models(af_llm_provider_t *p,
                        af_llm_model_info_t *out, int cap_out, int *n_out,
                        af_llm_error_t *err)
{
    if (!p || !out || !n_out || !err) return -1;
    af_llm_error_clear(err);

    char *rbody = NULL;
    int   status = 0;
    int rc = do_request(p, "GET", "/api/tags", NULL, &rbody, &status, err);
    if (rc != 0) { free(rbody); return rc; }

    rc = parse_list_models(rbody, out, cap_out, n_out, err);
    free(rbody);
    return rc;
}

/* ============================================================
 * Indicator sort helper (alphabetical key sort for JSON)
 * ============================================================ */

static void sort_kv(af_llm_kv_t *kvs, int n)
{
    /* Simple insertion sort — n is typically small (< 32) */
    for (int i = 1; i < n; i++) {
        af_llm_kv_t tmp = kvs[i];
        int j = i - 1;
        while (j >= 0 && strcmp(kvs[j].key, tmp.key) > 0) {
            kvs[j + 1] = kvs[j];
            j--;
        }
        kvs[j + 1] = tmp;
    }
}

/* Emit indicators as {"key":value,...} with keys sorted alphabetically */
static int emit_indicators_json(char *buf, size_t bufsz,
                                  af_llm_kv_t *kvs, int n)
{
    /* Copy + sort */
    af_llm_kv_t sorted[AF_LLM_MAX_INDICATORS];
    int cnt = n < AF_LLM_MAX_INDICATORS ? n : AF_LLM_MAX_INDICATORS;
    memcpy(sorted, kvs, (size_t)cnt * sizeof(af_llm_kv_t));
    sort_kv(sorted, cnt);

    /* Build JSON with compact separators (no spaces) — matches Python's
     * json.dumps(..., separators=(',',':')) */
    jbuf_t j;
    if (jbuf_init(&j) != 0) return -1;
    if (jbuf_appendc(&j, '{') != 0) goto fail;
    for (int i = 0; i < cnt; i++) {
        if (i > 0 && jbuf_appendc(&j, ',') != 0) goto fail;
        if (jbuf_append_str(&j, sorted[i].key) != 0) goto fail;
        if (jbuf_appendc(&j, ':') != 0) goto fail;
        /* Python uses repr-like formatting for floats; we use %g which
         * matches for the fixture values */
        if (jbuf_append_double(&j, sorted[i].value) != 0) goto fail;
    }
    if (jbuf_appendc(&j, '}') != 0) goto fail;

    snprintf(buf, bufsz, "%s", j.buf);
    jbuf_free(&j);
    return 0;
fail:
    jbuf_free(&j);
    return -1;
}

/* ============================================================
 * Prompt string constants — must be byte-identical to Python prompts.py
 * ============================================================ */

/* U+2013 EN DASH encoded as UTF-8: 0xE2 0x80 0x93 */
#define EN_DASH "\xe2\x80\x93"

static const char *AF_PROMPT_TRADE_RATIONALE_SYSTEM =
    "You are an experienced trading assistant. Given a fired signal, explain in 1"
    EN_DASH
    "3 plain sentences why this trade triggered. Be concrete; reference the "
    "indicator values and patterns. Do not give financial advice.";

static const char *AF_PROMPT_BACKTEST_COMMENTARY_SYSTEM =
    "You are an experienced quant analyst. Given a backtest summary, write a 3"
    EN_DASH
    "5 sentence narrative covering: overall result, risk-adjusted performance, "
    "drawdown behavior, and one concrete improvement suggestion. Plain prose, no "
    "bullet points.";

static const char *AF_PROMPT_NEWS_SENTIMENT_SYSTEM =
    "You are a financial news sentiment classifier. Given a headline (and optional "
    "body), respond with exactly one JSON object: "
    "{\"sentiment\":\"bullish|bearish|neutral\",\"score\":-1.0..1.0,"
    "\"confidence\":0.0..1.0,\"reason\":\"<one short sentence>\"}. "
    "No prose outside the JSON.";

static const char *AF_PROMPT_STRATEGY_DESCRIBE_SYSTEM =
    "You are a trading strategy documenter. Given a structured strategy spec, "
    "write a 2"
    EN_DASH
    "4 sentence plain-English description suitable for a strategy catalog. "
    "Do not invent rules.";

/* ============================================================
 * Use-case implementations
 * ============================================================ */

/* Build a default chat request structure */
static void init_use_case_request(af_llm_chat_request_t *req,
                                   af_llm_provider_t *p)
{
    memset(req, 0, sizeof(*req));
    safe_strncpy(req->model, p->model, sizeof(req->model));
    req->temperature = 0.2;
    req->max_tokens  = 512;
    req->seed        = 42;
    req->timeout_s   = p->timeout_s;
    req->stream      = 0;
}

int af_llm_trade_rationale(af_llm_provider_t *p,
                            const af_llm_trade_rationale_input_t *inp,
                            af_llm_trade_rationale_output_t *out,
                            af_llm_error_t *err)
{
    if (!p || !inp || !out || !err) return -1;
    af_llm_error_clear(err);

    /* Build indicators JSON */
    char ind_json[1024];
    af_llm_kv_t kvs_copy[AF_LLM_MAX_INDICATORS];
    memcpy(kvs_copy, inp->indicators,
           (size_t)inp->n_indicators * sizeof(af_llm_kv_t));
    if (emit_indicators_json(ind_json, sizeof(ind_json),
                              kvs_copy, inp->n_indicators) != 0) {
        set_error(err, AF_LLM_ERR_DECODE, 0, "failed to emit indicators JSON");
        return -1;
    }

    /* Build patterns CSV */
    char patterns_csv[512];
    patterns_csv[0] = '\0';
    for (int i = 0; i < inp->n_patterns; i++) {
        if (i > 0) strncat(patterns_csv, ",", sizeof(patterns_csv) - strlen(patterns_csv) - 1);
        strncat(patterns_csv, inp->patterns[i], sizeof(patterns_csv) - strlen(patterns_csv) - 1);
    }

    /* Build user prompt */
    char user_content[AF_LLM_MAX_CONTENT];
    snprintf(user_content, sizeof(user_content),
             "SIGNAL\n"
             "symbol=%s\n"
             "side=%s\n"
             "timeframe=%s\n"
             "entry=%.4f\n"
             "sl=%.4f\n"
             "tp=%.4f\n"
             "indicators=%s\n"
             "patterns=%s\n"
             "\n"
             "Explain.",
             inp->symbol,
             inp->side,
             inp->timeframe,
             inp->entry_price,
             inp->stop_loss,
             inp->take_profit,
             ind_json,
             patterns_csv);

    /* Build request */
    af_llm_chat_request_t req;
    init_use_case_request(&req, p);
    req.n_messages = 2;
    safe_strncpy(req.messages[0].role, "system", sizeof(req.messages[0].role));
    safe_strncpy(req.messages[0].content, AF_PROMPT_TRADE_RATIONALE_SYSTEM,
                 sizeof(req.messages[0].content));
    safe_strncpy(req.messages[1].role, "user", sizeof(req.messages[1].role));
    safe_strncpy(req.messages[1].content, user_content,
                 sizeof(req.messages[1].content));

    af_llm_chat_response_t resp;
    int rc = af_llm_chat(p, &req, &resp, err);
    if (rc != 0) return rc;

    safe_strncpy(out->explanation, resp.message.content, sizeof(out->explanation));
    safe_strncpy(out->model, resp.model, sizeof(out->model));
    out->tokens = resp.completion_tokens;
    return 0;
}

int af_llm_backtest_commentary(af_llm_provider_t *p,
                                const af_llm_backtest_commentary_input_t *inp,
                                af_llm_backtest_commentary_output_t *out,
                                af_llm_error_t *err)
{
    if (!p || !inp || !out || !err) return -1;
    af_llm_error_clear(err);

    char user_content[AF_LLM_MAX_CONTENT];
    snprintf(user_content, sizeof(user_content),
             "BACKTEST\n"
             "strategy=%s\n"
             "period=%s\n"
             "total_return=%.4f\n"
             "sharpe=%.4f\n"
             "sortino=%.4f\n"
             "max_drawdown=%.4f\n"
             "win_rate=%.4f\n"
             "trade_count=%d\n"
             "best_trade=%.4f\n"
             "worst_trade=%.4f\n"
             "\n"
             "Comment.",
             inp->strategy_name,
             inp->period,
             inp->total_return,
             inp->sharpe,
             inp->sortino,
             inp->max_drawdown,
             inp->win_rate,
             inp->trade_count,
             inp->best_trade,
             inp->worst_trade);

    af_llm_chat_request_t req;
    init_use_case_request(&req, p);
    req.n_messages = 2;
    safe_strncpy(req.messages[0].role, "system", sizeof(req.messages[0].role));
    safe_strncpy(req.messages[0].content, AF_PROMPT_BACKTEST_COMMENTARY_SYSTEM,
                 sizeof(req.messages[0].content));
    safe_strncpy(req.messages[1].role, "user", sizeof(req.messages[1].role));
    safe_strncpy(req.messages[1].content, user_content,
                 sizeof(req.messages[1].content));

    af_llm_chat_response_t resp;
    int rc = af_llm_chat(p, &req, &resp, err);
    if (rc != 0) return rc;

    safe_strncpy(out->commentary, resp.message.content, sizeof(out->commentary));
    safe_strncpy(out->model, resp.model, sizeof(out->model));
    out->tokens = resp.completion_tokens;
    return 0;
}

/* JSON extraction for news_sentiment — depth counter scanner */
static int extract_json_object(const char *text,
                                char *out, size_t outsz)
{
    int   depth     = 0;
    int   in_string = 0;
    int   escape    = 0;
    const char *start = NULL;

    for (const char *p = text; *p; p++) {
        if (escape) { escape = 0; continue; }
        if (*p == '\\' && in_string) { escape = 1; continue; }
        if (*p == '"') { in_string = !in_string; continue; }
        if (in_string) continue;

        if (*p == '{') {
            if (depth == 0) start = p;
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0 && start != NULL) {
                size_t len = (size_t)(p - start + 1);
                if (out && len < outsz) {
                    memcpy(out, start, len);
                    out[len] = '\0';
                }
                return 0;
            }
        }
    }
    return -1; /* no complete JSON object found */
}

int af_llm_news_sentiment(af_llm_provider_t *p,
                           const af_llm_news_sentiment_input_t *inp,
                           af_llm_news_sentiment_output_t *out,
                           af_llm_error_t *err)
{
    if (!p || !inp || !out || !err) return -1;
    af_llm_error_clear(err);

    char user_content[AF_LLM_MAX_CONTENT];
    snprintf(user_content, sizeof(user_content),
             "HEADLINE: %s\nBODY: %s",
             inp->headline,
             inp->body);

    af_llm_chat_request_t req;
    init_use_case_request(&req, p);
    req.n_messages = 2;
    safe_strncpy(req.messages[0].role, "system", sizeof(req.messages[0].role));
    safe_strncpy(req.messages[0].content, AF_PROMPT_NEWS_SENTIMENT_SYSTEM,
                 sizeof(req.messages[0].content));
    safe_strncpy(req.messages[1].role, "user", sizeof(req.messages[1].role));
    safe_strncpy(req.messages[1].content, user_content,
                 sizeof(req.messages[1].content));

    af_llm_chat_response_t resp;
    int rc = af_llm_chat(p, &req, &resp, err);
    if (rc != 0) return rc;

    /* Extract JSON object from response */
    char json_buf[1024];
    if (extract_json_object(resp.message.content, json_buf, sizeof(json_buf)) != 0) {
        set_error(err, AF_LLM_ERR_DECODE, 0,
                  "no valid JSON object found in sentiment response");
        return -1;
    }

    /* Parse sentiment JSON */
    jparser_t jp;
    jp_init(&jp, json_buf);
    if (jp_enter_object(&jp) != 0) {
        set_error(err, AF_LLM_ERR_DECODE, 0, "malformed JSON in sentiment response");
        return -1;
    }

    memset(out, 0, sizeof(*out));
    char key[32];
    while (jp_next_key(&jp, key, sizeof(key)) == 0) {
        if (strcmp(key, "sentiment") == 0) {
            jp_parse_string(&jp, out->sentiment, sizeof(out->sentiment));
        } else if (strcmp(key, "score") == 0) {
            jp_parse_number(&jp, &out->score);
        } else if (strcmp(key, "confidence") == 0) {
            jp_parse_number(&jp, &out->confidence);
        } else if (strcmp(key, "reason") == 0) {
            jp_parse_string(&jp, out->reason, sizeof(out->reason));
        } else {
            jp_skip_value(&jp);
        }
    }

    if (out->sentiment[0] == '\0') {
        set_error(err, AF_LLM_ERR_DECODE, 0, "missing sentiment field");
        return -1;
    }
    return 0;
}

int af_llm_strategy_describe(af_llm_provider_t *p,
                               const af_llm_strategy_describe_input_t *inp,
                               af_llm_strategy_describe_output_t *out,
                               af_llm_error_t *err)
{
    if (!p || !inp || !out || !err) return -1;
    af_llm_error_clear(err);

    /* Build CSV lists */
    char ind_csv[1024]  = "";
    char ent_csv[1024]  = "";
    char exit_csv[1024] = "";
    char risk_csv[1024] = "";

    for (int i = 0; i < inp->n_indicators_used; i++) {
        if (i > 0) strncat(ind_csv, ",", sizeof(ind_csv) - strlen(ind_csv) - 1);
        strncat(ind_csv, inp->indicators_used[i], sizeof(ind_csv) - strlen(ind_csv) - 1);
    }
    for (int i = 0; i < inp->n_entry_rules; i++) {
        if (i > 0) strncat(ent_csv, ",", sizeof(ent_csv) - strlen(ent_csv) - 1);
        strncat(ent_csv, inp->entry_rules[i], sizeof(ent_csv) - strlen(ent_csv) - 1);
    }
    for (int i = 0; i < inp->n_exit_rules; i++) {
        if (i > 0) strncat(exit_csv, ",", sizeof(exit_csv) - strlen(exit_csv) - 1);
        strncat(exit_csv, inp->exit_rules[i], sizeof(exit_csv) - strlen(exit_csv) - 1);
    }
    for (int i = 0; i < inp->n_risk_rules; i++) {
        if (i > 0) strncat(risk_csv, ",", sizeof(risk_csv) - strlen(risk_csv) - 1);
        strncat(risk_csv, inp->risk_rules[i], sizeof(risk_csv) - strlen(risk_csv) - 1);
    }

    char user_content[AF_LLM_MAX_CONTENT];
    snprintf(user_content, sizeof(user_content),
             "STRATEGY\n"
             "name=%s\n"
             "timeframe=%s\n"
             "indicators=%s\n"
             "entry_rules=%s\n"
             "exit_rules=%s\n"
             "risk_rules=%s\n"
             "\n"
             "Describe.",
             inp->name,
             inp->timeframe,
             ind_csv,
             ent_csv,
             exit_csv,
             risk_csv);

    af_llm_chat_request_t req;
    init_use_case_request(&req, p);
    req.n_messages = 2;
    safe_strncpy(req.messages[0].role, "system", sizeof(req.messages[0].role));
    safe_strncpy(req.messages[0].content, AF_PROMPT_STRATEGY_DESCRIBE_SYSTEM,
                 sizeof(req.messages[0].content));
    safe_strncpy(req.messages[1].role, "user", sizeof(req.messages[1].role));
    safe_strncpy(req.messages[1].content, user_content,
                 sizeof(req.messages[1].content));

    af_llm_chat_response_t resp;
    int rc = af_llm_chat(p, &req, &resp, err);
    if (rc != 0) return rc;

    safe_strncpy(out->description, resp.message.content, sizeof(out->description));
    safe_strncpy(out->model, resp.model, sizeof(out->model));
    out->tokens = resp.completion_tokens;
    return 0;
}
