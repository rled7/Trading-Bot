from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

# ── helpers ──────────────────────────────────────────────────────────────────

FIXTURES_DIR = Path(__file__).parent.parent.parent / "tests" / "fixtures" / "llm"


def load_fixture(name: str) -> dict:
    path = FIXTURES_DIR / f"{name}.json"
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def make_provider(fixture_name: str):
    from algoforge.llm.fixtures import MockTransport
    from algoforge.llm.ollama import OllamaProvider

    fixture = load_fixture(fixture_name)
    transport = MockTransport(fixture)
    provider = OllamaProvider(
        host="http://localhost:11434",
        model="llama3.1:8b",
        embed_model="nomic-embed-text",
        timeout_s=60.0,
        seed=42,
        temperature=0.2,
        max_tokens=256,
        transport=transport,
    )
    return provider, transport, fixture


# ── §2 fixture schema tests ──────────────────────────────────────────────────

class TestFixtureSchema:
    FIXTURE_NAMES = [
        "complete_basic", "chat_basic", "chat_streaming", "embed_basic",
        "health_ok", "health_down", "list_models",
        "trade_rationale_long", "trade_rationale_short",
        "backtest_commentary_winning", "backtest_commentary_losing",
        "news_sentiment_bullish", "news_sentiment_bearish", "news_sentiment_neutral",
        "strategy_describe_breakout", "strategy_describe_meanrev",
    ]

    @pytest.mark.parametrize("name", FIXTURE_NAMES)
    def test_fixture_has_name(self, name):
        f = load_fixture(name)
        assert f["name"] == name

    @pytest.mark.parametrize("name", FIXTURE_NAMES)
    def test_fixture_has_request(self, name):
        f = load_fixture(name)
        assert "request" in f

    @pytest.mark.parametrize("name", FIXTURE_NAMES)
    def test_fixture_has_response(self, name):
        f = load_fixture(name)
        assert "response" in f
        assert "status" in f["response"]
        assert "body" in f["response"]

    def test_all_16_fixtures_exist(self):
        for name in self.FIXTURE_NAMES:
            p = FIXTURES_DIR / f"{name}.json"
            assert p.exists(), f"fixture missing: {name}.json"


# ── provider tests ─────────────────────────────────────────────────────────

class TestComplete:
    def test_outgoing_request_body(self):
        provider, transport, fixture = make_provider("complete_basic")
        from algoforge.llm import CompletionRequest
        req = CompletionRequest(
            model="llama3.1:8b",
            prompt="Why did EURUSD break out?",
            temperature=0.2,
            max_tokens=256,
            seed=42,
        )
        provider.complete(req)
        assert transport.last_request is not None
        body = transport.last_request["body"]
        expected = fixture["request"]["body"]
        assert body == expected

    def test_response_fields(self):
        provider, transport, fixture = make_provider("complete_basic")
        from algoforge.llm import CompletionRequest
        req = CompletionRequest(
            model="llama3.1:8b",
            prompt="Why did EURUSD break out?",
            temperature=0.2,
            max_tokens=256,
            seed=42,
        )
        resp = provider.complete(req)
        rb = fixture["response"]["body"]
        assert resp.text == rb["response"]
        assert resp.model == rb["model"]
        assert resp.prompt_tokens == rb["prompt_eval_count"]
        assert resp.completion_tokens == rb["eval_count"]
        assert resp.total_duration_ns == rb["total_duration"]
        assert resp.finish_reason == rb["done_reason"]

    def test_complete_no_seed_no_max_tokens(self):
        from algoforge.llm.fixtures import MockTransport
        from algoforge.llm.ollama import OllamaProvider
        from algoforge.llm import CompletionRequest

        fake_resp = {
            "model": "llama3.1:8b", "response": "ok", "done": True,
            "done_reason": "stop", "prompt_eval_count": 5, "eval_count": 3,
            "total_duration": 100
        }
        fixture = {"response": {"status": 200, "body": fake_resp}}
        transport = MockTransport(fixture)
        provider = OllamaProvider(transport=transport)
        req = CompletionRequest(model="llama3.1:8b", prompt="test")
        provider.complete(req)
        body = transport.last_request["body"]
        assert "num_predict" not in body["options"]
        assert "seed" not in body["options"]


