'use strict';

/**
 * PaperBroker — paper trading implementation of IBroker.
 *
 * Price simulation: seeded LCG random walk per symbol.
 * Commission: $7/lot charged on open.
 * P&L: (exit − open) × lots × 100 000.
 */

const { IBroker, OrderType, Direction, Tick, AccountInfo,
        SymbolInfo, Order, Position } = require('./broker');

const PAIRS = [
    { sym: 'EURUSD', start: 1.08500, vol: 0.0080, seed:  1 },
    { sym: 'GBPUSD', start: 1.26500, vol: 0.0100, seed:  2 },
    { sym: 'USDJPY', start: 149.500, vol: 0.0070, seed:  3 },
    { sym: 'XAUUSD', start: 2050.00, vol: 0.0120, seed:  4 },
    { sym: 'EURJPY', start: 162.000, vol: 0.0090, seed:  5 },
    { sym: 'GBPJPY', start: 188.000, vol: 0.0110, seed:  6 },
    { sym: 'AUDUSD', start: 0.65500, vol: 0.0090, seed:  7 },
    { sym: 'USDCAD', start: 1.36000, vol: 0.0075, seed:  8 },
    { sym: 'USDCHF', start: 0.90500, vol: 0.0070, seed:  9 },
    { sym: 'EURGBP', start: 0.85800, vol: 0.0060, seed: 10 },
];

/* ── Seeded LCG (Numerical Recipes) ── */
function lcgNext(state) {
    state[0] = (Math.imul(state[0], 1664525) + 1013904223) >>> 0;
    return state[0];
}
function lcgUniform(state) {
    return (lcgNext(state) + 0.5) / 4294967296;
}
function lcgNormal(state) {
    const u1 = Math.max(lcgUniform(state), 1e-30);
    const u2 = lcgUniform(state);
    return Math.sqrt(-2 * Math.log(u1)) * Math.cos(2 * Math.PI * u2);
}

/* ── Default spread ── */
function defaultSpread(sym) {
    if (sym.includes('XAU') || sym.includes('XAG')) return 0.25;
    if (sym.includes('JPY'))                         return 0.030;
    return 0.00012;
}

/* ── Per-symbol price simulator ── */
class PriceSim {
    constructor(start, vol, seed) {
        this.mid    = start;
        this._vol   = vol;
        this._state = [seed >>> 0];
    }
    nextMid() {
        const step = this._vol / Math.sqrt(24 * 60);
        this.mid = Math.max(this.mid * (1 + step * lcgNormal(this._state)), 0.0001);
        return this.mid;
    }
}

/* ── PaperBroker ── */
class PaperBroker extends IBroker {
    constructor(balance = 10_000) {
        super();
        this._balance     = balance;
        this._connected   = false;
        this._nextTicket  = 1_000_001;
        this._sims        = new Map(PAIRS.map(p => [p.sym, new PriceSim(p.start, p.vol, p.seed)]));
        this._positions   = new Map();
        this._rngGlobal   = [1337];
    }

    /* ── Connection ── */

    connect() {
        this._connected = true;
        console.log(`[PAPER] Connected | balance=${this._balance.toFixed(2)}`);
        return 0;
    }
    disconnect()  { this._connected = false; }
    isConnected() { return this._connected; }
    brokerName()  { return 'PaperSimulator'; }

    /* ── Account ── */

    getAccount() {
        const floating = [...this._positions.values()].reduce((s, p) => s + p.profit, 0);
        return new AccountInfo({
            balance: this._balance, equity: this._balance + floating,
            freeMargin: this._balance, leverage: 100, currency: 'USD', login: 99999999,
        });
    }

    /* ── Market data ── */

    getTick(symbol) {
        const sim = this._sims.get(symbol);
        if (!sim) throw new Error(`Unknown symbol: ${symbol}`);
        const mid = sim.nextMid();
        const sp  = defaultSpread(symbol);
        return new Tick(symbol, mid - sp / 2, mid + sp / 2,
                        500 + lcgUniform(this._rngGlobal) * 2000);
    }

    getSymbolInfo(symbol) {
        const digits = (symbol.includes('XAU') || symbol.includes('XAG')) ? 2
                     : symbol.includes('JPY') ? 3 : 5;
        return new SymbolInfo({
            name: symbol, digits, point: Math.pow(10, -digits),
            contractSize: 100_000, volumeMin: 0.01, volumeMax: 100, volumeStep: 0.01,
        });
    }

    getBars(symbol, tf, count) {
        const sim = this._sims.get(symbol);
        if (!sim) throw new Error(`Unknown symbol: ${symbol}`);
        const rng = [42 ^ tf];
        const volPerBar = 0.0080 / Math.sqrt(86400 / tf) / Math.sqrt(252);
        let price = sim.mid;
        const bars = [];
        for (let i = 0; i < count; i++) {
            const o = price;
            let h = o, l = o;
            for (let t = 0; t < 4; t++) {
                const p = o * (1 + volPerBar * lcgNormal(rng));
                if (p > h) h = p;
                if (p < l) l = p;
            }
            const c = o * (1 + volPerBar * lcgNormal(rng));
            if (c > h) h = c;
            if (c < l) l = c;
            bars.push({
                timestamp: 0, open: o, high: h, low: l, close: c,
                volume: 500 + lcgUniform(rng) * 4500, spread: defaultSpread(symbol),
            });
            price = c;
        }
        return bars;
    }

