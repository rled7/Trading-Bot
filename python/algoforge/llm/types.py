from __future__ import annotations

from dataclasses import dataclass, field


@dataclass(frozen=True)
class CompletionRequest:
    model: str
    prompt: str
    temperature: float = 0.2
    max_tokens: int | None = None
    stop: list[str] = field(default_factory=list)
    seed: int | None = None
    timeout_s: float = 60.0


@dataclass(frozen=True)
class CompletionResponse:
    text: str
    model: str
    prompt_tokens: int
    completion_tokens: int
    total_duration_ns: int
    finish_reason: str


@dataclass(frozen=True)
class ChatMessage:
    role: str
    content: str


@dataclass(frozen=True)
class ChatRequest:
    model: str
    messages: list[ChatMessage]
    temperature: float = 0.2
    max_tokens: int | None = None
    seed: int | None = None
    timeout_s: float = 60.0


@dataclass(frozen=True)
class ChatResponse:
    message: ChatMessage
    model: str
    prompt_tokens: int
    completion_tokens: int
    total_duration_ns: int
    finish_reason: str


@dataclass(frozen=True)
class ChatChunk:
    delta: str
    done: bool
    finish_reason: str | None


@dataclass(frozen=True)
class EmbedRequest:
    model: str
    input: str | list[str]


@dataclass(frozen=True)
class EmbedResponse:
    embeddings: list[list[float]]
    model: str


@dataclass(frozen=True)
class HealthStatus:
    ok: bool
    model_loaded: bool
    model: str | None
    error: str | None


@dataclass(frozen=True)
class ModelInfo:
    name: str
    size_bytes: int
    modified_at: str


@dataclass(frozen=True)
class TradeRationaleInput:
    symbol: str
    side: str
    entry_price: float
    stop_loss: float
    take_profit: float
    indicators: dict[str, float]
    patterns: list[str]
    timeframe: str


@dataclass(frozen=True)
class TradeRationaleOutput:
    explanation: str
    model: str
    tokens: int


@dataclass(frozen=True)
class BacktestCommentaryInput:
    strategy_name: str
    period: str
    total_return: float
    sharpe: float
    sortino: float
    max_drawdown: float
    win_rate: float
    trade_count: int
    best_trade: float
    worst_trade: float


@dataclass(frozen=True)
class BacktestCommentaryOutput:
    commentary: str
    model: str
    tokens: int


@dataclass(frozen=True)
class NewsSentimentOutput:
    sentiment: str
    score: float
    confidence: float
    reason: str


@dataclass(frozen=True)
class StrategyDescribeInput:
    name: str
    indicators_used: list[str]
    entry_rules: list[str]
    exit_rules: list[str]
    risk_rules: list[str]
    timeframe: str


@dataclass(frozen=True)
class StrategyDescribeOutput:
    description: str
    model: str
    tokens: int
