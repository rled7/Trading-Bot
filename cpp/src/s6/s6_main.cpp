/**
 * AlgoForge — src/s6/s6_main.cpp
 *
 * Production entry point for the S6 background algo-discovery daemon
 * (docs/s6_spec.md). Boots a DiscoveryDaemon against a live bar feed (via
 * IBroker::get_bars polling) and an LLM provider, proposing/promoting
 * candidate manifests into the algo registry without human prompting.
 *
 * This process is independent of the live trading engine (src/main.cpp) — it
 * only observes bars and proposes manifests; it NEVER places orders. Run it
 * alongside the engine (paper or live) as a separate process, or standalone
 * for research. The daemon class + golden-oracle tests existed with nothing
 * that ever started it — this file is that missing entrypoint.
 *
 * Deliberately drives DiscoveryDaemon::observe() directly from this process's
 * own poll loop rather than using run()/feed()/stop() (the class's optional
 * internal-thread wrapper) — observe() is the class's documented "unit of
 * test" (see discovery_daemon.hpp) and is what the golden-oracle suite
 * exercises, so driving it directly here means this binary's behavior is
 * exactly what test_s6 already proves, with no extra thread and with the
 * DiscoveryEvent result available to log.
 *
 * Usage:
 *   af_s6_daemon --llm-host URL [--symbol EURUSD] [--registry DIR]
 *                [--balance N] [--poll-ms N] [--help]
 *
 * --llm-host is required: S6 cannot generate candidate manifests without an
 * LLM, so (matching this codebase's "fail loudly on misconfiguration"
 * convention — see BrokerConfig::require()) it refuses to start without one
 * rather than silently idling.
 */
#include "s6/discovery_daemon.hpp"
#include "broker/broker.hpp"
#include "core/llm.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#ifndef AF_VERSION
#define AF_VERSION "dev"
#endif

namespace af { std::unique_ptr<IBroker> make_paper_broker(double balance); }

namespace {

std::atomic<bool> g_stop{false};
void signal_handler(int sig) {
    std::printf("\n[S6] Signal %d received — shutting down...\n", sig);
    g_stop.store(true);
}

// synthetic_ts_ms: PaperBroker::get_bars() sets AF_Bar::timestamp = 0 on every
// bar it returns (it's a stateless, per-call synthetic random walk, not a
// clock-driven feed) — real content, meaningless timestamp. We synthesize our
// own strictly-increasing millisecond timestamps for the daemon, which only
// needs *an* increasing clock for its rate limiter and window math, not the
// broker's literal wall time. Against a real broker (Alpaca/OANDA/etc. once
// live-validated), AF_Bar::timestamp is real and this conversion still works
// (the daemon only reads what we set here, not the raw field).
algoforge::s6::Bar to_s6_bar(const AF_Bar& raw, int64_t synthetic_ts_ms) {
    algoforge::s6::Bar b;
    b.timestamp = synthetic_ts_ms;
    b.open = raw.open; b.high = raw.high; b.low = raw.low;
    b.close = raw.close; b.volume = raw.volume; b.spread = raw.spread;
    return b;
}

const char* event_kind_name(algoforge::s6::DiscoveryEvent::Kind k) {
    using Kind = algoforge::s6::DiscoveryEvent::Kind;
    switch (k) {
        case Kind::HypothesisFormed:  return "HypothesisFormed";
        case Kind::ManifestEmitted:   return "ManifestEmitted";
        case Kind::RateLimited:       return "RateLimited";
        case Kind::ValidatorRejected: return "ValidatorRejected";
        case Kind::GenerationFailed:  return "GenerationFailed";
        case Kind::None:              return nullptr;
    }
    return "Unknown";
}

} // namespace

