/**
 * AlgoForge — src/dashboard/dashboard_main.cpp
 *
 * Serving binary for the C++ web dashboard. Constructs the ServerDeps a host
 * process must provide and runs the httplib server — i.e. the C++ analogue of
 * `uvicorn algoforge.dashboard.__main__:app`.
 *
 * Always serves: /, /api/health, /api/symbols, /api/logs, /api/account,
 *                /api/positions, /api/orders, /api/bars/{sym}, /api/stream/ticks
 *   (backed by an in-process PaperBroker).
 *
 * Enabled with --llm-host URL: the /api/llm and /api/algos groups
 *   (an OllamaProvider + an AlgoGenService over canonical EURUSD/H1 bars).
 *   Without it those routes honestly return 503 (llm_disabled / algo_gen_disabled).
 *
 * Usage:
 *   af_dashboard_server [--host H] [--port N] [--balance N]
 *                       [--static DIR] [--llm-host URL] [--registry DIR]
 */
#include "dashboard/server.hpp"
#include "dashboard/log_buffer.hpp"
#include "broker/broker.hpp"
#include "core/llm.hpp"
#include "core/algo_gen.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef AF_VERSION
#define AF_VERSION "dev"
#endif

namespace af { std::unique_ptr<IBroker> make_paper_broker(double balance); }

namespace {

const std::vector<std::string> kPaperSymbols = {
    "EURUSD", "GBPUSD", "USDJPY", "XAUUSD", "EURJPY",
    "GBPJPY", "AUDUSD", "USDCAD", "USDCHF", "EURGBP",
};

// Load canonical bars for AlgoGenService validate()/backtest().
std::vector<algoforge::algo_gen::Bar> load_canonical_bars(
    const af::IBroker& broker, const char* symbol, int count) {
    std::vector<AF_Bar> raw(static_cast<size_t>(count));
    int filled = 0;
    broker.get_bars(symbol, AF_TF_H1, count, raw.data(), &filled);
    if (filled < 0) filled = 0;
    std::vector<algoforge::algo_gen::Bar> out;
    out.reserve(static_cast<size_t>(filled));
    for (int i = 0; i < filled; ++i) {
        algoforge::algo_gen::Bar b;
        b.timestamp = raw[i].timestamp;
        b.open = raw[i].open; b.high = raw[i].high; b.low = raw[i].low;
        b.close = raw[i].close; b.volume = raw[i].volume; b.spread = raw[i].spread;
        out.push_back(b);
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::string host = "0.0.0.0";
    int         port = 8080;
    double      balance = 10000.0;
    std::string static_dir;
    std::string llm_host;
    std::string registry_dir = "registry";

    for (int i = 1; i < argc; ++i) {
        auto next = [&](const char* def) -> std::string {
            return (i + 1 < argc) ? argv[++i] : def;
        };
        if (!std::strcmp(argv[i], "--host"))         host = next(host.c_str());
        else if (!std::strcmp(argv[i], "--port"))    port = std::atoi(next("8080").c_str());
        else if (!std::strcmp(argv[i], "--balance")) balance = std::atof(next("10000").c_str());
        else if (!std::strcmp(argv[i], "--static"))  static_dir = next("");
        else if (!std::strcmp(argv[i], "--llm-host")) llm_host = next("");
        else if (!std::strcmp(argv[i], "--registry")) registry_dir = next("registry");
        else if (!std::strcmp(argv[i], "--help")) {
            std::printf("Usage: %s [--host H] [--port N] [--balance N] "
                        "[--static DIR] [--llm-host URL] [--registry DIR]\n", argv[0]);
            return 0;
        }
    }

    // ── Broker ──
    std::shared_ptr<af::IBroker> broker = af::make_paper_broker(balance);
    broker->connect();

    // ── Log buffer ──
    algoforge::dashboard::LogRingBuffer logs(500);
    logs.push({"", "INFO", "dashboard", "AlgoForge dashboard server starting"});

    // ── Optional LLM + algo_gen ──
    std::unique_ptr<algoforge::llm::OllamaProvider>     llm;
    std::unique_ptr<algoforge::algo_gen::AlgoGenService> algo_gen;
    if (!llm_host.empty()) {
        llm = std::make_unique<algoforge::llm::OllamaProvider>(llm_host);
        auto bars = load_canonical_bars(*broker, "EURUSD", 2000);
        algo_gen = std::make_unique<algoforge::algo_gen::AlgoGenService>(
            *llm, registry_dir, std::move(bars));
        logs.push({"", "INFO", "dashboard", "LLM + algo_gen enabled (" + llm_host + ")"});
    } else {
        logs.push({"", "INFO", "dashboard",
                   "LLM + algo_gen disabled (no --llm-host); /api/llm + /api/algos → 503"});
    }

    // ── Deps ──
    const auto start = std::chrono::steady_clock::now();
    algoforge::dashboard::ServerDeps deps;
    deps.version    = AF_VERSION;
    deps.symbols    = kPaperSymbols;
    deps.logs       = &logs;
    deps.broker     = broker.get();
    deps.llm        = llm.get();
    deps.algo_gen   = algo_gen.get();
    deps.llm_host   = llm_host;
    deps.static_dir = static_dir;
    deps.broker_status = [b = broker.get()] {
        return std::make_pair(std::string(b->broker_name()), b->is_connected());
    };
    deps.uptime = [start] {
        return std::chrono::duration<double>(
                   std::chrono::steady_clock::now() - start).count();
    };

    // ── Serve ──
    httplib::Server srv;
    algoforge::dashboard::register_routes(srv, deps);

    std::printf("AlgoForge dashboard %s listening on http://%s:%d  (broker=%s, llm=%s)\n",
                AF_VERSION, host.c_str(), port,
                broker->broker_name(), llm ? llm_host.c_str() : "disabled");
    if (!srv.listen(host.c_str(), port)) {
        std::fprintf(stderr, "ERROR: failed to bind %s:%d\n", host.c_str(), port);
        return 1;
    }
    return 0;
}
