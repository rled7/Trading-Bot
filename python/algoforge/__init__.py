"""AlgoForge — Python implementation of the trading-bot blueprint."""

__version__ = "0.1.0"

from .types      import Bar, Direction, Timeframe
from .algorithm  import AlgoDecision, AlgoSignal, IAlgorithm, StubAlgo
from .backtest   import BTConfig, BTResult, BTTrade, BacktestEngine
from .indicators import EngineResult, IndicatorEngine
from .patterns   import PatternEngine, PatternMatch
from .algorithms import (
    AlgorithmRegistry,
    BreakoutTrader,
    MeanReversion,
    SwingTrader,
    TrendFollower,
)
from .risk import (
    SizingMethod,
    SizeResult,
    GuardState,
    GuardStatus,
    PositionSizer,
    DrawdownGuard,
    SignalProcessor,
)
from .analysis import (
    # enums
    TrendState,
    TrendBias,
    VolRegime,
    SessionType,
    PatternCategory,
    # result dataclasses
    StructureResult,
    TrendClassification,
    ConfluenceScore,
    SessionScore,
    RegimeResult,
    MTFResult,
    # analysis classes
    MarketStructure,
    TrendClassifier,
    ConfluenceScorer,
    RegimeClassifier,
    MultiTimeframeAnalyzer,
    # module-level functions
    eval_session,
    classify_regime,
    # constants
    TF_WEIGHTS,
)

__all__ = [
    # types
    "Bar", "Direction", "Timeframe",
    # algorithm
    "AlgoDecision", "AlgoSignal", "IAlgorithm", "StubAlgo",
    # backtest
    "BTConfig", "BTResult", "BTTrade", "BacktestEngine",
    # indicators
    "EngineResult", "IndicatorEngine",
    # patterns
    "PatternEngine", "PatternMatch",
    # algorithms
    "AlgorithmRegistry",
    "BreakoutTrader",
    "MeanReversion",
    "SwingTrader",
    "TrendFollower",
    # analysis enums
    "TrendState",
    "TrendBias",
    "VolRegime",
    "SessionType",
    "PatternCategory",
    # analysis result dataclasses
    "StructureResult",
    "TrendClassification",
    "ConfluenceScore",
    "SessionScore",
    "RegimeResult",
    "MTFResult",
    # analysis classes
    "MarketStructure",
    "TrendClassifier",
    "ConfluenceScorer",
    "RegimeClassifier",
    "MultiTimeframeAnalyzer",
    # analysis functions
    "eval_session",
    "classify_regime",
    # analysis constants
    "TF_WEIGHTS",
    # risk
    "SizingMethod",
    "SizeResult",
    "GuardState",
    "GuardStatus",
    "PositionSizer",
    "DrawdownGuard",
    "SignalProcessor",
    # version
    "__version__",
]