class TestChat:
    def test_outgoing_request_body(self):
        provider, transport, fixture = make_provider("chat_basic")
        from algoforge.llm import ChatRequest, ChatMessage
        req = ChatRequest(
            model="llama3.1:8b",
            messages=[
                ChatMessage(role="system", content="You are a trading assistant."),
                ChatMessage(role="user", content="Explain ATR."),
            ],
            temperature=0.2,
            max_tokens=256,
            seed=42,
        )
        provider.chat(req)
        body = transport.last_request["body"]
        assert body == fixture["request"]["body"]

    def test_response_fields(self):
        provider, transport, fixture = make_provider("chat_basic")
        from algoforge.llm import ChatRequest, ChatMessage
        req = ChatRequest(
            model="llama3.1:8b",
            messages=[
                ChatMessage(role="system", content="You are a trading assistant."),
                ChatMessage(role="user", content="Explain ATR."),
            ],
            temperature=0.2,
            max_tokens=256,
            seed=42,
        )
        resp = provider.chat(req)
        rb = fixture["response"]["body"]
        assert resp.message.role == rb["message"]["role"]
        assert resp.message.content == rb["message"]["content"]
        assert resp.model == rb["model"]
        assert resp.prompt_tokens == rb["prompt_eval_count"]
        assert resp.completion_tokens == rb["eval_count"]
        assert resp.total_duration_ns == rb["total_duration"]
        assert resp.finish_reason == rb["done_reason"]

    def test_chat_omits_seed_when_none(self):
        from algoforge.llm.fixtures import MockTransport
        from algoforge.llm.ollama import OllamaProvider
        from algoforge.llm import ChatRequest, ChatMessage

        fake_resp = {
            "model": "llama3.1:8b",
            "message": {"role": "assistant", "content": "hi"},
            "done": True, "done_reason": "stop",
            "prompt_eval_count": 3, "eval_count": 2, "total_duration": 50
        }
        fixture = {"response": {"status": 200, "body": fake_resp}}
        transport = MockTransport(fixture)
        provider = OllamaProvider(transport=transport, seed=None)
        req = ChatRequest(
            model="llama3.1:8b",
            messages=[ChatMessage(role="user", content="hi")],
            seed=None,
        )
        provider.chat(req)
        body = transport.last_request["body"]
        assert "seed" not in body["options"]

    def test_chat_omits_num_predict_when_none(self):
        from algoforge.llm.fixtures import MockTransport
        from algoforge.llm.ollama import OllamaProvider
        from algoforge.llm import ChatRequest, ChatMessage

        fake_resp = {
            "model": "llama3.1:8b",
            "message": {"role": "assistant", "content": "hi"},
            "done": True, "done_reason": "stop",
            "prompt_eval_count": 3, "eval_count": 2, "total_duration": 50
        }
        fixture = {"response": {"status": 200, "body": fake_resp}}
        transport = MockTransport(fixture)
        provider = OllamaProvider(transport=transport)
        req = ChatRequest(
            model="llama3.1:8b",
            messages=[ChatMessage(role="user", content="hi")],
            max_tokens=None,
            seed=42,
        )
        provider.chat(req)
        body = transport.last_request["body"]
        assert "num_predict" not in body["options"]
        assert body["options"]["seed"] == 42


class TestChatStreaming:
    def test_stream_chunks(self):
        provider, transport, fixture = make_provider("chat_streaming")
        from algoforge.llm import ChatRequest, ChatMessage
        req = ChatRequest(
            model="llama3.1:8b",
            messages=[
                ChatMessage(role="system", content="You are a trading assistant."),
                ChatMessage(role="user", content="What is RSI?"),
            ],
            temperature=0.2,
            max_tokens=256,
            seed=42,
        )
        chunks = list(provider.chat_stream(req))
        expected_body_chunks = fixture["response"]["body"]
        assert len(chunks) == len(expected_body_chunks)
        non_final = [c for c in chunks if not c.done]
        final = [c for c in chunks if c.done]
        assert len(final) == 1
        assert final[0].finish_reason == "stop"
        text = "".join(c.delta for c in non_final)
        assert len(text) > 0

    def test_stream_request_body(self):
        provider, transport, fixture = make_provider("chat_streaming")
        from algoforge.llm import ChatRequest, ChatMessage
        req = ChatRequest(
            model="llama3.1:8b",
            messages=[
                ChatMessage(role="system", content="You are a trading assistant."),
                ChatMessage(role="user", content="What is RSI?"),
            ],
            temperature=0.2,
            max_tokens=256,
            seed=42,
        )
        list(provider.chat_stream(req))
        body = transport.last_request["body"]
        assert body["stream"] is True
        assert body == fixture["request"]["body"]


