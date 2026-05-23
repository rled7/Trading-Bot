/**
 * index.js — public API re-exports for the analytics submodule.
 * Import from 'js/src/analytics/index.js' or individual submodules.
 */

// metrics
export {
    barReturns,
    sharpe,
    sortino,
    calmar,
    omega,
    profitFactor,
    winRate,
    expectancy,
    avgWin,
    avgLoss,
    payoffRatio,
    recoveryFactor,
    timeInMarket,
    longestWinStreak,
    longestLossStreak,
    maxConsecLosses,
    allMetrics,
} from './metrics.js';

// drawdown
export {
    drawdownSeries,
    drawdownEpisodes,
    top5Drawdowns,
} from './drawdown.js';

// distributions
export {
    skewness,
    excessKurtosis,
    quantile,
    distributionStats,
} from './distributions.js';

// correlations
export {
    pearson,
    correlationMatrix,
    rollingBeta,
} from './correlations.js';

// montecarlo
export {
    SeededLCG,
    mcBootstrap,
} from './montecarlo.js';

// walkforward
export {
    walkforwardAnchored,
    walkforwardRolling,
} from './walkforward.js';

// linalg
export {
    matMul,
    transpose,
    matVec,
    matInv,
    dot,
} from './linalg.js';

// ml_attribution
export {
    ols,
    addIntercept,
} from './ml_attribution.js';

// factors
export {
    factorRegression,
} from './factors.js';

// streaming
export {
    OnlineMetrics,
} from './streaming.js';

// html_report
export {
    renderReport,
} from './html_report.js';
