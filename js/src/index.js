import { Timeframe } from "./types.js";

export const VERSION = "0.1.0";

if (import.meta.url === `file://${process.argv[1]}`) {
    console.log("AlgoForge JS — scaffold (phase 0)");
    console.log(`  Timeframe.H1 = ${Timeframe.H1} seconds`);
}
