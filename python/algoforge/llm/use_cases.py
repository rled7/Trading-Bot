from __future__ import annotations

import json
import re

from algoforge.llm.provider import LLMError, LLMProvider
from algoforge.llm.prompts import (
    BACKTEST_COMMENTARY_SYSTEM,
    BACKTEST_COMMENTARY_USER,
    NEWS_SENTIMENT_SYSTEM,
    NEWS_SENTIMENT_USER,
    STRATEGY_DESCRIBE_SYSTEM,
    STRATEGY_DESCRIBE_USER,
    TRADE_RATIONALE_SYSTEM,
    TRADE_RATIONALE_USER,
)
from algoforge.llm.types import (
    BacktestCommentaryInput,
    BacktestCommentaryOutput,
    ChatMessage,
    ChatRequest,
    NewsSentimentOutput,
    StrategyDescribeInput,
    StrategyDescribeOutput,
    TradeRationaleInput,
    TradeRationaleOutput,
)

_DEFAULT_MODEL = "llama3.1:8b"
_DEFAULT_TEMPERATURE = 0.2
_DEFAULT_MAX_TOKENS = 512
_DEFAULT_SEED = 42


def _extract_json(text: str) -> dict:
    depth = 0
    in_string = False
    escape = False
    start = None
    for i, ch in enumerate(text):
        if escape:
            escape = False
            continue
        if ch == "\\" and in_string:
            escape = True
            continue
        if ch == '"':
            in_string = not in_string
            continue
        if in_string:
            continue
        if ch == "{":
            if depth == 0:
                start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start is not None:
                candidate = text[start : i + 1]
                try:
                    return json.loads(candidate)
                except json.JSONDecodeError:
                    pass
                start = None
    raise LLMError("decode", "no valid JSON object found in response")


def trade_rationale(
    provider: LLMProvider,
    inp: TradeRationaleInput,
    model: str = _DEFAULT_MODEL,
    temperature: float = _DEFAULT_TEMPERATURE,
    max_tokens: int | None = _DEFAULT_MAX_TOKENS,
    seed: int | None = _DEFAULT_SEED,
) -> TradeRationaleOutput:
    indicators_json = json.dumps(inp.indicators, sort_keys=True, separators=(",", ":"))
    patterns_csv = ",".join(inp.patterns)
    user_content = TRADE_RATIONALE_USER.format(
        symbol=inp.symbol,
        side=inp.side,
        timeframe=inp.timeframe,
        entry_price=f"{inp.entry_price:.4f}",
        stop_loss=f"{inp.stop_loss:.4f}",
        take_profit=f"{inp.take_profit:.4f}",
        indicators_json_sorted=indicators_json,
        patterns_csv=patterns_csv,
    )
    req = ChatRequest(
        model=model,
        messages=[
            ChatMessage(role="system", content=TRADE_RATIONALE_SYSTEM),
            ChatMessage(role="user", content=user_content),
        ],
        temperature=temperature,
        max_tokens=max_tokens,
        seed=seed,
    )
    resp = provider.chat(req)
    return TradeRationaleOutput(
        explanation=resp.message.content,
        model=resp.model,
        tokens=resp.completion_tokens,
    )


def backtest_commentary(
    provider: LLMProvider,
    inp: BacktestCommentaryInput,
    model: str = _DEFAULT_MODEL,
    temperature: float = _DEFAULT_TEMPERATURE,
    max_tokens: int | None = _DEFAULT_MAX_TOKENS,
    seed: int | None = _DEFAULT_SEED,
) -> BacktestCommentaryOutput:
    user_content = BACKTEST_COMMENTARY_USER.format(
        strategy_name=inp.strategy_name,
        period=inp.period,
        total_return=f"{inp.total_return:.4f}",
        sharpe=f"{inp.sharpe:.4f}",
        sortino=f"{inp.sortino:.4f}",
        max_drawdown=f"{inp.max_drawdown:.4f}",
        win_rate=f"{inp.win_rate:.4f}",
        trade_count=inp.trade_count,
        best_trade=f"{inp.best_trade:.4f}",
        worst_trade=f"{inp.worst_trade:.4f}",
    )
    req = ChatRequest(
        model=model,
        messages=[
            ChatMessage(role="system", content=BACKTEST_COMMENTARY_SYSTEM),
            ChatMessage(role="user", content=user_content),
        ],
        temperature=temperature,
        max_tokens=max_tokens,
        seed=seed,
    )
    resp = provider.chat(req)
    return BacktestCommentaryOutput(
        commentary=resp.message.content,
        model=resp.model,
        tokens=resp.completion_tokens,
    )


def news_sentiment(
    provider: LLMProvider,
    headline: str,
    body: str = "",
    model: str = _DEFAULT_MODEL,
    temperature: float = _DEFAULT_TEMPERATURE,
    max_tokens: int | None = _DEFAULT_MAX_TOKENS,
    seed: int | None = _DEFAULT_SEED,
) -> NewsSentimentOutput:
    user_content = NEWS_SENTIMENT_USER.format(
        headline=headline,
        body_or_empty=body,
    )
    req = ChatRequest(
        model=model,
        messages=[
            ChatMessage(role="system", content=NEWS_SENTIMENT_SYSTEM),
            ChatMessage(role="user", content=user_content),
        ],
        temperature=temperature,
        max_tokens=max_tokens,
        seed=seed,
    )
    resp = provider.chat(req)
    data = _extract_json(resp.message.content)
    return NewsSentimentOutput(
        sentiment=data["sentiment"],
        score=float(data["score"]),
        confidence=float(data["confidence"]),
        reason=data["reason"],
    )


def strategy_describe(
    provider: LLMProvider,
    inp: StrategyDescribeInput,
    model: str = _DEFAULT_MODEL,
    temperature: float = _DEFAULT_TEMPERATURE,
    max_tokens: int | None = _DEFAULT_MAX_TOKENS,
    seed: int | None = _DEFAULT_SEED,
) -> StrategyDescribeOutput:
    user_content = STRATEGY_DESCRIBE_USER.format(
        name=inp.name,
        timeframe=inp.timeframe,
        indicators_csv=",".join(inp.indicators_used),
        entry_rules_csv=",".join(inp.entry_rules),
        exit_rules_csv=",".join(inp.exit_rules),
        risk_rules_csv=",".join(inp.risk_rules),
    )
    req = ChatRequest(
        model=model,
        messages=[
            ChatMessage(role="system", content=STRATEGY_DESCRIBE_SYSTEM),
            ChatMessage(role="user", content=user_content),
        ],
        temperature=temperature,
        max_tokens=max_tokens,
        seed=seed,
    )
    resp = provider.chat(req)
    return StrategyDescribeOutput(
        description=resp.message.content,
        model=resp.model,
        tokens=resp.completion_tokens,
    )
