// Core data types — mirror of the blueprint's AF_* C types.

export const Timeframe = Object.freeze({
    S15: 15,
    M1:  60,
    M5:  300,
    M15: 900,
    M30: 1800,
    H1:  3600,
    H4:  14400,
    D1:  86400,
    W1:  604800,
});

export const Direction = Object.freeze({
    NONE:  0,
    LONG:  1,
    SHORT: -1,
});

export class Bar {
    constructor({ timestamp, open, high, low, close, volume, spread = 0 }) {
        this.timestamp = timestamp;
        this.open  = open;
        this.high  = high;
        this.low   = low;
        this.close = close;
        this.volume = volume;
        this.spread = spread;
    }
    get body()       { return Math.abs(this.close - this.open); }
    get range()      { return this.high - this.low; }
    get isBullish()  { return this.close > this.open; }
}
