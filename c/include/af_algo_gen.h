/**
 * AlgoForge — c/include/af_algo_gen.h
 *
 * S1 Phase 1 — Algorithm Generator: validator + tier scorer (C port).
 *
 * LLM generation and escape-hatch sandbox remain Python-only (§10).
 * Manifests with a non-null "code" field return a deterministic skip record.
 *
 * All public symbols use the af_algo_gen_ prefix.
 * Error information is returned via af_algo_error_t (a dedicated type below,
 * not the af_error_t used by the analytics/backtest layer, to keep this
 * module self-contained).
 *
 * C17 standard.
 */
#ifndef AF_ALGO_GEN_H
#define AF_ALGO_GEN_H

#include <stddef.h>
#include <stdint.h>
#include "af_types.h"
#include "af_backtest.h"
#include "af_analytics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Error type
 * ============================================================ */

/**
 * af_algo_error_t — error information returned via out-param.
 * kind:    short error token (e.g. "schema_error", "dsl_error", "stage_failed").
 * message: human-readable description (max 511 chars + NUL).
 */
typedef struct {
    char kind[64];
    char message[512];
} af_algo_error_t;

static inline void af_algo_error_clear(af_algo_error_t *e) {
    e->kind[0]    = '\0';
    e->message[0] = '\0';
}

/* ============================================================
 * Tier enum
 * ============================================================ */

typedef enum {
    AF_TIER_RED    = 0,   /* < 70 — not promoted                    */
    AF_TIER_ORANGE = 1,   /* 70–79 — experimental, paper only       */
    AF_TIER_YELLOW = 2,   /* 80–89 — experimental, half sizing      */
    AF_TIER_GREEN  = 3,   /* 90–94 — full sizing                    */
    AF_TIER_WHITE  = 4    /* 95–100 — elevated sizing               */
} af_algo_tier_t;

/** String representation of a tier (static storage). */
const char *af_algo_tier_str(af_algo_tier_t tier);

/** Tier color hex string (e.g. "#ff4444"). */
const char *af_algo_tier_color(af_algo_tier_t tier);

/* ============================================================
 * Tier configuration (hard-coded defaults; no INI reading in C)
 * ============================================================ */

typedef struct {
    double orange_size_mult;   /* default 0.25 */
    double yellow_size_mult;   /* default 0.50 */
    double green_size_mult;    /* default 1.00 */
    double white_size_mult;    /* default 1.50 */
} af_algo_tier_cfg_t;

/** Fill *cfg with spec §6 defaults. */
void af_algo_tier_cfg_default(af_algo_tier_cfg_t *cfg);

/* ============================================================
 * Tier report
 * ============================================================ */

#define AF_ALGO_GEN_MAX_STAGES    8
#define AF_ALGO_GEN_MAX_REASON    256
#define AF_ALGO_GEN_MAX_NAME      64
#define AF_ALGO_GEN_MAX_STAGE_NAME 32

typedef struct {
    int  stage;
    char name[AF_ALGO_GEN_MAX_STAGE_NAME];
    int  passed;                         /* 0 = failed, 1 = passed */
    char reason[AF_ALGO_GEN_MAX_REASON];
    /* Numeric metrics (stage-specific) */
    int    trades;
    double score;                        /* 0-100 for stages 3-5 */
} af_algo_stage_result_t;

typedef struct {
    char            manifest_name[AF_ALGO_GEN_MAX_NAME];
    int             passed;              /* 1 = all stages passed */
    af_algo_tier_t  tier;
    double          min_score;
    double          walk_forward;
    double          mc_bootstrap;
    double          robustness;
    double          size_mult;
    int             experimental;        /* 0/1 */
    int             paper_only;          /* 0/1 */
    af_algo_stage_result_t stages[AF_ALGO_GEN_MAX_STAGES];
    int             n_stages;
    int             is_escape_hatch;     /* 1 = skipped (code field set) */
} af_algo_tier_report_t;

/* ============================================================
 * Indicator declaration (parsed from manifest)
 * ============================================================ */

#define AF_ALGO_MAX_INDICATORS  16
#define AF_ALGO_MAX_IND_ID      32
#define AF_ALGO_MAX_IND_KIND    32
#define AF_ALGO_MAX_PARAMS       8
#define AF_ALGO_MAX_PARAM_KEY   32

