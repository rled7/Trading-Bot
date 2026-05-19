"""IAlgorithm — abstract algorithm interface and helpers.

Mirrors the C++ IAlgorithm / AF_AlgoDecision / AF_AlgoSignal blueprint.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import IntEnum

from .types import Bar, Direction


class AlgoSignal(IntEnum):
    NONE = 0
    BUY  = 1
    SELL = -1


@dataclass
class AlgoDecision:
    signal:     AlgoSignal = AlgoSignal.NONE
    symbol:     str        = ""
    direction:  Direction  = Direction.NONE
    confidence: float      = 0.0
    reason:     str        = ""
    atr:        float      = 0.0

    def is_actionable(self) -> bool:
        return self.signal != AlgoSignal.NONE and self.confidence >= 0.55

    @staticmethod
    def none(symbol: str = "") -> "AlgoDecision":
        return AlgoDecision(symbol=symbol)


class IAlgorithm(ABC):
    _enabled: bool = True

    @abstractmethod
    def name(self) -> str: ...

    @abstractmethod
    def description(self) -> str: ...

    @abstractmethod
    def magic(self) -> int: ...

    @abstractmethod
    def evaluate(
        self,
        symbol: str,
        tf: int,
        bars: list[Bar],
        count: int,
        ind: object,
        pat: object,
    ) -> AlgoDecision: ...

    def safe_evaluate(
        self,
        symbol: str,
        tf: int,
        bars: list[Bar],
        count: int,
        ind: object,
        pat: object,
    ) -> AlgoDecision:
        if not self._enabled:
            return AlgoDecision.none(symbol)
        try:
            return self.evaluate(symbol, tf, bars, count, ind, pat)
        except Exception:
            return AlgoDecision.none(symbol)

    def enable(self) -> None:
        self._enabled = True

    def disable(self) -> None:
        self._enabled = False

    def is_enabled(self) -> bool:
        return self._enabled


class StubAlgo(IAlgorithm):
    """Always returns NONE — for testing the backtest harness."""

    def name(self) -> str:
        return "stub"

    def description(self) -> str:
        return "Stub algo that always returns NONE"

    def magic(self) -> int:
        return 0

    def evaluate(
        self,
        symbol: str,
        tf: int,
        bars: list[Bar],
        count: int,
        ind: object,
        pat: object,
    ) -> AlgoDecision:
        return AlgoDecision.none(symbol)
