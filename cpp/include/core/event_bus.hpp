/**
 * AlgoForge — include/core/event_bus.hpp
 * Type-safe, thread-safe publish/subscribe event bus.
 */
#pragma once
#include <functional>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <string>
#include <variant>
#include <optional>
#include "core/types.h"

namespace af {

/* ── Event types ── */
enum class EventType : uint32_t {
    ENGINE_STARTED=0x001, ENGINE_STOPPING, ENGINE_STOPPED, ENGINE_ERROR, HEARTBEAT,
    BROKER_CONNECTED=0x100, BROKER_DISCONNECTED, BROKER_ERROR,
    TICK_RECEIVED=0x200, BAR_CLOSED, HISTORICAL_LOADED,
    SIGNAL_GENERATED=0x300,
    ORDER_PLACED=0x400, ORDER_FILLED, ORDER_CANCELLED, ORDER_ERROR,
    POSITION_OPENED=0x500, POSITION_CLOSED, POSITION_SL_HIT, POSITION_TP_HIT,
    DRAWDOWN_WARNING=0x600, DRAWDOWN_LIMIT_HIT, DAILY_LOSS_LIMIT, MAX_POSITIONS_HIT,
    PATTERN_DETECTED=0x700,
    ALGO_DECISION=0x800,
};

/* ── Payload types ── */
struct EmptyPayload {};
struct TickPayload      { AF_Tick tick; };
struct BarPayload       { AF_Bar bar; char symbol[12]; AF_Timeframe tf; };
struct OrderPayload     { AF_Order order; };
struct PositionPayload  { AF_Position pos; double realized_pnl{0}; std::string close_reason; };
struct SignalPayload {
    char symbol[12]; AF_Timeframe tf; AF_Direction direction;
    double score{0}, atr{0}; char algorithm[64]; char reason[256];
};
struct HeartbeatPayload { double uptime_sec{0}; uint64_t tick_count{0}; double balance{0}; };
struct ErrorPayload     { AF_Error code; char message[256]; };

using EventPayload = std::variant<
    EmptyPayload, TickPayload, BarPayload, OrderPayload,
    PositionPayload, SignalPayload, HeartbeatPayload, ErrorPayload>;

/* ── Event ── */
struct Event {
    EventType    type;
    EventPayload payload;
    std::string  source;
    int64_t      ts_us;  /* microseconds since epoch */

    Event(EventType t, EventPayload p={EmptyPayload{}}, std::string s="system")
        : type(t), payload(std::move(p)), source(std::move(s)) {
        using namespace std::chrono;
        ts_us = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
    }

    template<typename T> bool      has()       const { return std::holds_alternative<T>(payload); }
    template<typename T> const T&  get()       const { return std::get<T>(payload); }
    template<typename T> std::optional<T> try_get() const {
        if (auto *p = std::get_if<T>(&payload)) return *p;
        return std::nullopt;
    }
};

using EventHandler = std::function<void(const Event&)>;
using HandlerID    = uint64_t;

} /* namespace af */

/* std::hash specialisation so EventType can be used as unordered_map key */
template<> struct std::hash<af::EventType> {
    std::size_t operator()(af::EventType t) const noexcept {
        return std::hash<uint32_t>{}(static_cast<uint32_t>(t));
    }
};

namespace af {

/* ── EventBus ── */
class EventBus {
public:
    static EventBus& instance() { static EventBus b; return b; }

    HandlerID on(EventType t, EventHandler h) {
        std::lock_guard lk(mu_);
        auto id = ++next_id_;
        subs_[t].push_back({id, std::move(h)});
        return id;
    }
    HandlerID on_any(EventHandler h) {
        std::lock_guard lk(mu_);
        auto id = ++next_id_;
        wild_.push_back({id, std::move(h)});
        return id;
    }
    bool off(HandlerID id) {
        std::lock_guard lk(mu_);
        for (auto &[t,v] : subs_) {
            auto it = std::remove_if(v.begin(),v.end(),[id](auto &e){return e.id==id;});
            if (it!=v.end()) { v.erase(it,v.end()); return true; }
        }
        auto it = std::remove_if(wild_.begin(),wild_.end(),[id](auto &e){return e.id==id;});
        if (it!=wild_.end()) { wild_.erase(it,wild_.end()); return true; }
        return false;
    }

    size_t emit(const Event &ev) {
        ++total_;
        /* snapshot under lock, dispatch outside */
        std::vector<Entry> hs, ws;
        {
            std::lock_guard lk(mu_);
            auto it = subs_.find(ev.type);
            if (it != subs_.end()) hs = it->second;
            ws = wild_;
            if (hist_.size() >= 500) hist_.erase(hist_.begin());
            hist_.push_back(ev);
        }
        size_t n = 0;
        for (auto &e : hs) { safe(e.h, ev); ++n; }
        for (auto &e : ws) { safe(e.h, ev); ++n; }
        return n;
    }
    size_t emit(EventType t, EventPayload p={EmptyPayload{}}, const std::string &src="system") {
        return emit(Event{t, std::move(p), src});
    }

    uint64_t total_emitted()    const { return total_.load(); }
    size_t   subscriber_count(EventType t) const {
        std::lock_guard lk(mu_); auto it=subs_.find(t);
        return it!=subs_.end() ? it->second.size() : 0;
    }
    std::vector<Event> history(std::optional<EventType> f={}, size_t lim=50) const {
        std::lock_guard lk(mu_);
        std::vector<Event> r;
        for (auto it=hist_.rbegin(); it!=hist_.rend() && r.size()<lim; ++it)
            if (!f || it->type==*f) r.push_back(*it);
        std::reverse(r.begin(),r.end());
        return r;
    }

private:
    struct Entry { HandlerID id; EventHandler h; };
    struct ETypeHash {
        std::size_t operator()(EventType t) const noexcept {
            return std::hash<uint32_t>{}(static_cast<uint32_t>(t));
        }
    };
    mutable std::mutex mu_;
    std::unordered_map<EventType, std::vector<Entry>, ETypeHash> subs_;
    std::vector<Entry> wild_;
    std::vector<Event> hist_;
    std::atomic<uint64_t> total_{0}, next_id_{0};

    static void safe(const EventHandler &h, const Event &e) noexcept {
        try { h(e); } catch (...) {}
    }
};

inline EventBus& get_bus() { return EventBus::instance(); }

} /* namespace af */
