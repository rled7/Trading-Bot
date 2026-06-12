/**
 * AlgoForge — src/monitoring/health.cpp
 * HealthMonitor + HealthSnapshot. Python oracle: python/algoforge/health.py.
 */
#include "monitoring/health.hpp"
#include "broker/broker.hpp"

#include <cstdio>
#include <sstream>

namespace algoforge::monitoring {

namespace {
std::string num(double v) {
    std::ostringstream o; o << v; return o.str();
}
} // namespace

std::string HealthSnapshot::to_json() const {
    std::ostringstream o;
    o << "{\"uptime_sec\":" << uptime_sec
      << ",\"broker_connected\":" << (broker_connected ? "true" : "false")
      << ",\"equity\":" << num(equity)
      << ",\"daily_pnl_pct\":" << num(daily_pnl_pct)
      << ",\"guard_state\":" << guard_state
      << ",\"open_positions\":" << open_positions
      << ",\"signals_processed\":" << signals_processed
      << ",\"signals_rejected\":" << signals_rejected
      << ",\"status\":\"" << status << "\"}";
    return o.str();
}

int HealthMonitor::uptime_sec() const {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_).count());
}

HealthSnapshot HealthMonitor::check(const af::IBroker* broker,
                                    double equity,
                                    double daily_pnl_pct,
                                    int    guard_state,
                                    int    signals_processed,
                                    int    signals_rejected) const {
    HealthSnapshot s;
    s.uptime_sec        = uptime_sec();
    s.broker_connected  = (broker != nullptr) && broker->is_connected();
    s.equity            = equity;
    s.daily_pnl_pct     = daily_pnl_pct;
    s.guard_state       = guard_state;
    s.signals_processed = signals_processed;
    s.signals_rejected  = signals_rejected;

    // Count open positions — swallow any broker errors gracefully.
    s.open_positions = 0;
    if (broker != nullptr && s.broker_connected) {
        try {
            s.open_positions = static_cast<int>(broker->get_positions().size());
        } catch (...) {
            // leave at 0
        }
    }

    // Derive overall status (mirrors the Python thresholds exactly).
    if (guard_state == 2)
        s.status = "CRITICAL";
    else if (guard_state == 1 || daily_pnl_pct < -0.03)
        s.status = "WARN";
    else
        s.status = "OK";

    return s;
}

} // namespace algoforge::monitoring
