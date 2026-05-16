/**
 * AlgoForge — include/algorithms/algorithm.hpp
 * Base algorithm interface + decision type.
 */
#pragma once
#include <string>
#include <optional>
#include <memory>
#include <vector>
#include "core/types.h"
#include "indicators/indicator_engine.hpp"
#include "patterns/pattern_types.h"

namespace af {

/* ── Algorithm decision ── */
enum class AlgoSignal { NONE=0, BUY=1, SELL=-1 };

struct AlgoDecision {
    AlgoSignal signal{AlgoSignal::NONE};
    std::string symbol;
    AF_Direction direction{AF_DIR_NONE};
    double confidence{0};
    std::string reason;
    double atr{0};

    bool is_actionable() const {
        return signal != AlgoSignal::NONE && confidence >= 0.55;
    }
    static AlgoDecision none(const char *sym="") {
        AlgoDecision d; d.symbol=sym; return d;
    }
};

/* ── Base algorithm ── */
class IAlgorithm {
public:
    virtual ~IAlgorithm() = default;

    virtual const char* name()        const = 0;
    virtual const char* description() const = 0;
    virtual uint32_t    magic()       const = 0;

    /**
     * Evaluate bars and produce a trading decision.
     * bars[0]=oldest, bars[count-1]=newest.
     */
    virtual AlgoDecision evaluate(
        const char        *symbol,
        AF_Timeframe       tf,
        const AF_Bar      *bars,
        size_t             count,
        const EngineResult &ind,
        const AF_PatternResult &pat
    ) = 0;

    /** Safe wrapper: catches all exceptions, returns NONE */
    AlgoDecision safe_evaluate(
        const char *symbol, AF_Timeframe tf,
        const AF_Bar *bars, size_t count,
        const EngineResult &ind, const AF_PatternResult &pat)
    {
        if (!enabled_) return AlgoDecision::none(symbol);
        try { return evaluate(symbol, tf, bars, count, ind, pat); }
        catch (...) { return AlgoDecision::none(symbol); }
    }

    bool is_enabled() const { return enabled_; }
    void enable()           { enabled_ = true; }
    void disable()          { enabled_ = false; }

protected:
    bool enabled_ = true;
};

/* ── Algorithm registry ── */
class AlgorithmRegistry {
public:
    AlgorithmRegistry();

    void register_algo(std::unique_ptr<IAlgorithm> algo);
    void enable (const char *name);
    void disable(const char *name);

    IAlgorithm* get(const char *name) const;
    const std::vector<std::unique_ptr<IAlgorithm>>& all() const { return algos_; }

    /** Run all enabled algos, collect non-NONE decisions */
    std::vector<AlgoDecision> evaluate_all(
        const char *symbol, AF_Timeframe tf,
        const AF_Bar *bars, size_t count,
        const EngineResult &ind, const AF_PatternResult &pat) const;

    int count() const { return static_cast<int>(algos_.size()); }

private:
    std::vector<std::unique_ptr<IAlgorithm>> algos_;
};

} /* namespace af */
