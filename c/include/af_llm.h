/**
 * AlgoForge — c/include/af_llm.h
 *
 * Single public header for the S2 LLM layer.
 * All structs, enums, and function prototypes for:
 *   OllamaProvider, transport vtable, provider interface,
 *   and the four use cases (trade_rationale, backtest_commentary,
 *   news_sentiment, strategy_describe).
 *
 * Conventions (matching project style):
 *   - af_llm_ prefix on all public symbols
 *   - snake_case throughout
 *   - output params via pointers
 *   - return int 0/non-zero for success/failure
 *   - af_llm_error_t returned via out-param
 *   - C17 standard
 */
#ifndef AF_LLM_H
#define AF_LLM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Section 1 — Error model
 * ============================================================ */

/* Error kind strings — use strcmp to compare */
#define AF_LLM_ERR_UNREACHABLE  "unreachable"
#define AF_LLM_ERR_TIMEOUT      "timeout"
#define AF_LLM_ERR_HTTP         "http"
#define AF_LLM_ERR_DECODE       "decode"
#define AF_LLM_ERR_MODEL_MISSING "model_missing"

/**
 * af_llm_error_t — error information returned via out-param.
 * kind:    one of the AF_LLM_ERR_* strings, or "" on success.
 * message: human-readable description.
 * status:  HTTP status code (populated when kind == AF_LLM_ERR_HTTP).
 */
typedef struct {
    char kind[32];
    char message[256];
    int  status;
} af_llm_error_t;

/* Initialise an error struct to the "no error" state */
static inline void af_llm_error_clear(af_llm_error_t *e) {
    e->kind[0]    = '\0';
    e->message[0] = '\0';
    e->status     = 0;
}

/* ============================================================
 * Section 2 — HTTP transport abstraction
 * ============================================================ */

/**
 * af_llm_transport_t — vtable-based HTTP transport.
 *
 * request():  performs a single HTTP call.
 *   ctx:         opaque context pointer stored in the struct.
 *   method:      "GET" or "POST".
 *   path:        e.g. "/api/generate"  (no host).
 *   body:        request body (may be NULL for GET).
 *   resp_body:   receives a heap-allocated NUL-terminated response body;
 *                caller must free() it.
 *   resp_status: receives the HTTP status code.
 *   err:         receives error details on failure.
 *   Returns 0 on success, non-zero on failure.
 *
 * destroy(): optional cleanup; may be NULL.
 */
typedef struct af_llm_transport {
    int  (*request)(void *ctx, const char *method, const char *path,
                    const char *body, char **resp_body, int *resp_status,
                    af_llm_error_t *err);
    void (*destroy)(void *ctx);
    void *ctx;
} af_llm_transport_t;

/* ============================================================
 * Section 3 — Core provider types
 * ============================================================ */

/** Maximum array sizes for inline storage */
#define AF_LLM_MAX_MESSAGES   64
#define AF_LLM_MAX_STOP_SEQS  8
#define AF_LLM_MAX_STOP_LEN   64
#define AF_LLM_MAX_MODEL_LEN  64
#define AF_LLM_MAX_CONTENT    65536  /* 64 KiB for message content */
#define AF_LLM_MAX_ROLE_LEN   16
#define AF_LLM_MAX_MODELS     64
#define AF_LLM_MAX_HOST_LEN   256

/**
 * af_llm_chat_message_t — one role/content pair.
 */
typedef struct {
    char role[AF_LLM_MAX_ROLE_LEN];
    char content[AF_LLM_MAX_CONTENT];
} af_llm_chat_message_t;

/**
 * af_llm_completion_request_t — maps to POST /api/generate.
 */
typedef struct {
    char   model[AF_LLM_MAX_MODEL_LEN];
    char   prompt[AF_LLM_MAX_CONTENT];
    double temperature;       /* default 0.2 */
    int    max_tokens;        /* -1 = not set (omitted from request) */
    char   stop[AF_LLM_MAX_STOP_SEQS][AF_LLM_MAX_STOP_LEN];
    int    n_stop;
    int    seed;              /* -1 = not set */
    double timeout_s;
} af_llm_completion_request_t;