typedef struct {
    char   key[AF_ALGO_MAX_PARAM_KEY];
    double value;
    int    is_int;   /* 1 if original value was integer */
} af_algo_param_t;

typedef struct {
    char           id[AF_ALGO_MAX_IND_ID];
    char           kind[AF_ALGO_MAX_IND_KIND];
    af_algo_param_t params[AF_ALGO_MAX_PARAMS];
    int            n_params;
} af_algo_indicator_decl_t;

/* ============================================================
 * Entry/exit rules
 * ============================================================ */

#define AF_ALGO_MAX_RULES       8
#define AF_ALGO_MAX_EXPR_LEN    256
#define AF_ALGO_MAX_SIDE_LEN    8

typedef struct {
    char side[AF_ALGO_MAX_SIDE_LEN];   /* "long" | "short" */
    char when[AF_ALGO_MAX_EXPR_LEN];
} af_algo_entry_rule_t;

typedef struct {
    char   side[AF_ALGO_MAX_SIDE_LEN];
    char   when[AF_ALGO_MAX_EXPR_LEN]; /* may be empty string */
    double sl_atr;                     /* 0.0 = not set */
    double tp_atr;                     /* 0.0 = not set */
    int    has_when;
    int    has_sl_atr;
    int    has_tp_atr;
} af_algo_exit_rule_t;

/* ============================================================
 * Risk specification
 * ============================================================ */

typedef struct {
    char   size[8];         /* "atr" | "fixed" */
    double atr_mult;
    double fixed_lots;
    int    max_concurrent;
    int    hedge;
    int    cool_down_bars;
} af_algo_risk_spec_t;

/* ============================================================
 * Manifest container
 * ============================================================ */

#define AF_ALGO_MAX_TIMEFRAMES   8
#define AF_ALGO_MAX_TF_LEN       8
#define AF_ALGO_MAX_SYMBOLS      16
#define AF_ALGO_MAX_SYM_LEN      16
#define AF_ALGO_MAX_DESC_LEN     288
#define AF_ALGO_MAX_RAT_LEN      1032
#define AF_ALGO_MAX_CODE_LEN     8192

typedef struct af_algo_manifest {
    char   schema_version[8];
    char   name[AF_ALGO_GEN_MAX_NAME];
    char   description[AF_ALGO_MAX_DESC_LEN];
    char   rationale[AF_ALGO_MAX_RAT_LEN];

    char   timeframes[AF_ALGO_MAX_TIMEFRAMES][AF_ALGO_MAX_TF_LEN];
    int    n_timeframes;

    char   symbols[AF_ALGO_MAX_SYMBOLS][AF_ALGO_MAX_SYM_LEN];
    int    n_symbols;
    int    symbols_any;   /* 1 if symbols == "any" */

    af_algo_indicator_decl_t indicators[AF_ALGO_MAX_INDICATORS];
    int                      n_indicators;

    af_algo_entry_rule_t entries[AF_ALGO_MAX_RULES];
    int                  n_entries;

    af_algo_exit_rule_t  exits[AF_ALGO_MAX_RULES];
    int                  n_exits;

    af_algo_risk_spec_t  risk;

    char   code[AF_ALGO_MAX_CODE_LEN];   /* empty string = not set */
    int    has_code;                      /* 1 if code field is non-null */
} af_algo_manifest_t;

/**
 * af_algo_manifest_parse — parse a JSON string into an af_algo_manifest_t.
 *
 * json:  NUL-terminated JSON string.
 * len:   length of json (or 0 to use strlen).
 * out:   caller-allocated manifest struct to populate.
 * err:   caller-allocated error struct (populated on failure).
 *
 * Returns 0 on success, non-zero on failure.
 */
int af_algo_manifest_parse(const char *json, size_t len,
                            af_algo_manifest_t *out,
                            af_algo_error_t *err);

/* ============================================================
 * DSL tokenizer + recursive-descent parser (no eval)
 * ============================================================ */

/**
 * af_algo_expr_t — compiled DSL expression tree node (opaque outside algo_gen.c).
 * Allocated by af_algo_dsl_compile; freed by af_algo_expr_free.
 */
