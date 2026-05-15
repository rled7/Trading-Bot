/**
 * AlgoForge — include/core/engine.hpp (auto-generated minimal)
 */
#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include "core/types.h"

namespace af {

class IBroker;

enum class EngineState { IDLE, STARTING, RUNNING, RECONNECTING, STOPPING, STOPPED, ERROR };
const char* engine_state_name(EngineState s);

struct EngineConfig {
    AF_TradingMode mode            = AF_MODE_PAPER;
    double tick_interval_sec       = 1.0;
    double heartbeat_interval_sec  = 30.0;
    int    reconnect_attempts      = 5;
    double reconnect_delay_sec     = 10.0;
    double max_risk_per_trade      = 0.01;
    double max_daily_drawdown      = 0.05;
    double max_total_drawdown      = 0.15;
    int    max_open_positions      = 5;
    double max_lots                = 1.0;
    double min_lots                = 0.01;
};

class TradingEngine {
public:
    explicit TradingEngine(std::shared_ptr<IBroker> broker, EngineConfig config = {});
    ~TradingEngine();
    void run();
    void stop();
    EngineState state()    const { return state_.load(); }
    bool        is_running()const{ return state_.load()==EngineState::RUNNING; }
    double      uptime_seconds() const;
    uint64_t    tick_count() const { return tick_count_.load(); }
    const EngineConfig& config() const { return config_; }
private:
    void on_startup(); void on_shutdown(); void main_loop();
    void on_tick(); void heartbeat(); void handle_disconnection();
    bool connect_broker(); void set_state(EngineState s);
    std::shared_ptr<IBroker>   broker_;
    EngineConfig               config_;
    class EventBus&            bus_;
    std::atomic<EngineState>   state_{EngineState::IDLE};
    std::atomic<bool>          stop_requested_{false};
    std::atomic<uint64_t>      tick_count_{0};
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_heartbeat_;
};

} /* namespace af */
