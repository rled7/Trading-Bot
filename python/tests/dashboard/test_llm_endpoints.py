"""Tests for the LLM dashboard endpoints (/api/llm/*).

SSE stream tests iterate `response.body_iterator` directly — do NOT
route them through TestClient / ASGITransport, which deadlocks on
infinite/long generators.  Auth-rejection tests can still use TestClient
because the 401 is raised synchronously before the stream starts.

Run with:
    cd /Users/user/Trading-Bot/python && python -m pytest tests/dashboard/test_llm_endpoints.py -v
"""
from __future__ import annotations

import asyncio
import json
from typing import Any, Iterator

import pytest
from fastapi.testclient import TestClient

from algoforge.dashboard import make_app
from algoforge.llm.provider import LLMError, LLMProvider
from algoforge.llm.types import (
    ChatChunk,
    ChatRequest,
    ChatResponse,
    ChatMessage,
    CompletionRequest,
    CompletionResponse,
    EmbedRequest,
    EmbedResponse,
    HealthStatus,
    ModelInfo,
)
from algoforge.paper_broker import PaperBroker


# ---------------------------------------------------------------------------
# Fake LLM provider
# ---------------------------------------------------------------------------

class FakeLLMProvider(LLMProvider):
    """In-memory LLM provider for tests.  No network calls.

    Attributes
    ----------
    health_result : HealthStatus | LLMError
        Return value (or exception) for `health()`.
    models_result : list[ModelInfo]
        Return value for `list_models()`.
    chat_result : ChatResponse | LLMError | Exception
        Return value (or exception to raise) for `chat()`.
    stream_chunks : list[ChatChunk]
        Chunks yielded by `chat_stream()`.
    """

    def __init__(
        self,
        *,
        health_result: HealthStatus | LLMError | Exception | None = None,
        models_result: list[ModelInfo] | None = None,
        chat_result: ChatResponse | LLMError | Exception | None = None,
        stream_chunks: list[ChatChunk] | None = None,
    ) -> None:
        self._health_result = health_result or HealthStatus(
            ok=True, model_loaded=True, model="llama3.1:8b", error=None
        )
        self._models_result = models_result or [
            ModelInfo(name="llama3.1:8b", size_bytes=4_000_000_000, modified_at="2024-01-01T00:00:00Z")
        ]
        self._chat_result = chat_result or ChatResponse(
            message=ChatMessage(role="assistant", content="Hello!"),
            model="llama3.1:8b",
            prompt_tokens=10,
            completion_tokens=5,
            total_duration_ns=100_000_000,
            finish_reason="stop",
        )
        self._stream_chunks = stream_chunks or [
            ChatChunk(delta="Hello", done=False, finish_reason=None),
            ChatChunk(delta=" world", done=False, finish_reason=None),
            ChatChunk(delta="", done=True, finish_reason="stop"),
        ]
        # Track calls for assertions
        self.chat_calls: list[ChatRequest] = []
        self.stream_calls: list[ChatRequest] = []
        # Expose host attribute (OllamaProvider has _host)
        self._host = "http://localhost:11434"

    def health(self) -> HealthStatus:
        if isinstance(self._health_result, (LLMError, Exception)):
            raise self._health_result
        return self._health_result

    def list_models(self) -> list[ModelInfo]:
        return list(self._models_result)

    def chat(self, req: ChatRequest) -> ChatResponse:
        self.chat_calls.append(req)
        if isinstance(self._chat_result, (LLMError, Exception)):
            raise self._chat_result
        return self._chat_result

    def chat_stream(self, req: ChatRequest) -> Iterator[ChatChunk]:
        self.stream_calls.append(req)
        yield from self._stream_chunks

    def complete(self, req: CompletionRequest) -> CompletionResponse:
        raise NotImplementedError

    def embed(self, req: EmbedRequest) -> EmbedResponse:
        raise NotImplementedError


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def broker() -> PaperBroker:
    b = PaperBroker(balance=10_000.0)
    b.connect()
    yield b
    b.disconnect()


@pytest.fixture
def fake_llm() -> FakeLLMProvider:
    return FakeLLMProvider()


@pytest.fixture
def app_with_llm(broker: PaperBroker, fake_llm: FakeLLMProvider):
    return make_app(broker, llm=fake_llm)


@pytest.fixture
def client_with_llm(app_with_llm):
    with TestClient(app_with_llm, raise_server_exceptions=True) as c:
        yield c


@pytest.fixture
def app_no_llm(broker: PaperBroker):
    return make_app(broker, llm=None)


