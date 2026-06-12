/**
 * AlgoForge — include/monitoring/health.hpp
 *
 * HealthMonitor + HealthSnapshot — a point-in-time view of the bot's operational
 * state, gathered from the broker, risk guard, and internal counters.
 * Python oracle: python/algoforge/health.py.
 */
#pragma once

#include <chrono>
#include <string>

namespace af { class IBroker; }

namespace algoforge::monitoring {

/** Immutable point-in-time health report. */
struct HealthSnapshot {
    int         uptime_sec        = 0;
    bool        broker_connected  = false;
    double      equity            = 0.0;
    double      daily_pnl_pct     = 0.0;
    int         guard_state       = 0;   ///< 0 GREEN, 1 YELLOW, 2 RED
    int         open_positions    = 0;
    int         signals_processed = 0;
    int         signals_rejected  = 0;
    std::string status;                  ///< "OK" | "WARN" | "CRITICAL"

    /** JSON object with the nine snapshot fields (mirrors to_dict/to_json). */
    std::string to_json() const;
};

/** Builds HealthSnapshots on demand; records its creation time for uptime. */
class HealthMonitor {
public:
    HealthMonitor() : start_(std::chrono::steady_clock::now()) {}

    /**
     * Build a snapshot. status is derived as: RED guard → "CRITICAL";
     * YELLOW guard OR daily_pnl_pct < -0.03 → "WARN"; else "OK".
     * open_positions is broker.get_positions().size() when connected (errors swallowed).
     */
    HealthSnapshot check(const af::IBroker* broker,
                         double equity,
                         double daily_pnl_pct,
                         int    guard_state,
                         int    signals_processed = 0,
                         int    signals_rejected  = 0) const;

    /** Seconds elapsed since construction. */
    int uptime_sec() const;

private:
    std::chrono::steady_clock::time_point start_;
};

} // namespace algoforge::monitoring
