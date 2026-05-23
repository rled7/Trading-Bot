/**
 * AlgoForge — c/tests/llm_mock.c
 *
 * Mock HTTP transport implementation.
 * Loads fixture JSON, validates requests, returns canned responses.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "llm_mock.h"

/* ============================================================
 * Fixture directory resolution
 * ============================================================ */

/*
 * The test binary is run from c/ (via make -C c test → ./build/af_tests).
 * Fixtures live at ../tests/fixtures/llm/<name>.json relative to c/.
 */
#define DEFAULT_FIXTURE_DIR "../tests/fixtures/llm"

static void fixture_path(const char *name, char *buf, size_t bufsz)
{
    const char *dir = getenv("AF_FIXTURE_DIR");
    if (!dir || !dir[0]) dir = DEFAULT_FIXTURE_DIR;
    snprintf(buf, bufsz, "%s/%s.json", dir, name);
}

/* ============================================================
 * Simple file reader
 * ============================================================ */

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[nread] = '\0';
    return buf;
}

/* ============================================================
 * Minimal JSON normaliser for request validation
 *
 * Strategy: parse both fixture body and actual body into a canonical
 * token stream, compare token-by-token.  This handles different key
 * ordering, whitespace differences, etc.
 *
 * For this test harness we use a simpler approach: we only need to check
 * that the bodies are semantically equivalent.  We normalise by:
 *   1. Removing all insignificant whitespace outside strings.
 *   2. Sorting object keys alphabetically at every level.
 *   3. Re-serialising.
 *
 * We implement a recursive sort-and-emit using a straightforward
 * parse-tree approach kept purely in C.
 * ============================================================ */

/* --- JSON value tree --- */

typedef enum {
    JV_NULL, JV_BOOL, JV_NUMBER, JV_STRING, JV_ARRAY, JV_OBJECT
} jvtype_t;

typedef struct jv jv_t;
typedef struct jv_kv jv_kv_t;

struct jv_kv {
    char   *key;
    jv_t   *val;
};

struct jv {
    jvtype_t  type;
    union {
        int       bool_val;
        double    num_val;
        char     *str_val;
        struct { jv_t     **items; int n; } arr;
        struct { jv_kv_t  *pairs; int n; } obj;
    } u;
};

static void jv_free(jv_t *v);

static void jv_free_kv(jv_kv_t *kv)
{
    free(kv->key);
    jv_free(kv->val);
}

static void jv_free(jv_t *v)
{
    if (!v) return;
    switch (v->type) {
        case JV_STRING: free(v->u.str_val); break;
        case JV_ARRAY:
            for (int i = 0; i < v->u.arr.n; i++) jv_free(v->u.arr.items[i]);
            free(v->u.arr.items);
            break;
        case JV_OBJECT:
            for (int i = 0; i < v->u.obj.n; i++) jv_free_kv(&v->u.obj.pairs[i]);
            free(v->u.obj.pairs);
            break;
        default: break;
    }
    free(v);
}

/* Parse state */
typedef struct {
    const char *src;
    size_t pos;
    size_t len;
} ps_t;

static void ps_skip(ps_t *p)
{
    while (p->pos < p->len &&
           (p->src[p->pos] == ' ' || p->src[p->pos] == '\t' ||
            p->src[p->pos] == '\n' || p->src[p->pos] == '\r'))
        p->pos++;
}

static char ps_peek(ps_t *p) {
    ps_skip(p);
    return p->pos < p->len ? p->src[p->pos] : '\0';
}

static jv_t *jv_parse(ps_t *p);