/**
 * af_llm_completion_response_t — parsed response from /api/generate.
 */
typedef struct {
    char     text[AF_LLM_MAX_CONTENT];
    char     model[AF_LLM_MAX_MODEL_LEN];
    int      prompt_tokens;
    int      completion_tokens;
    int64_t  total_duration_ns;
    char     finish_reason[32];
} af_llm_completion_response_t;

/**
 * af_llm_chat_request_t — maps to POST /api/chat.
 */
typedef struct {
    char                   model[AF_LLM_MAX_MODEL_LEN];
    af_llm_chat_message_t  messages[AF_LLM_MAX_MESSAGES];
    int                    n_messages;
    double                 temperature;
    int                    max_tokens;   /* -1 = not set */
    int                    seed;         /* -1 = not set */
    double                 timeout_s;
    int                    stream;       /* 0 = false, 1 = true */
} af_llm_chat_request_t;

/**
 * af_llm_chat_response_t — parsed response from /api/chat.
 */
typedef struct {
    af_llm_chat_message_t message;
    char                  model[AF_LLM_MAX_MODEL_LEN];
    int                   prompt_tokens;
    int                   completion_tokens;
    int64_t               total_duration_ns;
    char                  finish_reason[32];
} af_llm_chat_response_t;

/**
 * af_llm_chat_chunk_t — one chunk from a streaming chat response.
 */
typedef struct {
    char delta[AF_LLM_MAX_CONTENT];
    int  done;
    char finish_reason[32];  /* populated when done=1 */
} af_llm_chat_chunk_t;

/**
 * af_llm_embed_request_t — maps to POST /api/embeddings.
 * For batched input, use inputs[0..n_inputs-1].
 */
typedef struct {
    char model[AF_LLM_MAX_MODEL_LEN];
    char inputs[AF_LLM_MAX_MESSAGES][AF_LLM_MAX_CONTENT];
    int  n_inputs;
} af_llm_embed_request_t;

/**
 * af_llm_embed_response_t — parsed response.
 * embeddings: heap-allocated flat array. embeddings[i] points to
 *             embedding i (length embed_dim each).
 * Caller must call af_llm_embed_response_free().
 */
typedef struct {
    double **embeddings;   /* array of n_inputs pointers */
    int      n_inputs;
    int      embed_dim;    /* length of each embedding vector */
    char     model[AF_LLM_MAX_MODEL_LEN];
} af_llm_embed_response_t;

/**
 * af_llm_health_status_t — result of a health check.
 */
typedef struct {
    int  ok;
    int  model_loaded;
    char model[AF_LLM_MAX_MODEL_LEN];
    char error[256];
} af_llm_health_status_t;

/**
 * af_llm_model_info_t — one entry from /api/tags.
 */
typedef struct {
    char    name[AF_LLM_MAX_MODEL_LEN];
    int64_t size_bytes;
    char    modified_at[64];
} af_llm_model_info_t;

/* ============================================================
 * Section 4 — Provider
 * ============================================================ */

/**
 * af_llm_provider_t — opaque provider handle.
 * Contains configuration and the transport vtable.
 */
typedef struct {
    char               host[AF_LLM_MAX_HOST_LEN];
    char               model[AF_LLM_MAX_MODEL_LEN];
    char               embed_model[AF_LLM_MAX_MODEL_LEN];
    double             temperature;
    int                max_tokens;
    int                seed;
    double             timeout_s;
    af_llm_transport_t transport;
} af_llm_provider_t;

/**
 * af_llm_provider_init — initialise a provider with explicit parameters.
 *
 * host:        base URL, e.g. "http://localhost:11434"
 * model:       default chat/completion model
 * embed_model: embedding model
 * temperature: default temperature
 * max_tokens:  default num_predict
 * seed:        RNG seed
 * timeout_s:   request timeout
 * transport:   transport vtable to use
 *
 * Returns 0 on success.
 */
