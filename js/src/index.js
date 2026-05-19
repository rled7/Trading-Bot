import { Timeframe } from "./types.js";

export const VERSION = "0.1.0";

// ── analysis.js re-exports ───────────────────────────────────────────────────
export {
    TrendState,
    TrendBias,
    VolRegime,
    PatternCategory,
    MarketStructure,
    TrendClassifier,
    ConfluenceScorer,
    evalSession,
    classifyRegime,
    TF_WEIGHTS,
    MultiTimeframeAnalyzer,
} from "./analysis.js";

// ── algorithm.js re-exports ──────────────────────────────────────────────────
export { AlgoSignal, AlgoDecision, IAlgorithm, StubAlgo } from "./algorithm.js";

// ── algorithms.js re-exports ─────────────────────────────────────────────────
export {
    TrendFollower,
    MeanReversion,
    BreakoutTrader,
    SwingTrader,
    AlgorithmRegistry,
} from "./algorithms.js";

// ── backtest.js re-exports ───────────────────────────────────────────────────
export { BacktestEngine, BTConfig, BTResult, makeBTTrade } from "./backtest.js";

if (import.meta.url === `file://${process.argv[1]}`) {
    console.log("AlgoForge JS — scaffold (phase 0)");
    console.log(`  Timeframe.H1 = ${Timeframe.H1} seconds`);
}
