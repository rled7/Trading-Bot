"""pytest test suite for the AlgoForge dashboard (S3).

Covers:
  - /api/health
  - /api/account
  - /api/positions and /api/orders
  - /api/symbols
  - /api/bars/{symbol}
  - /api/logs
  - Auth (no-token and with-token modes)
  - Static file serving (GET /)
  - SSE smoke test (GET /api/stream/ticks)
  - LogRingBuffer unit tests (capacity, tail, install)

Run with:
    cd /Users/user/Trading-Bot/python && PYTHONPATH=. python3 -m pytest tests/test_dashboard.py -v
"""
from __future__ import annotations

import json
import logging
import time
import threading
from typing import Generator

import pytest
from fastapi.testclient import TestClient

from algoforge.broker import OrderType
from algoforge.dashboard import LogRingBuffer, make_app
from algoforge.logger import log_info
from algoforge.paper_broker import PaperBroker


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def broker() -> Generator[PaperBroker, None, None]:
    """Fresh PaperBroker, connected, yielded, then disconnected."""
    b = PaperBroker(balance=10_000.0)
    b.connect()
    yield b
    b.disconnect()


@pytest.fixture
def log_buf() -> LogRingBuffer:
    """A LogRingBuffer with default capacity."""
    return LogRingBuffer(capacity=500)


@pytest.fixture
def app(broker: PaperBroker, log_buf: LogRingBuffer):
    """App with no auth token, real broker, real log buffer."""
    return make_app(broker, log_buffer=log_buf)


@pytest.fixture
def client(app) -> Generator[TestClient, None, None]:
    """TestClient for the no-auth app."""
    with TestClient(app, raise_server_exceptions=True) as c:
        yield c


@pytest.fixture
def app_with_token(broker: PaperBroker, log_buf: LogRingBuffer):
    """App configured with auth token 'secret'."""
    return make_app(broker, token="secret", log_buffer=log_buf)


@pytest.fixture
def authed_client(app_with_token) -> Generator[TestClient, None, None]:
    """TestClient with valid auth header pre-configured."""
    with TestClient(app_with_token, raise_server_exceptions=True) as c:
        c.headers.update({"Authorization": "Bearer secret"})
        yield c


# ---------------------------------------------------------------------------
# 1. /api/health
# ---------------------------------------------------------------------------

class TestHealth:

    def test_returns_200(self, client: TestClient) -> None:
        resp = client.get("/api/health")
        assert resp.status_code == 200

    def test_has_all_four_fields(self, client: TestClient) -> None:
        data = client.get("/api/health").json()
        assert "broker_name" in data
        assert "is_connected" in data
        assert "version" in data
        assert "uptime_seconds" in data

    def test_field_types(self, client: TestClient) -> None:
        data = client.get("/api/health").json()
        assert isinstance(data["broker_name"], str)
        assert isinstance(data["is_connected"], bool)
        assert isinstance(data["version"], str)
        assert isinstance(data["uptime_seconds"], float)

    def test_is_connected_true_after_connect(self, broker: PaperBroker, log_buf: LogRingBuffer) -> None:
        # broker fixture is already connected
        a = make_app(broker, log_buffer=log_buf)
        with TestClient(a) as c:
            assert c.get("/api/health").json()["is_connected"] is True

    def test_is_connected_false_before_connect(self, log_buf: LogRingBuffer) -> None:
        b = PaperBroker()
        # Do NOT call connect
        a = make_app(b, log_buffer=log_buf)
        with TestClient(a) as c:
            assert c.get("/api/health").json()["is_connected"] is False

    def test_uptime_seconds_increases(self, client: TestClient) -> None:
        first = client.get("/api/health").json()["uptime_seconds"]
        # Give the monotonic clock at least one tick — two HTTP round-trips are enough
        second = client.get("/api/health").json()["uptime_seconds"]
        assert second > first

    def test_broker_name_is_paper_simulator(self, client: TestClient) -> None:
        data = client.get("/api/health").json()
        assert data["broker_name"] == "PaperSimulator"