int af_llm_provider_init(
    af_llm_provider_t  *p,
    const char         *host,
    const char         *model,
    const char         *embed_model,
    double              temperature,
    int                 max_tokens,
    int                 seed,
    double              timeout_s,
    af_llm_transport_t  transport
);

/**
 * af_llm_provider_from_env — initialise a provider from environment variables.
 * Uses the libcurl transport (or stub if AF_NO_LIBCURL is defined).
 * Returns 0 on success.
 */
int af_llm_provider_from_env(af_llm_provider_t *p);

/**
 * af_llm_provider_with_transport — like from_env but injects a custom transport.
 * Used by tests to inject the mock transport.
 */
int af_llm_provider_with_transport(
    af_llm_provider_t  *p,
    af_llm_transport_t  transport
);

/**
 * af_llm_provider_free — release any resources held by the provider.
 */
void af_llm_provider_free(af_llm_provider_t *p);

/* ============================================================
 * Section 5 — Provider interface functions
 * ============================================================ */

/**
 * af_llm_complete — POST /api/generate.
 * Returns 0 on success, non-zero on failure (err populated).
 */
int af_llm_complete(
    af_llm_provider_t             *p,
    const af_llm_completion_request_t *req,
    af_llm_completion_response_t  *resp,
    af_llm_error_t                *err
);

/**
 * af_llm_chat — POST /api/chat (stream=false).
 * Returns 0 on success, non-zero on failure.
 */
int af_llm_chat(
    af_llm_provider_t           *p,
    const af_llm_chat_request_t *req,
    af_llm_chat_response_t      *resp,
    af_llm_error_t              *err
);

/**
 * af_llm_chat_stream — POST /api/chat (stream=true).
 * Calls chunk_cb for each received chunk.
 * chunk_cb returns non-zero to abort streaming.
 * Returns 0 on success, non-zero on failure.
 */
int af_llm_chat_stream(
    af_llm_provider_t           *p,
    const af_llm_chat_request_t *req,
    int (*chunk_cb)(const af_llm_chat_chunk_t *chunk, void *userdata),
    void                        *userdata,
    af_llm_error_t              *err
);

/**
 * af_llm_embed — POST /api/embeddings.
 * Caller must call af_llm_embed_response_free() on success.
 * Returns 0 on success, non-zero on failure.
 */
int af_llm_embed(
    af_llm_provider_t           *p,
    const af_llm_embed_request_t *req,
    af_llm_embed_response_t     *resp,
    af_llm_error_t              *err
);

/**
 * af_llm_embed_response_free — free heap memory in an embed response.
 */
void af_llm_embed_response_free(af_llm_embed_response_t *resp);

/**
 * af_llm_health — check provider health.
 * Returns 0 on success (even if health is bad — check status->ok).
 */
int af_llm_health(
    af_llm_provider_t      *p,
    af_llm_health_status_t *status,
    af_llm_error_t         *err
);

/**
 * af_llm_list_models — GET /api/tags.
 * out:    caller-supplied array of af_llm_model_info_t, capacity cap_out.
 * n_out:  receives number of models written.
 * Returns 0 on success, non-zero on failure.
 */
int af_llm_list_models(
    af_llm_provider_t  *p,
    af_llm_model_info_t *out,
    int                  cap_out,
    int                 *n_out,
    af_llm_error_t      *err
);

/* ============================================================
 * Section 6 — Use-case input/output structs
 * ============================================================ */

#define AF_LLM_MAX_INDICATORS  32
#define AF_LLM_MAX_PATTERNS    16
#define AF_LLM_MAX_RULES       16
#define AF_LLM_MAX_KEY_LEN     64
#define AF_LLM_MAX_VAL_LEN     64
#define AF_LLM_MAX_RULE_LEN    128
#define AF_LLM_MAX_TEXT        8192

/** af_llm_kv_t — a key-value pair for indicators dict */
typedef struct {
    char   key[AF_LLM_MAX_KEY_LEN];
    double value;
} af_llm_kv_t;

/* --- trade_rationale --- */

