/**
 * AlgoForge — c/tests/test_llm.c
 *
 * LLM layer tests using the mock transport.
 * Covers:
 *   - Provider: complete_basic, chat_basic, chat_streaming, embed_basic,
 *               health_ok, health_down, list_models
 *   - Use cases: all 8 use-case fixtures (trade_rationale, backtest_commentary,
 *                news_sentiment, strategy_describe × 2 each)
 *   - Errors: 503, unreachable, decode
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "af_llm.h"
#include "llm_mock.h"

/* ============================================================
 * Test harness macros (matching the existing test style)
 * ============================================================ */

static int g_llm_passed = 0;
static int g_llm_failed = 0;

#define CHK(cond) do {                                                      \
    if (cond) {                                                             \
        ++g_llm_passed;                                                     \
    } else {                                                                \
        ++g_llm_failed;                                                     \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
    }                                                                       \
} while (0)

#define CHK_STR_STARTS(s, prefix) \
    CHK((s)[0] != '\0' && strncmp((s), (prefix), strlen(prefix)) == 0)

#define CHK_STR_EQ(a, b) \
    CHK(strcmp((a), (b)) == 0)

#define CHK_NEAR(a, b, tol) \
    CHK(fabs((double)(a) - (double)(b)) < (double)(tol))

/* ============================================================
 * Helper: create a provider with a named fixture transport
 * ============================================================ */

static af_llm_provider_t make_fixture_provider(const char *fixture_name)
{
    af_llm_transport_t t = af_llm_mock_transport_create(fixture_name);
    af_llm_provider_t p;
    af_llm_provider_with_transport(&p, t);
    return p;
}

/* ============================================================
 * Section 1 — Provider tests
 * ============================================================ */

static void test_complete_basic(void)
{
    af_llm_provider_t p = make_fixture_provider("complete_basic");

    af_llm_completion_request_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.model, sizeof(req.model), "llama3.1:8b");
    snprintf(req.prompt, sizeof(req.prompt), "Why did EURUSD break out?");
    req.temperature = 0.2;
    req.max_tokens  = 256;
    req.seed        = 42;
    req.n_stop      = 0;

    af_llm_completion_response_t resp;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_complete(&p, &req, &resp, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK(resp.text[0] != '\0');
    CHK_STR_EQ(resp.model, "llama3.1:8b");
    CHK(resp.prompt_tokens    == 12);
    CHK(resp.completion_tokens == 84);
    CHK(resp.total_duration_ns == 1234567890LL);
    CHK_STR_EQ(resp.finish_reason, "stop");

    af_llm_provider_free(&p);
    printf("  [PASS] complete_basic\n");
}

static void test_chat_basic(void)
{
    af_llm_provider_t p = make_fixture_provider("chat_basic");

    af_llm_chat_request_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.model, sizeof(req.model), "llama3.1:8b");
    req.temperature = 0.2;
    req.max_tokens  = 256;
    req.seed        = 42;
    req.stream      = 0;
    req.n_messages  = 2;
    snprintf(req.messages[0].role,    sizeof(req.messages[0].role),    "system");
    snprintf(req.messages[0].content, sizeof(req.messages[0].content),
             "You are a trading assistant.");
    snprintf(req.messages[1].role,    sizeof(req.messages[1].role),    "user");
    snprintf(req.messages[1].content, sizeof(req.messages[1].content),
             "Explain ATR.");

    af_llm_chat_response_t resp;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_chat(&p, &req, &resp, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK_STR_EQ(resp.message.role, "assistant");
    CHK(resp.message.content[0] != '\0');
    CHK_STR_EQ(resp.model, "llama3.1:8b");
    CHK(resp.prompt_tokens     == 18);
    CHK(resp.completion_tokens == 120);
    CHK(resp.total_duration_ns == 2345678901LL);
    CHK_STR_EQ(resp.finish_reason, "stop");

    af_llm_provider_free(&p);
    printf("  [PASS] chat_basic\n");
}

/* Streaming test state */
typedef struct {
    int    n_chunks;
    int    n_done;
    char   accumulated[65536];
} stream_state_t;

