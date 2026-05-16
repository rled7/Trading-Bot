/**
 * AlgoForge — src/patterns/pattern_engine.cpp
 * PatternEngine implementation.
 */
#include "patterns/pattern_engine.hpp"
#include "patterns/pattern_types.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace af {

/* Declared in other .cpp files */
void register_candlestick_patterns(PatternEngine *eng);
void register_chart_patterns(PatternEngine *eng);
void register_harmonic_patterns(PatternEngine *eng);
void register_volume_patterns(PatternEngine *eng);

PatternEngine::PatternEngine(double min_confidence)
    : min_conf_(min_confidence)
{
    register_all();
}

void PatternEngine::register_all() {
    register_candlestick_patterns(this);
    register_chart_patterns(this);
    register_harmonic_patterns(this);
    register_volume_patterns(this);
}

void PatternEngine::reg(const char *name, AF_PatternCategory cat,
                         AF_PatternDirection dir, double base_conf,
                         PatternDetector det) {
    detectors_.push_back({name, cat, dir, base_conf, std::move(det)});
}

AF_PatternResult PatternEngine::scan(const AF_Bar *bars, size_t count) const {
    AF_PatternResult result{};
    result.count = 0; result.bullish_count = 0; result.bearish_count = 0;

    if (!bars || count < 2) return result;

    for (auto &entry : detectors_) {
        int before = result.count;
        entry.detector(bars, count, result);
        /* Apply confidence filter */
        for (int i = before; i < result.count; i++) {
            if (result.matches[i].confidence < min_conf_) {
                /* Remove this match */
                if (result.matches[i].direction == AF_PAT_DIR_BULLISH) result.bullish_count--;
                else if (result.matches[i].direction == AF_PAT_DIR_BEARISH) result.bearish_count--;
                for (int j = i; j < result.count-1; j++)
                    result.matches[j] = result.matches[j+1];
                result.count--; i--;
            }
        }
    }

    /* Sort by confidence descending */
    std::sort(result.matches, result.matches + result.count,
              [](const AF_PatternMatch &a, const AF_PatternMatch &b){
                  return a.confidence > b.confidence;
              });

    return result;
}

} /* namespace af */
