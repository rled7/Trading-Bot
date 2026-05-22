/**
 * MT5Broker — stub implementation of IBroker for MetaTrader 5.
 *
 * Mirrors cpp/src/broker/mt5_broker.cpp: fails cleanly on connect() so
 * callers get an unambiguous "use PAPER mode" signal instead of a
 * cryptic runtime error. All trading methods return AF_ERR_NOT_CONNECTED
 * (-1); query methods return empty arrays / null.
 *
 * Real MT5 connectivity requires the MetaTrader 5 API/DLL, which is not
 * available in a Node.js environment. Use TRADING_MODE=PAPER to trade, or
 * run via the Python layer for live MT5 connectivity.
 *
 * IBroker is not imported here because broker.js uses CommonJS syntax
 * (module.exports) and is not directly importable in this ESM project.
 * MT5Broker is duck-typed to the IBroker contract — all method signatures
 * match IBroker exactly.
 */

/* Error code mirroring AF_ERR_NOT_CONNECTED from af_broker_t.h */
const AF_ERR_NOT_CONNECTED = -1;

class MT5Broker {
    /* ── Connection ── */

    connect() {
        console.error(
            '[MT5] connect() called but MT5 connectivity is not available in Node.\n' +
            '      The MT5 broker is a stub — real connectivity requires the MetaTrader 5\n' +
            '      DLL/API, which is not accessible from a Node.js process.\n' +
            '      Set TRADING_MODE=PAPER to use the paper broker, or run via Python for\n' +
            '      live MT5 connectivity.'
        );
        return AF_ERR_NOT_CONNECTED;
    }

    disconnect() { /* no-op: never connected */ }

    isConnected() { return false; }

    brokerName() { return 'MT5 (stub)'; }

    /* ── Account ── */

    getAccount() { return AF_ERR_NOT_CONNECTED; }

    /* ── Market data ── */

    getTick(/* symbol */) { return AF_ERR_NOT_CONNECTED; }

    getSymbolInfo(/* symbol */) { return AF_ERR_NOT_CONNECTED; }

    getBars(/* symbol, tf, count */) { return AF_ERR_NOT_CONNECTED; }

    /* ── Trading ── */

    placeOrder(/* symbol, type, lots, price, sl, tp, magic, comment */) {
        return AF_ERR_NOT_CONNECTED;
    }

    closePosition(/* ticket, lots */) { return AF_ERR_NOT_CONNECTED; }

    modifyPosition(/* ticket, sl, tp */) { return AF_ERR_NOT_CONNECTED; }

    cancelOrder(/* ticket */) { return AF_ERR_NOT_CONNECTED; }

    /* ── Queries ── */

    getPositions() { return []; }

    getOrders() { return []; }

    getPosition(/* ticket */) { return null; }

    pollSlTp() { /* optional — no-op in stub */ }
}

function makeMT5Broker() {
    return new MT5Broker();
}

export { MT5Broker, makeMT5Broker };