class TestEmbed:
    def test_outgoing_request_body(self):
        provider, transport, fixture = make_provider("embed_basic")
        from algoforge.llm import EmbedRequest
        req = EmbedRequest(model="nomic-embed-text", input="EURUSD broke out")
        provider.embed(req)
        body = transport.last_request["body"]
        assert body == fixture["request"]["body"]

    def test_response_fields(self):
        provider, transport, fixture = make_provider("embed_basic")
        from algoforge.llm import EmbedRequest
        req = EmbedRequest(model="nomic-embed-text", input="EURUSD broke out")
        resp = provider.embed(req)
        assert resp.model == "nomic-embed-text"
        assert len(resp.embeddings) == 1
        assert resp.embeddings[0] == fixture["response"]["body"]["embedding"]

    def test_batched_embed(self):
        from algoforge.llm.fixtures import MockTransport
        from algoforge.llm.ollama import OllamaProvider
        from algoforge.llm import EmbedRequest

        call_count = 0
        class CountingTransport:
            def request(self, method, url, body, headers, timeout):
                nonlocal call_count
                call_count += 1
                resp = {"embedding": [0.1, 0.2, 0.3]}
                return 200, json.dumps(resp).encode("utf-8")

        provider = OllamaProvider(transport=CountingTransport())
        req = EmbedRequest(model="nomic-embed-text", input=["text1", "text2", "text3"])
        resp = provider.embed(req)
        assert len(resp.embeddings) == 3
        assert call_count == 3


class TestHealth:
    def test_health_ok(self):
        from algoforge.llm.fixtures import MockTransport
        from algoforge.llm.ollama import OllamaProvider

        # health does root GET then list_models; we need both to succeed
        root_resp = b"Ollama is running"
        tags_resp = json.dumps({
            "models": [
                {"name": "llama3.1:8b", "size": 4661211808, "modified_at": "2024-07-15T10:30:00Z"}
            ]
        }).encode("utf-8")

        call_n = [0]
        class MultiTransport:
            def request(self, method, url, body, headers, timeout):
                call_n[0] += 1
                if "/api/tags" in url:
                    return 200, tags_resp
                return 200, root_resp

        provider = OllamaProvider(transport=MultiTransport(), model="llama3.1:8b")
        status = provider.health()
        assert status.ok is True
        assert status.model_loaded is True
        assert status.model == "llama3.1:8b"
        assert status.error is None

    def test_health_down_503(self):
        from algoforge.llm.fixtures import MockTransport
        from algoforge.llm.ollama import OllamaProvider

        class ErrorTransport:
            def request(self, method, url, body, headers, timeout):
                return 503, b'{"error":"service unavailable"}'

        provider = OllamaProvider(transport=ErrorTransport())
        status = provider.health()
        assert status.ok is False
        assert status.error is not None

    def test_health_model_not_loaded(self):
        from algoforge.llm.ollama import OllamaProvider

        root_resp = b"Ollama is running"
        tags_resp = json.dumps({"models": [
            {"name": "other-model:7b", "size": 1000, "modified_at": "2024-01-01T00:00:00Z"}
        ]}).encode("utf-8")

        class MultiTransport:
            def request(self, method, url, body, headers, timeout):
                if "/api/tags" in url:
                    return 200, tags_resp
                return 200, root_resp

        provider = OllamaProvider(transport=MultiTransport(), model="llama3.1:8b")
        status = provider.health()
        assert status.ok is True
        assert status.model_loaded is False


class TestListModels:
    def test_outgoing_request(self):
        provider, transport, fixture = make_provider("list_models")
        provider.list_models()
        assert transport.last_request["method"] == "GET"

    def test_response_fields(self):
        provider, transport, fixture = make_provider("list_models")
        models = provider.list_models()
        expected = fixture["response"]["body"]["models"]
        assert len(models) == len(expected)
        for m, e in zip(models, expected):
            assert m.name == e["name"]
            assert m.size_bytes == e["size"]
            assert m.modified_at == e["modified_at"]


# ── use-case tests ─────────────────────────────────────────────────────────

def _use_case_provider(fixture_name: str):
    from algoforge.llm.fixtures import MockTransport
    from algoforge.llm.ollama import OllamaProvider

    fixture = load_fixture(fixture_name)
    transport = MockTransport(fixture)
    provider = OllamaProvider(
        host="http://localhost:11434",
        model="llama3.1:8b",
        transport=transport,
    )
    return provider, transport, fixture


