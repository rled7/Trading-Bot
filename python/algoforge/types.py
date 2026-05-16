"""Core data types — mirror of the blueprint's AF_* C types."""

from dataclasses import dataclass
from enum import IntEnum


class Timeframe(IntEnum):
    S15 = 15
    M1  = 60
    M5  = 300
    M15 = 900
    M30 = 1800
    H1  = 3600
    H4  = 14400
    D1  = 86400
    W1  = 604800


class Direction(IntEnum):
    NONE  = 0
    LONG  = 1
    SHORT = -1


@dataclass
class Bar:
    timestamp: int
    open:      float
    high:      float
    low:       float
    close:     float
    volume:    float
    spread:    float = 0.0

    @property
    def body(self) -> float:
        return abs(self.close - self.open)

    @property
    def range(self) -> float:
        return self.high - self.low

    @property
    def is_bullish(self) -> bool:
        return self.close > self.open
