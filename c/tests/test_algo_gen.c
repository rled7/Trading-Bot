/**
 * AlgoForge — c/tests/test_algo_gen.c
 *
 * Fixture-driven tests for the algo_gen module.
 * Covers:
 *   - af_algo_score_to_tier() boundary values (all 5 tiers + edge cases)
 *   - af_algo_tier_str() / af_algo_tier_color() for all tiers
 *   - af_algo_tier_report_from_json() round-trip for all 7 tier_report fixtures
 *   - af_algo_manifest_parse() escape-hatch detection
 *   - af_algo_tier_cfg_default() defaults
 *
 * C17 standard.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "af_algo_gen.h"

/* ============================================================
 * Test harness macros (matching the existing test style)
 * ============================================================ */

static int g_algo_gen_passed = 0;
static int g_algo_gen_failed = 0;

#define CHK(cond) do {                                                          \
    if (cond) {                                                                 \
        ++g_algo_gen_passed;                                                    \
    } else {                                                                    \
        ++g_algo_gen_failed;                                                    \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                           \
} while (0)

#define CHK_STR_EQ(a, b) \
    CHK(strcmp((a), (b)) == 0)

#define CHK_NEAR(a, b, tol) \
    CHK(fabs((double)(a) - (double)(b)) < (double)(tol))

/* ============================================================
 * Fixture loading helper
 * ============================================================ */

static const char *fixture_dir(void)
{
    const char *env = getenv("AF_FIXTURE_DIR");
    return env ? env : "../tests/fixtures/algo_gen";
}

/**
 * load_fixture — read a fixture file into a heap-allocated NUL-terminated
 * buffer.  Returns NULL on failure.  Caller must free().
 */
static char *load_fixture(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", fixture_dir(), name);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "SKIP: cannot open fixture '%s'\n", path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    if (sz <= 0) { fclose(fp); return NULL; }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    (void)fread(buf, 1, (size_t)sz, fp);
    buf[sz] = '\0';
    fclose(fp);
    return buf;
}

/* ============================================================
 * Section 1 — score_to_tier + tier helpers
 * ============================================================ */

static void test_score_to_tier_boundaries(void)
{
    /* Boundary: < 70 → RED */
    CHK(af_algo_score_to_tier(69.9, 69.9, 69.9, NULL) == AF_TIER_RED);
    CHK(af_algo_score_to_tier(0.0,  0.0,  0.0,  NULL) == AF_TIER_RED);

    /* Boundary: exactly 70 → ORANGE */
    CHK(af_algo_score_to_tier(70.0, 70.0, 70.0, NULL) == AF_TIER_ORANGE);
    CHK(af_algo_score_to_tier(79.9, 79.9, 79.9, NULL) == AF_TIER_ORANGE);

    /* Boundary: exactly 80 → YELLOW */
    CHK(af_algo_score_to_tier(80.0, 80.0, 80.0, NULL) == AF_TIER_YELLOW);
    CHK(af_algo_score_to_tier(89.9, 89.9, 89.9, NULL) == AF_TIER_YELLOW);

    /* Boundary: exactly 90 → GREEN */
    CHK(af_algo_score_to_tier(90.0, 90.0, 90.0, NULL) == AF_TIER_GREEN);
    CHK(af_algo_score_to_tier(94.9, 94.9, 94.9, NULL) == AF_TIER_GREEN);

    /* Boundary: exactly 95 → WHITE */
    CHK(af_algo_score_to_tier(95.0, 95.0, 95.0, NULL) == AF_TIER_WHITE);
    CHK(af_algo_score_to_tier(100.0, 100.0, 100.0, NULL) == AF_TIER_WHITE);

    /* min() semantics: lowest of the three scores determines tier */
    CHK(af_algo_score_to_tier(95.0, 95.0, 80.0, NULL) == AF_TIER_YELLOW);
    CHK(af_algo_score_to_tier(95.0, 65.0, 95.0, NULL) == AF_TIER_RED);
    CHK(af_algo_score_to_tier(75.0, 95.0, 95.0, NULL) == AF_TIER_ORANGE);

    printf("  [PASS] score_to_tier_boundaries\n");
}

