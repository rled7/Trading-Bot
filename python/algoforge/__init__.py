"""AlgoForge — Python implementation of the trading-bot blueprint."""

__version__ = "0.1.0"

from .types      import Bar, Direction, Timeframe
from .algorithm  import AlgoDecision, AlgoSignal, IAlgorithm, StubAlgo
from .backtest   import BTConfig, BTResult, BTTrade, BacktestEngine
from .indicators import EngineResult, IndicatorEngine
from .patterns   import PatternEngine, PatternMatch

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
    # version
    "__version__",
]