class TestTradeRationale:
    def _make_long_input(self):
        from algoforge.llm import TradeRationaleInput
        return TradeRationaleInput(
            symbol="EURUSD", side="long", entry_price=1.0850, stop_loss=1.0800,
            take_profit=1.0950, indicators={"atr": 0.0014, "rsi": 68.2},
            patterns=["BullishFlag", "Hammer"], timeframe="H1",
        )

    def _make_short_input(self):
        from algoforge.llm import TradeRationaleInput
        return TradeRationaleInput(
            symbol="GBPJPY", side="short", entry_price=189.50, stop_loss=190.20,
            take_profit=187.80, indicators={"ema_fast": 189.45, "ema_slow": 189.80, "rsi": 31.5},
            patterns=["BearishEngulfing"], timeframe="M15",
        )

    def test_long_prompt_byte_identical(self):
        provider, transport, fixture = _use_case_provider("trade_rationale_long")
        from algoforge.llm import trade_rationale
        trade_rationale(provider, self._make_long_input())
        actual_content = transport.last_request["body"]["messages"][1]["content"]
        expected_content = fixture["request"]["body"]["messages"][1]["content"]
        assert actual_content == expected_content

    def test_long_output_structure(self):
        provider, transport, fixture = _use_case_provider("trade_rationale_long")
        from algoforge.llm import trade_rationale
        result = trade_rationale(provider, self._make_long_input())
        rb = fixture["response"]["body"]
        assert result.explanation == rb["message"]["content"]
        assert result.model == rb["model"]
        assert result.tokens == rb["eval_count"]

    def test_short_prompt_byte_identical(self):
        provider, transport, fixture = _use_case_provider("trade_rationale_short")
        from algoforge.llm import trade_rationale
        trade_rationale(provider, self._make_short_input())
        actual_content = transport.last_request["body"]["messages"][1]["content"]
        expected_content = fixture["request"]["body"]["messages"][1]["content"]
        assert actual_content == expected_content

    def test_short_output_structure(self):
        provider, transport, fixture = _use_case_provider("trade_rationale_short")
        from algoforge.llm import trade_rationale
        result = trade_rationale(provider, self._make_short_input())
        rb = fixture["response"]["body"]
        assert result.explanation == rb["message"]["content"]
        assert result.model == rb["model"]
        assert result.tokens == rb["eval_count"]

    def test_indicators_sorted_in_prompt(self):
        provider, transport, fixture = _use_case_provider("trade_rationale_long")
        from algoforge.llm import trade_rationale
        trade_rationale(provider, self._make_long_input())
        content = transport.last_request["body"]["messages"][1]["content"]
        assert '"atr":0.0014,"rsi":68.2' in content

    def test_request_options_include_seed_and_temp(self):
        provider, transport, fixture = _use_case_provider("trade_rationale_long")
        from algoforge.llm import trade_rationale
        trade_rationale(provider, self._make_long_input())
        opts = transport.last_request["body"]["options"]
        assert opts["seed"] == 42
        assert opts["temperature"] == 0.2


class TestBacktestCommentary:
    def _make_winning_input(self):
        from algoforge.llm import BacktestCommentaryInput
        return BacktestCommentaryInput(
            strategy_name="MomentumBreakout", period="2024-01-01 \u2192 2024-06-30",
            total_return=0.2341, sharpe=1.82, sortino=2.45,
            max_drawdown=-0.0812, win_rate=0.6200, trade_count=147,
            best_trade=0.0312, worst_trade=-0.0198,
        )

    def _make_losing_input(self):
        from algoforge.llm import BacktestCommentaryInput
        return BacktestCommentaryInput(
            strategy_name="MeanReversionEUR", period="2024-01-01 \u2192 2024-06-30",
            total_return=-0.1123, sharpe=-0.74, sortino=-0.98,
            max_drawdown=-0.2341, win_rate=0.4100, trade_count=89,
            best_trade=0.0201, worst_trade=-0.0456,
        )

    def test_winning_prompt_byte_identical(self):
        provider, transport, fixture = _use_case_provider("backtest_commentary_winning")
        from algoforge.llm import backtest_commentary
        backtest_commentary(provider, self._make_winning_input())
        actual = transport.last_request["body"]["messages"][1]["content"]
        expected = fixture["request"]["body"]["messages"][1]["content"]
        assert actual == expected

    def test_winning_output_structure(self):
        provider, transport, fixture = _use_case_provider("backtest_commentary_winning")
        from algoforge.llm import backtest_commentary
        result = backtest_commentary(provider, self._make_winning_input())
        rb = fixture["response"]["body"]
        assert result.commentary == rb["message"]["content"]
        assert result.model == rb["model"]
        assert result.tokens == rb["eval_count"]

    def test_losing_prompt_byte_identical(self):
        provider, transport, fixture = _use_case_provider("backtest_commentary_losing")
        from algoforge.llm import backtest_commentary
        backtest_commentary(provider, self._make_losing_input())
        actual = transport.last_request["body"]["messages"][1]["content"]
        expected = fixture["request"]["body"]["messages"][1]["content"]
        assert actual == expected

    def test_losing_output_structure(self):
        provider, transport, fixture = _use_case_provider("backtest_commentary_losing")
        from algoforge.llm import backtest_commentary
        result = backtest_commentary(provider, self._make_losing_input())
        rb = fixture["response"]["body"]
        assert result.commentary == rb["message"]["content"]
        assert result.model == rb["model"]
        assert result.tokens == rb["eval_count"]

    def test_floats_formatted_4dp(self):
        provider, transport, fixture = _use_case_provider("backtest_commentary_winning")
        from algoforge.llm import backtest_commentary
        backtest_commentary(provider, self._make_winning_input())
        content = transport.last_request["body"]["messages"][1]["content"]
        assert "total_return=0.2341" in content
        assert "sharpe=1.8200" in content
        assert "win_rate=0.6200" in content


