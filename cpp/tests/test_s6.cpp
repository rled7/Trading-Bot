/**
 * AlgoForge — tests/test_s6.cpp
 *
 * S6 discovery-daemon golden oracle (Phase 4). Spec: docs/s6_spec.md §7.
 * S6 has no Python reference; the spec IS the oracle. Determinism comes from
 * injecting generate_fn/validate_fn (spec §8) so we test the daemon's ORCHESTRATION
 * (drift trigger, rate limit, tier-floor gate, promotion, bounded memory, pause)
 * rather than the already-tested numeric primitives it composes.
 */
#include "test_helpers.hpp"
#include "s6/discovery_daemon.hpp"
#include "core/llm.hpp"

#include <filesystem>
#include <vector>

using namespace algoforge::s6;
namespace ag = algoforge::algo_gen;
namespace fs = std::filesystem;

/* Minimal LLMProvider — never called (generate_fn is injected in every test). */
class NullProvider : public algoforge::llm::LLMProvider {
public:
    algoforge::llm::HealthStatus           health() override { return {}; }
    std::vector<algoforge::llm::ModelInfo> list_models() override { return {}; }
    algoforge::llm::CompletionResponse     complete(const algoforge::llm::CompletionRequest&) override { return {}; }
    algoforge::llm::ChatResponse           chat(const algoforge::llm::ChatRequest&) override { return {}; }
    std::vector<algoforge::llm::ChatChunk>  chat_stream(const algoforge::llm::ChatRequest&) override { return {}; }
    algoforge::llm::EmbedResponse          embed(const algoforge::llm::EmbedRequest&) override { return {}; }
};

static Bar mk(double close, int64_t ts) {
    Bar b{};
    b.timestamp = ts; b.open = b.high = b.low = b.close = close; b.volume = 100.0;
    return b;
}

/* Build closes: `flat` bars at `level`, then `trend` bars rising `step` (fractional) each. */
static std::vector<double> flat_then_trend(int flat, double level, int trend, double step) {
    std::vector<double> v;
    for (int i = 0; i < flat; ++i) v.push_back(level);
    double c = level;
    for (int i = 0; i < trend; ++i) { c *= (1.0 + step); v.push_back(c); }
    return v;
}

static ag::TierReport report_of(bool passed, ag::Tier t) {
    ag::TierReport r;
    r.passed = passed; r.tier = t; r.has_tier = true; r.manifest_name = "s6-candidate";
    return r;
}
static ag::AlgoManifest fixed_manifest() {
    ag::AlgoManifest m;
    m.schema_version = "1.0"; m.name = "s6-candidate"; m.symbols = std::string("any");
    return m;
}

/* Drive a close series; return the daemon and the per-tick event kinds. */
struct DriveResult { std::vector<DiscoveryEvent::Kind> kinds; };
static DriveResult drive(DiscoveryDaemon& d, const std::vector<double>& closes,
                         int64_t t0 = 1'000'000, int64_t dt = 1000) {
    DriveResult out;
    int64_t now = t0;
    for (size_t i = 0; i < closes.size(); ++i) {
        auto ev = d.observe(mk(closes[i], (int64_t)i), now);
        out.kinds.push_back(ev.kind);
        now += dt;
    }
    return out;
}
static int count_kind(const DriveResult& r, DiscoveryEvent::Kind k) {
    int n = 0; for (auto x : r.kinds) if (x == k) ++n; return n;
}

static DaemonConfig base_cfg() {
    DaemonConfig c;
    c.horizons_bars = {4};
    c.drift_threshold = 2.0;
    c.max_emits_per_hour = 4;
    c.target_tier_floor = ag::Tier::Yellow;
    c.max_bars_retained = 10080;
    c.pause_during_live = true;
    c.generate_fn = [](const Hypothesis&) { return fixed_manifest(); };
    return c;
}

