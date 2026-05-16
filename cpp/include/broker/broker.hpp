/**
 * AlgoForge — include/broker/broker.hpp
 * Abstract broker interface. Paper and MT5 both implement this.
 */
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include "core/types.h"

namespace af {

/** Callback types for push-based data */
using TickCallback    = std::function<void(const AF_Tick&)>;
using BarCallback     = std::function<void(const AF_Bar&, const char*, AF_Timeframe)>;
using PositionCallback= std::function<void(const AF_Position&, const std::string&)>;

/**
 * IBroker — pure virtual interface for broker connectivity.
 * Every method is synchronous from the caller's perspective;
 * internally MT5 may use async calls but exposes blocking results.
 */
class IBroker {
public:
    virtual ~IBroker() = default;

    /* ── Connection ── */
    virtual AF_Error connect()    = 0;
    virtual void     disconnect() = 0;
    virtual bool     is_connected() const = 0;
    virtual const char* broker_name() const = 0;

    /* ── Account ── */
    virtual AF_Error get_account(AF_AccountInfo &out) const = 0;

    /* ── Market data ── */
    virtual AF_Error get_tick(const char *symbol, AF_Tick &out) const = 0;
    virtual AF_Error get_symbol_info(const char *symbol, AF_SymbolInfo &out) const = 0;

    /**
     * Fetch historical bars into caller-allocated array.
     * bars_out must be allocated to at least `count` AF_Bar elements.
     * On return, *filled = number of bars actually written (oldest first).
     */
    virtual AF_Error get_bars(const char *symbol, AF_Timeframe tf,
                               int count, AF_Bar *bars_out, int *filled) const = 0;

    /* ── Trading ── */
    virtual AF_Error place_order(const char *symbol, AF_OrderType type,
                                  double lots, double price,
                                  double sl, double tp,
                                  uint32_t magic, const char *comment,
                                  AF_Order &out) = 0;

    virtual AF_Error close_position(uint64_t ticket, double lots = 0.0) = 0;
    virtual AF_Error modify_position(uint64_t ticket, double sl, double tp) = 0;
    virtual AF_Error cancel_order(uint64_t ticket) = 0;

    /* ── Position/Order queries ── */
    virtual std::vector<AF_Position> get_positions() const = 0;
    virtual std::vector<AF_Order>    get_orders()    const = 0;
    virtual std::optional<AF_Position> get_position(uint64_t ticket) const = 0;

    /* ── Subscriptions (optional — paper uses polling) ── */
    virtual void set_tick_callback(TickCallback cb)       { (void)cb; }
    virtual void set_position_callback(PositionCallback cb){ (void)cb; }

    /* ── Paper-mode simulation helpers ── */
    /** Poll SL/TP against latest tick (call from main loop for paper trading) */
    virtual void poll_sl_tp() {}
};

} /* namespace af */