class TestNewsSentiment:
    def test_bullish_prompt_byte_identical(self):
        provider, transport, fixture = _use_case_provider("news_sentiment_bullish")
        from algoforge.llm import news_sentiment
        news_sentiment(provider, headline="Fed signals rate cuts ahead; markets rally strongly")
        actual = transport.last_request["body"]["messages"][1]["content"]
        expected = fixture["request"]["body"]["messages"][1]["content"]
        assert actual == expected

    def test_bullish_output_structure(self):
        provider, transport, fixture = _use_case_provider("news_sentiment_bullish")
        from algoforge.llm import news_sentiment
        result = news_sentiment(provider, headline="Fed signals rate cuts ahead; markets rally strongly")
        assert result.sentiment == "bullish"
        assert isinstance(result.score, float)
        assert 0.0 <= result.confidence <= 1.0
        assert isinstance(result.reason, str)

    def test_bearish_prompt_byte_identical(self):
        provider, transport, fixture = _use_case_provider("news_sentiment_bearish")
        from algoforge.llm import news_sentiment
        news_sentiment(provider, headline="Recession fears mount as unemployment hits 5-year high")
        actual = transport.last_request["body"]["messages"][1]["content"]
        expected = fixture["request"]["body"]["messages"][1]["content"]
        assert actual == expected

    def test_bearish_output_structure(self):
        provider, transport, fixture = _use_case_provider("news_sentiment_bearish")
        from algoforge.llm import news_sentiment
        result = news_sentiment(provider, headline="Recession fears mount as unemployment hits 5-year high")
        assert result.sentiment == "bearish"
        assert result.score < 0

    def test_neutral_prompt_byte_identical(self):
        provider, transport, fixture = _use_case_provider("news_sentiment_neutral")
        from algoforge.llm import news_sentiment
        news_sentiment(provider, headline="Central bank holds rates steady, cites mixed economic data")
        actual = transport.last_request["body"]["messages"][1]["content"]
        expected = fixture["request"]["body"]["messages"][1]["content"]
        assert actual == expected

    def test_neutral_output_structure(self):
        provider, transport, fixture = _use_case_provider("news_sentiment_neutral")
        from algoforge.llm import news_sentiment
        result = news_sentiment(provider, headline="Central bank holds rates steady, cites mixed economic data")
        assert result.sentiment == "neutral"

    def test_with_body(self):
        from algoforge.llm.fixtures import MockTransport
        from algoforge.llm.ollama import OllamaProvider
        from algoforge.llm import news_sentiment
        from algoforge.llm.prompts import NEWS_SENTIMENT_USER

        fake_resp = {
            "model": "llama3.1:8b",
            "message": {"role": "assistant",
                         "content": '{"sentiment":"bullish","score":0.7,"confidence":0.9,"reason":"test"}'},
            "done": True, "done_reason": "stop",
            "prompt_eval_count": 10, "eval_count": 20, "total_duration": 100
        }
        fixture = {"response": {"status": 200, "body": fake_resp}}
        transport = MockTransport(fixture)
        provider = OllamaProvider(transport=transport)
        result = news_sentiment(provider, headline="Headline here", body="Some body text.")
        content = transport.last_request["body"]["messages"][1]["content"]
        assert "BODY: Some body text." in content
        assert result.sentiment == "bullish"


