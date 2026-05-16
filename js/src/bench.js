// Cross-language benchmark — js/.
//
// Run standalone:
//   npm run bench
//   node src/bench.js 100000 10

import { sma, ema, rsi, atr } from "./indicators.js";

function genSeries(n) {
    // Deterministic walk shared bit-for-bit across all four language benches.
    const out = new Array(n);
    let p = 100.0;
    for (let i = 0; i < n; i++) {
        p += ((i * 9301 + 49297) % 233 - 116) * 0.01;
        out[i] = p;
    }
    return out;
}

function timed(fn, iters) {
    const t0 = process.hrtime.bigint();
    for (let r = 0; r < iters; r++) fn();
    const total = process.hrtime.bigint() - t0;
    return [total, total / BigInt(iters)];
}

function main() {
    const n     = parseInt(process.argv[2] ?? "100000", 10);
    const iters = parseInt(process.argv[3] ?? "10", 10);

    const src = genSeries(n);
    const hi  = src.map(v => v + 0.5);
    const lo  = src.map(v => v - 0.5);

    console.log("# language\tjs");
    console.log(`# bars\t${n}`);
    console.log(`# iterations\t${iters}`);
    console.log("indicator\ttotal_ns\tns_per_iter");

    const cases = [
        ["sma20", () => sma(src, 20)],
        ["ema50", () => ema(src, 50)],
        ["rsi14", () => rsi(src, 14)],
        ["atr14", () => atr(hi, lo, src, 14)],
    ];
    for (const [label, fn] of cases) {
        const [total, per] = timed(fn, iters);
        console.log(`${label}\t${total}\t${per}`);
    }
}

main();