typedef struct {
    char         symbol[32];
    char         side[8];           /* "long" | "short" */
    double       entry_price;
    double       stop_loss;
    double       take_profit;
    af_llm_kv_t  indicators[AF_LLM_MAX_INDICATORS];
    int          n_indicators;
    char         patterns[AF_LLM_MAX_PATTERNS][64];
    int          n_patterns;
    char         timeframe[16];
} af_llm_trade_rationale_input_t;

typedef struct {
    char explanation[AF_LLM_MAX_TEXT];
    char model[AF_LLM_MAX_MODEL_LEN];
    int  tokens;
} af_llm_trade_rationale_output_t;

/* --- backtest_commentary --- */

typedef struct {
    char   strategy_name[128];
    char   period[64];
    double total_return;
    double sharpe;
    double sortino;
    double max_drawdown;
    double win_rate;
    int    trade_count;
    double best_trade;
    double worst_trade;
} af_llm_backtest_commentary_input_t;

typedef struct {
    char commentary[AF_LLM_MAX_TEXT];
    char model[AF_LLM_MAX_MODEL_LEN];
    int  tokens;
} af_llm_backtest_commentary_output_t;

/* --- news_sentiment --- */

typedef struct {
    char headline[512];
    char body[AF_LLM_MAX_TEXT];
} af_llm_news_sentiment_input_t;

typedef struct {
    char  sentiment[16];   /* "bullish" | "bearish" | "neutral" */
    double score;          /* -1.0 .. 1.0 */
    double confidence;     /* 0.0 .. 1.0 */
    char  reason[512];
} af_llm_news_sentiment_output_t;

/* --- strategy_describe --- */

typedef struct {
    char name[128];
    char indicators_used[AF_LLM_MAX_RULES][AF_LLM_MAX_RULE_LEN];
    int  n_indicators_used;
    char entry_rules[AF_LLM_MAX_RULES][AF_LLM_MAX_RULE_LEN];
    int  n_entry_rules;
    char exit_rules[AF_LLM_MAX_RULES][AF_LLM_MAX_RULE_LEN];
    int  n_exit_rules;
    char risk_rules[AF_LLM_MAX_RULES][AF_LLM_MAX_RULE_LEN];
    int  n_risk_rules;
    char timeframe[16];
} af_llm_strategy_describe_input_t;

typedef struct {
    char description[AF_LLM_MAX_TEXT];
    char model[AF_LLM_MAX_MODEL_LEN];
    int  tokens;
} af_llm_strategy_describe_output_t;

/* ============================================================
 * Section 7 — Use-case function prototypes
 * ============================================================ */

/**
 * af_llm_trade_rationale — generate a trade rationale explanation.
 * Returns 0 on success, non-zero on failure.
 */
int af_llm_trade_rationale(
    af_llm_provider_t                    *p,
    const af_llm_trade_rationale_input_t *inp,
    af_llm_trade_rationale_output_t      *out,
    af_llm_error_t                       *err
);

/**
 * af_llm_backtest_commentary — generate a backtest narrative.
 * Returns 0 on success, non-zero on failure.
 */
int af_llm_backtest_commentary(
    af_llm_provider_t                        *p,
    const af_llm_backtest_commentary_input_t *inp,
    af_llm_backtest_commentary_output_t      *out,
    af_llm_error_t                           *err
);

/**
 * af_llm_news_sentiment — classify news sentiment.
 * Returns 0 on success, non-zero on failure (including decode errors).
 */
int af_llm_news_sentiment(
    af_llm_provider_t                   *p,
    const af_llm_news_sentiment_input_t *inp,
    af_llm_news_sentiment_output_t      *out,
    af_llm_error_t                      *err
);

/**
 * af_llm_strategy_describe — generate a strategy description.
 * Returns 0 on success, non-zero on failure.
 */
int af_llm_strategy_describe(
    af_llm_provider_t                      *p,
    const af_llm_strategy_describe_input_t *inp,
    af_llm_strategy_describe_output_t      *out,
    af_llm_error_t                         *err
);

#ifdef __cplusplus
}
#endif

#endif /* AF_LLM_H */
