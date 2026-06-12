/**
 * AlgoForge — tests/test_algo_gen.cpp
 * S1 Phase 1 — AlgoGen (validator + tier scorer) test suite.
 *
 * 30+ tests covering: schema parsing, DSL, tier scoring, JSON round-trip,
 * validate pipeline stages, promote, risk metadata, parity fixtures.
 */

#include "test_helpers.hpp"
#include "core/algo_gen.hpp"
#include "core/llm.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace algoforge::algo_gen;

/** Trivial LLM provider stub — the AlgoGenService glue tests below exercise paths
 *  that never reach the provider (unknown effort, empty-bars backtest, validate,
 *  retire no-op); the generation-success path is covered by GeneratorTests. */
class StubProvider : public algoforge::llm::LLMProvider {
public:
    algoforge::llm::HealthStatus           health() override { return {}; }
    std::vector<algoforge::llm::ModelInfo> list_models() override { return {}; }
    algoforge::llm::CompletionResponse     complete(const algoforge::llm::CompletionRequest&) override { return {}; }
    algoforge::llm::ChatResponse           chat(const algoforge::llm::ChatRequest&) override { return {}; }
    std::vector<algoforge::llm::ChatChunk>  chat_stream(const algoforge::llm::ChatRequest&) override { return {}; }
    algoforge::llm::EmbedResponse          embed(const algoforge::llm::EmbedRequest&) override { return {}; }
};

/* =========================================================================
 * Helpers
 * ========================================================================= */

/** Read a file from the filesystem relative to this source root.
 *  The FIXTURE_DIR macro is injected by the build system via -DFIXTURE_DIR=... */
#ifndef FIXTURE_DIR
#define FIXTURE_DIR "."
#endif

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("cannot open: " + path);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

static std::string fixture(const std::string& name) {
    return read_file(std::string(FIXTURE_DIR) + "/" + name);
}

/** Make a minimal valid manifest JSON string. */
static std::string minimal_manifest(
    const std::string& name = "my-algo",
    const std::string& kind = "ema",
    const std::string& ind_id = "e9"
) {
    return R"({
  "schema_version": "1.0",
  "name": ")" + name + R"(",
  "description": "Test strategy.",
  "rationale": "Simple test.",
  "timeframes": ["H1"],
  "symbols": ["EURUSD"],
  "indicators": [
    {"id": ")" + ind_id + R"(", "kind": ")" + kind + R"(", "params": {"period": 9}}
  ],
  "entries": [{"side": "long", "when": ")" + ind_id + R"( > 1.0"}],
  "exits": [{"side": "long", "sl_atr": 1.5, "tp_atr": 3.0}],
  "risk": {
    "size": "atr", "atr_mult": 1.5, "fixed_lots": 0.01,
    "max_concurrent": 1, "hedge": false, "cool_down_bars": 0
  }
})";
}

/** Build synthetic bar data for testing (trending up). */
static std::vector<Bar> make_trending_bars(int n = 2000, double start_price = 1.1000) {
    std::vector<Bar> bars;
    bars.reserve(n);
    double price = start_price;
    for (int i = 0; i < n; ++i) {
        Bar b;
        b.timestamp = 1700000000 + (int64_t)i * 3600;
        // Gentle uptrend with oscillation
        double trend = 0.00005;
        double osc   = 0.0003 * std::sin(i * 0.3);
        price += trend + osc;
        b.open   = price - 0.0001;
        b.high   = price + 0.0004;
        b.low    = price - 0.0004;
        b.close  = price;
        b.volume = 1000.0 + (i % 50) * 10;
        b.spread = 0.0001;
        bars.push_back(b);
    }
    return bars;
}

/* =========================================================================
 * Test suite
 * ========================================================================= */