static void test_tier_str_and_color(void)
{
    CHK_STR_EQ(af_algo_tier_str(AF_TIER_RED),    "red");
    CHK_STR_EQ(af_algo_tier_str(AF_TIER_ORANGE), "orange");
    CHK_STR_EQ(af_algo_tier_str(AF_TIER_YELLOW), "yellow");
    CHK_STR_EQ(af_algo_tier_str(AF_TIER_GREEN),  "green");
    CHK_STR_EQ(af_algo_tier_str(AF_TIER_WHITE),  "white");

    /* Colors must be non-empty strings starting with '#' */
    CHK(af_algo_tier_color(AF_TIER_RED)[0]    == '#');
    CHK(af_algo_tier_color(AF_TIER_ORANGE)[0] == '#');
    CHK(af_algo_tier_color(AF_TIER_YELLOW)[0] == '#');
    CHK(af_algo_tier_color(AF_TIER_GREEN)[0]  == '#');
    CHK(af_algo_tier_color(AF_TIER_WHITE)[0]  == '#');

    printf("  [PASS] tier_str_and_color\n");
}

static void test_tier_cfg_defaults(void)
{
    af_algo_tier_cfg_t cfg;
    af_algo_tier_cfg_default(&cfg);
    CHK_NEAR(cfg.orange_size_mult, 0.25, 1e-9);
    CHK_NEAR(cfg.yellow_size_mult, 0.50, 1e-9);
    CHK_NEAR(cfg.green_size_mult,  1.00, 1e-9);
    CHK_NEAR(cfg.white_size_mult,  1.50, 1e-9);
    printf("  [PASS] tier_cfg_defaults\n");
}

/* ============================================================
 * Section 2 — from_json round-trip for tier_report fixtures
 * ============================================================ */

static void test_from_json_green(void)
{
    char *json = load_fixture("tier_report_trend_follow_green.json");
    if (!json) { ++g_algo_gen_failed; return; }

    af_algo_tier_report_t r;
    af_algo_error_t err;
    af_algo_error_clear(&err);
    int rc = af_algo_tier_report_from_json(json, 0, &r, &err);
    free(json);

    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK_STR_EQ(r.manifest_name, "trend-follow-ema-rsi");
    CHK(r.passed == 1);
    CHK(r.tier == AF_TIER_GREEN);
    CHK_NEAR(r.min_score,    90.2, 0.01);
    CHK_NEAR(r.walk_forward, 91.0, 0.01);
    CHK_NEAR(r.mc_bootstrap, 90.5, 0.01);
    CHK_NEAR(r.robustness,   90.2, 0.01);
    CHK_NEAR(r.size_mult,     1.0, 0.01);
    CHK(r.experimental == 0);
    CHK(r.paper_only   == 0);
    CHK(r.n_stages == 5);
    CHK(r.stages[0].stage  == 1);
    CHK(r.stages[0].passed == 1);
    CHK_STR_EQ(r.stages[0].name, "schema_lint");
    CHK(r.stages[1].stage  == 2);
    CHK(r.stages[1].passed == 1);
    CHK(r.stages[1].trades == 80);
    CHK(r.stages[2].stage  == 3);
    CHK_NEAR(r.stages[2].score, 91.0, 0.01);
    CHK(r.stages[3].stage  == 4);
    CHK_NEAR(r.stages[3].score, 90.5, 0.01);
    CHK(r.stages[4].stage  == 5);
    CHK_NEAR(r.stages[4].score, 90.2, 0.01);

    printf("  [PASS] from_json_green\n");
}

static void test_from_json_white(void)
{
    char *json = load_fixture("tier_report_breakout_white.json");
    if (!json) { ++g_algo_gen_failed; return; }

    af_algo_tier_report_t r;
    af_algo_error_t err;
    af_algo_error_clear(&err);
    int rc = af_algo_tier_report_from_json(json, 0, &r, &err);
    free(json);

    CHK(rc == 0);
    CHK_STR_EQ(r.manifest_name, "breakout-sma-momentum");
    CHK(r.passed == 1);
    CHK(r.tier == AF_TIER_WHITE);
    CHK_NEAR(r.min_score,    95.0, 0.01);
    CHK_NEAR(r.walk_forward, 96.0, 0.01);
    CHK_NEAR(r.mc_bootstrap, 95.5, 0.01);
    CHK_NEAR(r.robustness,   95.0, 0.01);
    CHK_NEAR(r.size_mult,     1.5, 0.01);
    CHK(r.experimental == 0);
    CHK(r.paper_only   == 0);
    CHK(r.n_stages == 5);
    CHK(r.stages[1].trades == 100);
    CHK_NEAR(r.stages[2].score, 96.0, 0.01);

    printf("  [PASS] from_json_white\n");
}