class TestStrategyDescribe:
    def _make_breakout_input(self):
        from algoforge.llm import StrategyDescribeInput
        return StrategyDescribeInput(
            name="BreakoutMomentum", timeframe="H1",
            indicators_used=["EMA20", "EMA50", "ATR14", "RSI14"],
            entry_rules=["price crosses above EMA20", "RSI above 55", "ATR above 0.001"],
            exit_rules=["RSI above 75", "price below EMA20"],
            risk_rules=["max 2% per trade", "stop loss at ATR*1.5", "max 3 open positions"],
        )

    def _make_meanrev_input(self):
        from algoforge.llm import StrategyDescribeInput
        return StrategyDescribeInput(
            name="MeanReversionBand", timeframe="M30",
            indicators_used=["BollingerBands20", "RSI14", "VWAP"],
            entry_rules=["price touches lower band", "RSI below 35", "price below VWAP"],
            exit_rules=["price reverts to midband", "RSI above 50"],
            risk_rules=["max 1.5% per trade", "stop below lower band", "daily loss limit 3%"],
        )

    def test_breakout_prompt_byte_identical(self):
        provider, transport, fixture = _use_case_provider("strategy_describe_breakout")
        from algoforge.llm import strategy_describe
        strategy_describe(provider, self._make_breakout_input())
        actual = transport.last_request["body"]["messages"][1]["content"]
        expected = fixture["request"]["body"]["messages"][1]["content"]
        assert actual == expected

    def test_breakout_output_structure(self):
        provider, transport, fixture = _use_case_provider("strategy_describe_breakout")
        from algoforge.llm import strategy_describe
        result = strategy_describe(provider, self._make_breakout_input())
        rb = fixture["response"]["body"]
        assert result.description == rb["message"]["content"]
        assert result.model == rb["model"]
        assert result.tokens == rb["eval_count"]

    def test_meanrev_prompt_byte_identical(self):
        provider, transport, fixture = _use_case_provider("strategy_describe_meanrev")
        from algoforge.llm import strategy_describe
        strategy_describe(provider, self._make_meanrev_input())
        actual = transport.last_request["body"]["messages"][1]["content"]
        expected = fixture["request"]["body"]["messages"][1]["content"]
        assert actual == expected

    def test_meanrev_output_structure(self):
        provider, transport, fixture = _use_case_provider("strategy_describe_meanrev")
        from algoforge.llm import strategy_describe
        result = strategy_describe(provider, self._make_meanrev_input())
        rb = fixture["response"]["body"]
        assert result.description == rb["message"]["content"]
        assert result.model == rb["model"]
        assert result.tokens == rb["eval_count"]


# ── error tests §10 ────────────────────────────────────────────────────────

class TestErrors:
    def test_http_503_raises_llm_error(self):
        from algoforge.llm.ollama import OllamaProvider
        from algoforge.llm.provider import LLMError
        from algoforge.llm import ChatRequest, ChatMessage

        class Transport503:
            def request(self, method, url, body, headers, timeout):
                return 503, b'{"error":"service unavailable"}'

        provider = OllamaProvider(transport=Transport503())
        req = ChatRequest(
            model="llama3.1:8b",
            messages=[ChatMessage(role="user", content="hi")],
        )
        with pytest.raises(LLMError) as exc_info:
            provider.chat(req)
        assert exc_info.value.kind == "http"
        assert exc_info.value.status == 503

    def test_http_error_has_status(self):
        from algoforge.llm.ollama import OllamaProvider
        from algoforge.llm.provider import LLMError
        from algoforge.llm import CompletionRequest

        class Transport404:
            def request(self, method, url, body, headers, timeout):
                return 404, b'{"error":"not found"}'

        provider = OllamaProvider(transport=Transport404())
        req = CompletionRequest(model="llama3.1:8b", prompt="test")
        with pytest.raises(LLMError) as exc_info:
            provider.complete(req)
        assert exc_info.value.kind == "http"
        assert exc_info.value.status == 404

    def test_connection_refused_raises_unreachable(self):
        from algoforge.llm.ollama import OllamaProvider
        from algoforge.llm.provider import LLMError
        from algoforge.llm import ChatRequest, ChatMessage

        class TransportUnreachable:
            def request(self, method, url, body, headers, timeout):
                raise LLMError("unreachable", "Connection refused")

        provider = OllamaProvider(transport=TransportUnreachable())
        req = ChatRequest(
            model="llama3.1:8b",
            messages=[ChatMessage(role="user", content="hi")],
        )
        with pytest.raises(LLMError) as exc_info:
            provider.chat(req)
        assert exc_info.value.kind == "unreachable"

    def test_timeout_raises_timeout(self):
        from algoforge.llm.ollama import OllamaProvider
        from algoforge.llm.provider import LLMError
        from algoforge.llm import ChatRequest, ChatMessage

        class TransportTimeout:
            def request(self, method, url, body, headers, timeout):
                raise LLMError("timeout", "Request timed out")

        provider = OllamaProvider(transport=TransportTimeout())
        req = ChatRequest(
            model="llama3.1:8b",
            messages=[ChatMessage(role="user", content="hi")],
        )
        with pytest.raises(LLMError) as exc_info:
            provider.chat(req)
        assert exc_info.value.kind == "timeout"

    def test_malformed_json_sentiment_raises_decode(self):
        from algoforge.llm.fixtures import MockTransport
        from algoforge.llm.ollama import OllamaProvider
        from algoforge.llm.provider import LLMError
        from algoforge.llm import news_sentiment

        fake_resp = {
            "model": "llama3.1:8b",
            "message": {"role": "assistant", "content": "this is not valid json at all"},
            "done": True, "done_reason": "stop",
            "prompt_eval_count": 5, "eval_count": 8, "total_duration": 100
        }
        fixture = {"response": {"status": 200, "body": fake_resp}}
        transport = MockTransport(fixture)
        provider = OllamaProvider(transport=transport)
        with pytest.raises(LLMError) as exc_info:
            news_sentiment(provider, headline="test headline")
        assert exc_info.value.kind == "decode"

    def test_malformed_json_in_complete_response(self):
        from algoforge.llm.ollama import OllamaProvider
        from algoforge.llm.provider import LLMError
        from algoforge.llm import CompletionRequest

        class TransportBadJson:
            def request(self, method, url, body, headers, timeout):
                return 200, b"not valid json {"

        provider = OllamaProvider(transport=TransportBadJson())
        req = CompletionRequest(model="llama3.1:8b", prompt="test")
        with pytest.raises(LLMError) as exc_info:
            provider.complete(req)
        assert exc_info.value.kind == "decode"

    def test_llm_error_attributes(self):
        from algoforge.llm.provider import LLMError
        err = LLMError(kind="http", message="HTTP 503", status=503)
        assert err.kind == "http"
        assert err.message == "HTTP 503"
        assert err.status == 503
        assert str(err) == "HTTP 503"

    def test_llm_error_no_status(self):
        from algoforge.llm.provider import LLMError
        err = LLMError(kind="unreachable", message="Connection refused")
        assert err.status is None

    def test_embed_503(self):
        from algoforge.llm.ollama import OllamaProvider
        from algoforge.llm.provider import LLMError
        from algoforge.llm import EmbedRequest

        class Transport503:
            def request(self, method, url, body, headers, timeout):
                return 503, b"{}"

        provider = OllamaProvider(transport=Transport503())
        with pytest.raises(LLMError) as exc_info:
            provider.embed(EmbedRequest(model="nomic-embed-text", input="test"))
        assert exc_info.value.kind == "http"

    def test_list_models_503(self):
        from algoforge.llm.ollama import OllamaProvider
        from algoforge.llm.provider import LLMError

        class Transport503:
            def request(self, method, url, body, headers, timeout):
                return 503, b"{}"

        provider = OllamaProvider(transport=Transport503())
        with pytest.raises(LLMError):
            provider.list_models()