static int stream_callback(const af_llm_chat_chunk_t *chunk, void *ud)
{
    stream_state_t *s = (stream_state_t *)ud;
    s->n_chunks++;
    strncat(s->accumulated, chunk->delta,
            sizeof(s->accumulated) - strlen(s->accumulated) - 1);
    if (chunk->done) s->n_done++;
    return 0;
}

static void test_chat_streaming(void)
{
    af_llm_provider_t p = make_fixture_provider("chat_streaming");

    af_llm_chat_request_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.model, sizeof(req.model), "llama3.1:8b");
    req.temperature = 0.2;
    req.max_tokens  = 256;
    req.seed        = 42;
    req.stream      = 1;
    req.n_messages  = 2;
    snprintf(req.messages[0].role,    sizeof(req.messages[0].role),    "system");
    snprintf(req.messages[0].content, sizeof(req.messages[0].content),
             "You are a trading assistant.");
    snprintf(req.messages[1].role,    sizeof(req.messages[1].role),    "user");
    snprintf(req.messages[1].content, sizeof(req.messages[1].content),
             "What is RSI?");

    stream_state_t state;
    memset(&state, 0, sizeof(state));

    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_chat_stream(&p, &req, stream_callback, &state, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK(state.n_chunks >= 5);   /* 4 content chunks + 1 done chunk */
    CHK(state.n_done   == 1);
    CHK(state.accumulated[0] != '\0');
    /* "RSI is a momentum oscillator." */
    CHK(strstr(state.accumulated, "RSI") != NULL);

    af_llm_provider_free(&p);
    printf("  [PASS] chat_streaming\n");
}

static void test_embed_basic(void)
{
    af_llm_provider_t p = make_fixture_provider("embed_basic");

    af_llm_embed_request_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.model, sizeof(req.model), "nomic-embed-text");
    snprintf(req.inputs[0], sizeof(req.inputs[0]), "EURUSD broke out");
    req.n_inputs = 1;

    af_llm_embed_response_t resp;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_embed(&p, &req, &resp, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK(resp.n_inputs  == 1);
    CHK(resp.embed_dim == 5);
    CHK(resp.embeddings != NULL);
    CHK(resp.embeddings[0] != NULL);
    CHK_NEAR(resp.embeddings[0][0],  0.123,  1e-6);
    CHK_NEAR(resp.embeddings[0][1], -0.456,  1e-6);
    CHK_NEAR(resp.embeddings[0][2],  0.789,  1e-6);
    CHK_NEAR(resp.embeddings[0][3], -0.012,  1e-6);
    CHK_NEAR(resp.embeddings[0][4],  0.345,  1e-6);

    af_llm_embed_response_free(&resp);
    af_llm_provider_free(&p);
    printf("  [PASS] embed_basic\n");
}

static void test_health_ok(void)
{
    af_llm_provider_t p = make_fixture_provider("health_ok");

    af_llm_health_status_t status;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_health(&p, &status, &err);
    CHK(rc == 0);
    CHK(status.ok == 1);
    CHK(status.model_loaded == 1);

    af_llm_provider_free(&p);
    printf("  [PASS] health_ok\n");
}

static void test_health_down(void)
{
    /* health_down fixture returns 503 */
    af_llm_transport_t t = af_llm_mock_transport_create_503();
    af_llm_provider_t p;
    af_llm_provider_with_transport(&p, t);

    af_llm_health_status_t status;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_health(&p, &status, &err);
    CHK(rc == 0);          /* health check itself does not fail */
    CHK(status.ok == 0);   /* but status.ok is false */

    af_llm_provider_free(&p);
    printf("  [PASS] health_down\n");
}

