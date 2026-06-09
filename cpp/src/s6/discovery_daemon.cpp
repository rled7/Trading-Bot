/**
 * AlgoForge — src/s6/discovery_daemon.cpp
 * Implements docs/s6_spec.md. See header for the determinism contract.
 */
#include "s6/discovery_daemon.hpp"

#include "core/types.h"            // AF_Bar, af_bar_init
#include "algo_gen_internal.hpp"   // algoforge::algo_gen::generator::generate_balanced, GenResult

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>

namespace algoforge::s6 {

namespace {
constexpr int64_t kHourMs = 3'600'000;

/** Convert an algo_gen::Bar to a fully-initialised AF_Bar (derived fields filled). */
AF_Bar to_af_bar(const Bar& b) {
    AF_Bar a{};
    a.timestamp = b.timestamp;
    a.open = b.open; a.high = b.high; a.low = b.low;
    a.close = b.close; a.volume = b.volume; a.spread = b.spread;
    af_bar_init(&a);
    return a;
}
} // namespace

DiscoveryDaemon::DiscoveryDaemon(DaemonConfig cfg, algoforge::llm::LLMProvider& llm)
    : cfg_(std::move(cfg)), llm_(llm) {
    above_.assign(cfg_.horizons_bars.size(), 0);

    // Decision 5 / spec §2: default the pipeline steps to the real, already-tested code.
    if (!cfg_.generate_fn) {
        algoforge::llm::LLMProvider* p = &llm_;
        cfg_.generate_fn = [p](const Hypothesis& h) {
            return algoforge::algo_gen::generator::generate_balanced(h.summary, *p).manifest;
        };
    }
    if (!cfg_.validate_fn) {
        cfg_.validate_fn = [](const AlgoManifest& m, const std::vector<Bar>& bars) {
            return algoforge::algo_gen::validate(m, bars);
        };
    }
}

DiscoveryDaemon::~DiscoveryDaemon() { stop(); }

// Two-sample mean-shift z-score over the trailing 2*H close-returns for a horizon.
bool DiscoveryDaemon::horizon_drift(int H, double& z_out) const {
    if (H < 1) return false;
    const size_t need = static_cast<size_t>(2 * H) + 1;
    if (bars_.size() < need) return false;

    // Build the last 2H returns (recent = last H, prior = the H before).
    std::vector<double> r;
    r.reserve(2 * H);
    const size_t start = bars_.size() - need;          // index of the first close used
    for (size_t i = start + 1; i < bars_.size(); ++i) {
        const double prev = bars_[i - 1].close;
        const double cur  = bars_[i].close;
        r.push_back(prev != 0.0 ? (cur / prev - 1.0) : 0.0);
    }
    // r.size() == 2H. prior = r[0..H), recent = r[H..2H).
    double mean_recent = 0.0, mean_prior = 0.0, mean_all = 0.0;
    for (int i = 0; i < H; ++i)        mean_prior  += r[i];
    for (int i = H; i < 2 * H; ++i)    mean_recent += r[i];
    mean_prior  /= H; mean_recent /= H;
    for (double v : r) mean_all += v;
    mean_all /= (2.0 * H);
    double s2 = 0.0;
    for (double v : r) s2 += (v - mean_all) * (v - mean_all);
    s2 /= (2.0 * H);
    const double se = std::sqrt(s2 * (2.0 / H)) + 1e-12;
    z_out = std::fabs(mean_recent - mean_prior) / se;
    return true;
}

void DiscoveryDaemon::prune_emits(int64_t now_ms) {
    while (!emit_times_.empty() && now_ms - emit_times_.front() >= kHourMs)
        emit_times_.pop_front();
}

size_t DiscoveryDaemon::emits_this_hour(int64_t now_ms) const {
    size_t n = 0;
    for (auto it = emit_times_.rbegin(); it != emit_times_.rend(); ++it)
        if (now_ms - *it < kHourMs) ++n; else break;
    return n;
}

DiscoveryEvent DiscoveryDaemon::observe(const Bar& bar, int64_t now_ms) {
    DiscoveryEvent ev;

    // Decision 6: yield the machine to live trading.
    if (cfg_.pause_during_live && live_session_.load()) return ev;  // Kind::None

    // Bounded ring buffer (Decision 6).
    bars_.push_back(bar);
    while (bars_.size() > cfg_.max_bars_retained) bars_.pop_front();

    // Decision 3: edge-triggered threshold crossing across horizons (Decision 2).
    int    fired_horizon = -1;
    size_t fired_idx     = 0;
    double fired_z       = 0.0;
    for (size_t i = 0; i < cfg_.horizons_bars.size(); ++i) {
        double z = 0.0;
        const bool ok = horizon_drift(cfg_.horizons_bars[i], z);
        const bool above = ok && z >= cfg_.drift_threshold;
        const bool rising_edge = above && !above_[i];
        above_[i] = above ? 1 : 0;
        if (rising_edge && z > fired_z) { fired_horizon = cfg_.horizons_bars[i]; fired_idx = i; fired_z = z; }
    }
    (void)fired_idx;
    if (fired_horizon < 0) return ev;   // Kind::None — no false positives on quiet data

    // Build the hypothesis (Decision 1: regime + drift + co-occurrence feed the summary).
    Hypothesis h;
    h.detected_at  = now_ms;
    h.horizon_bars = fired_horizon;
    h.drift_z      = fired_z;
    if (bars_.size() >= 30) {            // classify_regime needs enough bars; non-critical
        std::vector<AF_Bar> win;
        win.reserve(bars_.size());
        for (const auto& b : bars_) win.push_back(to_af_bar(b));
        h.regime = af::classify_regime(win.data(), win.size()).regime;
    }
    h.summary = "regime-shift candidate: horizon=" + std::to_string(fired_horizon) +
                " bars, drift_z=" + std::to_string(fired_z) +
                ", target tier >= " + algo_gen::tier_name(cfg_.target_tier_floor);

    // Decision 3: rate limit BEFORE spending a generation.
    prune_emits(now_ms);
    if (emits_this_hour(now_ms) >= static_cast<size_t>(cfg_.max_emits_per_hour)) {
        ev.kind = DiscoveryEvent::Kind::RateLimited;
        ev.hypothesis = std::move(h);
        return ev;
    }

    // Generate → validate → gate (Decisions 4 & 5).
    AlgoManifest manifest = cfg_.generate_fn(h);
    std::vector<Bar> window(bars_.begin(), bars_.end());
    TierReport report = cfg_.validate_fn(manifest, window);

    const bool meets_floor =
        report.passed && static_cast<int>(report.tier) >= static_cast<int>(cfg_.target_tier_floor);
    if (!meets_floor) {
        ev.kind = DiscoveryEvent::Kind::ValidatorRejected;
        ev.hypothesis = std::move(h);
        ev.report = std::move(report);
        return ev;
    }

    // Emitted. Auto-promote only Green/White (Orange/Yellow require a manual review gate;
    // promote() itself throws without confirm — so we emit them for review, not auto-promote).
    emit_times_.push_back(now_ms);
    if ((static_cast<int>(report.tier) >= static_cast<int>(Tier::Green)) &&
        !cfg_.registry_dir.empty()) {
        try { algo_gen::promote(report, manifest, cfg_.registry_dir, /*confirm=*/false); }
        catch (...) { /* promotion best-effort; emission already recorded */ }
    }
    ev.kind = DiscoveryEvent::Kind::ManifestEmitted;
    ev.hypothesis = std::move(h);
    ev.manifest = std::move(manifest);
    ev.report = std::move(report);
    return ev;
}

// ── Thin background-thread wrapper (Decision 6). No logic beyond the loop. ──
void DiscoveryDaemon::feed(const Bar& bar, int64_t now_ms) {
    { std::lock_guard<std::mutex> lk(q_mtx_); queue_.push({bar, now_ms}); }
    q_cv_.notify_one();
}

void DiscoveryDaemon::run() {
    if (running_.exchange(true)) return;
    worker_ = std::thread([this]{
        while (running_.load()) {
            FeedItem item;
            {
                std::unique_lock<std::mutex> lk(q_mtx_);
                q_cv_.wait_for(lk, std::chrono::milliseconds(100),
                               [this]{ return !queue_.empty(); });
                if (queue_.empty()) continue;
                item = queue_.front(); queue_.pop();
            }
            observe(item.bar, item.now_ms);
        }
    });
}

void DiscoveryDaemon::stop() {
    if (!running_.exchange(false)) return;
    q_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

} // namespace algoforge::s6
