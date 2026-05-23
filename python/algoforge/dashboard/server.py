"""FastAPI application factory for the AlgoForge dashboard.

Usage::

    from algoforge.dashboard import make_app, LogRingBuffer
    from algoforge.paper_broker import PaperBroker

    broker = PaperBroker()
    broker.connect()
    buf = LogRingBuffer()
    buf.install()
    app = make_app(broker, token="secret", log_buffer=buf)
"""
from __future__ import annotations

import asyncio
import dataclasses
import json
import time
from typing import Any, AsyncGenerator, Iterator

from fastapi import Depends, FastAPI, HTTPException, Query
from fastapi.responses import FileResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

try:
    from algoforge import __version__ as _VERSION
except ImportError:
    _VERSION = "0.1.0"

from algoforge.broker import IBroker
from algoforge.llm.provider import LLMError, LLMProvider
from algoforge.llm.types import ChatMessage, ChatRequest
from algoforge.logger import get_logger
from algoforge.types import Timeframe

from .log_buffer import LogRingBuffer

# Symbol list taken from PaperBroker._PAIRS (same order)
_PAPER_SYMBOLS: list[str] = [
    "EURUSD", "GBPUSD", "USDJPY", "XAUUSD", "EURJPY",
    "GBPJPY", "AUDUSD", "USDCAD", "USDCHF", "EURGBP",
]

_STATIC_DIR = __file__.rsplit("/", 1)[0] + "/static"


# ---------------------------------------------------------------------------
# Auth helpers
# ---------------------------------------------------------------------------

def _make_auth_dep(token: str):
    """Return a FastAPI dependency that validates Bearer token or ?token= param."""

    def _check(
        authorization: str | None = None,
        token_param: str | None = Query(default=None, alias="token"),
    ) -> None:
        # Header takes precedence
        bearer: str | None = None
        if authorization and authorization.lower().startswith("bearer "):
            bearer = authorization[7:].strip()
        supplied = bearer or token_param
        if supplied != token:
            raise HTTPException(status_code=401, detail="unauthorized")

    # FastAPI cannot auto-inject `authorization` from the raw header via a
    # plain function parameter — use Header() explicitly.
    from fastapi import Header

    def dependency(
        authorization: str | None = Header(default=None),
        token_param: str | None = Query(default=None, alias="token"),
    ) -> None:
        bearer: str | None = None
        if authorization and authorization.lower().startswith("bearer "):
            bearer = authorization[7:].strip()
        supplied = bearer or token_param
        if supplied != token:
            raise HTTPException(status_code=401, detail="unauthorized")

    return dependency


# ---------------------------------------------------------------------------
# Dataclass serialisation helpers
# ---------------------------------------------------------------------------

def _dc_to_dict(obj: Any) -> dict[str, Any]:
    """Convert a dataclass to a plain dict; leave non-dataclass fields intact."""
    return dataclasses.asdict(obj)


def _position_dict(pos: Any) -> dict[str, Any]:
    d = _dc_to_dict(pos)
    d["side"] = int(pos.side)
    return d


def _order_dict(order: Any) -> dict[str, Any]:
    d = _dc_to_dict(order)
    d["type"] = int(order.type)
    return d


# ---------------------------------------------------------------------------
# Factory
# ---------------------------------------------------------------------------

class _ChatRequestBody(BaseModel):
    messages: list[dict[str, str]]
    model: str | None = None
    temperature: float | None = None
    max_tokens: int | None = None
    seed: int | None = None


_LLM_DISABLED_RESP = {"error": "llm_disabled", "detail": "LLM not configured"}

_LLM_ERROR_STATUS: dict[str, int] = {
    "timeout": 504,
    "unreachable": 502,
    "http": 502,
    "decode": 500,
    "model_missing": 503,
}