static void test_from_json_yellow(void)
{
    char *json = load_fixture("tier_report_mean_revert_yellow.json");
    if (!json) { ++g_algo_gen_failed; return; }

    af_algo_tier_report_t r;
    af_algo_error_t err;
    af_algo_error_clear(&err);
    int rc = af_algo_tier_report_from_json(json, 0, &r, &err);
    free(json);

    CHK(rc == 0);
    CHK_STR_EQ(r.manifest_name, "mean-revert-rsi-bb");
    CHK(r.passed == 1);
    CHK(r.tier == AF_TIER_YELLOW);
    CHK_NEAR(r.min_score,    80.5, 0.01);
    CHK_NEAR(r.walk_forward, 85.0, 0.01);
    CHK_NEAR(r.mc_bootstrap, 83.0, 0.01);
    CHK_NEAR(r.robustness,   80.5, 0.01);
    CHK_NEAR(r.size_mult,     0.5, 0.01);
    CHK(r.experimental == 1);
    CHK(r.paper_only   == 0);
    CHK(r.n_stages == 5);
    CHK(r.stages[1].trades == 70);

    printf("  [PASS] from_json_yellow\n");
}

static void test_from_json_orange(void)
{
    char *json = load_fixture("tier_report_trend_follow_orange.json");
    if (!json) { ++g_algo_gen_failed; return; }

    af_algo_tier_report_t r;
    af_algo_error_t err;
    af_algo_error_clear(&err);
    int rc = af_algo_tier_report_from_json(json, 0, &r, &err);
    free(json);

    CHK(rc == 0);
    CHK_STR_EQ(r.manifest_name, "trend-follow-orange");
    CHK(r.passed == 1);
    CHK(r.tier == AF_TIER_ORANGE);
    CHK_NEAR(r.min_score,    72.0, 0.01);
    CHK_NEAR(r.walk_forward, 79.0, 0.01);
    CHK_NEAR(r.mc_bootstrap, 74.0, 0.01);
    CHK_NEAR(r.robustness,   72.0, 0.01);
    CHK_NEAR(r.size_mult,    0.25, 0.01);
    CHK(r.experimental == 1);
    CHK(r.paper_only   == 1);
    CHK(r.n_stages == 5);
    CHK(r.stages[1].trades == 60);

    printf("  [PASS] from_json_orange\n");
}

static void test_from_json_invalid_schema(void)
{
    char *json = load_fixture("tier_report_invalid_schema.json");
    if (!json) { ++g_algo_gen_failed; return; }

    af_algo_tier_report_t r;
    af_algo_error_t err;
    af_algo_error_clear(&err);
    int rc = af_algo_tier_report_from_json(json, 0, &r, &err);
    free(json);

    CHK(rc == 0);
    CHK_STR_EQ(r.manifest_name, "invalid-schema-test");
    CHK(r.passed == 0);
    CHK(r.n_stages == 1);
    CHK(r.stages[0].stage  == 1);
    CHK(r.stages[0].passed == 0);
    CHK_STR_EQ(r.stages[0].name, "schema_lint");

    printf("  [PASS] from_json_invalid_schema\n");
}

static void test_from_json_wf_fail(void)
{
    char *json = load_fixture("tier_report_wf_fail.json");
    if (!json) { ++g_algo_gen_failed; return; }

    af_algo_tier_report_t r;
    af_algo_error_t err;
    af_algo_error_clear(&err);
    int rc = af_algo_tier_report_from_json(json, 0, &r, &err);
    free(json);

    CHK(rc == 0);
    CHK_STR_EQ(r.manifest_name, "wf-fail-test");
    CHK(r.passed == 0);
    CHK(r.n_stages == 3);
    CHK(r.stages[0].passed == 1);          /* schema_lint: passed */
    CHK(r.stages[1].passed == 1);          /* sandbox_backtest: passed */
    CHK(r.stages[1].trades == 30);
    CHK(r.stages[2].passed == 0);          /* walk_forward: failed */
    CHK_NEAR(r.stages[2].score, 40.0, 0.01);

    printf("  [PASS] from_json_wf_fail\n");
}

static void test_from_json_low_trade_count(void)
{
    char *json = load_fixture("tier_report_low_trade_count.json");
    if (!json) { ++g_algo_gen_failed; return; }

    af_algo_tier_report_t r;
    af_algo_error_t err;
    af_algo_error_clear(&err);
    int rc = af_algo_tier_report_from_json(json, 0, &r, &err);
    free(json);

    CHK(rc == 0);
    CHK_STR_EQ(r.manifest_name, "low-trade-count-test");
    CHK(r.passed == 0);
    CHK(r.n_stages == 2);
    CHK(r.stages[0].passed == 1);          /* schema_lint: passed */
    CHK(r.stages[1].passed == 0);          /* sandbox_backtest: failed */
    CHK(r.stages[1].trades == 0);

    printf("  [PASS] from_json_low_trade_count\n");
}

