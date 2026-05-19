'use strict';

/**
 * Broker interface and shared data types (mirrors C++ IBroker / af_broker_t).
 *
 * IBroker is a duck-typed base class — every method throws if not overridden.
 * OrderType and Direction are frozen enum objects.
 */

const OrderType = Object.freeze({
    BUY: 0, SELL: 1,
    BUY_LIMIT: 2, SELL_LIMIT: 3,
    BUY_STOP: 4, SELL_STOP: 5,
});

const Direction = Object.freeze({
    NONE: 0, LONG: 1, SHORT: -1,
});

class Tick {
    constructor(symbol = '', bid = 0, ask = 0, volume = 0, timestamp = 0) {
        this.symbol    = symbol;
        this.bid       = bid;
        this.ask       = ask;
        this.volume    = volume;
        this.timestamp = timestamp;
    }
}

class AccountInfo {
    constructor({ balance = 0, equity = 0, margin = 0, freeMargin = 0,
                  profit = 0, leverage = 100, currency = 'USD', login = 0 } = {}) {
        this.balance    = balance;
        this.equity     = equity;
        this.margin     = margin;
        this.freeMargin = freeMargin;
        this.profit     = profit;
        this.leverage   = leverage;
        this.currency   = currency;
        this.login      = login;
    }
}

class SymbolInfo {
    constructor({ name = '', digits = 5, point = 0.00001, contractSize = 100_000,
                  volumeMin = 0.01, volumeMax = 100, volumeStep = 0.01 } = {}) {
        this.name         = name;
        this.digits       = digits;
        this.point        = point;
        this.contractSize = contractSize;
        this.volumeMin    = volumeMin;
        this.volumeMax    = volumeMax;
        this.volumeStep   = volumeStep;
    }
}

class Order {
    constructor({ ticket = 0, symbol = '', type = OrderType.BUY, lots = 0,
                  price = 0, sl = 0, tp = 0, fillPrice = 0,
                  magic = 0, comment = '' } = {}) {
        this.ticket    = ticket;
        this.symbol    = symbol;
        this.type      = type;
        this.lots      = lots;
        this.price     = price;
        this.sl        = sl;
        this.tp        = tp;
        this.fillPrice = fillPrice;
        this.magic     = magic;
        this.comment   = comment;
    }
}

class Position {
    constructor({ ticket = 0, symbol = '', side = Direction.NONE, lots = 0,
                  openPrice = 0, currentPrice = 0, sl = 0, tp = 0,
                  profit = 0, commission = 0, swap = 0,
                  magic = 0, comment = '' } = {}) {
        this.ticket       = ticket;
        this.symbol       = symbol;
        this.side         = side;
        this.lots         = lots;
        this.openPrice    = openPrice;
        this.currentPrice = currentPrice;
        this.sl           = sl;
        this.tp           = tp;
        this.profit       = profit;
        this.commission   = commission;
        this.swap         = swap;
        this.magic        = magic;
        this.comment      = comment;
    }
}

class IBroker {
    connect()                                                   { throw new Error('not implemented'); }
    disconnect()                                                { throw new Error('not implemented'); }
    isConnected()                                               { throw new Error('not implemented'); }
    brokerName()                                                { throw new Error('not implemented'); }
    getAccount()                                                { throw new Error('not implemented'); }
    getTick(symbol)                                             { throw new Error('not implemented'); }
    getSymbolInfo(symbol)                                       { throw new Error('not implemented'); }
    getBars(symbol, tf, count)                                  { throw new Error('not implemented'); }
    placeOrder(symbol, type, lots, price, sl, tp, magic, comment) { throw new Error('not implemented'); }
    closePosition(ticket, lots = 0)                             { throw new Error('not implemented'); }
    modifyPosition(ticket, sl, tp)                              { throw new Error('not implemented'); }
    cancelOrder(ticket)                                         { throw new Error('not implemented'); }
    getPositions()                                              { throw new Error('not implemented'); }
    getOrders()                                                 { throw new Error('not implemented'); }
    getPosition(ticket)                                         { throw new Error('not implemented'); }
    pollSlTp()                                                  { /* optional */ }
}

module.exports = { IBroker, OrderType, Direction, Tick, AccountInfo, SymbolInfo, Order, Position };