    /* ── Trading ── */

    placeOrder(symbol, type, lots, price = 0, sl = 0, tp = 0, magic = 0, comment = '') {
        if (lots < 0.01) throw new Error('lots must be >= 0.01');
        const sim = this._sims.get(symbol);
        if (!sim) throw new Error(`Unknown symbol: ${symbol}`);

        const mid = sim.nextMid();
        const sp  = defaultSpread(symbol);
        const bid = mid - sp / 2, ask = mid + sp / 2;

        let fillP;
        if (type === OrderType.BUY  || type === OrderType.BUY_STOP)   fillP = ask;
        else if (type === OrderType.SELL || type === OrderType.SELL_STOP) fillP = bid;
        else if (type === OrderType.BUY_LIMIT)                         fillP = Math.min(price, ask);
        else                                                            fillP = Math.max(price, bid);

        const isLong     = type === OrderType.BUY || type === OrderType.BUY_LIMIT || type === OrderType.BUY_STOP;
        const commission = 7.0 * lots;
        const ticket     = this._nextTicket++;

        const pos = new Position({
            ticket, symbol, side: isLong ? Direction.LONG : Direction.SHORT,
            lots, openPrice: fillP, currentPrice: fillP,
            sl, tp, profit: 0, commission: -commission, magic, comment,
        });
        this._positions.set(ticket, pos);
        this._balance -= commission;

        console.log(`[PAPER] ${isLong ? 'BUY' : 'SELL'} ${lots.toFixed(2)} ${symbol} @ ${fillP.toFixed(5)}` +
                    ` | SL=${sl.toFixed(5)} TP=${tp.toFixed(5)} | comm=-${commission.toFixed(2)}`);
        return new Order({ ticket, symbol, type, lots, fillPrice: fillP, sl, tp, magic });
    }

    closePosition(ticket, lots = 0) {
        const pos = this._positions.get(ticket);
        if (!pos) return -6;  /* AF_ERR_POSITION_NOT_FOUND */
        const sim  = this._sims.get(pos.symbol);
        const mid  = sim ? sim.nextMid() : pos.openPrice;
        const sp   = defaultSpread(pos.symbol);
        const exitP = pos.side === Direction.LONG ? mid - sp / 2 : mid + sp / 2;
        const closeLots = (lots > 0 && lots < pos.lots) ? lots : pos.lots;
        const rawPnl = (pos.side === Direction.LONG
                        ? exitP - pos.openPrice : pos.openPrice - exitP) * closeLots * 100_000;
        this._balance += rawPnl;
        console.log(`[PAPER] CLOSE ${pos.symbol} ${pos.side === Direction.LONG ? 'LONG' : 'SHORT'} ` +
                    `${closeLots.toFixed(2)} @ ${exitP.toFixed(5)} | P&L=${rawPnl.toFixed(2)}`);
        if (closeLots >= pos.lots) this._positions.delete(ticket);
        else pos.lots -= closeLots;
        return 0;
    }

    modifyPosition(ticket, sl, tp) {
        const pos = this._positions.get(ticket);
        if (!pos) return -6;
        pos.sl = sl; pos.tp = tp;
        return 0;
    }

    cancelOrder(ticket) { return 0; }

    /* ── Queries ── */

    getPositions() { return [...this._positions.values()]; }
    getOrders()    { return []; }
    getPosition(ticket) { return this._positions.get(ticket) ?? null; }

    /* ── Simulation helper ── */

    pollSlTp() {
        const toClose = [];
        for (const [ticket, pos] of this._positions) {
            const sim = this._sims.get(pos.symbol);
            if (!sim) continue;
            const mid = sim.nextMid();
            const sp  = defaultSpread(pos.symbol);
            const bid = mid - sp / 2, ask = mid + sp / 2;
            pos.currentPrice = pos.side === Direction.LONG ? bid : ask;
            let hit = false;
            if (pos.sl > 0)
                hit = (pos.side === Direction.LONG  && bid <= pos.sl) ||
                      (pos.side === Direction.SHORT && ask >= pos.sl);
            if (!hit && pos.tp > 0)
                hit = (pos.side === Direction.LONG  && bid >= pos.tp) ||
                      (pos.side === Direction.SHORT && ask <= pos.tp);
            if (hit) toClose.push(ticket);
        }
        for (const t of toClose) this.closePosition(t);
    }
}

function makePaperBroker(balance = 10_000) {
    return new PaperBroker(balance);
}

module.exports = { PaperBroker, makePaperBroker };