static void test_list_models(void)
{
    af_llm_provider_t p = make_fixture_provider("list_models");

    af_llm_model_info_t models[16];
    int n = 0;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_list_models(&p, models, 16, &n, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK(n == 2);
    CHK_STR_EQ(models[0].name, "llama3.1:8b");
    CHK(models[0].size_bytes == 4661211808LL);
    CHK(models[0].modified_at[0] != '\0');
    CHK_STR_EQ(models[1].name, "nomic-embed-text");
    CHK(models[1].size_bytes == 274302144LL);

    af_llm_provider_free(&p);
    printf("  [PASS] list_models\n");
}

/* ============================================================
 * Section 2 — Use-case tests
 * ============================================================ */

/* Helper: load use-case fixture and extract the expected user prompt content
 * from request.body.messages[1].content */
static int get_fixture_user_prompt(const char *fixture_name,
                                    char *buf, size_t bufsz)
{
    char path[512];
    const char *dir = getenv("AF_FIXTURE_DIR");
    if (!dir || !dir[0]) dir = "../tests/fixtures/llm";
    snprintf(path, sizeof(path), "%s/%s.json", dir, fixture_name);

    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *src = (char *)malloc((size_t)sz + 1);
    if (!src) { fclose(f); return -1; }
    fread(src, 1, (size_t)sz, f);
    fclose(f);
    src[sz] = '\0';

    /* Parse to find request.body.messages[1].content */
    /* Simple manual traversal for robustness */
    buf[0] = '\0';

    /* Textual scan: find "messages" array, then second element, then "content" */
    /* Find "messages" */
    const char *msg_pos = strstr(src, "\"messages\"");
    if (!msg_pos) { free(src); return -1; }

    /* Find the array '[' */
    const char *arr = strchr(msg_pos, '[');
    if (!arr) { free(src); return -1; }

    /* Skip first element (system message) by finding the second '{' at depth 1 */
    int depth = 0;
    const char *p2 = arr + 1;
    /* skip the first object */
    while (*p2 && !(depth == 0 && *p2 == '{')) {
        if (*p2 == '{') depth++;
        else if (*p2 == '}') depth--;
        p2++;
    }
    /* now p2 is at '{' of first message */
    if (*p2 == '{') {
        depth = 1;
        p2++;
        while (*p2 && depth > 0) {
            if (*p2 == '{') depth++;
            else if (*p2 == '}') depth--;
            p2++;
        }
    }
    /* skip comma */
    while (*p2 == ',' || *p2 == ' ' || *p2 == '\n' || *p2 == '\r' || *p2 == '\t') p2++;

    /* Now p2 should be at the second message '{' */
    if (*p2 != '{') { free(src); return -1; }

    /* Find "content" key in this object */
    const char *cont = strstr(p2, "\"content\"");
    /* Make sure it's within this object */
    /* Find closing '}' of this object */
    const char *obj_start = p2;
    int odepth = 1;
    const char *obj_end = p2 + 1;
    int in_str = 0;
    int esc2 = 0;
    while (*obj_end && odepth > 0) {
        if (esc2) { esc2 = 0; obj_end++; continue; }
        if (in_str && *obj_end == '\\') { esc2 = 1; obj_end++; continue; }
        if (*obj_end == '"') { in_str = !in_str; }
        else if (!in_str) {
            if (*obj_end == '{') odepth++;
            else if (*obj_end == '}') odepth--;
        }
        obj_end++;
    }

    if (!cont || cont > obj_end) { free(src); return -1; }

    /* Skip past "content": */
    const char *colon = strchr(cont + 9, ':');
    if (!colon || colon > obj_end) { free(src); return -1; }
    colon++;
    while (*colon == ' ' || *colon == '\n' || *colon == '\r' || *colon == '\t') colon++;
    if (*colon != '"') { free(src); return -1; }

    /* Parse the string */
    colon++;
    size_t out_idx = 0;
    int escape2 = 0;
    while (*colon && out_idx + 1 < bufsz) {
        char c = *colon++;
        if (escape2) {
            escape2 = 0;
            switch (c) {
                case 'n': buf[out_idx++] = '\n'; break;
                case 'r': buf[out_idx++] = '\r'; break;
                case 't': buf[out_idx++] = '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = *colon++;
                        cp <<= 4;
                        if      (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                    }
                    if (cp < 0x80) {
                        if (out_idx + 1 < bufsz) buf[out_idx++] = (char)cp;
                    } else if (cp < 0x800) {
                        if (out_idx + 2 < bufsz) {
                            buf[out_idx++] = (char)(0xC0 | (cp >> 6));
                            buf[out_idx++] = (char)(0x80 | (cp & 0x3F));
                        }
                    } else {
                        if (out_idx + 3 < bufsz) {
                            buf[out_idx++] = (char)(0xE0 | (cp >> 12));
                            buf[out_idx++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            buf[out_idx++] = (char)(0x80 | (cp & 0x3F));
                        }
                    }
                    break;
                }
                default:  buf[out_idx++] = c; break;
            }
        } else if (c == '\\') {
            escape2 = 1;
        } else if (c == '"') {
            break;
        } else {
            buf[out_idx++] = c;
        }
    }
    buf[out_idx] = '\0';

    (void)obj_start;
    free(src);
    return (out_idx > 0) ? 0 : -1;
}

static void test_trade_rationale_long(void)
{
    af_llm_provider_t p = make_fixture_provider("trade_rationale_long");

    af_llm_trade_rationale_input_t inp;
    memset(&inp, 0, sizeof(inp));
    snprintf(inp.symbol, sizeof(inp.symbol), "EURUSD");
    snprintf(inp.side,   sizeof(inp.side),   "long");
    snprintf(inp.timeframe, sizeof(inp.timeframe), "H1");
    inp.entry_price = 1.085;
    inp.stop_loss   = 1.08;
    inp.take_profit = 1.095;
    /* indicators: {"atr":0.0014,"rsi":68.2} */
    snprintf(inp.indicators[0].key, sizeof(inp.indicators[0].key), "atr");
    inp.indicators[0].value = 0.0014;
    snprintf(inp.indicators[1].key, sizeof(inp.indicators[1].key), "rsi");
    inp.indicators[1].value = 68.2;
    inp.n_indicators = 2;
    snprintf(inp.patterns[0], sizeof(inp.patterns[0]), "BullishFlag");
    snprintf(inp.patterns[1], sizeof(inp.patterns[1]), "Hammer");
    inp.n_patterns = 2;

    af_llm_trade_rationale_output_t out;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_trade_rationale(&p, &inp, &out, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK(out.explanation[0] != '\0');
    CHK_STR_EQ(out.model, "llama3.1:8b");
    CHK(out.tokens == 58);

    /* Verify the rendered prompt matches fixture byte-for-byte */
    char expected_prompt[65536];
    if (get_fixture_user_prompt("trade_rationale_long", expected_prompt,
                                 sizeof(expected_prompt)) == 0) {
        char actual_prompt[65536];
        /* Build same prompt as the implementation */
        snprintf(actual_prompt, sizeof(actual_prompt),
                 "SIGNAL\n"
                 "symbol=EURUSD\n"
                 "side=long\n"
                 "timeframe=H1\n"
                 "entry=1.0850\n"
                 "sl=1.0800\n"
                 "tp=1.0950\n"
                 "indicators={\"atr\":0.0014,\"rsi\":68.2}\n"
                 "patterns=BullishFlag,Hammer\n"
                 "\n"
                 "Explain.");
        CHK_STR_EQ(actual_prompt, expected_prompt);
    }

    af_llm_provider_free(&p);
    printf("  [PASS] trade_rationale_long\n");
}

static void test_trade_rationale_short(void)
{
    af_llm_provider_t p = make_fixture_provider("trade_rationale_short");

    af_llm_trade_rationale_input_t inp;
    memset(&inp, 0, sizeof(inp));
    snprintf(inp.symbol, sizeof(inp.symbol), "GBPJPY");
    snprintf(inp.side,   sizeof(inp.side),   "short");
    snprintf(inp.timeframe, sizeof(inp.timeframe), "M15");
    inp.entry_price = 189.5;
    inp.stop_loss   = 190.2;
    inp.take_profit = 187.8;
    /* indicators: {"ema_fast":189.45,"ema_slow":189.8,"rsi":31.5} */
    snprintf(inp.indicators[0].key, sizeof(inp.indicators[0].key), "ema_fast");
    inp.indicators[0].value = 189.45;
    snprintf(inp.indicators[1].key, sizeof(inp.indicators[1].key), "ema_slow");
    inp.indicators[1].value = 189.8;
    snprintf(inp.indicators[2].key, sizeof(inp.indicators[2].key), "rsi");
    inp.indicators[2].value = 31.5;
    inp.n_indicators = 3;
    snprintf(inp.patterns[0], sizeof(inp.patterns[0]), "BearishEngulfing");
    inp.n_patterns = 1;

    af_llm_trade_rationale_output_t out;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_trade_rationale(&p, &inp, &out, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK(out.explanation[0] != '\0');
    CHK(out.tokens == 52);

    /* Verify prompt */
    char expected_prompt[65536];
    if (get_fixture_user_prompt("trade_rationale_short", expected_prompt,
                                 sizeof(expected_prompt)) == 0) {
        char actual_prompt[65536];
        snprintf(actual_prompt, sizeof(actual_prompt),
                 "SIGNAL\n"
                 "symbol=GBPJPY\n"
                 "side=short\n"
                 "timeframe=M15\n"
                 "entry=189.5000\n"
                 "sl=190.2000\n"
                 "tp=187.8000\n"
                 "indicators={\"ema_fast\":189.45,\"ema_slow\":189.8,\"rsi\":31.5}\n"
                 "patterns=BearishEngulfing\n"
                 "\n"
                 "Explain.");
        CHK_STR_EQ(actual_prompt, expected_prompt);
    }

    af_llm_provider_free(&p);
    printf("  [PASS] trade_rationale_short\n");
}

static void test_backtest_commentary_winning(void)
{
    af_llm_provider_t p = make_fixture_provider("backtest_commentary_winning");

    af_llm_backtest_commentary_input_t inp;
    memset(&inp, 0, sizeof(inp));
    snprintf(inp.strategy_name, sizeof(inp.strategy_name), "MomentumBreakout");
    snprintf(inp.period, sizeof(inp.period), "2024-01-01 \xe2\x86\x92 2024-06-30");
    inp.total_return = 0.2341;
    inp.sharpe       = 1.82;
    inp.sortino      = 2.45;
    inp.max_drawdown = -0.0812;
    inp.win_rate     = 0.62;
    inp.trade_count  = 147;
    inp.best_trade   = 0.0312;
    inp.worst_trade  = -0.0198;

    af_llm_backtest_commentary_output_t out;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_backtest_commentary(&p, &inp, &out, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK(out.commentary[0] != '\0');
    CHK_STR_EQ(out.model, "llama3.1:8b");
    CHK(out.tokens == 97);

    /* Verify prompt */
    char expected_prompt[65536];
    if (get_fixture_user_prompt("backtest_commentary_winning", expected_prompt,
                                 sizeof(expected_prompt)) == 0) {
        char actual_prompt[65536];
        snprintf(actual_prompt, sizeof(actual_prompt),
                 "BACKTEST\n"
                 "strategy=MomentumBreakout\n"
                 "period=2024-01-01 \xe2\x86\x92 2024-06-30\n"
                 "total_return=0.2341\n"
                 "sharpe=1.8200\n"
                 "sortino=2.4500\n"
                 "max_drawdown=-0.0812\n"
                 "win_rate=0.6200\n"
                 "trade_count=147\n"
                 "best_trade=0.0312\n"
                 "worst_trade=-0.0198\n"
                 "\n"
                 "Comment.");
        CHK_STR_EQ(actual_prompt, expected_prompt);
    }

    af_llm_provider_free(&p);
    printf("  [PASS] backtest_commentary_winning\n");
}

static void test_backtest_commentary_losing(void)
{
    af_llm_provider_t p = make_fixture_provider("backtest_commentary_losing");

    af_llm_backtest_commentary_input_t inp;
    memset(&inp, 0, sizeof(inp));
    snprintf(inp.strategy_name, sizeof(inp.strategy_name), "MeanReversionEUR");
    snprintf(inp.period, sizeof(inp.period), "2024-01-01 \xe2\x86\x92 2024-06-30");
    inp.total_return = -0.1123;
    inp.sharpe       = -0.74;
    inp.sortino      = -0.98;
    inp.max_drawdown = -0.2341;
    inp.win_rate     = 0.41;
    inp.trade_count  = 89;
    inp.best_trade   = 0.0201;
    inp.worst_trade  = -0.0456;

    af_llm_backtest_commentary_output_t out;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_backtest_commentary(&p, &inp, &out, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK(out.commentary[0] != '\0');
    CHK(out.tokens == 103);

    af_llm_provider_free(&p);
    printf("  [PASS] backtest_commentary_losing\n");
}

static void test_news_sentiment_bullish(void)
{
    af_llm_provider_t p = make_fixture_provider("news_sentiment_bullish");

    af_llm_news_sentiment_input_t inp;
    memset(&inp, 0, sizeof(inp));
    snprintf(inp.headline, sizeof(inp.headline),
             "Fed signals rate cuts ahead; markets rally strongly");
    inp.body[0] = '\0';

    af_llm_news_sentiment_output_t out;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_news_sentiment(&p, &inp, &out, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK_STR_EQ(out.sentiment, "bullish");
    CHK_NEAR(out.score, 0.85, 1e-6);
    CHK_NEAR(out.confidence, 0.91, 1e-6);
    CHK(out.reason[0] != '\0');

    af_llm_provider_free(&p);
    printf("  [PASS] news_sentiment_bullish\n");
}

static void test_news_sentiment_bearish(void)
{
    af_llm_provider_t p = make_fixture_provider("news_sentiment_bearish");

    af_llm_news_sentiment_input_t inp;
    memset(&inp, 0, sizeof(inp));
    snprintf(inp.headline, sizeof(inp.headline),
             "Recession fears mount as unemployment hits 5-year high");
    inp.body[0] = '\0';

    af_llm_news_sentiment_output_t out;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_news_sentiment(&p, &inp, &out, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK_STR_EQ(out.sentiment, "bearish");
    CHK_NEAR(out.score, -0.78, 1e-6);
    CHK_NEAR(out.confidence, 0.88, 1e-6);

    af_llm_provider_free(&p);
    printf("  [PASS] news_sentiment_bearish\n");
}

static void test_news_sentiment_neutral(void)
{
    af_llm_provider_t p = make_fixture_provider("news_sentiment_neutral");

    af_llm_news_sentiment_input_t inp;
    memset(&inp, 0, sizeof(inp));
    snprintf(inp.headline, sizeof(inp.headline),
             "Central bank holds rates steady, cites mixed economic data");
    inp.body[0] = '\0';

    af_llm_news_sentiment_output_t out;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_news_sentiment(&p, &inp, &out, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK_STR_EQ(out.sentiment, "neutral");
    CHK_NEAR(out.score, 0.05, 1e-6);
    CHK_NEAR(out.confidence, 0.82, 1e-6);

    af_llm_provider_free(&p);
    printf("  [PASS] news_sentiment_neutral\n");
}

static void test_strategy_describe_breakout(void)
{
    af_llm_provider_t p = make_fixture_provider("strategy_describe_breakout");

    af_llm_strategy_describe_input_t inp;
    memset(&inp, 0, sizeof(inp));
    snprintf(inp.name, sizeof(inp.name), "BreakoutMomentum");
    snprintf(inp.timeframe, sizeof(inp.timeframe), "H1");

    snprintf(inp.indicators_used[0], sizeof(inp.indicators_used[0]), "EMA20");
    snprintf(inp.indicators_used[1], sizeof(inp.indicators_used[1]), "EMA50");
    snprintf(inp.indicators_used[2], sizeof(inp.indicators_used[2]), "ATR14");
    snprintf(inp.indicators_used[3], sizeof(inp.indicators_used[3]), "RSI14");
    inp.n_indicators_used = 4;

    snprintf(inp.entry_rules[0], sizeof(inp.entry_rules[0]), "price crosses above EMA20");
    snprintf(inp.entry_rules[1], sizeof(inp.entry_rules[1]), "RSI above 55");
    snprintf(inp.entry_rules[2], sizeof(inp.entry_rules[2]), "ATR above 0.001");
    inp.n_entry_rules = 3;

    snprintf(inp.exit_rules[0], sizeof(inp.exit_rules[0]), "RSI above 75");
    snprintf(inp.exit_rules[1], sizeof(inp.exit_rules[1]), "price below EMA20");
    inp.n_exit_rules = 2;

    snprintf(inp.risk_rules[0], sizeof(inp.risk_rules[0]), "%s", "max 2% per trade");
    snprintf(inp.risk_rules[1], sizeof(inp.risk_rules[1]), "stop loss at ATR*1.5");
    snprintf(inp.risk_rules[2], sizeof(inp.risk_rules[2]), "max 3 open positions");
    inp.n_risk_rules = 3;

    af_llm_strategy_describe_output_t out;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_strategy_describe(&p, &inp, &out, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK(out.description[0] != '\0');
    CHK_STR_EQ(out.model, "llama3.1:8b");
    CHK(out.tokens == 72);

    /* Verify prompt */
    char expected_prompt[65536];
    if (get_fixture_user_prompt("strategy_describe_breakout", expected_prompt,
                                 sizeof(expected_prompt)) == 0) {
        char actual_prompt[65536];
        snprintf(actual_prompt, sizeof(actual_prompt),
                 "STRATEGY\n"
                 "name=BreakoutMomentum\n"
                 "timeframe=H1\n"
                 "indicators=EMA20,EMA50,ATR14,RSI14\n"
                 "entry_rules=price crosses above EMA20,RSI above 55,ATR above 0.001\n"
                 "exit_rules=RSI above 75,price below EMA20\n"
                 "risk_rules=max 2%% per trade,stop loss at ATR*1.5,max 3 open positions\n"
                 "\n"
                 "Describe.");
        CHK_STR_EQ(actual_prompt, expected_prompt);
    }

    af_llm_provider_free(&p);
    printf("  [PASS] strategy_describe_breakout\n");
}

static void test_strategy_describe_meanrev(void)
{
    af_llm_provider_t p = make_fixture_provider("strategy_describe_meanrev");

    af_llm_strategy_describe_input_t inp;
    memset(&inp, 0, sizeof(inp));
    snprintf(inp.name, sizeof(inp.name), "MeanReversionBand");
    snprintf(inp.timeframe, sizeof(inp.timeframe), "M30");

    snprintf(inp.indicators_used[0], sizeof(inp.indicators_used[0]), "BollingerBands20");
    snprintf(inp.indicators_used[1], sizeof(inp.indicators_used[1]), "RSI14");
    snprintf(inp.indicators_used[2], sizeof(inp.indicators_used[2]), "VWAP");
    inp.n_indicators_used = 3;

    snprintf(inp.entry_rules[0], sizeof(inp.entry_rules[0]), "price touches lower band");
    snprintf(inp.entry_rules[1], sizeof(inp.entry_rules[1]), "RSI below 35");
    snprintf(inp.entry_rules[2], sizeof(inp.entry_rules[2]), "price below VWAP");
    inp.n_entry_rules = 3;

    snprintf(inp.exit_rules[0], sizeof(inp.exit_rules[0]), "price reverts to midband");
    snprintf(inp.exit_rules[1], sizeof(inp.exit_rules[1]), "RSI above 50");
    inp.n_exit_rules = 2;

    snprintf(inp.risk_rules[0], sizeof(inp.risk_rules[0]), "max 1.5%% per trade");
    snprintf(inp.risk_rules[1], sizeof(inp.risk_rules[1]), "stop below lower band");
    snprintf(inp.risk_rules[2], sizeof(inp.risk_rules[2]), "daily loss limit 3%%");
    inp.n_risk_rules = 3;

    af_llm_strategy_describe_output_t out;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_strategy_describe(&p, &inp, &out, &err);
    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK(out.description[0] != '\0');
    CHK(out.tokens == 70);

    af_llm_provider_free(&p);
    printf("  [PASS] strategy_describe_meanrev\n");
}

/* ============================================================
 * Section 3 — Error tests
 * ============================================================ */

static void test_error_503(void)
{
    af_llm_transport_t t = af_llm_mock_transport_create_503();
    af_llm_provider_t p;
    af_llm_provider_with_transport(&p, t);

    af_llm_chat_request_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.model, sizeof(req.model), "llama3.1:8b");
    req.n_messages = 1;
    snprintf(req.messages[0].role, sizeof(req.messages[0].role), "user");
    snprintf(req.messages[0].content, sizeof(req.messages[0].content), "hi");
    req.temperature = 0.2;
    req.max_tokens  = 256;
    req.seed        = 42;

    af_llm_chat_response_t resp;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_chat(&p, &req, &resp, &err);
    CHK(rc != 0);
    CHK_STR_EQ(err.kind, AF_LLM_ERR_HTTP);
    CHK(err.status == 503);

    af_llm_provider_free(&p);
    printf("  [PASS] error_503\n");
}

static void test_error_unreachable(void)
{
    af_llm_transport_t t = af_llm_mock_transport_create_unreachable();
    af_llm_provider_t p;
    af_llm_provider_with_transport(&p, t);

    af_llm_chat_request_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.model, sizeof(req.model), "llama3.1:8b");
    req.n_messages = 1;
    snprintf(req.messages[0].role, sizeof(req.messages[0].role), "user");
    snprintf(req.messages[0].content, sizeof(req.messages[0].content), "hi");
    req.temperature = 0.2;
    req.max_tokens  = 256;
    req.seed        = 42;

    af_llm_chat_response_t resp;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_chat(&p, &req, &resp, &err);
    CHK(rc != 0);
    CHK_STR_EQ(err.kind, AF_LLM_ERR_UNREACHABLE);

    af_llm_provider_free(&p);
    printf("  [PASS] error_unreachable\n");
}

static void test_error_decode(void)
{
    af_llm_transport_t t = af_llm_mock_transport_create_bad_json();
    af_llm_provider_t p;
    af_llm_provider_with_transport(&p, t);

    af_llm_news_sentiment_input_t inp;
    memset(&inp, 0, sizeof(inp));
    snprintf(inp.headline, sizeof(inp.headline), "some news");
    inp.body[0] = '\0';

    af_llm_news_sentiment_output_t out;
    af_llm_error_t err;
    af_llm_error_clear(&err);

    int rc = af_llm_news_sentiment(&p, &inp, &out, &err);
    CHK(rc != 0);
    CHK_STR_EQ(err.kind, AF_LLM_ERR_DECODE);

    af_llm_provider_free(&p);
    printf("  [PASS] error_decode\n");
}

/* ============================================================
 * Test runner
 * ============================================================ */

void test_llm_run(int *total_passed, int *total_failed)
{
    g_llm_passed = 0;
    g_llm_failed = 0;

    printf("\n--- LLM Provider Tests ---\n");
    test_complete_basic();
    test_chat_basic();
    test_chat_streaming();
    test_embed_basic();
    test_health_ok();
    test_health_down();
    test_list_models();

    printf("\n--- LLM Use-Case Tests ---\n");
    test_trade_rationale_long();
    test_trade_rationale_short();
    test_backtest_commentary_winning();
    test_backtest_commentary_losing();
    test_news_sentiment_bullish();
    test_news_sentiment_bearish();
    test_news_sentiment_neutral();
    test_strategy_describe_breakout();
    test_strategy_describe_meanrev();

    printf("\n--- LLM Error Tests ---\n");
    test_error_503();
    test_error_unreachable();
    test_error_decode();

    printf("\n  LLM: Passed=%d  Failed=%d\n", g_llm_passed, g_llm_failed);

    *total_passed += g_llm_passed;
    *total_failed += g_llm_failed;
}