@pytest.fixture
def client_no_llm(app_no_llm):
    with TestClient(app_no_llm, raise_server_exceptions=False) as c:
        yield c


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _get_stream_endpoint(app):
    """Return the async handler for /api/llm/chat/stream."""
    for route in app.routes:
        if getattr(route, "path", None) == "/api/llm/chat/stream":
            return route.endpoint
    raise AssertionError("/api/llm/chat/stream route not found")


async def _collect_sse(endpoint, body: dict, max_chunks: int = 50, timeout: float = 5.0) -> list[str]:
    """Call the SSE endpoint and collect raw data lines."""
    from pydantic import BaseModel as _BM

    # Build a pydantic-like body model
    from algoforge.dashboard.server import _ChatRequestBody

    body_model = _ChatRequestBody(**body)
    resp = await endpoint(body=body_model)

    chunks: list[str] = []

    async def _consume():
        async for chunk in resp.body_iterator:
            if isinstance(chunk, bytes):
                chunk = chunk.decode("utf-8")
            chunks.append(chunk)
            if len(chunks) >= max_chunks:
                break

    try:
        await asyncio.wait_for(_consume(), timeout=timeout)
    except asyncio.TimeoutError:
        pass
    try:
        await resp.body_iterator.aclose()
    except Exception:
        pass
    return chunks


# ---------------------------------------------------------------------------
# Tests: /api/llm/health
# ---------------------------------------------------------------------------

class TestLLMHealth:

    def test_health_ok(self, broker: PaperBroker) -> None:
        llm = FakeLLMProvider(
            health_result=HealthStatus(ok=True, model_loaded=True, model="llama3.1:8b", error=None)
        )
        app = make_app(broker, llm=llm)
        with TestClient(app) as c:
            resp = c.get("/api/llm/health")
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"
        assert "host" in data
        assert data["model"] == "llama3.1:8b"

    def test_health_down_when_provider_raises(self, broker: PaperBroker) -> None:
        llm = FakeLLMProvider(
            health_result=LLMError("unreachable", "connection refused")
        )
        app = make_app(broker, llm=llm)
        with TestClient(app, raise_server_exceptions=False) as c:
            resp = c.get("/api/llm/health")
        assert resp.status_code == 503
        data = resp.json()
        # FastAPI wraps HTTPException detail under "detail"
        detail = data.get("detail", data)
        assert detail.get("error") == "down"

    def test_health_down_when_status_not_ok(self, broker: PaperBroker) -> None:
        llm = FakeLLMProvider(
            health_result=HealthStatus(ok=False, model_loaded=False, model="llama3.1:8b", error="timeout")
        )
        app = make_app(broker, llm=llm)
        with TestClient(app, raise_server_exceptions=False) as c:
            resp = c.get("/api/llm/health")
        assert resp.status_code == 503
        data = resp.json()
        detail = data.get("detail", data)
        assert detail.get("error") == "down"


# ---------------------------------------------------------------------------
# Tests: /api/llm/models
# ---------------------------------------------------------------------------

class TestLLMModels:

    def test_models_returns_list(self, client_with_llm: TestClient) -> None:
        resp = client_with_llm.get("/api/llm/models")
        assert resp.status_code == 200
        data = resp.json()
        assert "models" in data
        assert isinstance(data["models"], list)
        assert "llama3.1:8b" in data["models"]


# ---------------------------------------------------------------------------
# Tests: /api/llm/chat
# ---------------------------------------------------------------------------

class TestLLMChat:

    def test_chat_basic(self, broker: PaperBroker) -> None:
        llm = FakeLLMProvider(
            chat_result=ChatResponse(
                message=ChatMessage(role="assistant", content="ATR is a volatility measure."),
                model="llama3.1:8b",
                prompt_tokens=12,
                completion_tokens=8,
                total_duration_ns=500_000_000,
                finish_reason="stop",
            )
        )
        app = make_app(broker, llm=llm)
        with TestClient(app) as c:
            resp = c.post(
                "/api/llm/chat",
                json={"messages": [{"role": "user", "content": "Explain ATR."}]},
            )
        assert resp.status_code == 200
        data = resp.json()
        assert data["content"] == "ATR is a volatility measure."
        assert data["model"] == "llama3.1:8b"
        assert data["tokens"] == 8

    def test_chat_llm_error_timeout(self, broker: PaperBroker) -> None:
        llm = FakeLLMProvider(chat_result=LLMError("timeout", "request timed out"))
        app = make_app(broker, llm=llm)
        with TestClient(app, raise_server_exceptions=False) as c:
            resp = c.post(
                "/api/llm/chat",
                json={"messages": [{"role": "user", "content": "hi"}]},
            )
        assert resp.status_code == 504

    def test_chat_llm_error_unreachable(self, broker: PaperBroker) -> None:
        llm = FakeLLMProvider(chat_result=LLMError("unreachable", "connection refused"))
        app = make_app(broker, llm=llm)
        with TestClient(app, raise_server_exceptions=False) as c:
            resp = c.post(
                "/api/llm/chat",
                json={"messages": [{"role": "user", "content": "hi"}]},
            )
        assert resp.status_code == 502

    def test_chat_llm_error_decode(self, broker: PaperBroker) -> None:
        llm = FakeLLMProvider(chat_result=LLMError("decode", "JSON decode error"))
        app = make_app(broker, llm=llm)
        with TestClient(app, raise_server_exceptions=False) as c:
            resp = c.post(
                "/api/llm/chat",
                json={"messages": [{"role": "user", "content": "hi"}]},
            )
        assert resp.status_code == 500

    def test_chat_validation_empty_messages(self, client_with_llm: TestClient) -> None:
        resp = client_with_llm.post(
            "/api/llm/chat",
            json={"messages": []},
        )
        # Either 400 (our check) or 422 (FastAPI schema validation) — must be 4xx
        assert 400 <= resp.status_code < 500