typedef struct af_algo_expr af_algo_expr_t;

/**
 * DSL environment for evaluation — one value per indicator ID plus bar fields.
 * indicator_ids[] / indicator_vals[] are parallel arrays.
 */
#define AF_DSL_MAX_IDS 32
#define AF_DSL_ID_LEN  32

typedef struct {
    /* Indicator values (NaN = not available / warmup) */
    char   indicator_ids[AF_DSL_MAX_IDS][AF_DSL_ID_LEN];
    double indicator_vals[AF_DSL_MAX_IDS];
    int    n_indicators;

    /* Bar fields */
    double close, open, high, low, volume, spread;

    /* Pattern flags — one per pattern name (see PATTERN_NAMES in spec) */
    /* accessed via name lookup; 0 = false */
    char   pattern_names[24][32];
    int    pattern_vals[24];
    int    n_patterns;
} af_algo_dsl_env_t;

/**
 * af_algo_dsl_compile — tokenise and parse a DSL expression string.
 *
 * src:            NUL-terminated DSL expression string.
 * declared_names: array of declared indicator IDs.
 * n_names:        number of declared indicator IDs.
 * out:            receives a heap-allocated expression tree.
 * err:            receives error information on failure.
 *
 * Returns 0 on success, non-zero on failure.
 */
int af_algo_dsl_compile(const char *src,
                         const char **declared_names, size_t n_names,
                         af_algo_expr_t **out,
                         af_algo_error_t *err);

/**
 * af_algo_dsl_eval — evaluate a compiled DSL expression.
 *
 * Returns 0 on success (result_bool populated), non-zero on error.
 * A missing indicator value (NaN) evaluates to false, not an error.
 */
int af_algo_dsl_eval(const af_algo_expr_t *e,
                     const af_algo_dsl_env_t *env,
                     int *result_bool);

/**
 * af_algo_expr_free — release memory for a compiled expression tree.
 */
void af_algo_expr_free(af_algo_expr_t *e);

/* ============================================================
 * Runtime: manifest → IAlgorithm-equivalent driver
 * ============================================================ */

/**
 * af_algo_manifest_algo_t — algorithm instance compiled from a manifest.
 * Wraps af_algorithm_t so it can be passed to af_backtest_run().
 */
typedef struct af_algo_manifest_algo {
    af_algorithm_t   base;   /* must be first — passed to backtest engine */

    /* Internal state */
    const af_algo_manifest_t *manifest;
    af_algo_expr_t   *entry_exprs[AF_ALGO_MAX_RULES];
    int               entry_sides[AF_ALGO_MAX_RULES]; /* 1=long, -1=short */
    int               n_entry_exprs;

    af_algo_expr_t   *exit_exprs[AF_ALGO_MAX_RULES];
    int               exit_sides[AF_ALGO_MAX_RULES];
    int               n_exit_exprs;

    int   cool_down;
    int   bars_since_exit;
    int   atr_ind_idx;        /* index into manifest->indicators for ATR, or -1 */

    /* sl/tp brackets per side */
    double sl_atr_long,  tp_atr_long;
    double sl_atr_short, tp_atr_short;

    /* Indicator scratch buffers */
    double ind_buf[AF_ALGO_MAX_INDICATORS][65536]; /* one buffer per indicator */
    int    ind_buf_len[AF_ALGO_MAX_INDICATORS];
} af_algo_manifest_algo_t;

/**
 * af_algo_manifest_algo_init — initialise an af_algo_manifest_algo_t.
 * The manifest must remain valid for the lifetime of *a.
 *
 * Returns 0 on success, non-zero if DSL compilation fails.
 */
int af_algo_manifest_algo_init(af_algo_manifest_algo_t *a,
                                const af_algo_manifest_t *m,
                                af_algo_error_t *err);

/**
 * af_algo_manifest_algo_destroy — free all DSL expression trees inside *a.
 * Does not free *a itself.
 */
void af_algo_manifest_algo_destroy(af_algo_manifest_algo_t *a);

/* ============================================================
 * Validator: 5 stages
 * ============================================================ */