# ── JSON extraction tests ──────────────────────────────────────────────────

class TestJsonExtraction:
    def test_extracts_clean_json(self):
        from algoforge.llm.use_cases import _extract_json
        text = '{"sentiment":"bullish","score":0.8,"confidence":0.9,"reason":"test"}'
        result = _extract_json(text)
        assert result["sentiment"] == "bullish"
        assert result["score"] == 0.8

    def test_extracts_json_with_prose(self):
        from algoforge.llm.use_cases import _extract_json
        text = 'Here is my analysis: {"sentiment":"bearish","score":-0.7,"confidence":0.8,"reason":"bad news"} Hope that helps.'
        result = _extract_json(text)
        assert result["sentiment"] == "bearish"

    def test_handles_nested_json(self):
        from algoforge.llm.use_cases import _extract_json
        text = '{"sentiment":"neutral","score":0.0,"confidence":0.5,"reason":"mixed signals with {nested} content"}'
        result = _extract_json(text)
        assert result["sentiment"] == "neutral"

    def test_raises_decode_on_no_json(self):
        from algoforge.llm.use_cases import _extract_json
        from algoforge.llm.provider import LLMError
        with pytest.raises(LLMError) as exc_info:
            _extract_json("no JSON here at all")
        assert exc_info.value.kind == "decode"

    def test_handles_escaped_braces_in_string(self):
        from algoforge.llm.use_cases import _extract_json
        text = '{"sentiment":"bullish","score":0.5,"confidence":0.7,"reason":"price broke \\\"support\\\""}'
        result = _extract_json(text)
        assert result["sentiment"] == "bullish"


# ── fixture loader tests ──────────────────────────────────────────────────

class TestFixtureLoader:
    def test_load_fixture_returns_dict(self):
        from algoforge.llm.fixtures import load_fixture
        f = load_fixture("complete_basic")
        assert isinstance(f, dict)

    def test_load_fixture_not_found(self):
        from algoforge.llm.fixtures import load_fixture
        with pytest.raises(FileNotFoundError):
            load_fixture("nonexistent_fixture_xyz")

    def test_mock_transport_stores_last_request(self):
        from algoforge.llm.fixtures import MockTransport
        fixture = load_fixture("complete_basic")
        transport = MockTransport(fixture)
        assert transport.last_request is None
        transport.request("POST", "http://localhost:11434/api/generate",
                          b'{"model":"llama3.1:8b"}', {}, 60.0)
        assert transport.last_request is not None
        assert transport.last_request["method"] == "POST"

    def test_mock_transport_returns_fixture_status(self):
        from algoforge.llm.fixtures import MockTransport
        fixture = load_fixture("health_down")
        transport = MockTransport(fixture)
        status, body = transport.request("GET", "http://localhost:11434/", None, {}, 60.0)
        assert status == 503

    def test_mock_transport_streaming_returns_ndjson(self):
        from algoforge.llm.fixtures import MockTransport
        fixture = load_fixture("chat_streaming")
        transport = MockTransport(fixture)
        status, raw = transport.request("POST", "http://localhost:11434/api/chat",
                                        b"{}", {}, 60.0)
        assert status == 200
        lines = [l for l in raw.splitlines() if l.strip()]
        assert len(lines) == len(fixture["response"]["body"])