# ---------------------------------------------------------------------------
# Tests: /api/llm/chat/stream (SSE)
# ---------------------------------------------------------------------------

class TestLLMChatStream:

    def test_chat_stream_basic(self, broker: PaperBroker) -> None:
        """SSE: iterate body_iterator directly to avoid TestClient deadlock."""
        chunks = [
            ChatChunk(delta="Hello", done=False, finish_reason=None),
            ChatChunk(delta=" world", done=False, finish_reason=None),
            ChatChunk(delta="", done=True, finish_reason="stop"),
        ]
        llm = FakeLLMProvider(stream_chunks=chunks)
        app = make_app(broker, llm=llm)
        endpoint = _get_stream_endpoint(app)

        raw_chunks = asyncio.run(
            _collect_sse(
                endpoint,
                {"messages": [{"role": "user", "content": "hi"}]},
                max_chunks=20,
                timeout=5.0,
            )
        )

        joined = "".join(raw_chunks)
        # Must contain token data and the final [DONE]
        assert "Hello" in joined
        assert " world" in joined
        assert "[DONE]" in joined

    def test_stream_aborts_cleanly(self, broker: PaperBroker) -> None:
        """Start the generator and close it early; should not raise."""
        # Use many chunks to ensure we can stop early
        chunks = [ChatChunk(delta=f"tok{i}", done=False, finish_reason=None) for i in range(100)]
        chunks.append(ChatChunk(delta="", done=True, finish_reason="stop"))
        llm = FakeLLMProvider(stream_chunks=chunks)
        app = make_app(broker, llm=llm)
        endpoint = _get_stream_endpoint(app)

        async def _run():
            from algoforge.dashboard.server import _ChatRequestBody
            body = _ChatRequestBody(messages=[{"role": "user", "content": "hi"}])
            resp = await endpoint(body=body)
            collected: list[str] = []
            # Only read 2 chunks then close
            async for chunk in resp.body_iterator:
                if isinstance(chunk, bytes):
                    chunk = chunk.decode("utf-8")
                collected.append(chunk)
                if len(collected) >= 2:
                    break
            try:
                await resp.body_iterator.aclose()
            except Exception:
                pass
            return collected

        result = asyncio.run(_run())
        assert len(result) >= 1  # Got at least something


# ---------------------------------------------------------------------------
# Tests: llm_disabled (llm=None)
# ---------------------------------------------------------------------------

class TestLLMDisabled:

    def test_health_returns_503_when_disabled(self, client_no_llm: TestClient) -> None:
        resp = client_no_llm.get("/api/llm/health")
        assert resp.status_code == 503
        detail = resp.json().get("detail", resp.json())
        assert detail.get("error") == "llm_disabled"

    def test_models_returns_503_when_disabled(self, client_no_llm: TestClient) -> None:
        resp = client_no_llm.get("/api/llm/models")
        assert resp.status_code == 503
        detail = resp.json().get("detail", resp.json())
        assert detail.get("error") == "llm_disabled"

    def test_chat_returns_503_when_disabled(self, client_no_llm: TestClient) -> None:
        resp = client_no_llm.post(
            "/api/llm/chat",
            json={"messages": [{"role": "user", "content": "hi"}]},
        )
        assert resp.status_code == 503
        detail = resp.json().get("detail", resp.json())
        assert detail.get("error") == "llm_disabled"

    def test_chat_stream_returns_503_when_disabled(self, client_no_llm: TestClient) -> None:
        resp = client_no_llm.post(
            "/api/llm/chat/stream",
            json={"messages": [{"role": "user", "content": "hi"}]},
        )
        assert resp.status_code == 503
        detail = resp.json().get("detail", resp.json())
        assert detail.get("error") == "llm_disabled"