/* ============================================================
 * Section 3 — Escape hatch: manifest with code field
 * ============================================================ */

static void test_escape_hatch_manifest_parse(void)
{
    char *json = load_fixture("manifest_escape_hatch_green.json");
    if (!json) { ++g_algo_gen_failed; return; }

    af_algo_manifest_t m;
    af_algo_error_t err;
    af_algo_error_clear(&err);
    int rc = af_algo_manifest_parse(json, 0, &m, &err);
    free(json);

    CHK(rc == 0);
    CHK_STR_EQ(err.kind, "");
    CHK_STR_EQ(m.name, "escape-hatch-green");
    CHK(m.has_code == 1);
    CHK(m.code[0] != '\0');

    printf("  [PASS] escape_hatch_manifest_parse\n");
}

static void test_escape_hatch_validate_skip(void)
{
    char *json = load_fixture("manifest_escape_hatch_green.json");
    if (!json) { ++g_algo_gen_failed; return; }

    af_algo_manifest_t m;
    af_algo_error_t err;
    af_algo_error_clear(&err);
    int parse_rc = af_algo_manifest_parse(json, 0, &m, &err);
    free(json);

    if (parse_rc != 0) {
        fprintf(stderr, "SKIP test_escape_hatch_validate_skip: manifest parse failed\n");
        ++g_algo_gen_failed;
        return;
    }
    CHK(m.has_code == 1);

    af_algo_tier_report_t report;
    af_algo_error_t verr;
    af_algo_error_clear(&verr);
    int rc = af_algo_validate(&m, NULL, 0, 0, &report, &verr);

    /* Escape hatch → returns 0 (no error), sets is_escape_hatch, passed=0, n_stages=0 */
    CHK(rc == 0);
    CHK(report.is_escape_hatch == 1);
    CHK(report.passed == 0);
    CHK(report.n_stages == 0);

    printf("  [PASS] escape_hatch_validate_skip\n");
}

/* ============================================================
 * Section 4 — from_json error path
 * ============================================================ */

static void test_from_json_bad_input(void)
{
    af_algo_tier_report_t r;
    af_algo_error_t err;
    af_algo_error_clear(&err);

    /* Completely invalid JSON */
    int rc = af_algo_tier_report_from_json("NOT_JSON", 8, &r, &err);
    CHK(rc != 0);
    CHK(err.kind[0] != '\0');

    printf("  [PASS] from_json_bad_input\n");
}

/* ============================================================
 * Section 5 — metadata_create
 * ============================================================ */

static void test_metadata_create_and_destroy(void)
{
    af_algo_tier_report_t r;
    memset(&r, 0, sizeof(r));
    strncpy(r.manifest_name, "test-algo", AF_ALGO_GEN_MAX_NAME - 1);
    r.tier        = AF_TIER_GREEN;
    r.size_mult   = 1.0;
    r.experimental = 0;
    r.paper_only   = 0;

    af_algo_metadata_t *m = af_algo_metadata_create(&r);
    CHK(m != NULL);
    if (m) {
        CHK_STR_EQ(m->manifest_name, "test-algo");
        CHK_STR_EQ(m->tier, "green");
        CHK_NEAR(m->size_mult, 1.0, 1e-9);
        CHK(m->experimental == 0);
        CHK(m->paper_only   == 0);
        af_algo_metadata_destroy(m);
    }

    printf("  [PASS] metadata_create_and_destroy\n");
}

/* ============================================================
 * Test runner
 * ============================================================ */

void test_algo_gen_run(int *total_passed, int *total_failed)
{
    g_algo_gen_passed = 0;
    g_algo_gen_failed = 0;

    printf("\n--- AlgoGen Tier / Validator Tests ---\n");
    test_score_to_tier_boundaries();
    test_tier_str_and_color();
    test_tier_cfg_defaults();

    printf("\n--- AlgoGen Fixture Tests (tier reports) ---\n");
    test_from_json_green();
    test_from_json_white();
    test_from_json_yellow();
    test_from_json_orange();
    test_from_json_invalid_schema();
    test_from_json_wf_fail();
    test_from_json_low_trade_count();

    printf("\n--- AlgoGen Escape Hatch Tests ---\n");
    test_escape_hatch_manifest_parse();
    test_escape_hatch_validate_skip();

    printf("\n--- AlgoGen Error & Metadata Tests ---\n");
    test_from_json_bad_input();
    test_metadata_create_and_destroy();

    printf("\n  AlgoGen: Passed=%d  Failed=%d\n",
           g_algo_gen_passed, g_algo_gen_failed);

    *total_passed += g_algo_gen_passed;
    *total_failed += g_algo_gen_failed;
}