# ── from_env tests ─────────────────────────────────────────────────────────

class TestFromEnv:
    def test_from_env_defaults(self, monkeypatch):
        for var in ["AF_LLM_HOST", "AF_LLM_MODEL", "AF_LLM_EMBED_MODEL",
                    "AF_LLM_TIMEOUT", "AF_LLM_SEED", "AF_LLM_TEMPERATURE", "AF_LLM_MAX_TOKENS"]:
            monkeypatch.delenv(var, raising=False)
        from algoforge.llm import OllamaProvider
        p = OllamaProvider.from_env()
        assert p._host == "http://localhost:11434"
        assert p._model == "llama3.1:8b"
        assert p._embed_model == "nomic-embed-text"
        assert p._timeout_s == 60.0
        assert p._seed == 42
        assert p._temperature == 0.2
        assert p._max_tokens == 512

    def test_from_env_custom(self, monkeypatch):
        monkeypatch.setenv("AF_LLM_HOST", "http://my-server:11434")
        monkeypatch.setenv("AF_LLM_MODEL", "mistral:7b")
        monkeypatch.setenv("AF_LLM_TIMEOUT", "120")
        monkeypatch.setenv("AF_LLM_SEED", "99")
        monkeypatch.setenv("AF_LLM_TEMPERATURE", "0.5")
        monkeypatch.setenv("AF_LLM_MAX_TOKENS", "1024")
        from algoforge.llm import OllamaProvider
        p = OllamaProvider.from_env()
        assert p._host == "http://my-server:11434"
        assert p._model == "mistral:7b"
        assert p._timeout_s == 120.0
        assert p._seed == 99
        assert p._temperature == 0.5
        assert p._max_tokens == 1024


# ── type/dataclass tests ───────────────────────────────────────────────────

class TestTypes:
    def test_chat_message_frozen(self):
        from algoforge.llm import ChatMessage
        msg = ChatMessage(role="user", content="hi")
        with pytest.raises((AttributeError, TypeError)):
            msg.role = "assistant"  # type: ignore

    def test_completion_request_defaults(self):
        from algoforge.llm import CompletionRequest
        req = CompletionRequest(model="llama3.1:8b", prompt="test")
        assert req.temperature == 0.2
        assert req.max_tokens is None
        assert req.seed is None
        assert req.timeout_s == 60.0

    def test_chat_request_defaults(self):
        from algoforge.llm import ChatRequest, ChatMessage
        req = ChatRequest(model="llama3.1:8b", messages=[ChatMessage("user", "hi")])
        assert req.temperature == 0.2
        assert req.max_tokens is None
        assert req.seed is None

    def test_health_status_fields(self):
        from algoforge.llm import HealthStatus
        h = HealthStatus(ok=True, model_loaded=True, model="llama3.1:8b", error=None)
        assert h.ok is True
        assert h.model_loaded is True
        assert h.model == "llama3.1:8b"
        assert h.error is None

    def test_model_info_fields(self):
        from algoforge.llm import ModelInfo
        m = ModelInfo(name="llama3.1:8b", size_bytes=4661211808, modified_at="2024-07-15T10:30:00Z")
        assert m.name == "llama3.1:8b"
        assert m.size_bytes == 4661211808

    def test_embed_request_accepts_list(self):
        from algoforge.llm import EmbedRequest
        req = EmbedRequest(model="nomic-embed-text", input=["a", "b"])
        assert isinstance(req.input, list)

    def test_chat_chunk_fields(self):
        from algoforge.llm import ChatChunk
        chunk = ChatChunk(delta="hello", done=False, finish_reason=None)
        assert chunk.delta == "hello"
        assert chunk.done is False
        assert chunk.finish_reason is None


# ── CLI tests ──────────────────────────────────────────────────────────────

class TestCLI:
    def test_health_subcommand_exists(self):
        from algoforge.llm.__main__ import main
        # verifies module is importable and has main()
        assert callable(main)

    def test_unknown_subcommand_exits(self):
        from algoforge.llm.__main__ import main
        with pytest.raises(SystemExit):
            main(["unknown_subcommand_xyz"])

    def test_no_args_exits(self):
        from algoforge.llm.__main__ import main
        with pytest.raises(SystemExit):
            main([])