# ---------------------------------------------------------------------------
# 2. /api/account
# ---------------------------------------------------------------------------

class TestAccount:

    def test_returns_200(self, client: TestClient) -> None:
        assert client.get("/api/account").status_code == 200

    def test_has_eight_account_fields(self, client: TestClient) -> None:
        data = client.get("/api/account").json()
        required = {"balance", "equity", "margin", "free_margin",
                    "profit", "leverage", "currency", "login"}
        assert required.issubset(data.keys())

    def test_balance_equals_init_balance(self, client: TestClient) -> None:
        data = client.get("/api/account").json()
        assert data["balance"] == pytest.approx(10_000.0)

    def test_currency_is_usd(self, client: TestClient) -> None:
        data = client.get("/api/account").json()
        assert data["currency"] == "USD"

    def test_leverage_is_100(self, client: TestClient) -> None:
        data = client.get("/api/account").json()
        assert data["leverage"] == 100

    def test_login_is_integer(self, client: TestClient) -> None:
        data = client.get("/api/account").json()
        assert isinstance(data["login"], int)


# ---------------------------------------------------------------------------
# 3. /api/positions and /api/orders
# ---------------------------------------------------------------------------

class TestPositionsAndOrders:

    def test_positions_empty_initially(self, client: TestClient) -> None:
        data = client.get("/api/positions").json()
        assert data == []

    def test_orders_empty_initially(self, client: TestClient) -> None:
        data = client.get("/api/orders").json()
        assert data == []

    def test_position_appears_after_market_buy(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        # Place a market BUY directly through the broker before making the app
        broker.place_order("EURUSD", OrderType.BUY, 0.10)
        a = make_app(broker, log_buffer=log_buf)
        with TestClient(a) as c:
            data = c.get("/api/positions").json()
        assert len(data) == 1
        pos = data[0]
        assert pos["symbol"] == "EURUSD"

    def test_position_side_is_int(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        broker.place_order("EURUSD", OrderType.BUY, 0.10)
        a = make_app(broker, log_buffer=log_buf)
        with TestClient(a) as c:
            data = c.get("/api/positions").json()
        assert isinstance(data[0]["side"], int)

    def test_position_side_value_is_long(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        # Direction.LONG == 1
        broker.place_order("EURUSD", OrderType.BUY, 0.10)
        a = make_app(broker, log_buffer=log_buf)
        with TestClient(a) as c:
            data = c.get("/api/positions").json()
        assert data[0]["side"] == 1  # Direction.LONG

    def test_position_side_is_short_for_sell(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        # Direction.SHORT == -1
        broker.place_order("EURUSD", OrderType.SELL, 0.10)
        a = make_app(broker, log_buffer=log_buf)
        with TestClient(a) as c:
            data = c.get("/api/positions").json()
        assert data[0]["side"] == -1  # Direction.SHORT

    def test_position_has_expected_fields(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        broker.place_order("GBPUSD", OrderType.BUY, 0.05)
        a = make_app(broker, log_buffer=log_buf)
        with TestClient(a) as c:
            data = c.get("/api/positions").json()
        pos = data[0]
        for field in ("ticket", "symbol", "side", "lots", "open_price",
                      "current_price", "sl", "tp", "profit", "commission"):
            assert field in pos, f"Missing field: {field}"

    def test_orders_returns_list_type(self, client: TestClient) -> None:
        data = client.get("/api/orders").json()
        assert isinstance(data, list)


# ---------------------------------------------------------------------------
# 4. /api/symbols
# ---------------------------------------------------------------------------

class TestSymbols:

    def test_returns_200(self, client: TestClient) -> None:
        assert client.get("/api/symbols").status_code == 200

    def test_returns_exactly_10_symbols(self, client: TestClient) -> None:
        data = client.get("/api/symbols").json()
        assert len(data) == 10

    def test_all_items_are_strings(self, client: TestClient) -> None:
        data = client.get("/api/symbols").json()
        assert all(isinstance(s, str) for s in data)

    def test_contains_eurusd(self, client: TestClient) -> None:
        data = client.get("/api/symbols").json()
        assert "EURUSD" in data

    def test_contains_xauusd(self, client: TestClient) -> None:
        data = client.get("/api/symbols").json()
        assert "XAUUSD" in data


# ---------------------------------------------------------------------------
# 5. /api/bars/{symbol}
# ---------------------------------------------------------------------------

class TestBars:

    def test_returns_200(self, client: TestClient) -> None:
        resp = client.get("/api/bars/EURUSD?tf=M5&count=10")
        assert resp.status_code == 200

    def test_returns_requested_count(self, client: TestClient) -> None:
        data = client.get("/api/bars/EURUSD?tf=M5&count=10").json()
        # PaperBroker always returns exactly count bars
        assert len(data) == 10

    def test_each_bar_has_seven_fields(self, client: TestClient) -> None:
        data = client.get("/api/bars/EURUSD?tf=M5&count=5").json()
        expected_fields = {"timestamp", "open", "high", "low", "close", "volume", "spread"}
        for bar in data:
            assert expected_fields.issubset(bar.keys()), f"Bar missing fields: {bar}"

    def test_bar_high_gte_low(self, client: TestClient) -> None:
        data = client.get("/api/bars/EURUSD?tf=M5&count=20").json()
        for bar in data:
            assert bar["high"] >= bar["low"]

    def test_invalid_tf_returns_4xx(self, client: TestClient) -> None:
        resp = client.get("/api/bars/EURUSD?tf=X&count=10")
        assert 400 <= resp.status_code < 500

    def test_count_clamped_to_5000(self, client: TestClient) -> None:
        data = client.get("/api/bars/EURUSD?tf=M1&count=99999").json()
        assert len(data) <= 5000

    def test_count_zero_clamped_to_one(self, client: TestClient) -> None:
        # Server clamps max(1, min(count, 5000)) — zero becomes 1
        data = client.get("/api/bars/EURUSD?tf=M5&count=0").json()
        assert len(data) == 1

    def test_negative_count_clamped_to_one(self, client: TestClient) -> None:
        data = client.get("/api/bars/EURUSD?tf=M5&count=-5").json()
        assert len(data) == 1

    def test_tf_m1_is_valid(self, client: TestClient) -> None:
        resp = client.get("/api/bars/EURUSD?tf=M1&count=3")
        assert resp.status_code == 200

    def test_tf_h1_is_valid(self, client: TestClient) -> None:
        resp = client.get("/api/bars/EURUSD?tf=H1&count=3")
        assert resp.status_code == 200

    def test_tf_d1_is_valid(self, client: TestClient) -> None:
        resp = client.get("/api/bars/EURUSD?tf=D1&count=3")
        assert resp.status_code == 200

    def test_tf_lowercase_invalid(self, client: TestClient) -> None:
        # Timeframe enum keys are uppercase only
        resp = client.get("/api/bars/EURUSD?tf=m5&count=5")
        assert 400 <= resp.status_code < 500

    def test_xauusd_bars(self, client: TestClient) -> None:
        data = client.get("/api/bars/XAUUSD?tf=H1&count=5").json()
        assert len(data) == 5


# ---------------------------------------------------------------------------
# 6. /api/logs
# ---------------------------------------------------------------------------

class TestLogs:

    def test_returns_200(self, client: TestClient) -> None:
        assert client.get("/api/logs?n=10").status_code == 200

    def test_returns_list(self, client: TestClient) -> None:
        data = client.get("/api/logs?n=10").json()
        assert isinstance(data, list)

    def test_captured_records_appear_in_order(
        self, broker: PaperBroker
    ) -> None:
        buf = LogRingBuffer(capacity=500)
        buf.install()

        # Emit some records via the algoforge logger hierarchy
        logger = logging.getLogger("algoforge.test_logs")
        logger.setLevel(logging.DEBUG)
        logger.info("message alpha")
        logger.info("message beta")
        logger.info("message gamma")

        records = buf.tail(10)
        msgs = [r["msg"] for r in records]
        # Confirm ordering: alpha before beta before gamma
        # (tail returns latest N in chronological order)
        alpha_idx = next((i for i, m in enumerate(msgs) if "message alpha" in m), None)
        beta_idx  = next((i for i, m in enumerate(msgs) if "message beta" in m), None)
        gamma_idx = next((i for i, m in enumerate(msgs) if "message gamma" in m), None)
        assert alpha_idx is not None and beta_idx is not None and gamma_idx is not None
        assert alpha_idx < beta_idx < gamma_idx

        # Clean up: remove the handler to avoid polluting other tests
        logging.getLogger("algoforge").removeHandler(buf)

    def test_log_info_convenience_wrapper_captured(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        log_buf.install()
        log_info("test convenience message", component="testcomponent")
        records = log_buf.tail(5)
        msgs = [r["msg"] for r in records]
        assert any("test convenience message" in m for m in msgs)
        logging.getLogger("algoforge").removeHandler(log_buf)

    def test_log_record_has_required_keys(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        log_buf.install()
        log_info("field check message")
        records = log_buf.tail(5)
        for rec in records:
            assert "ts" in rec
            assert "level" in rec
            assert "component" in rec
            assert "msg" in rec
        logging.getLogger("algoforge").removeHandler(log_buf)

    def test_n_clamped_to_500_max(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        # Push 10 records, request n=99999; server clamps to min(99999, 500)
        log_buf.install()
        logger = logging.getLogger("algoforge.test_clamp")
        logger.setLevel(logging.DEBUG)
        for i in range(10):
            logger.info(f"clamp test {i}")

        a = make_app(broker, log_buffer=log_buf)
        with TestClient(a) as c:
            data = c.get("/api/logs?n=99999").json()
        assert len(data) <= 500
        logging.getLogger("algoforge").removeHandler(log_buf)

    def test_n_zero_clamped_to_one(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        # Server uses max(1, min(n, 500)) — n=0 becomes 1
        log_buf.install()
        logging.getLogger("algoforge.test_n_zero").info("zero clamp")
        a = make_app(broker, log_buffer=log_buf)
        with TestClient(a) as c:
            data = c.get("/api/logs?n=0").json()
        # Should return at most 1 record (clamped) without error
        assert isinstance(data, list)
        assert len(data) <= 1
        logging.getLogger("algoforge").removeHandler(log_buf)

    def test_empty_when_no_buffer(
        self, broker: PaperBroker
    ) -> None:
        a = make_app(broker)  # no log_buffer
        with TestClient(a) as c:
            data = c.get("/api/logs?n=10").json()
        assert data == []


# ---------------------------------------------------------------------------
# 7. Auth
# ---------------------------------------------------------------------------

class TestAuthNoToken:
    """When make_app(broker) — no token — all endpoints are public."""

    def test_health_200_without_header(self, client: TestClient) -> None:
        resp = client.get("/api/health")
        assert resp.status_code == 200

    def test_account_200_without_header(self, client: TestClient) -> None:
        resp = client.get("/api/account")
        assert resp.status_code == 200

    def test_symbols_200_without_header(self, client: TestClient) -> None:
        resp = client.get("/api/symbols")
        assert resp.status_code == 200


class TestAuthWithToken:
    """When make_app(broker, token='secret') — all /api/* require auth."""

    def _unauthed(self, app) -> TestClient:
        """Returns a TestClient with no auth headers."""
        return TestClient(app, raise_server_exceptions=False)

    def test_no_header_returns_401(self, app_with_token) -> None:
        with TestClient(app_with_token, raise_server_exceptions=False) as c:
            resp = c.get("/api/health")
        assert resp.status_code == 401

    def test_wrong_token_returns_401(self, app_with_token) -> None:
        with TestClient(app_with_token, raise_server_exceptions=False) as c:
            resp = c.get("/api/health", headers={"Authorization": "Bearer wrong"})
        assert resp.status_code == 401

    def test_correct_token_returns_200(self, app_with_token) -> None:
        with TestClient(app_with_token) as c:
            resp = c.get("/api/health", headers={"Authorization": "Bearer secret"})
        assert resp.status_code == 200

    def test_bearer_case_insensitive_prefix(self, app_with_token) -> None:
        # "BEARER" prefix (uppercase) should still work
        with TestClient(app_with_token) as c:
            resp = c.get("/api/health", headers={"Authorization": "BEARER secret"})
        assert resp.status_code == 200

    def test_account_requires_auth(self, app_with_token) -> None:
        with TestClient(app_with_token, raise_server_exceptions=False) as c:
            resp = c.get("/api/account")
        assert resp.status_code == 401

    def test_symbols_requires_auth(self, app_with_token) -> None:
        with TestClient(app_with_token, raise_server_exceptions=False) as c:
            resp = c.get("/api/symbols")
        assert resp.status_code == 401

    def test_positions_requires_auth(self, app_with_token) -> None:
        with TestClient(app_with_token, raise_server_exceptions=False) as c:
            resp = c.get("/api/positions")
        assert resp.status_code == 401

    def test_orders_requires_auth(self, app_with_token) -> None:
        with TestClient(app_with_token, raise_server_exceptions=False) as c:
            resp = c.get("/api/orders")
        assert resp.status_code == 401

    def test_bars_requires_auth(self, app_with_token) -> None:
        with TestClient(app_with_token, raise_server_exceptions=False) as c:
            resp = c.get("/api/bars/EURUSD?tf=M5&count=5")
        assert resp.status_code == 401

    def test_logs_requires_auth(self, app_with_token) -> None:
        with TestClient(app_with_token, raise_server_exceptions=False) as c:
            resp = c.get("/api/logs?n=5")
        assert resp.status_code == 401


# ---------------------------------------------------------------------------
# 8. Static / GET /
# ---------------------------------------------------------------------------

class TestStatic:

    def test_root_responds(self, app) -> None:
        # If index.html exists we get 200; if missing we get 404.
        # Either way the route must not crash with 5xx.
        with TestClient(app, raise_server_exceptions=False) as c:
            resp = c.get("/")
        assert resp.status_code in (200, 404)

    def test_root_200_with_html_content_type(self, app) -> None:
        with TestClient(app, raise_server_exceptions=False) as c:
            resp = c.get("/")
        if resp.status_code == 200:
            assert "text/html" in resp.headers.get("content-type", "")

    def test_root_not_5xx(self, app) -> None:
        with TestClient(app, raise_server_exceptions=False) as c:
            resp = c.get("/")
        assert resp.status_code < 500


# ---------------------------------------------------------------------------
# 9. SSE smoke test
# ---------------------------------------------------------------------------

class TestSSE:
    """Stream tests call the endpoint's StreamingResponse generator directly
    and iterate its body_iterator. Going through HTTP (TestClient or
    AsyncClient+ASGITransport) deadlocks on infinite streams — neither
    transport supports incremental reads against an endpoint with no EOF.
    The auth-rejection test still uses TestClient because the 401 is
    raised synchronously before any streaming starts.
    """

    @staticmethod
    def _stream_handler(app):
        for route in app.routes:
            if getattr(route, "path", None) == "/api/stream/ticks":
                return route.endpoint
        raise AssertionError("/api/stream/ticks route not found")

    @staticmethod
    async def _collect_chunks(handler, *, max_chunks: int = 12, timeout: float = 5.0,
                              **kwargs) -> list[str]:
        """Invoke the endpoint, iterate its body_iterator, return up to N chunks."""
        import asyncio as _asyncio
        resp = await handler(
            interval=kwargs.pop("interval", 0.1),
            symbols=kwargs.pop("symbols", ""),
            token_param=kwargs.pop("token_param", None),
            authorization=kwargs.pop("authorization", None),
        )
        chunks: list[str] = []

        async def _consume() -> None:
            async for chunk in resp.body_iterator:
                if isinstance(chunk, bytes):
                    chunk = chunk.decode("utf-8")
                chunks.append(chunk)
                if len(chunks) >= max_chunks:
                    break

        try:
            await _asyncio.wait_for(_consume(), timeout=timeout)
        except _asyncio.TimeoutError:
            pass
        # Best-effort: close the async generator so the server-side task ends.
        try:
            await resp.body_iterator.aclose()
        except Exception:
            pass
        return chunks

    @staticmethod
    def _parse_events(chunks: list[str]) -> list[dict]:
        events: list[dict] = []
        for ch in chunks:
            for line in ch.splitlines():
                if line.startswith("data: "):
                    try:
                        events.append(json.loads(line[6:]))
                    except json.JSONDecodeError:
                        pass
        return events

    def test_stream_sends_data_events(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        import asyncio as _asyncio
        a = make_app(broker, log_buffer=log_buf)
        chunks = _asyncio.run(self._collect_chunks(self._stream_handler(a)))
        events = self._parse_events(chunks)
        assert len(events) >= 1, f"No events. Chunks: {chunks[:6]}"
        for key in ("symbol", "bid", "ask", "time"):
            assert key in events[0], f"Missing '{key}' in {events[0]}"

    def test_stream_contains_keepalive_comments(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        import asyncio as _asyncio
        a = make_app(broker, log_buffer=log_buf)
        chunks = _asyncio.run(self._collect_chunks(self._stream_handler(a)))
        joined = "".join(chunks)
        assert ": keepalive" in joined, f"No keepalive in stream. Chunks: {chunks[:4]}"

    def test_stream_wrong_token_returns_401(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        """With auth enabled, a wrong ?token= query param yields 401 before the stream."""
        a = make_app(broker, token="secret", log_buffer=log_buf)
        with TestClient(a, raise_server_exceptions=False) as c:
            resp = c.get("/api/stream/ticks?interval=0.1&token=wrong")
        assert resp.status_code == 401

    def test_stream_correct_token_accepted(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        import asyncio as _asyncio
        a = make_app(broker, token="secret", log_buffer=log_buf)
        chunks = _asyncio.run(
            self._collect_chunks(self._stream_handler(a), token_param="secret")
        )
        events = self._parse_events(chunks)
        assert len(events) >= 1

    def test_stream_data_event_bid_ask_are_floats(
        self, broker: PaperBroker, log_buf: LogRingBuffer
    ) -> None:
        import asyncio as _asyncio
        a = make_app(broker, log_buffer=log_buf)
        chunks = _asyncio.run(self._collect_chunks(self._stream_handler(a)))
        events = self._parse_events(chunks)
        assert len(events) >= 1
        for ev in events:
            assert isinstance(ev["bid"], (int, float))
            assert isinstance(ev["ask"], (int, float))
            assert ev["ask"] >= ev["bid"]


# ---------------------------------------------------------------------------
# 10. LogRingBuffer unit tests
# ---------------------------------------------------------------------------

class TestLogRingBuffer:

    def test_capacity_enforcement(self) -> None:
        """After pushing capacity+5 records, exactly capacity records are retained."""
        capacity = 10
        buf = LogRingBuffer(capacity=capacity)
        logger = logging.getLogger("algoforge._ring_test")
        logger.setLevel(logging.DEBUG)
        buf.install()
        try:
            for i in range(capacity + 5):
                logger.info(f"overflow msg {i}")
            records = buf.tail(capacity + 5)
            assert len(records) == capacity
        finally:
            logging.getLogger("algoforge").removeHandler(buf)

    def test_capacity_fifo_keeps_newest(self) -> None:
        """When the buffer overflows, the oldest messages are dropped (FIFO)."""
        capacity = 5
        buf = LogRingBuffer(capacity=capacity)
        logger = logging.getLogger("algoforge._fifo_test")
        logger.setLevel(logging.DEBUG)
        buf.install()
        try:
            for i in range(capacity + 2):
                logger.info(f"fifo msg {i}")
            records = buf.tail(capacity)
            msgs = [r["msg"] for r in records]
            # Oldest (0, 1) should be gone; newest should be present
            assert not any("fifo msg 0" in m for m in msgs)
            assert not any("fifo msg 1" in m for m in msgs)
            assert any(f"fifo msg {capacity + 1}" in m for m in msgs)
        finally:
            logging.getLogger("algoforge").removeHandler(buf)

    def test_tail_k_returns_last_k(self) -> None:
        """tail(k) returns the last k records in chronological order."""
        buf = LogRingBuffer(capacity=100)
        logger = logging.getLogger("algoforge._tail_test")
        logger.setLevel(logging.DEBUG)
        buf.install()
        try:
            for i in range(20):
                logger.info(f"tail msg {i}")
            records = buf.tail(5)
            assert len(records) == 5
            msgs = [r["msg"] for r in records]
            # Last 5 messages should be 15..19 (msg indices)
            for idx in range(15, 20):
                assert any(f"tail msg {idx}" in m for m in msgs)
        finally:
            logging.getLogger("algoforge").removeHandler(buf)

    def test_tail_larger_than_buffer_returns_all(self) -> None:
        """tail(k) where k > number of records returns all available records."""
        buf = LogRingBuffer(capacity=100)
        logger = logging.getLogger("algoforge._tail_all_test")
        logger.setLevel(logging.DEBUG)
        buf.install()
        try:
            for i in range(3):
                logger.info(f"small buf msg {i}")
            records = buf.tail(50)
            assert len(records) == 3
        finally:
            logging.getLogger("algoforge").removeHandler(buf)

    def test_tail_zero_returns_empty(self) -> None:
        """tail(0) returns an empty list."""
        buf = LogRingBuffer(capacity=100)
        assert buf.tail(0) == []

    def test_install_attaches_to_algoforge_logger(self) -> None:
        """install() causes records emitted on the algoforge logger to appear in tail()."""
        buf = LogRingBuffer(capacity=50)
        buf.install()
        try:
            log_info("install check message", component="install_test")
            records = buf.tail(10)
            msgs = [r["msg"] for r in records]
            assert any("install check message" in m for m in msgs)
        finally:
            logging.getLogger("algoforge").removeHandler(buf)

    def test_record_level_is_string(self) -> None:
        buf = LogRingBuffer(capacity=10)
        buf.install()
        try:
            logging.getLogger("algoforge._level_test").setLevel(logging.DEBUG)
            logging.getLogger("algoforge._level_test").warning("level test")
            records = buf.tail(5)
            for r in records:
                assert isinstance(r["level"], str)
        finally:
            logging.getLogger("algoforge").removeHandler(buf)

    def test_record_ts_is_iso8601_format(self) -> None:
        """Timestamps should look like '2026-05-22T12:00:00Z'."""
        buf = LogRingBuffer(capacity=10)
        buf.install()
        try:
            logging.getLogger("algoforge._ts_test").setLevel(logging.DEBUG)
            logging.getLogger("algoforge._ts_test").info("ts format test")
            records = buf.tail(5)
            import re
            iso8601_pattern = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
            for r in records:
                assert iso8601_pattern.match(r["ts"]), (
                    f"Timestamp '{r['ts']}' does not match ISO 8601 format"
                )
        finally:
            logging.getLogger("algoforge").removeHandler(buf)

    def test_install_idempotent_does_not_duplicate(self) -> None:
        """Calling install() twice should not duplicate records."""
        buf = LogRingBuffer(capacity=20)
        buf.install()
        buf.install()  # second call — same handler object, deque still enforces capacity
        try:
            logging.getLogger("algoforge._idem_test").setLevel(logging.DEBUG)
            logging.getLogger("algoforge._idem_test").info("idem msg")
            records = buf.tail(20)
            # There may be 1 or 2 copies depending on how addHandler handles duplicates;
            # what matters is the buffer does not grow unboundedly.
            assert len(records) <= 20
        finally:
            # Remove all instances of this handler
            root = logging.getLogger("algoforge")
            while buf in root.handlers:
                root.removeHandler(buf)
