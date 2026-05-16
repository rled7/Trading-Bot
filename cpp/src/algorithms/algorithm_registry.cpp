/**
 * AlgoForge — src/algorithms/algorithm_registry.cpp
 */
#include "algorithms/algorithm.hpp"
#include <algorithm>
#include <cstring>

namespace af {

/* Forward-declared factories */
std::unique_ptr<IAlgorithm> make_trend_follower(int fast=21, int slow=50, double adx_min=22.0, uint32_t magic=1001);
std::unique_ptr<IAlgorithm> make_mean_reversion();
std::unique_ptr<IAlgorithm> make_breakout_trader(int dc=20, double vm=1.5);
std::unique_ptr<IAlgorithm> make_swing_trader();

AlgorithmRegistry::AlgorithmRegistry() {
    register_algo(make_trend_follower(21, 50, 22.0, 1001));
    register_algo(make_trend_follower(9,  21, 20.0, 1005));
    register_algo(make_mean_reversion());
    register_algo(make_breakout_trader(20, 1.5));
    register_algo(make_swing_trader());
}

void AlgorithmRegistry::register_algo(std::unique_ptr<IAlgorithm> algo) {
    if (algo) algos_.push_back(std::move(algo));
}

void AlgorithmRegistry::enable(const char *name) {
    if (auto *a = get(name)) a->enable();
}
void AlgorithmRegistry::disable(const char *name) {
    if (auto *a = get(name)) a->disable();
}

IAlgorithm* AlgorithmRegistry::get(const char *name) const {
    for (auto &a : algos_)
        if (strcmp(a->name(), name) == 0) return a.get();
    return nullptr;
}

std::vector<AlgoDecision> AlgorithmRegistry::evaluate_all(
    const char *symbol, AF_Timeframe tf,
    const AF_Bar *bars, size_t count,
    const EngineResult &ind, const AF_PatternResult &pat) const
{
    std::vector<AlgoDecision> results;
    for (auto &algo : algos_) {
        if (!algo->is_enabled()) continue;
        auto d = algo->safe_evaluate(symbol, tf, bars, count, ind, pat);
        if (d.is_actionable()) results.push_back(std::move(d));
    }
    return results;
}

} /* namespace af */
