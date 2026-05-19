import { Timeframe } from "./types.js";

export const VERSION = "0.1.0";

// ── algorithm.js re-exports ──────────────────────────────────────────────────
export { AlgoSignal, AlgoDecision, IAlgorithm, StubAlgo } from "./algorithm.js";

// ── backtest.js re-exports ───────────────────────────────────────────────────
export { BacktestEngine, BTConfig, BTResult, makeBTTrade } from "./backtest.js";

if (import.meta.url === `file://${process.argv[1]}`) {
    console.log("AlgoForge JS — scaffold (phase 0)");
    console.log(`  Timeframe.H1 = ${Timeframe.H1} seconds`);
}