def make_app(
    broker: IBroker,
    *,
    token: str | None = None,
    log_buffer: LogRingBuffer | None = None,
    llm: LLMProvider | None = None,
) -> FastAPI:
    """Create and return the dashboard FastAPI application.

    Parameters
    ----------
    broker:
        An already-connected (or soon-to-be-connected) broker instance.
    token:
        Bearer token for API auth.  ``None`` disables auth (a warning is
        logged at creation time).
    log_buffer:
        Optional :class:`LogRingBuffer` whose records are served by
        ``GET /api/logs``.  If ``None``, the logs endpoint always returns
        an empty list.
    llm:
        Optional :class:`LLMProvider` instance.  When ``None``, all
        ``/api/llm/*`` endpoints return 503 with ``{"error":"llm_disabled"}``.
    """
    log = get_logger("dashboard")
    _start_time = time.monotonic()

    if token is None:
        log.warning("Dashboard auth token not set — all /api/* endpoints are public")

    app = FastAPI(title="AlgoForge Dashboard", version=_VERSION)

    # ---- auth dependency (or no-op) ----------------------------------------
    if token is not None:
        _auth = Depends(_make_auth_dep(token))
        _auth_dep: list[Any] = [_auth]
    else:
        _auth_dep = []

    # ---- static files -------------------------------------------------------
    import os as _os
    if _os.path.isdir(_STATIC_DIR):
        app.mount("/static", StaticFiles(directory=_STATIC_DIR), name="static")

    # ---- root ---------------------------------------------------------------
    @app.get("/", include_in_schema=False)
    def index() -> FileResponse:
        return FileResponse(_STATIC_DIR + "/index.html")

    # ---- /api/health --------------------------------------------------------
    @app.get("/api/health", dependencies=_auth_dep)
    def health() -> dict[str, Any]:
        return {
            "broker_name": broker.broker_name(),
            "is_connected": broker.is_connected(),
            "version": _VERSION,
            "uptime_seconds": time.monotonic() - _start_time,
        }

    # ---- /api/account -------------------------------------------------------
    @app.get("/api/account", dependencies=_auth_dep)
    def account() -> dict[str, Any]:
        return _dc_to_dict(broker.get_account())

    # ---- /api/positions -----------------------------------------------------
    @app.get("/api/positions", dependencies=_auth_dep)
    def positions() -> list[dict[str, Any]]:
        return [_position_dict(p) for p in broker.get_positions()]

    # ---- /api/orders --------------------------------------------------------
    @app.get("/api/orders", dependencies=_auth_dep)
    def orders() -> list[dict[str, Any]]:
        return [_order_dict(o) for o in broker.get_orders()]

    # ---- /api/symbols -------------------------------------------------------
    @app.get("/api/symbols", dependencies=_auth_dep)
    def symbols() -> list[str]:
        return _PAPER_SYMBOLS

    # ---- /api/bars/{symbol} -------------------------------------------------
    @app.get("/api/bars/{symbol}", dependencies=_auth_dep)
    def bars(
        symbol: str,
        tf: str = Query(default="M5"),
        count: int = Query(default=200),
    ) -> list[dict[str, Any]]:
        # Validate timeframe
        try:
            tf_enum = Timeframe[tf]
        except KeyError:
            valid = [m.name for m in Timeframe]
            raise HTTPException(
                status_code=422,
                detail=f"Invalid tf '{tf}'. Must be one of: {valid}",
            )
        # Clamp count
        count = max(1, min(count, 5000))
        bar_list = broker.get_bars(symbol, int(tf_enum), count)
        return [_dc_to_dict(b) for b in bar_list]

    # ---- /api/logs ----------------------------------------------------------
    @app.get("/api/logs", dependencies=_auth_dep)
    def logs(n: int = Query(default=50)) -> list[dict[str, Any]]:
        n = max(1, min(n, 500))
        if log_buffer is None:
            return []
        return log_buffer.tail(n)

    # ---- /api/stream/ticks --------------------------------------------------
    @app.get("/api/stream/ticks")
    async def stream_ticks(
        interval: float = Query(default=1.0),
        symbols: str = Query(default=""),
        token_param: str | None = Query(default=None, alias="token"),
        authorization: str | None = None,
    ) -> StreamingResponse:
        # Auth check (token via header or query param)
        if token is not None:
            bearer: str | None = None
            if authorization and authorization.lower().startswith("bearer "):
                bearer = authorization[7:].strip()
            supplied = bearer or token_param
            if supplied != token:
                raise HTTPException(status_code=401, detail="unauthorized")

        # Resolve symbol list
        if symbols.strip():
            sym_list = [s.strip() for s in symbols.split(",") if s.strip()]
        else:
            sym_list = list(_PAPER_SYMBOLS)

        # Clamp interval
        interval = max(0.1, min(interval, 10.0))

        async def _generator() -> AsyncGenerator[str, None]:
            try:
                while True:
                    yield ": keepalive\n\n"
                    for sym in sym_list:
                        try:
                            tick = broker.get_tick(sym)
                            payload = json.dumps(
                                {
                                    "symbol": tick.symbol,
                                    "bid": tick.bid,
                                    "ask": tick.ask,
                                    "time": tick.timestamp,
                                }
                            )
                            yield f"data: {payload}\n\n"
                        except Exception as exc:  # noqa: BLE001
                            log.warning(f"stream_ticks error for {sym}: {exc}")
                    await asyncio.sleep(interval)
            except asyncio.CancelledError:
                return

        return StreamingResponse(
            _generator(),
            media_type="text/event-stream",
            headers={
                "Cache-Control": "no-cache",
                "X-Accel-Buffering": "no",
            },
        )

    # ---- /api/llm/health ----------------------------------------------------
    @app.get("/api/llm/health", dependencies=_auth_dep)
    def llm_health() -> dict[str, Any]:
        if llm is None:
            raise HTTPException(status_code=503, detail=_LLM_DISABLED_RESP)
        try:
            status = llm.health()
        except LLMError as exc:
            raise HTTPException(
                status_code=503,
                detail={"error": "down", "detail": exc.message},
            )
        except Exception as exc:
            raise HTTPException(
                status_code=503,
                detail={"error": "down", "detail": str(exc)},
            )
        if not status.ok:
            raise HTTPException(
                status_code=503,
                detail={"error": "down", "detail": status.error or "unhealthy"},
            )
        return {
            "status": "ok",
            "host": getattr(llm, "_host", None),
            "model": status.model,
            "model_loaded": status.model_loaded,
        }

    # ---- /api/llm/models ----------------------------------------------------
    @app.get("/api/llm/models", dependencies=_auth_dep)
    def llm_models() -> dict[str, Any]:
        if llm is None:
            raise HTTPException(status_code=503, detail=_LLM_DISABLED_RESP)
        try:
            models = llm.list_models()
        except LLMError as exc:
            raise HTTPException(
                status_code=_LLM_ERROR_STATUS.get(exc.kind, 500),
                detail={"error": exc.kind, "detail": exc.message},
            )
        except Exception as exc:
            raise HTTPException(status_code=500, detail={"error": "internal", "detail": str(exc)})
        return {"models": [m.name for m in models]}

    # ---- /api/llm/chat (non-streaming) --------------------------------------
    @app.post("/api/llm/chat", dependencies=_auth_dep)
    def llm_chat(body: _ChatRequestBody) -> dict[str, Any]:
        if llm is None:
            raise HTTPException(status_code=503, detail=_LLM_DISABLED_RESP)
        if not body.messages:
            raise HTTPException(status_code=400, detail={"error": "validation", "detail": "messages must not be empty"})
        try:
            msgs = [ChatMessage(role=m["role"], content=m["content"]) for m in body.messages]
        except KeyError as exc:
            raise HTTPException(status_code=400, detail={"error": "validation", "detail": f"missing key {exc}"})
        req = ChatRequest(
            model=body.model or "llama3.1:8b",
            messages=msgs,
            temperature=body.temperature if body.temperature is not None else 0.2,
            max_tokens=body.max_tokens,
            seed=body.seed,
        )
        try:
            resp = llm.chat(req)
        except LLMError as exc:
            raise HTTPException(
                status_code=_LLM_ERROR_STATUS.get(exc.kind, 500),
                detail={"error": exc.kind, "detail": exc.message},
            )
        except Exception as exc:
            raise HTTPException(status_code=500, detail={"error": "internal", "detail": str(exc)})
        return {
            "content": resp.message.content,
            "model": resp.model,
            "tokens": resp.completion_tokens,
        }

    # ---- /api/llm/chat/stream (SSE) -----------------------------------------
    @app.post("/api/llm/chat/stream", dependencies=_auth_dep)
    async def llm_chat_stream(body: _ChatRequestBody) -> StreamingResponse:
        if llm is None:
            raise HTTPException(status_code=503, detail=_LLM_DISABLED_RESP)
        if not body.messages:
            raise HTTPException(status_code=400, detail={"error": "validation", "detail": "messages must not be empty"})
        try:
            msgs = [ChatMessage(role=m["role"], content=m["content"]) for m in body.messages]
        except KeyError as exc:
            raise HTTPException(status_code=400, detail={"error": "validation", "detail": f"missing key {exc}"})
        req = ChatRequest(
            model=body.model or "llama3.1:8b",
            messages=msgs,
            temperature=body.temperature if body.temperature is not None else 0.2,
            max_tokens=body.max_tokens,
            seed=body.seed,
        )

        async def _sse_generator() -> AsyncGenerator[str, None]:
            try:
                for chunk in llm.chat_stream(req):
                    yield f"data: {chunk.delta}\n\n"
                    if chunk.done:
                        break
                yield "data: [DONE]\n\n"
            except LLMError as exc:
                err_payload = json.dumps({"error": exc.kind, "detail": exc.message})
                yield f"data: {err_payload}\n\n"
            except GeneratorExit:
                return

        return StreamingResponse(
            _sse_generator(),
            media_type="text/event-stream",
            headers={
                "Cache-Control": "no-cache",
                "X-Accel-Buffering": "no",
            },
        )

    return app