/**
 * af_algo_validate — run the full 5-stage validation pipeline.
 *
 * m:      parsed manifest (must be non-NULL).
 * bars:   bar array (oldest first).
 * n_bars: number of bars.
 * seed:   LCG seed for Monte Carlo bootstrap.
 * out:    caller-allocated tier report struct.
 * err:    caller-allocated error struct.
 *
 * Returns 0 if all stages pass, non-zero if any stage fails.
 * The report is always populated with the stages that ran.
 *
 * For escape-hatch manifests (has_code == 1), returns a deterministic
 * skip record with is_escape_hatch == 1.
 */
int af_algo_validate(const af_algo_manifest_t *m,
                     const af_bar_t *bars, size_t n_bars,
                     uint64_t seed,
                     af_algo_tier_report_t *out,
                     af_algo_error_t *err);

/* ============================================================
 * Tier scoring
 * ============================================================ */

/**
 * af_algo_score_to_tier — compute tier from three component scores.
 *
 * wf:         walk-forward score [0, 100].
 * mc:         Monte Carlo score [0, 100].
 * robustness: parameter-robustness score [0, 100].
 * cfg:        tier thresholds/multipliers (pass NULL for defaults).
 *
 * Returns the af_algo_tier_t corresponding to min(wf, mc, robustness).
 */
af_algo_tier_t af_algo_score_to_tier(double wf, double mc, double robustness,
                                      const af_algo_tier_cfg_t *cfg);

/* ============================================================
 * Tier report JSON serialization
 * ============================================================ */

/**
 * af_algo_tier_report_to_json — serialise a tier report to JSON.
 *
 * buf:    caller-supplied output buffer.
 * bufsz:  capacity of buf.
 *
 * Returns number of bytes written (not including NUL), or -1 on error
 * (buffer too small).
 */
int af_algo_tier_report_to_json(const af_algo_tier_report_t *r,
                                 char *buf, size_t bufsz);

/**
 * af_algo_tier_report_from_json — deserialise a tier report from JSON.
 *
 * src:  NUL-terminated JSON string.
 * len:  length of src (or 0 to use strlen).
 * out:  caller-allocated struct to populate.
 * err:  caller-allocated error struct (populated on failure; may be NULL).
 *
 * Returns 0 on success, non-zero on parse error.
 */
int af_algo_tier_report_from_json(const char *src, size_t len,
                                   af_algo_tier_report_t *out,
                                   af_algo_error_t *err);

/* ============================================================
 * Promote: write manifest + metadata to registry dir
 * ============================================================ */

/**
 * af_algo_promote — write manifest JSON + metadata to registry_dir/<name>.json.
 *
 * Refuses to promote red-tier reports.
 * Writes the manifest JSON with a "_metadata" key added.
 *
 * registry_dir:  directory path (must already exist, or will be created).
 * err:           receives error information on failure.
 *
 * Returns 0 on success, non-zero on failure.
 */
int af_algo_promote(const af_algo_tier_report_t *r,
                    const af_algo_manifest_t *m,
                    const char *registry_dir,
                    af_algo_error_t *err);

/* ============================================================
 * Metadata type for promoted algorithms (extends af_algorithm_t)
 * ============================================================ */

typedef struct {
    char   manifest_name[AF_ALGO_GEN_MAX_NAME]; /* mirrors af_algo_tier_report_t */
    char   source[16];        /* "algo_gen" */
    char   schema_version[8];
    char   tier[16];          /* "red" | "orange" | "yellow" | "green" | "white" */
    char   tier_color[16];    /* e.g. "#00ff88" */
    double walk_forward;
    double mc_bootstrap;
    double robustness;
    double min_score;
    double size_mult;
    int    experimental;
    int    paper_only;
    char   created_at[32];    /* ISO-8601 UTC */
} af_algo_metadata_t;

/** Allocate and populate an af_algo_metadata_t from a tier report.
 *  Caller owns the returned pointer and must release it with
 *  af_algo_metadata_destroy. Returns NULL on allocation failure. */
af_algo_metadata_t *af_algo_metadata_create(const af_algo_tier_report_t *report);

/** Free a metadata struct allocated by af_algo_metadata_create. */
void af_algo_metadata_destroy(af_algo_metadata_t *m);

#ifdef __cplusplus
}
#endif

#endif /* AF_ALGO_GEN_H */
