from __future__ import annotations

"""FastAPI router for analytics SSE/snapshot endpoints.

Spec reference: analytics_spec.md §4 "Dashboard SSE (Python only)".

Usage::

    from algoforge.analytics.streaming import OnlineMetrics
    from algoforge.analytics.dashboard import make_router

    metrics = OnlineMetrics(initial_capital=10_000.0)
    router = make_router(metrics)

    # Wire into the existing FastAPI app (done by supervisor):
    app.include_router(router)

Endpoints
---------
GET /api/analytics/snapshot
    Returns the current OnlineMetrics snapshot as JSON.

GET /api/analytics/stream
    Server-Sent Events stream.  Polls every 1 s; emits a ``data:`` event
    only when the snapshot has changed (compared by JSON-serialised hash).
"""

import asyncio
import hashlib
import json
from typing import Any, AsyncGenerator

from fastapi import APIRouter
from fastapi.responses import StreamingResponse

from algoforge.analytics.streaming import OnlineMetrics


def make_router(online_metrics: OnlineMetrics) -> APIRouter:
    """Return an APIRouter wired to *online_metrics*.

    Parameters
    ----------
    online_metrics:
        A live :class:`OnlineMetrics` instance whose ``update()`` is called
        externally (e.g. from the backtest engine or paper broker) whenever
        a trade closes.

    Returns
    -------
    APIRouter
        Mount with ``app.include_router(router)``.
    """
    router = APIRouter(prefix="/api/analytics", tags=["analytics"])

    # ------------------------------------------------------------------ #
    # GET /api/analytics/snapshot                                          #
    # ------------------------------------------------------------------ #

    @router.get("/snapshot")
    def get_snapshot() -> dict[str, Any]:
        """Return the current OnlineMetrics snapshot as a JSON object."""
        return online_metrics.snapshot()

    # ------------------------------------------------------------------ #
    # GET /api/analytics/stream  (SSE)                                    #
    # ------------------------------------------------------------------ #

    @router.get("/stream")
    async def stream_analytics() -> StreamingResponse:
        """Server-Sent Events endpoint.

        Polls the snapshot every 1 second and emits a ``data:`` line only
        when the content has changed (compared by MD5 of the JSON string).
        """

        async def _generator() -> AsyncGenerator[str, None]:
            last_hash: str = ""
            try:
                while True:
                    snap = online_metrics.snapshot()
                    payload = json.dumps(snap, default=str)
                    digest = hashlib.md5(payload.encode()).hexdigest()

                    if digest != last_hash:
                        last_hash = digest
                        yield f"data: {payload}\n\n"

                    await asyncio.sleep(1.0)
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

    return router
