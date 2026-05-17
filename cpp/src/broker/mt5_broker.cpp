/**
 * AlgoForge — src/broker/mt5_broker.cpp
 *
 * MT5 broker stub. Compiled only when AF_MT5_AVAILABLE is defined
 * (currently: WIN32 builds, set by CMakeLists.txt). The entire
 * translation unit is empty on platforms where the macro is not
 * set, so Linux/Mac builds are unaffected.
 *
 * This is a *stub*: it exists so main.cpp's #ifdef AF_MT5_AVAILABLE
 * branch links. Real MT5 connectivity requires the MetaTrader 5
 * DLL/headers, which are not part of this repo. The stub fails
 * cleanly on connect() so callers (and operators) get an unambiguous
 * "use PAPER mode" signal instead of a mysterious linker error.
 */

#ifdef AF_MT5_AVAILABLE

#include "broker/broker.hpp"
#include <cstdio>
#include <memory>

namespace af {

namespace {

class Mt5BrokerStub : public IBroker {
public:
    AF_Error connect() override {
        fprintf(stderr,
            "[MT5] connect() called but MT5 connectivity is not implemented in this build.\n"
            "      The MT5 broker here is a stub — real connectivity requires linking\n"
            "      against the MetaTrader 5 DLL/API. Set TRADING_MODE=PAPER to use the\n"
            "      paper broker instead.\n");
        return AF_ERR_NOT_CONNECTED;
    }
    void        disconnect()           override {}
    bool        is_connected()  const  override { return false; }
    const char *broker_name()   const  override { return "MT5 (stub)"; }

    AF_Error get_account(AF_AccountInfo &)            const override { return AF_ERR_NOT_CONNECTED; }
    AF_Error get_tick(const char *, AF_Tick &)        const override { return AF_ERR_NOT_CONNECTED; }
    AF_Error get_symbol_info(const char *, AF_SymbolInfo &) const override {
        return AF_ERR_NOT_CONNECTED;
    }
    AF_Error get_bars(const char *, AF_Timeframe, int, AF_Bar *, int *filled) const override {
        if (filled) *filled = 0;
        return AF_ERR_NOT_CONNECTED;
    }

    AF_Error place_order(const char *, AF_OrderType, double, double,
                         double, double, uint32_t, const char *, AF_Order &) override {
        return AF_ERR_NOT_CONNECTED;
    }
    AF_Error close_position(uint64_t, double)         override { return AF_ERR_NOT_CONNECTED; }
    AF_Error modify_position(uint64_t, double, double) override { return AF_ERR_NOT_CONNECTED; }
    AF_Error cancel_order(uint64_t)                   override { return AF_ERR_NOT_CONNECTED; }

    std::vector<AF_Position>   get_positions() const override { return {}; }
    std::vector<AF_Order>      get_orders()    const override { return {}; }
    std::optional<AF_Position> get_position(uint64_t) const override { return std::nullopt; }
};

} /* anonymous namespace */

std::unique_ptr<IBroker> make_mt5_broker() {
    return std::make_unique<Mt5BrokerStub>();
}

} /* namespace af */

#endif /* AF_MT5_AVAILABLE */