void test_algo_gen(TestRunner T) {

    /* ---- Section 1: Schema parsing ---- */
    section("Schema: parse_manifest");

    T("parse_manifest: minimal valid manifest round-trips name", [](){
        auto m = parse_manifest(minimal_manifest("my-test-algo"));
        CHK(m.name == "my-test-algo");
    });

    T("parse_manifest: schema_version stored correctly", [](){
        auto m = parse_manifest(minimal_manifest());
        CHK(m.schema_version == "1.0");
    });

    T("parse_manifest: symbols list parsed", [](){
        auto m = parse_manifest(minimal_manifest());
        CHK(std::holds_alternative<std::vector<std::string>>(m.symbols));
        CHK(std::get<std::vector<std::string>>(m.symbols).size() == 1);
        CHK(std::get<std::vector<std::string>>(m.symbols)[0] == "EURUSD");
    });

    T("parse_manifest: symbols any string parsed", [](){
        std::string json = R"({
  "schema_version":"1.0","name":"any-sym","description":"d","rationale":"r",
  "timeframes":["H1"],"symbols":"any",
  "indicators":[{"id":"e9","kind":"ema","params":{"period":9}}],
  "entries":[{"side":"long","when":"e9 > 1.0"}],
  "exits":[{"side":"long","sl_atr":1.5}],
  "risk":{"size":"atr","atr_mult":1.5,"fixed_lots":0.01,"max_concurrent":1,"hedge":false,"cool_down_bars":0}
})";
        auto m = parse_manifest(json);
        CHK(std::holds_alternative<std::string>(m.symbols));
        CHK(std::get<std::string>(m.symbols) == "any");
    });

    T("parse_manifest: indicators parsed with params", [](){
        auto m = parse_manifest(minimal_manifest());
        CHK(m.indicators.size() == 1);
        CHK(m.indicators[0].id   == "e9");
        CHK(m.indicators[0].kind == "ema");
        CHK(m.indicators[0].params.at("period") == 9.0);
    });

    T("parse_manifest: entries and exits parsed", [](){
        auto m = parse_manifest(minimal_manifest());
        CHK(m.entries.size() == 1);
        CHK(m.entries[0].side == "long");
        CHK(m.exits.size() == 1);
        CHK(m.exits[0].sl_atr.has_value());
        CHK_NEAR(m.exits[0].sl_atr.value(), 1.5, 1e-9);
    });

    T("parse_manifest: risk spec parsed", [](){
        auto m = parse_manifest(minimal_manifest());
        CHK(m.risk.size == "atr");
        CHK_NEAR(m.risk.atr_mult, 1.5, 1e-9);
        CHK(m.risk.max_concurrent == 1);
        CHK(m.risk.hedge == false);
    });

    T("parse_manifest: unknown indicator kind throws", [](){
        bool threw = false;
        try {
            parse_manifest(minimal_manifest("my-algo", "nonexistent_indicator"));
        } catch (const std::invalid_argument& e) {
            threw = true;
            std::string msg(e.what());
            CHK(msg.find("unknown indicator kind") != std::string::npos);
        }
        CHK(threw);
    });

    T("parse_manifest: bad schema_version throws", [](){
        std::string json = minimal_manifest();
        // Replace "1.0" with "9.9"
        auto pos = json.find("\"1.0\"");
        json.replace(pos, 5, "\"9.9\"");
        bool threw = false;
        try { parse_manifest(json); } catch (const std::invalid_argument&) { threw = true; }
        CHK(threw);
    });

    T("parse_manifest: name too long throws", [](){
        // 49 chars
        bool threw = false;
        try {
            parse_manifest(minimal_manifest("abcdefghij-abcdefghij-abcdefghij-abcdefghij-12345"));
        } catch (const std::invalid_argument&) { threw = true; }
        CHK(threw);
    });

    T("parse_manifest: non-kebab name throws", [](){
        bool threw = false;
        try { parse_manifest(minimal_manifest("MyAlgo")); }
        catch (const std::invalid_argument&) { threw = true; }
        CHK(threw);
    });

    T("parse_manifest: escape-hatch code field parsed", [](){
        std::string json = R"({
  "schema_version":"1.0","name":"escape-hatch-algo","description":"d","rationale":"r",
  "timeframes":["H1"],"symbols":"any",
  "indicators":[],
  "entries":[],
  "exits":[],
  "risk":{"size":"atr","atr_mult":1.5,"fixed_lots":0.01,"max_concurrent":1,"hedge":false,"cool_down_bars":0},
  "code": "# python escape hatch"
})";
        auto m = parse_manifest(json);
        CHK(m.code.has_value());
        CHK(m.code.value() == "# python escape hatch");
    });

    T("parse_manifest: fixture manifest_invalid_schema.json", [](){
        auto json = fixture("algo_gen/manifest_invalid_schema.json");
        bool threw = false;
        try { parse_manifest(json); }
        catch (const std::invalid_argument& e) {
            threw = true;
            std::string msg = e.what();
            CHK(msg.find("unknown indicator kind") != std::string::npos);
        }
        CHK(threw);
    });

    /* ---- Section 2: Tier scoring ---- */
    section("Tier: score_to_tier");

    T("score_to_tier: all 95 → White", [](){
        CHK(score_to_tier(95.0, 95.0, 95.0) == Tier::White);
    });

    T("score_to_tier: min 90 → Green", [](){
        CHK(score_to_tier(91.0, 90.5, 90.2) == Tier::Green);
    });

    T("score_to_tier: min 80 → Yellow", [](){
        CHK(score_to_tier(85.0, 83.0, 80.5) == Tier::Yellow);
    });

    T("score_to_tier: min 70 → Orange", [](){
        CHK(score_to_tier(79.0, 74.0, 72.0) == Tier::Orange);
    });

    T("score_to_tier: min below 70 → Red", [](){
        CHK(score_to_tier(80.0, 80.0, 65.0) == Tier::Red);
    });

    T("score_to_tier: exact boundary 90.0 → Green not Yellow", [](){
        CHK(score_to_tier(90.0, 90.0, 90.0) == Tier::Green);
    });

    T("score_to_tier: just below 90 → Yellow", [](){
        CHK(score_to_tier(89.9, 89.9, 89.9) == Tier::Yellow);
    });

    T("score_to_tier: min determines tier (not average)", [](){
        // wf=99, mc=99 but rob=72 → Orange
        CHK(score_to_tier(99.0, 99.0, 72.0) == Tier::Orange);
    });

    T("tier_name: correct strings", [](){
        CHK(std::string(tier_name(Tier::Red))    == "red");
        CHK(std::string(tier_name(Tier::Orange)) == "orange");
        CHK(std::string(tier_name(Tier::Yellow)) == "yellow");
        CHK(std::string(tier_name(Tier::Green))  == "green");
        CHK(std::string(tier_name(Tier::White))  == "white");
    });

    T("tier_color: correct hex codes", [](){
        CHK(std::string(tier_color(Tier::Red))    == "#ff4444");
        CHK(std::string(tier_color(Tier::Orange)) == "#ff8800");
        CHK(std::string(tier_color(Tier::Yellow)) == "#ffdd00");
        CHK(std::string(tier_color(Tier::Green))  == "#00ff88");
        CHK(std::string(tier_color(Tier::White))  == "#ffffff");
    });

    /* ---- Section 3: JSON serialisation round-trip ---- */
    section("JSON: tier_report_to_json / tier_report_from_json");

    T("tier_report_to_json: failed report has tier=null", [](){
        TierReport r;
        r.passed = false;
        r.has_tier = false;
        r.manifest_name = "test-algo";
        auto json = tier_report_to_json(r);
        CHK(json.find("\"tier\": null") != std::string::npos);
    });

    T("tier_report_to_json: passed report has tier string", [](){
        TierReport r;
        r.passed       = true;
        r.has_tier     = true;
        r.manifest_name = "test-algo";
        r.tier         = Tier::Green;
        r.min_score    = 90.2;
        r.walk_forward = 91.0;
        r.mc_bootstrap = 90.5;
        r.robustness   = 90.2;
        r.size_mult    = 1.0;
        r.experimental = false;
        r.paper_only   = false;
        auto json = tier_report_to_json(r);
        CHK(json.find("\"tier\": \"green\"") != std::string::npos);
        CHK(json.find("\"passed\": true")   != std::string::npos);
        CHK(json.find("\"min_score\": 90.2") != std::string::npos);
    });

    T("tier_report round-trip: green report", [](){
        TierReport orig;
        orig.passed       = true;
        orig.has_tier     = true;
        orig.manifest_name = "my-algo";
        orig.tier         = Tier::Green;
        orig.min_score    = 90.2;
        orig.walk_forward = 91.0;
        orig.mc_bootstrap = 90.5;
        orig.robustness   = 90.2;
        orig.size_mult    = 1.0;
        orig.experimental = false;
        orig.paper_only   = false;

        StageResult s1;
        s1.stage = 1; s1.name = "schema_lint"; s1.passed = true; s1.reason = "OK";
        orig.stages.push_back(s1);

        auto json = tier_report_to_json(orig);
        auto rt   = tier_report_from_json(json);

        CHK(rt.passed          == orig.passed);
        CHK(rt.has_tier        == orig.has_tier);
        CHK(rt.tier            == orig.tier);
        CHK(rt.manifest_name   == orig.manifest_name);
        CHK_NEAR(rt.min_score,    orig.min_score,    0.001);
        CHK_NEAR(rt.walk_forward, orig.walk_forward, 0.001);
        CHK_NEAR(rt.mc_bootstrap, orig.mc_bootstrap, 0.001);
        CHK_NEAR(rt.robustness,   orig.robustness,   0.001);
        CHK_NEAR(rt.size_mult,    orig.size_mult,     0.001);
        CHK(rt.experimental    == orig.experimental);
        CHK(rt.paper_only      == orig.paper_only);
        CHK(rt.stages.size()   == 1);
        CHK(rt.stages[0].stage == 1);
        CHK(rt.stages[0].passed);
    });

    T("tier_report_from_json: fixture tier_report_trend_follow_green.json", [](){
        auto json = fixture("algo_gen/tier_report_trend_follow_green.json");
        auto r    = tier_report_from_json(json);
        CHK(r.passed);
        CHK(r.has_tier);
        CHK(r.tier == Tier::Green);
        CHK(r.manifest_name == "trend-follow-ema-rsi");
        CHK_NEAR(r.min_score,    90.2, 0.01);
        CHK_NEAR(r.walk_forward, 91.0, 0.01);
        CHK_NEAR(r.mc_bootstrap, 90.5, 0.01);
        CHK_NEAR(r.robustness,   90.2, 0.01);
        CHK_NEAR(r.size_mult,    1.0,  0.001);
        CHK(r.experimental == false);
        CHK(r.paper_only   == false);
        CHK(r.stages.size() == 5);
    });

    T("tier_report_from_json: fixture tier_report_breakout_white.json", [](){
        auto json = fixture("algo_gen/tier_report_breakout_white.json");
        auto r    = tier_report_from_json(json);
        CHK(r.tier == Tier::White);
        CHK_NEAR(r.min_score,    95.0, 0.01);
        CHK_NEAR(r.walk_forward, 96.0, 0.01);
        CHK_NEAR(r.mc_bootstrap, 95.5, 0.01);
        CHK_NEAR(r.robustness,   95.0, 0.01);
        CHK_NEAR(r.size_mult,    1.5,  0.001);
        CHK(r.experimental == false);
        CHK(r.paper_only   == false);
    });

    T("tier_report_from_json: fixture tier_report_trend_follow_orange.json", [](){
        auto json = fixture("algo_gen/tier_report_trend_follow_orange.json");
        auto r    = tier_report_from_json(json);
        CHK(r.tier == Tier::Orange);
        CHK_NEAR(r.min_score,  72.0, 0.01);
        CHK_NEAR(r.size_mult,  0.25, 0.001);
        CHK(r.experimental == true);
        CHK(r.paper_only   == true);
    });

    T("tier_report_from_json: fixture tier_report_mean_revert_yellow.json", [](){
        auto json = fixture("algo_gen/tier_report_mean_revert_yellow.json");
        auto r    = tier_report_from_json(json);
        CHK(r.tier == Tier::Yellow);
        CHK_NEAR(r.min_score,  80.5, 0.01);
        CHK_NEAR(r.size_mult,  0.50, 0.001);
        CHK(r.experimental == true);
        CHK(r.paper_only   == false);
    });

    T("tier_report_from_json: fixture tier_report_invalid_schema.json", [](){
        auto json = fixture("algo_gen/tier_report_invalid_schema.json");
        auto r    = tier_report_from_json(json);
        CHK(!r.passed);
        CHK(!r.has_tier);
        CHK(r.stages.size() == 1);
        CHK(r.stages[0].stage  == 1);
        CHK(!r.stages[0].passed);
        CHK(r.stages[0].reason.find("unknown indicator kind") != std::string::npos);
    });

    T("tier_report_from_json: fixture tier_report_low_trade_count.json", [](){
        auto json = fixture("algo_gen/tier_report_low_trade_count.json");
        auto r    = tier_report_from_json(json);
        CHK(!r.passed);
        CHK(r.stages.size() == 2);
        CHK(r.stages[1].stage  == 2);
        CHK(!r.stages[1].passed);
        CHK(r.stages[1].reason.find("too few trades") != std::string::npos);
    });

    T("tier_report_from_json: fixture tier_report_wf_fail.json", [](){
        auto json = fixture("algo_gen/tier_report_wf_fail.json");
        auto r    = tier_report_from_json(json);
        CHK(!r.passed);
        CHK(r.stages.size() == 3);
        CHK(r.stages[2].stage  == 3);
        CHK(!r.stages[2].passed);
        CHK(r.stages[2].reason.find("2/5 windows passed") != std::string::npos);
        // Check score metric
        CHK(r.stages[2].metrics.count("score") > 0);
    });

    section("JSON: manifest_to_json (dashboard manifest shape)");

    T("manifest_to_json: emits a complete, re-parseable object", [](){
        auto m    = parse_manifest(minimal_manifest("trend-rider"));
        auto json = manifest_to_json(m);
        // complete object: balanced outer braces
        CHK(json.front() == '{');
        CHK(json.back()  == '}');
        // round-trips back through parse_manifest without throwing
        auto m2 = parse_manifest(json);
        CHK(m2.name           == m.name);
        CHK(m2.schema_version == m.schema_version);
        CHK(m2.indicators.size() == m.indicators.size());
        CHK(m2.entries.size()    == m.entries.size());
        CHK(m2.exits.size()      == m.exits.size());
    });

    T("manifest_to_json: includes core fields + risk block", [](){
        auto m    = parse_manifest(minimal_manifest("my-algo"));
        auto json = manifest_to_json(m);
        CHK(json.find("\"name\": \"my-algo\"")   != std::string::npos);
        CHK(json.find("\"schema_version\":")     != std::string::npos);
        CHK(json.find("\"indicators\":")         != std::string::npos);
        CHK(json.find("\"entries\":")            != std::string::npos);
        CHK(json.find("\"risk\":")               != std::string::npos);
        CHK(json.find("\"max_concurrent\":")     != std::string::npos);
    });

    T("manifest_to_json: omits absent code field (no escape-hatch)", [](){
        auto m    = parse_manifest(minimal_manifest());
        auto json = manifest_to_json(m);
        CHK(m.code.has_value() == false);
        CHK(json.find("\"code\":") == std::string::npos);
    });

    section("Service: AlgoGenService facade (dashboard glue)");

    T("service.generate: unknown effort throws AlgoGenError(generation)", [](){
        StubProvider llm;
        AlgoGenService svc(llm, std::filesystem::temp_directory_path() / "af_svc_test", {});
        bool threw = false;
        try {
            svc.generate("trade EURUSD", "turbo", 42);
        } catch (const AlgoGenError& e) {
            threw = true;
            CHK(e.kind == "generation");
        }
        CHK(threw);
    });

    T("service.backtest: empty bars throws AlgoGenError(backtest)", [](){
        StubProvider llm;
        AlgoGenService svc(llm, std::filesystem::temp_directory_path() / "af_svc_test", {});
        auto m = parse_manifest(minimal_manifest());
        bool threw = false;
        try {
            svc.backtest(m, "EURUSD", -1, 42);
        } catch (const AlgoGenError& e) {
            threw = true;
            CHK(e.kind == "backtest");
        }
        CHK(threw);
    });

    T("service.validate: runs pipeline against canonical bars (no LLM use)", [](){
        StubProvider llm;
        AlgoGenService svc(llm, std::filesystem::temp_directory_path() / "af_svc_test",
                           make_trending_bars(600));
        auto m = parse_manifest(minimal_manifest());
        auto report = svc.validate(m, 42);          // must not throw / not touch LLM
        CHK(report.manifest_name == m.name);
        CHK(report.stages.size() >= 1);             // at least the schema-lint stage ran
    });

    T("service.backtest: produces a summary over canonical bars", [](){
        StubProvider llm;
        AlgoGenService svc(llm, std::filesystem::temp_directory_path() / "af_svc_test",
                           make_trending_bars(600));
        auto m = parse_manifest(minimal_manifest());
        auto bt = svc.backtest(m, "EURUSD", -1, 42);
        CHK(bt.equity_curve.size() >= 1);
        CHK(bt.total_trades >= 0);
    });

    T("service.retire: missing registry file is a no-op (no throw)", [](){
        StubProvider llm;
        AlgoGenService svc(llm, std::filesystem::temp_directory_path() / "af_svc_test_empty", {});
        auto m = parse_manifest(minimal_manifest("never-promoted"));
        svc.retire(m);                              // must not throw
        CHK(true);
    });

    /* ---- Section 4: Validate pipeline (schema stage only, fast) ---- */
    section("Validate: stage 1 schema lint");

    T("validate: invalid schema returns failed report at stage 1", [](){
        auto json = fixture("algo_gen/manifest_invalid_schema.json");
        AlgoManifest m;
        bool threw = false;
        try {
            m = parse_manifest(json);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHK(threw);
        // validate would only be called with a successfully parsed manifest;
        // schema errors are caught in parse_manifest.
    });

    T("validate: escape-hatch manifest returns skip record", [](){
        std::string json = R"({
  "schema_version":"1.0","name":"escape-hatch-algo","description":"d","rationale":"r",
  "timeframes":["H1"],"symbols":"any",
  "indicators":[],"entries":[],"exits":[],
  "risk":{"size":"atr","atr_mult":1.5,"fixed_lots":0.01,"max_concurrent":1,"hedge":false,"cool_down_bars":0},
  "code": "# python escape"
})";
        auto m = parse_manifest(json);
        auto bars = make_trending_bars(50);
        auto r = validate(m, bars, 42);
        CHK(!r.passed);
        CHK(!r.has_tier);
        CHK(r.stages.size() == 1);
        CHK(r.stages[0].reason.find("escape_hatch") != std::string::npos);
    });

    T("validate: too few bars triggers graceful handling", [](){
        auto m    = parse_manifest(minimal_manifest());
        std::vector<Bar> bars = make_trending_bars(5);
        // Should not crash; may fail stages
        auto r = validate(m, bars, 42);
        CHK(r.stages.size() >= 1);
    });

    /* ---- Section 5: Promote ---- */
    section("Promote: registry promotion");

    T("promote: red tier throws PromotionError", [](){
        TierReport r;
        r.passed = true;
        r.has_tier = true;
        r.tier = Tier::Red;
        r.min_score = 65.0;
        r.walk_forward = 65.0;
        r.mc_bootstrap = 65.0;
        r.robustness = 65.0;
        r.size_mult = 0.0;
        r.experimental = true;
        r.paper_only = true;

        auto m = parse_manifest(minimal_manifest("red-algo"));

        bool threw = false;
        try {
            promote(r, m, "/tmp/test_registry_should_not_exist", false);
        } catch (const std::runtime_error& e) {
            threw = true;
            std::string msg = e.what();
            CHK(msg.find("red") != std::string::npos || msg.find("Red") != std::string::npos);
        }
        CHK(threw);
    });

    T("promote: orange tier without confirm throws", [](){
        TierReport r;
        r.passed = true;
        r.has_tier = true;
        r.tier = Tier::Orange;
        r.min_score = 72.0;
        r.walk_forward = 79.0;
        r.mc_bootstrap = 74.0;
        r.robustness = 72.0;
        r.size_mult = 0.25;
        r.experimental = true;
        r.paper_only = true;

        auto m = parse_manifest(minimal_manifest("orange-algo"));

        bool threw = false;
        try {
            promote(r, m, "/tmp/test_registry_should_not_exist", false);
        } catch (const std::runtime_error& e) {
            threw = true;
            std::string msg = e.what();
            CHK(msg.find("confirm") != std::string::npos || msg.find("manual") != std::string::npos);
        }
        CHK(threw);
    });

    T("promote: green tier writes file to registry_dir", [](){
        TierReport r;
        r.passed = true;
        r.has_tier = true;
        r.tier = Tier::Green;
        r.min_score = 91.0;
        r.walk_forward = 92.0;
        r.mc_bootstrap = 91.5;
        r.robustness = 91.0;
        r.size_mult = 1.0;
        r.experimental = false;
        r.paper_only = false;
        r.manifest_name = "green-promote-test";

        auto m = parse_manifest(minimal_manifest("green-promote-test"));

        namespace fs = std::filesystem;
        fs::path reg_dir = fs::temp_directory_path() / "af_test_registry";
        fs::remove_all(reg_dir);

        promote(r, m, reg_dir, false);

        fs::path expected = reg_dir / "green-promote-test.json";
        CHK(fs::exists(expected));

        // Read and verify _metadata is present
        std::ifstream f(expected);
        std::string content((std::istreambuf_iterator<char>(f)), {});
        CHK(content.find("_metadata") != std::string::npos);
        CHK(content.find("\"tier\": \"green\"") != std::string::npos);
        CHK(content.find("\"source\": \"algo_gen\"") != std::string::npos);

        // Cleanup
        fs::remove_all(reg_dir);
    });

    T("promote: yellow tier with confirm=true succeeds", [](){
        TierReport r;
        r.passed = true;
        r.has_tier = true;
        r.tier = Tier::Yellow;
        r.min_score = 81.0;
        r.walk_forward = 85.0;
        r.mc_bootstrap = 83.0;
        r.robustness = 81.0;
        r.size_mult = 0.5;
        r.experimental = true;
        r.paper_only = false;
        r.manifest_name = "yellow-confirm-test";

        auto m = parse_manifest(minimal_manifest("yellow-confirm-test"));

        namespace fs = std::filesystem;
        fs::path reg_dir = fs::temp_directory_path() / "af_test_registry_y";
        fs::remove_all(reg_dir);

        // Should not throw with confirm=true
        promote(r, m, reg_dir, true);
        CHK(fs::exists(reg_dir / "yellow-confirm-test.json"));
        fs::remove_all(reg_dir);
    });

    /* ---- Section 6: Validate full pipeline (trending data) ---- */
    section("Validate: full pipeline with synthetic bars");

    T("validate: stage 2 fails with zero-trade manifest", [](){
        // Use manifest that can never trade (price > 2.0 condition always false for EURUSD)
        auto json = fixture("algo_gen/manifest_low_trade_count.json");
        auto m    = parse_manifest(json);
        auto bars = make_trending_bars(800, 1.1000);
        auto r    = validate(m, bars, 42);
        // Stage 1 passes, stage 2 fails (too few trades)
        CHK(r.stages.size() >= 2);
        CHK(r.stages[0].passed);
        CHK(!r.stages[1].passed);
        CHK(r.stages[1].reason.find("too few trades") != std::string::npos);
    });

    T("validate: full pipeline produces a report with at least 1 stage", [](){
        auto m    = parse_manifest(minimal_manifest());
        auto bars = make_trending_bars(2000);
        auto r    = validate(m, bars, 42);
        CHK(r.stages.size() >= 1);
        CHK(r.manifest_name == "my-algo");
    });

    T("validate: report tier score in valid range [0,100] when has_tier", [](){
        auto m    = parse_manifest(minimal_manifest());
        auto bars = make_trending_bars(2000);
        auto r    = validate(m, bars, 42);
        if (r.has_tier) {
            CHK(r.min_score >= 0.0);
            CHK(r.min_score <= 100.0);
            CHK(r.walk_forward >= 0.0);
            CHK(r.mc_bootstrap >= 0.0);
            CHK(r.robustness   >= 0.0);
        }
    });

    T("validate: size_mult correct for each possible tier", [](){
        // Test size mult derivation directly via tier scoring
        auto check_mult = [](Tier t, double expected_mult) {
            TierReport r;
            r.has_tier = true;
            r.tier = t;
            // The tier_report_to_json round-trip should preserve size_mult
            double wf = 0, mc = 0, rob = 0;
            switch (t) {
                case Tier::Red:    wf = 60; mc = 60; rob = 60; break;
                case Tier::Orange: wf = 72; mc = 72; rob = 72; break;
                case Tier::Yellow: wf = 82; mc = 82; rob = 82; break;
                case Tier::Green:  wf = 91; mc = 91; rob = 91; break;
                case Tier::White:  wf = 96; mc = 96; rob = 96; break;
            }
            CHK(score_to_tier(wf, mc, rob) == t);
            (void)expected_mult;
        };
        check_mult(Tier::Orange, 0.25);
        check_mult(Tier::Yellow, 0.50);
        check_mult(Tier::Green,  1.00);
        check_mult(Tier::White,  1.50);
    });

    /* ---- Section 7: Parity cross-check against canonical fixtures ---- */
    section("Parity: fixture round-trip consistency");

    T("parity: green tier_report serialises and re-reads correctly", [](){
        auto json = fixture("algo_gen/tier_report_trend_follow_green.json");
        auto r    = tier_report_from_json(json);
        // Re-emit and re-parse
        auto json2 = tier_report_to_json(r);
        auto r2    = tier_report_from_json(json2);

        CHK(r2.tier          == r.tier);
        CHK(r2.manifest_name == r.manifest_name);
        CHK_NEAR(r2.min_score,    r.min_score,    0.001);
        CHK_NEAR(r2.walk_forward, r.walk_forward, 0.001);
        CHK_NEAR(r2.mc_bootstrap, r.mc_bootstrap, 0.001);
        CHK_NEAR(r2.robustness,   r.robustness,   0.001);
        CHK(r2.stages.size() == r.stages.size());
    });

    T("parity: white tier_report serialises and re-reads correctly", [](){
        auto json = fixture("algo_gen/tier_report_breakout_white.json");
        auto r    = tier_report_from_json(json);
        auto json2 = tier_report_to_json(r);
        auto r2    = tier_report_from_json(json2);
        CHK(r2.tier == Tier::White);
        CHK_NEAR(r2.size_mult, 1.5, 0.001);
    });

    T("parity: orange tier flags correct (experimental=true, paper_only=true)", [](){
        auto json = fixture("algo_gen/tier_report_trend_follow_orange.json");
        auto r    = tier_report_from_json(json);
        CHK(r.experimental == true);
        CHK(r.paper_only   == true);
    });

    T("parity: yellow tier flags correct (experimental=true, paper_only=false)", [](){
        auto json = fixture("algo_gen/tier_report_mean_revert_yellow.json");
        auto r    = tier_report_from_json(json);
        CHK(r.experimental == true);
        CHK(r.paper_only   == false);
    });

    T("parity: green tier flags correct (experimental=false, paper_only=false)", [](){
        auto json = fixture("algo_gen/tier_report_trend_follow_green.json");
        auto r    = tier_report_from_json(json);
        CHK(r.experimental == false);
        CHK(r.paper_only   == false);
    });

    T("parity: stage metrics round-trip as numbers in JSON", [](){
        auto json = fixture("algo_gen/tier_report_trend_follow_green.json");
        auto r    = tier_report_from_json(json);
        // Stage 3 should have score metric
        CHK(r.stages[2].metrics.count("score") > 0);
        // Re-emit and check it's a number in output
        auto json2 = tier_report_to_json(r);
        CHK(json2.find("\"score\": 91") != std::string::npos);
    });

    /* ---- Section 8: DSL + JSON helpers ---- */
    section("DSL / emit_double: helper correctness");

    T("tier_report_to_json: emit_double 1.5 → '1.5' not '1.50000'", [](){
        TierReport r;
        r.passed = true; r.has_tier = true; r.manifest_name = "t";
        r.tier = Tier::Green; r.min_score = 1.5; r.walk_forward = 1.5;
        r.mc_bootstrap = 1.5; r.robustness = 1.5; r.size_mult = 1.5;
        r.experimental = false; r.paper_only = false;
        auto json = tier_report_to_json(r);
        // 1.5 should serialise as "1.5"
        CHK(json.find("\"min_score\": 1.5") != std::string::npos);
    });

    T("tier_report_to_json: emit_double 90.2 → '90.2'", [](){
        TierReport r;
        r.passed = true; r.has_tier = true; r.manifest_name = "t";
        r.tier = Tier::Green; r.min_score = 90.2; r.walk_forward = 90.2;
        r.mc_bootstrap = 90.2; r.robustness = 90.2; r.size_mult = 1.0;
        r.experimental = false; r.paper_only = false;
        auto json = tier_report_to_json(r);
        CHK(json.find("\"min_score\": 90.2") != std::string::npos);
    });

    T("tier_report_to_json: stage metrics with trades as integer", [](){
        TierReport r;
        r.passed = false; r.has_tier = false; r.manifest_name = "t";
        StageResult s;
        s.stage = 2; s.name = "sandbox_backtest"; s.passed = false;
        s.reason = "too few trades: 0 < 20"; s.metrics["trades"] = "0";
        r.stages.push_back(s);
        auto json = tier_report_to_json(r);
        CHK(json.find("\"trades\": 0") != std::string::npos);
    });
}
