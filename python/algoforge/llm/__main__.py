from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def _load_json(path: str) -> dict:
    p = Path(path)
    if not p.exists():
        print(f"error: file not found: {path}", file=sys.stderr)
        sys.exit(1)
    with p.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def _write_out(text: str, out: str | None) -> None:
    if out:
        Path(out).write_text(text, encoding="utf-8")
    else:
        print(text)


def cmd_rationale(args: argparse.Namespace) -> None:
    from algoforge.llm import (
        OllamaProvider,
        TradeRationaleInput,
        trade_rationale,
    )
    from algoforge.llm.provider import LLMError

    data = _load_json(args.signal)
    inp = TradeRationaleInput(
        symbol=data["symbol"],
        side=data["side"],
        entry_price=float(data["entry_price"]),
        stop_loss=float(data["stop_loss"]),
        take_profit=float(data["take_profit"]),
        indicators=data.get("indicators", {}),
        patterns=data.get("patterns", []),
        timeframe=data["timeframe"],
    )
    provider = OllamaProvider.from_env()
    model = args.model or provider._model
    try:
        result = trade_rationale(provider, inp, model=model)
    except LLMError as exc:
        print(f"error: {exc.message}", file=sys.stderr)
        sys.exit(2 if exc.kind == "unreachable" else 3)
    _write_out(result.explanation, args.out)


def cmd_commentary(args: argparse.Namespace) -> None:
    from algoforge.llm import (
        BacktestCommentaryInput,
        OllamaProvider,
        backtest_commentary,
    )
    from algoforge.llm.provider import LLMError

    data = _load_json(args.backtest)
    inp = BacktestCommentaryInput(
        strategy_name=data["strategy_name"],
        period=data["period"],
        total_return=float(data["total_return"]),
        sharpe=float(data["sharpe"]),
        sortino=float(data["sortino"]),
        max_drawdown=float(data["max_drawdown"]),
        win_rate=float(data["win_rate"]),
        trade_count=int(data["trade_count"]),
        best_trade=float(data["best_trade"]),
        worst_trade=float(data["worst_trade"]),
    )
    provider = OllamaProvider.from_env()
    model = args.model or provider._model
    try:
        result = backtest_commentary(provider, inp, model=model)
    except LLMError as exc:
        print(f"error: {exc.message}", file=sys.stderr)
        sys.exit(2 if exc.kind == "unreachable" else 3)
    _write_out(result.commentary, args.out)


def cmd_sentiment(args: argparse.Namespace) -> None:
    from algoforge.llm import OllamaProvider, news_sentiment
    from algoforge.llm.provider import LLMError

    provider = OllamaProvider.from_env()
    try:
        result = news_sentiment(provider, headline=args.headline, body=args.body or "")
    except LLMError as exc:
        print(f"error: {exc.message}", file=sys.stderr)
        sys.exit(2 if exc.kind == "unreachable" else 3)
    out = json.dumps(
        {
            "sentiment": result.sentiment,
            "score": result.score,
            "confidence": result.confidence,
            "reason": result.reason,
        },
        indent=2,
    )
    print(out)


def cmd_describe(args: argparse.Namespace) -> None:
    from algoforge.llm import OllamaProvider, StrategyDescribeInput, strategy_describe
    from algoforge.llm.provider import LLMError

    data = _load_json(args.strategy)
    inp = StrategyDescribeInput(
        name=data["name"],
        indicators_used=data.get("indicators_used", []),
        entry_rules=data.get("entry_rules", []),
        exit_rules=data.get("exit_rules", []),
        risk_rules=data.get("risk_rules", []),
        timeframe=data["timeframe"],
    )
    provider = OllamaProvider.from_env()
    try:
        result = strategy_describe(provider, inp)
    except LLMError as exc:
        print(f"error: {exc.message}", file=sys.stderr)
        sys.exit(2 if exc.kind == "unreachable" else 3)
    print(result.description)


def cmd_health(args: argparse.Namespace) -> None:
    from algoforge.llm import OllamaProvider

    provider = OllamaProvider.from_env()
    status = provider.health()
    print(json.dumps({"ok": status.ok, "model_loaded": status.model_loaded, "model": status.model, "error": status.error}, indent=2))
    if not status.ok:
        sys.exit(2)


def cmd_models(args: argparse.Namespace) -> None:
    from algoforge.llm import OllamaProvider
    from algoforge.llm.provider import LLMError

    provider = OllamaProvider.from_env()
    try:
        models = provider.list_models()
    except LLMError as exc:
        print(f"error: {exc.message}", file=sys.stderr)
        sys.exit(2)
    for m in models:
        print(f"{m.name}\t{m.size_bytes}\t{m.modified_at}")


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        prog="python -m algoforge.llm",
        description="AlgoForge LLM layer CLI",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_rationale = sub.add_parser("rationale", help="Generate trade rationale")
    p_rationale.add_argument("--signal", required=True, metavar="PATH")
    p_rationale.add_argument("--model", default=None, metavar="MODEL")
    p_rationale.add_argument("--out", default=None, metavar="PATH")
    p_rationale.set_defaults(func=cmd_rationale)

    p_commentary = sub.add_parser("commentary", help="Generate backtest commentary")
    p_commentary.add_argument("--backtest", required=True, metavar="PATH")
    p_commentary.add_argument("--model", default=None, metavar="MODEL")
    p_commentary.add_argument("--out", default=None, metavar="PATH")
    p_commentary.set_defaults(func=cmd_commentary)

    p_sentiment = sub.add_parser("sentiment", help="Classify news sentiment")
    p_sentiment.add_argument("--headline", required=True)
    p_sentiment.add_argument("--body", default=None)
    p_sentiment.set_defaults(func=cmd_sentiment)

    p_describe = sub.add_parser("describe", help="Describe strategy")
    p_describe.add_argument("--strategy", required=True, metavar="PATH")
    p_describe.set_defaults(func=cmd_describe)

    p_health = sub.add_parser("health", help="Check LLM health")
    p_health.set_defaults(func=cmd_health)

    p_models = sub.add_parser("models", help="List available models")
    p_models.set_defaults(func=cmd_models)

    args = parser.parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
