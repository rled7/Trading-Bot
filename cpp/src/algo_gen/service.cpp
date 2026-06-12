/**
 * AlgoForge — src/algo_gen/service.cpp
 *
 * §8  AlgoGenService — public facade over the algo_gen primitives, used by the
 * dashboard /api/algos/* routes. Mirrors
 * python/algoforge/dashboard/algo_gen_routes.py::AlgoGenFacade.
 *
 * It catches the algo_gen-internal exception types (generator::GenerationError,
 * std::runtime_error from promote) and rethrows the public, kind-tagged
 * AlgoGenError so the dashboard maps failures → HTTP status without seeing the
 * internal headers.
 */
#include "algo_gen_internal.hpp"
#include "core/algo_gen.hpp"
#include "core/llm.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace algoforge::algo_gen {

AlgoGenService::AlgoGenService(algoforge::llm::LLMProvider& llm,
                               std::filesystem::path registry_dir,
                               std::vector<Bar> bars)
    : llm_(llm), registry_dir_(std::move(registry_dir)), bars_(std::move(bars)) {}

// ---------------------------------------------------------------------------
// generate — effort dispatch, with GenerationError → AlgoGenError mapping
// ---------------------------------------------------------------------------
AlgoManifest AlgoGenService::generate(const std::string& brief,
                                      const std::string& effort,
                                      uint64_t seed) {
    std::string e = effort;
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    try {
        const int s = static_cast<int>(seed);
        if (e == "fast")            return generator::generate_fast(brief, llm_, s).manifest;
        if (e == "balanced")        return generator::generate_balanced(brief, llm_, s).manifest;
        if (e == "max")             return generator::generate_max(brief, llm_, s).manifest;
        throw AlgoGenError("generation",
                           "Unknown effort level: '" + effort +
                           "'. Must be 'fast', 'balanced', or 'max'.");
    } catch (const generator::GenerationError& ex) {
        std::string msg = ex.what();
        std::string lower = msg;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.find("timeout") != std::string::npos)
            throw AlgoGenError("timeout", msg);
        if (lower.find("unreachable") != std::string::npos ||
            lower.find("connection")  != std::string::npos)
            throw AlgoGenError("unreachable", msg);
        throw AlgoGenError("generation", msg);
    }
}

// ---------------------------------------------------------------------------
// validate — fully-qualified to avoid recursing into this member
// ---------------------------------------------------------------------------
TierReport AlgoGenService::validate(const AlgoManifest& m, uint64_t seed) {
    return ::algoforge::algo_gen::validate(m, bars_, seed);
}

// ---------------------------------------------------------------------------
// backtest — run_manifest_backtest → public BacktestSummary
// ---------------------------------------------------------------------------
BacktestSummary AlgoGenService::backtest(const AlgoManifest& m,
                                         const std::string& /*symbol*/,
                                         int n_bars,
                                         uint64_t /*seed*/) {
    std::vector<Bar> bars = bars_;
    if (n_bars >= 0 && static_cast<size_t>(n_bars) < bars.size())
        bars.resize(static_cast<size_t>(n_bars));
    if (bars.empty())
        throw AlgoGenError("backtest", "No bar data available for backtest");

    try {
        SimpleBacktestResult r = run_manifest_backtest(m, bars);
        BacktestSummary out;
        out.total_trades     = r.total_trades;
        out.net_profit       = r.net_profit;
        out.sharpe           = r.sharpe;
        out.max_drawdown_pct = r.max_dd_pct;
        out.equity_curve     = r.equity_curve;
        return out;
    } catch (const AlgoGenError&) {
        throw;
    } catch (const std::exception& ex) {
        throw AlgoGenError("backtest", ex.what());
    }
}

// ---------------------------------------------------------------------------
// promote — fully-qualified free function; runtime_error → AlgoGenError
// ---------------------------------------------------------------------------
void AlgoGenService::promote(const TierReport& report, const AlgoManifest& m) {
    try {
        ::algoforge::algo_gen::promote(report, m, registry_dir_, /*confirm=*/true);
    } catch (const std::exception& ex) {
        throw AlgoGenError("generation", ex.what());
    }
}

// ---------------------------------------------------------------------------
// retire — set is_enabled=false in the registry file (no-op if absent).
// The promoted file format is known (manifest fragment + _metadata), so we do a
// targeted rewrite rather than a full JSON round-trip (the json:: helper has a
// parser but no general serialiser). The dashboard's authoritative "retired"
// signal is the in-memory record status, matching the Python facade.
// ---------------------------------------------------------------------------
void AlgoGenService::retire(const AlgoManifest& m) {
    const std::filesystem::path path = registry_dir_ / (m.name + ".json");
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;  // not promoted → no-op

    std::ifstream in(path);
    if (!in.is_open()) return;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string body = ss.str();
    in.close();

    const std::string key = "\"is_enabled\"";
    auto kpos = body.find(key);
    if (kpos != std::string::npos) {
        // Force the existing flag to false.
        auto tpos = body.find("true", kpos);
        if (tpos != std::string::npos && tpos < kpos + 24) {
            body.replace(tpos, 4, "false");
        }
    } else {
        // Insert "is_enabled": false right after the opening brace.
        auto brace = body.find('{');
        if (brace != std::string::npos) {
            body.insert(brace + 1, "\n  \"is_enabled\": false,");
        }
    }

    std::ofstream out(path, std::ios::trunc);
    if (out.is_open()) out << body;
}

} /* namespace algoforge::algo_gen */
