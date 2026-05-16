/**
 * AlgoForge — src/core/engine.cpp
 */
#include "core/engine.hpp"
#include "broker/broker.hpp"
#include "core/event_bus.hpp"
#include <cstdio>
#include <thread>
#include <chrono>

namespace af {

const char* engine_state_name(EngineState s) {
    switch(s) {
        case EngineState::IDLE:         return "IDLE";
        case EngineState::STARTING:     return "STARTING";
        case EngineState::RUNNING:      return "RUNNING";
        case EngineState::RECONNECTING: return "RECONNECTING";
        case EngineState::STOPPING:     return "STOPPING";
        case EngineState::STOPPED:      return "STOPPED";
        case EngineState::ERROR:        return "ERROR";
    }
    return "UNKNOWN";
}

TradingEngine::TradingEngine(std::shared_ptr<IBroker> broker, EngineConfig config)
    : broker_(std::move(broker)), config_(config), bus_(EventBus::instance())
{}

TradingEngine::~TradingEngine() { stop(); }

double TradingEngine::uptime_seconds() const {
    using namespace std::chrono;
    return duration<double>(steady_clock::now() - start_time_).count();
}

void TradingEngine::set_state(EngineState s) {
    state_.store(s);
    printf("[Engine] State → %s\n", engine_state_name(s));
}

void TradingEngine::stop() {
    stop_requested_.store(true);
}

void TradingEngine::run() {
    on_startup();
    main_loop();
    on_shutdown();
}

void TradingEngine::on_startup() {
    set_state(EngineState::STARTING);
    start_time_     = std::chrono::steady_clock::now();
    last_heartbeat_ = start_time_;

    if (!connect_broker()) {
        set_state(EngineState::ERROR);
        bus_.emit(EventType::ENGINE_ERROR,
                  ErrorPayload{AF_ERR_NOT_CONNECTED, "Broker connection failed"});
        return;
    }

    set_state(EngineState::RUNNING);
    bus_.emit(EventType::ENGINE_STARTED,
              HeartbeatPayload{0.0, 0, 0.0});
    printf("[Engine] AlgoForge running | mode=%s\n",
           config_.mode==AF_MODE_LIVE ? "LIVE" : "PAPER");
}

bool TradingEngine::connect_broker() {
    for (int attempt = 1; attempt <= config_.reconnect_attempts; attempt++) {
        printf("[Engine] Connecting to broker (attempt %d/%d)...\n",
               attempt, config_.reconnect_attempts);
        if (broker_->connect() == AF_OK) {
            bus_.emit(EventType::BROKER_CONNECTED);
            return true;
        }
        if (attempt < config_.reconnect_attempts) {
            std::this_thread::sleep_for(
                std::chrono::duration<double>(config_.reconnect_delay_sec));
        }
    }
    return false;
}

void TradingEngine::main_loop() {
    if (state_.load() != EngineState::RUNNING) return;

    using namespace std::chrono;
    auto tick_interval = duration<double>(config_.tick_interval_sec);

    while (!stop_requested_.load()) {
        auto loop_start = steady_clock::now();

        if (!broker_->is_connected()) {
            handle_disconnection();
            if (!broker_->is_connected()) break;
        }

        on_tick();

        /* Heartbeat */
        auto now = steady_clock::now();
        if (duration<double>(now - last_heartbeat_).count() >= config_.heartbeat_interval_sec) {
            heartbeat();
            last_heartbeat_ = now;
        }

        /* Maintain tick rate */
        auto elapsed = steady_clock::now() - loop_start;
        auto sleep_time = tick_interval - elapsed;
        if (sleep_time > duration<double>(0))
            std::this_thread::sleep_for(sleep_time);
    }
}

void TradingEngine::on_tick() {
    tick_count_++;
    /* Poll SL/TP for paper trading */
    broker_->poll_sl_tp();
    bus_.emit(EventType::TICK_RECEIVED);
}

void TradingEngine::heartbeat() {
    AF_AccountInfo acct{};
    broker_->get_account(acct);

    HeartbeatPayload hb;
    hb.uptime_sec  = uptime_seconds();
    hb.tick_count  = tick_count_.load();
    hb.balance     = acct.balance;

    bus_.emit(EventType::HEARTBEAT, hb);
    printf("[Engine] ♥ uptime=%.0fs ticks=%llu balance=%.2f\n",
           hb.uptime_sec, (unsigned long long)hb.tick_count, hb.balance);
}

void TradingEngine::handle_disconnection() {
    set_state(EngineState::RECONNECTING);
    bus_.emit(EventType::BROKER_DISCONNECTED);
    printf("[Engine] Broker disconnected. Attempting reconnect...\n");

    if (!connect_broker()) {
        set_state(EngineState::ERROR);
        stop_requested_.store(true);
    } else {
        set_state(EngineState::RUNNING);
    }
}

void TradingEngine::on_shutdown() {
    set_state(EngineState::STOPPING);
    bus_.emit(EventType::ENGINE_STOPPING);

    if (broker_->is_connected()) broker_->disconnect();

    set_state(EngineState::STOPPED);
    bus_.emit(EventType::ENGINE_STOPPED);
    printf("[Engine] Shutdown complete. Uptime=%.1fs ticks=%llu\n",
           uptime_seconds(), (unsigned long long)tick_count_.load());
}

} /* namespace af */