void test_s6(RawTestFn& T) {
    using K = DiscoveryEvent::Kind;

    section("S6 oracle — no false positives on quiet data");
    T("flat series never forms a hypothesis", []{
        NullProvider llm;
        auto cfg = base_cfg();
        cfg.validate_fn = [](const ag::AlgoManifest&, const std::vector<Bar>&){ return report_of(true, ag::Tier::Green); };
        DiscoveryDaemon d(cfg, llm);
        std::vector<double> closes(40, 100.0);
        auto r = drive(d, closes);
        CHK_EQ(count_kind(r, K::ManifestEmitted), 0);
        CHK_EQ(count_kind(r, K::RateLimited), 0);
        for (auto k : r.kinds) CHK(k == K::None);
    });

    section("S6 oracle — single regime-shift => single emission");
    T("flat→trend crosses once and emits exactly one manifest", []{
        NullProvider llm;
        auto cfg = base_cfg();
        cfg.validate_fn = [](const ag::AlgoManifest&, const std::vector<Bar>&){ return report_of(true, ag::Tier::Yellow); };
        DiscoveryDaemon d(cfg, llm);
        auto closes = flat_then_trend(15, 100.0, 6, 0.01);
        auto r = drive(d, closes);
        CHK_EQ(count_kind(r, K::ManifestEmitted), 1);
        CHK_EQ(count_kind(r, K::ValidatorRejected), 0);
    });

    section("S6 oracle — validator rejection writes nothing");
    T("failed validation => ValidatorRejected, registry stays empty", []{
        NullProvider llm;
        auto dir = fs::temp_directory_path() / "s6_reject_test";
        fs::remove_all(dir);
        auto cfg = base_cfg();
        cfg.registry_dir = dir;
        cfg.validate_fn = [](const ag::AlgoManifest&, const std::vector<Bar>&){ return report_of(false, ag::Tier::Red); };
        DiscoveryDaemon d(cfg, llm);
        auto closes = flat_then_trend(15, 100.0, 6, 0.01);
        auto r = drive(d, closes);
        CHK_GE(count_kind(r, K::ValidatorRejected), 1);
        CHK_EQ(count_kind(r, K::ManifestEmitted), 0);
        CHK_FALSE(fs::exists(dir) && !fs::is_empty(dir));
        fs::remove_all(dir);
    });

    section("S6 oracle — tier floor (Decision 4)");
    T("below-floor (Orange) rejected; Green emitted + promoted", []{
        NullProvider llm;
        // Orange < Yellow floor → rejected.
        {
            auto cfg = base_cfg();
            cfg.validate_fn = [](const ag::AlgoManifest&, const std::vector<Bar>&){ return report_of(true, ag::Tier::Orange); };
            DiscoveryDaemon d(cfg, llm);
            auto r = drive(d, flat_then_trend(15, 100.0, 6, 0.01));
            CHK_EQ(count_kind(r, K::ManifestEmitted), 0);
            CHK_GE(count_kind(r, K::ValidatorRejected), 1);
        }
        // Green ≥ floor → emitted AND promoted to registry.
        {
            auto dir = fs::temp_directory_path() / "s6_promote_test";
            fs::remove_all(dir);
            auto cfg = base_cfg();
            cfg.registry_dir = dir;
            cfg.validate_fn = [](const ag::AlgoManifest&, const std::vector<Bar>&){ return report_of(true, ag::Tier::Green); };
            DiscoveryDaemon d(cfg, llm);
            auto r = drive(d, flat_then_trend(15, 100.0, 6, 0.01));
            CHK_EQ(count_kind(r, K::ManifestEmitted), 1);
            CHK(fs::exists(dir / "s6-candidate.json"));
            fs::remove_all(dir);
        }
    });

    section("S6 oracle — rate limit caps emissions (Decision 3)");
    T("many crossings within an hour => emits capped at max_emits_per_hour", []{
        NullProvider llm;
        auto cfg = base_cfg();
        cfg.max_emits_per_hour = 4;
        cfg.validate_fn = [](const ag::AlgoManifest&, const std::vector<Bar>&){ return report_of(true, ag::Tier::Yellow); };
        DiscoveryDaemon d(cfg, llm);
        // 8 up/down regime alternations → many crossings, all within one hour (dt=1s).
        std::vector<double> closes;
        double level = 100.0;
        for (int burst = 0; burst < 8; ++burst) {
            for (int i = 0; i < 12; ++i) closes.push_back(level);            // settle (de-latch)
            for (int i = 0; i < 5; ++i) { level *= 1.01; closes.push_back(level); } // trend up
        }
        auto r = drive(d, closes, 1'000'000, 1000);
        int emitted = count_kind(r, K::ManifestEmitted);
        int limited = count_kind(r, K::RateLimited);
        CHK_EQ(emitted, 4);
        CHK_GE(limited, 1);
        CHK_LE(d.emits_this_hour(closes.size() * 1000 + 1'000'000), (size_t)4);
    });

    section("S6 oracle — bounded memory (Decision 6)");
    T("ring buffer never exceeds max_bars_retained", []{
        NullProvider llm;
        auto cfg = base_cfg();
        cfg.max_bars_retained = 50;
        cfg.validate_fn = [](const ag::AlgoManifest&, const std::vector<Bar>&){ return report_of(true, ag::Tier::Yellow); };
        DiscoveryDaemon d(cfg, llm);
        for (int i = 0; i < 1050; ++i) { d.observe(mk(100.0 + (i % 3), i), 1'000'000 + i); }
        CHK_LE(d.buffer_size(), (size_t)50);
    });

    section("S6 oracle — pause during live (Decision 6)");
    T("live-session flag => every observe() yields None", []{
        NullProvider llm;
        auto cfg = base_cfg();
        cfg.validate_fn = [](const ag::AlgoManifest&, const std::vector<Bar>&){ return report_of(true, ag::Tier::Green); };
        DiscoveryDaemon d(cfg, llm);
        d.set_live_session(true);
        auto r = drive(d, flat_then_trend(15, 100.0, 10, 0.02));
        for (auto k : r.kinds) CHK(k == K::None);
        CHK_EQ(d.buffer_size(), (size_t)0);   // paused observe() retains nothing
    });
}