int main(int argc, char** argv) {
    std::string llm_host;
    std::string symbol = "EURUSD";
    std::string registry_dir = "registry";
    double      balance = 10000.0;
    int         poll_ms = 5000;

    for (int i = 1; i < argc; ++i) {
        auto next = [&](const char* def) -> std::string {
            return (i + 1 < argc) ? argv[++i] : def;
        };
        if (!std::strcmp(argv[i], "--llm-host"))      llm_host = next("");
        else if (!std::strcmp(argv[i], "--symbol"))   symbol = next(symbol.c_str());
        else if (!std::strcmp(argv[i], "--registry")) registry_dir = next(registry_dir.c_str());
        else if (!std::strcmp(argv[i], "--balance"))  balance = std::atof(next("10000").c_str());
        else if (!std::strcmp(argv[i], "--poll-ms"))  poll_ms = std::atoi(next("5000").c_str());
        else if (!std::strcmp(argv[i], "--help")) {
            std::printf(
                "Usage: %s --llm-host URL [--symbol SYM] [--registry DIR] "
                "[--balance N] [--poll-ms N]\n\n"
                "Background algo-discovery daemon (S6). Observes a paper-broker bar\n"
                "feed for %s, forms hypotheses on drift/pattern signals, and proposes\n"
                "candidate manifests through the validator into --registry. Never\n"
                "places orders.\n",
                argv[0], symbol.c_str());
            return 0;
        }
    }

    if (llm_host.empty()) {
        std::fprintf(stderr,
            "ERROR: --llm-host is required — S6 cannot generate manifests without an LLM.\n"
            "       Point it at your Ollama host, e.g. --llm-host http://localhost:11434\n");
        return 1;
    }

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    auto broker = af::make_paper_broker(balance);
    if (broker->connect() != AF_OK) {
        std::fprintf(stderr, "ERROR: failed to connect paper broker\n");
        return 1;
    }

    algoforge::llm::OllamaProvider llm(llm_host);

    algoforge::s6::DaemonConfig cfg;
    cfg.registry_dir = registry_dir;
    algoforge::s6::DiscoveryDaemon daemon(cfg, llm);

    std::printf(
        "AlgoForge S6 discovery daemon %s started\n"
        "  symbol   : %s\n"
        "  llm      : %s\n"
        "  registry : %s\n"
        "  horizons : %zu configured (~1h/24h/7d default)\n"
        "  poll     : every %dms\n\n",
        AF_VERSION, symbol.c_str(), llm_host.c_str(), registry_dir.c_str(),
        cfg.horizons_bars.size(), poll_ms);

    auto wall_now_ms = [] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    };
    auto log_event = [](const algoforge::s6::DiscoveryEvent& event) {
        const char* name = event_kind_name(event.kind);
        if (!name) return;
        if (event.kind == algoforge::s6::DiscoveryEvent::Kind::GenerationFailed) {
            std::printf("[S6] %s: %s\n", name, event.error.c_str());
        } else {
            std::printf("[S6] %s\n", name);
        }
    };

    // ── Bootstrap: fill the daemon's window in one shot instead of waiting
    // for max_bars_retained individual poll ticks (>14h at the default 5s
    // interval) before it can evaluate anything. Bars are fed oldest-first
    // with synthetic per-bar timestamps 60s apart, ending at "now".
    {
        const size_t bootstrap_count = cfg.max_bars_retained;
        std::vector<AF_Bar> history(bootstrap_count);
        int filled = 0;
        broker->get_bars(symbol.c_str(), AF_TF_M1, static_cast<int>(bootstrap_count),
                          history.data(), &filled);
        const int64_t now = wall_now_ms();
        for (int i = 0; i < filled; ++i) {
            const int64_t ts = now - static_cast<int64_t>(filled - 1 - i) * 60000;
            log_event(daemon.observe(to_s6_bar(history[i], ts), ts));
        }
        std::printf("[S6] Bootstrapped with %d historical bars\n", filled);
    }

    // ── Ongoing: observe() is safe to call every tick even with an unchanged
    // bar — the daemon's own edge-triggering (spec §4.3: "no identical-regime
    // hypothesis already open for that horizon") prevents duplicate signals,
    // so no timestamp-dedup bookkeeping is needed here. Against a real broker
    // (once live-validated) each tick reflects genuinely new market data;
    // against the paper broker's static synthetic feed it's a harmless no-op
    // after the bootstrap already captured its one deterministic history.
    while (!g_stop.load()) {
        AF_Bar raw{};
        int filled = 0;
        broker->get_bars(symbol.c_str(), AF_TF_M1, 1, &raw, &filled);

        if (filled > 0) {
            const int64_t now = wall_now_ms();
            log_event(daemon.observe(to_s6_bar(raw, now), now));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
    }

    broker->disconnect();
    std::printf("[S6] Exited cleanly.\n");
    return 0;
}