static char *ps_parse_str(ps_t *p)
{
    ps_skip(p);
    if (p->pos >= p->len || p->src[p->pos] != '"') return NULL;
    p->pos++;
    size_t start = p->pos;
    size_t cap = 64, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    while (p->pos < p->len) {
        char c = p->src[p->pos++];
        if (c == '"') { buf[len] = '\0'; return buf; }
        if (c == '\\') {
            if (p->pos >= p->len) break;
            char e = p->src[p->pos++];
            if (e == 'u') {
                /* Decode \uXXXX into UTF-8 (BMP only; surrogate pairs would need more work) */
                if (p->pos + 4 > p->len) break;
                unsigned cp = 0;
                for (int i = 0; i < 4; i++) {
                    char h = p->src[p->pos++];
                    cp <<= 4;
                    if      (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                }
                char utf8[4]; int n;
                if (cp < 0x80)        { utf8[0] = (char)cp; n = 1; }
                else if (cp < 0x800)  { utf8[0] = (char)(0xC0 | (cp >> 6));   utf8[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
                else                  { utf8[0] = (char)(0xE0 | (cp >> 12));  utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); utf8[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
                while (len + (size_t)n + 1 >= cap) { cap*=2; char*nb=(char*)realloc(buf,cap); if(!nb){free(buf);return NULL;} buf=nb; }
                for (int i = 0; i < n; i++) buf[len++] = utf8[i];
                continue;
            }
            char d;
            switch (e) {
                case '"': d='"'; break; case '\\': d='\\'; break;
                case '/': d='/'; break; case 'n': d='\n'; break;
                case 'r': d='\r'; break; case 't': d='\t'; break;
                case 'b': d='\b'; break; case 'f': d='\f'; break;
                default: d=e; break;
            }
            if (len + 1 >= cap) { cap*=2; char*nb=(char*)realloc(buf,cap); if(!nb){free(buf);return NULL;} buf=nb; }
            buf[len++] = d;
        } else {
            if (len + 1 >= cap) { cap*=2; char*nb=(char*)realloc(buf,cap); if(!nb){free(buf);return NULL;} buf=nb; }
            buf[len++] = c;
        }
    }
    (void)start;
    free(buf);
    return NULL;
}

static jv_t *jv_parse(ps_t *p)
{
    char c = ps_peek(p);
    jv_t *v = (jv_t *)calloc(1, sizeof(jv_t));
    if (!v) return NULL;

    if (c == '"') {
        v->type = JV_STRING;
        v->u.str_val = ps_parse_str(p);
        if (!v->u.str_val) { free(v); return NULL; }
        return v;
    }
    if (c == '{') {
        v->type = JV_OBJECT;
        p->pos++; /* consume '{' */
        ps_skip(p);
        if (ps_peek(p) == '}') { p->pos++; return v; }
        size_t cap = 8;
        v->u.obj.pairs = (jv_kv_t *)malloc(cap * sizeof(jv_kv_t));
        if (!v->u.obj.pairs) { free(v); return NULL; }
        for (;;) {
            ps_skip(p);
            char *key = ps_parse_str(p);
            if (!key) { jv_free(v); return NULL; }
            ps_skip(p);
            if (p->pos >= p->len || p->src[p->pos] != ':') { free(key); jv_free(v); return NULL; }
            p->pos++;
            jv_t *val = jv_parse(p);
            if (!val) { free(key); jv_free(v); return NULL; }
            if ((size_t)v->u.obj.n >= cap) {
                cap *= 2;
                jv_kv_t *nb = (jv_kv_t *)realloc(v->u.obj.pairs, cap*sizeof(jv_kv_t));
                if (!nb) { free(key); jv_free(val); jv_free(v); return NULL; }
                v->u.obj.pairs = nb;
            }
            v->u.obj.pairs[v->u.obj.n].key = key;
            v->u.obj.pairs[v->u.obj.n].val = val;
            v->u.obj.n++;
            ps_skip(p);
            if (p->pos >= p->len) break;
            if (p->src[p->pos] == '}') { p->pos++; break; }
            if (p->src[p->pos] == ',') { p->pos++; continue; }
            jv_free(v); return NULL;
        }
        return v;
    }
    if (c == '[') {
        v->type = JV_ARRAY;
        p->pos++;
        ps_skip(p);
        if (ps_peek(p) == ']') { p->pos++; return v; }
        size_t cap = 8;
        v->u.arr.items = (jv_t **)malloc(cap * sizeof(jv_t *));
        if (!v->u.arr.items) { free(v); return NULL; }
        for (;;) {
            jv_t *item = jv_parse(p);
            if (!item) { jv_free(v); return NULL; }
            if ((size_t)v->u.arr.n >= cap) {
                cap *= 2;
                jv_t **nb = (jv_t **)realloc(v->u.arr.items, cap*sizeof(jv_t*));
                if (!nb) { jv_free(item); jv_free(v); return NULL; }
                v->u.arr.items = nb;
            }
            v->u.arr.items[v->u.arr.n++] = item;
            ps_skip(p);
            if (p->pos >= p->len) break;
            if (p->src[p->pos] == ']') { p->pos++; break; }
            if (p->src[p->pos] == ',') { p->pos++; continue; }
            jv_free(v); return NULL;
        }
        return v;
    }
    if (strncmp(p->src + p->pos, "null", 4) == 0)  { p->pos+=4; v->type=JV_NULL; return v; }
    if (strncmp(p->src + p->pos, "true", 4) == 0)  { p->pos+=4; v->type=JV_BOOL; v->u.bool_val=1; return v; }
    if (strncmp(p->src + p->pos, "false", 5) == 0) { p->pos+=5; v->type=JV_BOOL; v->u.bool_val=0; return v; }
    if (c == '-' || isdigit((unsigned char)c)) {
        v->type = JV_NUMBER;
        char *end = NULL;
        v->u.num_val = strtod(p->src + p->pos, &end);
        if (end == p->src + p->pos) { free(v); return NULL; }
        p->pos = (size_t)(end - p->src);
        return v;
    }
    free(v);
    return NULL;
}

/* Sort object keys — insertion sort */
static void jv_sort_obj(jv_t *v)
{
    if (!v || v->type != JV_OBJECT) return;
    /* Sort children first */
    for (int i = 0; i < v->u.obj.n; i++) jv_sort_obj(v->u.obj.pairs[i].val);
    /* Sort pairs */
    for (int i = 1; i < v->u.obj.n; i++) {
        jv_kv_t tmp = v->u.obj.pairs[i];
        int j = i - 1;
        while (j >= 0 && strcmp(v->u.obj.pairs[j].key, tmp.key) > 0) {
            v->u.obj.pairs[j+1] = v->u.obj.pairs[j];
            j--;
        }
        v->u.obj.pairs[j+1] = tmp;
    }
    /* Also sort arrays recursively */
    for (int i = 0; i < v->u.obj.n; i++) {
        jv_t *child = v->u.obj.pairs[i].val;
        if (child && child->type == JV_ARRAY) {
            for (int k = 0; k < child->u.arr.n; k++) jv_sort_obj(child->u.arr.items[k]);
        }
    }
}

/* Emit JSON value to a string buffer (growable) */
typedef struct { char *buf; size_t len; size_t cap; } sbuf_t;

static int sb_append(sbuf_t *sb, const char *s)
{
    size_t n = strlen(s);
    if (sb->len + n + 1 > sb->cap) {
        size_t nc = sb->cap ? sb->cap * 2 : 512;
        while (nc < sb->len + n + 1) nc *= 2;
        char *nb = (char *)realloc(sb->buf, nc);
        if (!nb) return -1;
        sb->buf = nb; sb->cap = nc;
    }
    memcpy(sb->buf + sb->len, s, n+1);
    sb->len += n;
    return 0;
}

static int sb_appendc(sbuf_t *sb, char c)
{
    char tmp[2] = { c, '\0' };
    return sb_append(sb, tmp);
}

static int jv_emit(const jv_t *v, sbuf_t *sb)
{
    if (!v) return sb_append(sb, "null");
    switch (v->type) {
        case JV_NULL: return sb_append(sb, "null");
        case JV_BOOL: return sb_append(sb, v->u.bool_val ? "true" : "false");
        case JV_NUMBER: {
            char tmp[64];
            /* Use integer formatting when the value is a whole number to avoid
             * %g's 6-significant-digit truncation (e.g. 1234567890 → 1.23457e+09). */
            double d = v->u.num_val;
            if (d == (long long)d && d >= -9e18 && d <= 9e18) {
                snprintf(tmp, sizeof(tmp), "%lld", (long long)d);
            } else {
                snprintf(tmp, sizeof(tmp), "%.17g", d);
            }
            return sb_append(sb, tmp);
        }
        case JV_STRING: {
            if (sb_appendc(sb, '"') != 0) return -1;
            const char *s = v->u.str_val;
            for (; s && *s; s++) {
                unsigned char ch = (unsigned char)*s;
                if (ch == '"') { if (sb_append(sb,"\\\"")!=0) return -1; }
                else if (ch == '\\') { if (sb_append(sb,"\\\\")!=0) return -1; }
                else if (ch == '\n') { if (sb_append(sb,"\\n")!=0) return -1; }
                else if (ch == '\r') { if (sb_append(sb,"\\r")!=0) return -1; }
                else if (ch == '\t') { if (sb_append(sb,"\\t")!=0) return -1; }
                else if (ch < 0x20) {
                    char esc[8]; snprintf(esc,sizeof(esc),"\\u%04x",ch);
                    if (sb_append(sb,esc)!=0) return -1;
                } else {
                    if (sb_appendc(sb,(char)ch)!=0) return -1;
                }
            }
            return sb_appendc(sb, '"');
        }
        case JV_ARRAY: {
            if (sb_appendc(sb,'[')!=0) return -1;
            for (int i=0;i<v->u.arr.n;i++) {
                if (i>0 && sb_appendc(sb,',')!=0) return -1;
                if (jv_emit(v->u.arr.items[i], sb)!=0) return -1;
            }
            return sb_appendc(sb,']');
        }
        case JV_OBJECT: {
            if (sb_appendc(sb,'{')!=0) return -1;
            for (int i=0;i<v->u.obj.n;i++) {
                if (i>0 && sb_appendc(sb,',')!=0) return -1;
                /* emit key */
                if (sb_appendc(sb,'"')!=0) return -1;
                const char *k = v->u.obj.pairs[i].key;
                for (; k && *k; k++) {
                    if (*k == '"' || *k == '\\') sb_appendc(sb,'\\');
                    sb_appendc(sb,*k);
                }
                if (sb_appendc(sb,'"')!=0) return -1;
                if (sb_append(sb,":")!=0) return -1;
                if (jv_emit(v->u.obj.pairs[i].val, sb)!=0) return -1;
            }
            return sb_appendc(sb,'}');
        }
    }
    return -1;
}

/* Normalise a JSON string: parse, sort keys, re-emit */
static char *json_normalise(const char *src)
{
    if (!src || strcmp(src, "null") == 0) return strdup("null");
    ps_t p; p.src = src; p.pos = 0; p.len = strlen(src);
    jv_t *v = jv_parse(&p);
    if (!v) return strdup("");
    jv_sort_obj(v);
    sbuf_t sb = { NULL, 0, 0 };
    if (jv_emit(v, &sb) != 0) { jv_free(v); free(sb.buf); return strdup(""); }
    jv_free(v);
    return sb.buf;
}

/* ============================================================
 * Fixture loading
 * ============================================================ */

typedef struct {
    char *method;          /* request.method */
    char *path;            /* request.path */
    char *req_body_norm;   /* normalised request.body (NULL if no body) */
    char *resp_body;       /* response body as a string */
    int   resp_status;
    int   loaded;          /* 1 if fixture loaded OK */
    char  load_error[256];
} mock_ctx_t;

static void mock_ctx_free(mock_ctx_t *ctx)
{
    free(ctx->method);
    free(ctx->path);
    free(ctx->req_body_norm);
    free(ctx->resp_body);
    free(ctx);
}

/* Parse top-level fixture JSON and populate mock_ctx */
static mock_ctx_t *load_fixture(const char *fixture_name)
{
    char path[512];
    fixture_path(fixture_name, path, sizeof(path));

    char *src = read_file(path);
    if (!src) {
        mock_ctx_t *ctx = (mock_ctx_t *)calloc(1, sizeof(mock_ctx_t));
        snprintf(ctx->load_error, sizeof(ctx->load_error),
                 "cannot open fixture: %s", path);
        return ctx;
    }

    mock_ctx_t *ctx = (mock_ctx_t *)calloc(1, sizeof(mock_ctx_t));
    if (!ctx) { free(src); return NULL; }

    ps_t p; p.src = src; p.pos = 0; p.len = strlen(src);
    jv_t *root = jv_parse(&p);
    if (!root) {
        snprintf(ctx->load_error, sizeof(ctx->load_error),
                 "failed to parse fixture JSON: %s", path);
        free(src);
        return ctx;
    }

    /* Extract request.method, request.path, request.body */
    for (int i = 0; i < root->u.obj.n; i++) {
        jv_kv_t *kv = &root->u.obj.pairs[i];
        if (strcmp(kv->key, "request") == 0 && kv->val->type == JV_OBJECT) {
            jv_t *req = kv->val;
            for (int j = 0; j < req->u.obj.n; j++) {
                jv_kv_t *rkv = &req->u.obj.pairs[j];
                if (strcmp(rkv->key, "method") == 0 && rkv->val->type == JV_STRING) {
                    ctx->method = strdup(rkv->val->u.str_val);
                } else if (strcmp(rkv->key, "path") == 0 && rkv->val->type == JV_STRING) {
                    ctx->path = strdup(rkv->val->u.str_val);
                } else if (strcmp(rkv->key, "body") == 0) {
                    if (rkv->val->type == JV_NULL) {
                        ctx->req_body_norm = NULL;
                    } else {
                        /* Normalise for comparison */
                        jv_sort_obj(rkv->val);
                        sbuf_t sb = { NULL, 0, 0 };
                        jv_emit(rkv->val, &sb);
                        ctx->req_body_norm = sb.buf;
                    }
                }
            }
        } else if (strcmp(kv->key, "response") == 0 && kv->val->type == JV_OBJECT) {
            jv_t *resp = kv->val;
            for (int j = 0; j < resp->u.obj.n; j++) {
                jv_kv_t *rkv = &resp->u.obj.pairs[j];
                if (strcmp(rkv->key, "status") == 0 && rkv->val->type == JV_NUMBER) {
                    ctx->resp_status = (int)rkv->val->u.num_val;
                } else if (strcmp(rkv->key, "body") == 0) {
                    if (rkv->val->type == JV_ARRAY) {
                        /* Streaming: join array elements as NDJSON */
                        sbuf_t sb = { NULL, 0, 0 };
                        for (int k = 0; k < rkv->val->u.arr.n; k++) {
                            jv_t *elem = rkv->val->u.arr.items[k];
                            sbuf_t line = { NULL, 0, 0 };
                            jv_emit(elem, &line);
                            sb_append(&sb, line.buf);
                            sb_appendc(&sb, '\n');
                            free(line.buf);
                        }
                        ctx->resp_body = sb.buf;
                    } else {
                        /* Non-streaming: emit JSON object */
                        sbuf_t sb = { NULL, 0, 0 };
                        jv_emit(rkv->val, &sb);
                        ctx->resp_body = sb.buf;
                    }
                }
            }
        }
    }

    jv_free(root);
    free(src);
    ctx->loaded = 1;
    return ctx;
}

/* ============================================================
 * Mock transport request function
 * ============================================================ */

static int mock_request(void *ctx_ptr, const char *method, const char *path,
                         const char *body, char **resp_body,
                         int *resp_status, af_llm_error_t *err)
{
    mock_ctx_t *ctx = (mock_ctx_t *)ctx_ptr;

    if (!ctx->loaded) {
        snprintf(err->kind, sizeof(err->kind), "%s", AF_LLM_ERR_UNREACHABLE);
        snprintf(err->message, sizeof(err->message),
                 "fixture load error: %s", ctx->load_error);
        err->status = 0;
        return -1;
    }

    /* Validate method */
    if (ctx->method && strcmp(method, ctx->method) != 0) {
        fprintf(stderr, "MOCK: method mismatch: expected '%s' got '%s'\n",
                ctx->method, method);
        /* Still return the fixture response — soft mismatch */
    }

    /* Validate path */
    if (ctx->path && strcmp(path, ctx->path) != 0) {
        fprintf(stderr, "MOCK: path mismatch: expected '%s' got '%s'\n",
                ctx->path, path);
    }

    /* Validate body (if fixture has a body) */
    if (ctx->req_body_norm && body) {
        char *norm = json_normalise(body);
        if (norm && strcmp(norm, ctx->req_body_norm) != 0) {
            fprintf(stderr, "MOCK: body mismatch\n  expected: %s\n  got:      %s\n",
                    ctx->req_body_norm, norm);
        }
        free(norm);
    }

    /* Return fixture response */
    *resp_body  = ctx->resp_body ? strdup(ctx->resp_body) : strdup("");
    *resp_status = ctx->resp_status;

    /* Clear error */
    err->kind[0]    = '\0';
    err->message[0] = '\0';
    err->status     = 0;

    return 0;
}

static void mock_destroy(void *ctx_ptr)
{
    mock_ctx_free((mock_ctx_t *)ctx_ptr);
}

af_llm_transport_t af_llm_mock_transport_create(const char *fixture_name)
{
    mock_ctx_t *ctx = load_fixture(fixture_name);
    af_llm_transport_t t;
    t.request = mock_request;
    t.destroy = mock_destroy;
    t.ctx     = ctx;
    return t;
}

void af_llm_mock_transport_free(af_llm_transport_t *t)
{
    if (!t) return;
    if (t->destroy) t->destroy(t->ctx);
    t->ctx = NULL;
}

/* ============================================================
 * Special-purpose mock transports
 * ============================================================ */

static int req_503(void *ctx, const char *method, const char *path,
                    const char *body, char **resp_body,
                    int *resp_status, af_llm_error_t *err)
{
    (void)ctx; (void)method; (void)path; (void)body;
    *resp_body   = strdup("{\"error\":\"service unavailable\"}");
    *resp_status = 503;
    err->kind[0] = err->message[0] = '\0';
    err->status  = 0;
    return 0;
}

af_llm_transport_t af_llm_mock_transport_create_503(void)
{
    af_llm_transport_t t;
    t.request = req_503;
    t.destroy = NULL;
    t.ctx     = NULL;
    return t;
}

static int req_unreachable(void *ctx, const char *method, const char *path,
                             const char *body, char **resp_body,
                             int *resp_status, af_llm_error_t *err)
{
    (void)ctx; (void)method; (void)path; (void)body;
    *resp_body  = NULL;
    *resp_status = 0;
    snprintf(err->kind,    sizeof(err->kind),    "%s", AF_LLM_ERR_UNREACHABLE);
    snprintf(err->message, sizeof(err->message), "connection refused");
    err->status = 0;
    return -1;
}

af_llm_transport_t af_llm_mock_transport_create_unreachable(void)
{
    af_llm_transport_t t;
    t.request = req_unreachable;
    t.destroy = NULL;
    t.ctx     = NULL;
    return t;
}

static int req_bad_json(void *ctx, const char *method, const char *path,
                          const char *body, char **resp_body,
                          int *resp_status, af_llm_error_t *err)
{
    (void)ctx; (void)method; (void)path; (void)body;
    /* Return a chat-like response but with garbage content for sentiment */
    *resp_body = strdup(
        "{\"model\":\"llama3.1:8b\","
        "\"message\":{\"role\":\"assistant\","
        "\"content\":\"this is not json at all no curly braces\"},"
        "\"done\":true,\"done_reason\":\"stop\","
        "\"prompt_eval_count\":5,\"eval_count\":10,\"total_duration\":100}");
    *resp_status = 200;
    err->kind[0] = err->message[0] = '\0';
    err->status  = 0;
    return 0;
}

af_llm_transport_t af_llm_mock_transport_create_bad_json(void)
{
    af_llm_transport_t t;
    t.request = req_bad_json;
    t.destroy = NULL;
    t.ctx     = NULL;
    return t;
}
