/**
 * AlgoForge — include/patterns/pattern_engine.hpp
 * Pattern engine: runs all 95 pattern detectors on a bar array.
 */
#pragma once
#include <functional>
#include <vector>
#include <string>
#include "core/types.h"
#include "patterns/pattern_types.h"

namespace af {

/** A pattern detector: takes bars + count, appends matches to result */
using PatternDetector = std::function<void(const AF_Bar*, size_t, AF_PatternResult&)>;

class PatternEngine {
public:
    explicit PatternEngine(double min_confidence = 0.60);

    /**
     * Scan all registered patterns on bars[0..count-1].
     * bars[0]=oldest, bars[count-1]=newest.
     * Returns populated AF_PatternResult.
     */
    AF_PatternResult scan(const AF_Bar *bars, size_t count) const;
    AF_PatternResult scan(const AF_BarArray &arr) const {
        return scan(arr.bars, arr.count);
    }

    int total_patterns() const { return static_cast<int>(detectors_.size()); }
    double min_confidence() const { return min_conf_; }

    void register_all();
    void reg(const char *name, AF_PatternCategory cat,
             AF_PatternDirection dir, double base_conf,
             PatternDetector det);

private:

    struct Entry {
        std::string       name;
        AF_PatternCategory cat;
        AF_PatternDirection dir;
        double            base_conf;
        PatternDetector   detector;
    };

    std::vector<Entry> detectors_;
    double             min_conf_;
};

} /* namespace af */
