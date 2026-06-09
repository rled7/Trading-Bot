/**
 * AlgoForge — include/s6/discovery_daemon.hpp
 *
 * S6: Background C++ algo discovery daemon (Phase 4).
 * Implements docs/s6_spec.md. Adds ONLY an orchestration layer above already-
 * tested primitives (af::classify_regime, algo_gen::generate_balanced / validate /
 * promote). No new market math.
 *
 * Determinism contract (spec §8): observe() is the unit of test and is pure given
 * (internal state, inputs, injected generate/validate fns). run()/stop() are a thin
 * single-thread wrapper (Decision 6) that only calls observe().
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "core/algo_gen.hpp"   // AlgoManifest, TierReport, Tier, Bar, validate, promote
#include "analysis/analysis.hpp" // af::VolRegime, af::classify_regime
#include "core/llm.hpp"        // algoforge::llm::LLMProvider

namespace algoforge::s6 {

using algo_gen::AlgoManifest;
using algo_gen::Bar;
using algo_gen::Tier;
using algo_gen::TierReport;

/** One detected opportunity, before it becomes a manifest. */
struct Hypothesis {
    int64_t       detected_at = 0;
    int           horizon_bars = 0;
    af::VolRegime regime = af::VolRegime::LOW_RANGING;
    double        drift_z = 0.0;               ///< statistical-drift score (trigger)
    double        pattern_cooccurrence = 0.0;
    std::string   summary;                     ///< human-readable; seeds the LLM brief
};

/** Result of one observe() tick. */
struct DiscoveryEvent {
    enum class Kind { None, HypothesisFormed, ManifestEmitted, RateLimited, ValidatorRejected };
    Kind                        kind = Kind::None;
    std::optional<Hypothesis>   hypothesis;
    std::optional<AlgoManifest> manifest;
    std::optional<TierReport>   report;
};

/** Daemon configuration. Tunables + injectable dependencies (Decisions 2/3/4/5/6). */
struct DaemonConfig {
    std::vector<int> horizons_bars   = {60, 1440, 10080}; // ~1h/24h/7d on M1 (Decision 2)
    double           drift_threshold = 2.0;               // z-score crossing => candidate (Decision 3)
    int              max_emits_per_hour = 4;              // rate limit (Decision 3)
    Tier             target_tier_floor  = Tier::Yellow;   // S1 "mid" tier (Decision 4)
    size_t           max_bars_retained  = 10080;          // bounded memory (Decision 6)
    bool             pause_during_live  = true;           // yield to live trading (Decision 6)
    std::filesystem::path registry_dir;                   // promote() target (Decision 5)

    // Injected pipeline steps. Left empty => defaults wired in the constructor to the
    // real generate_balanced / validate (production path). Tests override for determinism.
    std::function<AlgoManifest(const Hypothesis&)>                                  generate_fn;
    std::function<TierReport(const AlgoManifest&, const std::vector<Bar>&)>         validate_fn;
};

class DiscoveryDaemon {
public:
    DiscoveryDaemon(DaemonConfig cfg, algoforge::llm::LLMProvider& llm);
    ~DiscoveryDaemon();

    /** Deterministic single step. Feed one new bar; returns what (if anything) happened.
     *  ALL daemon logic is reachable here. now_ms is wall-clock for the rate limiter. */
    DiscoveryEvent observe(const Bar& bar, int64_t now_ms);

    /** Decision 6: spawn ONE low-priority background thread that calls observe() as bars
     *  are pushed via feed(). Thin wrapper — no logic beyond the loop + pause flag. */
    void run();
    void stop();
    void feed(const Bar& bar, int64_t now_ms);  ///< enqueue a bar for the background loop

    /** Decision 6: when set, observe() early-returns None (yields machine to live trading). */
    void set_live_session(bool live) { live_session_.store(live); }

    /** Number of ManifestEmitted events within the trailing hour ending at now_ms. */
    size_t emits_this_hour(int64_t now_ms) const;

    size_t buffer_size() const { return bars_.size(); }

private:
    bool   horizon_drift(int horizon, double& z_out) const;
    void   prune_emits(int64_t now_ms);

    DaemonConfig                cfg_;
    algoforge::llm::LLMProvider& llm_;
    std::deque<Bar>             bars_;           // ring buffer (bounded by max_bars_retained)
    std::vector<char>           above_;          // per-horizon edge-trigger state (was-above)
    std::deque<int64_t>         emit_times_;     // ManifestEmitted timestamps (rate limiter)

    std::atomic<bool>           live_session_{false};
    std::atomic<bool>           running_{false};
    std::thread                 worker_;

    // Per-instance feed queue for the background thread (Decision 6).
    struct FeedItem { Bar bar; int64_t now_ms; };
    std::mutex                  q_mtx_;
    std::condition_variable     q_cv_;
    std::queue<FeedItem>        queue_;
};

} // namespace algoforge::s6
