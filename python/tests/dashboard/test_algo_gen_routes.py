"""Tests for /api/algos/* dashboard endpoints.

SSE stream tests iterate response.body_iterator directly — do NOT route
them through TestClient which deadlocks on streaming generators.
Auth 401 tests can use TestClient because 401 is raised synchronously
before any streaming begins.

Run with:
    cd /Users/user/Trading-Bot/python && \\
    PYTHONPATH=. python3 -m pytest tests/dashboard/test_algo_gen_routes.py -v
"""
from __future__ import annotations

import asyncio
import json
from typing import Any
from unittest.mock import MagicMock, patch
import dataclasses

import pytest
from fastapi.testclient import TestClient

from algoforge.dashboard.server import make_app
from algoforge.algo_gen.types import (
    AlgoManifest,
    IndicatorDecl,
    EntryRule,
    ExitRule,
    RiskSpec,
    TierReport,
    TierName,
    ValidationResult,
    StageResult,
)
from algoforge.paper_broker import PaperBroker
from algoforge.algo_gen.generator import GenerationError
from algoforge.algo_gen.promote import PromotionError
from algoforge.algo_gen.generator import GenerationTrace


# ---------------------------------------------------------------------------
# Fixtures and helpers
# ---------------------------------------------------------------------------

def _make_manifest(name: str = "test-algo") -> AlgoManifest:
    """Build a minimal but valid AlgoManifest."""
    return AlgoManifest(
        schema_version="1.0",
        name=name,
        description="Test algorithm",
        rationale="Testing",
        timeframes=["H1"],
        symbols=["EURUSD"],
        indicators=[
            IndicatorDecl(id="ema9",  kind="ema", params={"period": 9}),
            IndicatorDecl(id="ema21", kind="ema", params={"period": 21}),
            IndicatorDecl(id="atr14", kind="atr", params={"period": 14}),
        ],
        entries=[EntryRule(side="long", when="ema9 > ema21")],
        exits=[ExitRule(side="long", sl_atr=1.5, tp_atr=3.0)],
        risk=RiskSpec(
            size="atr",
            atr_mult=1.5,
            fixed_lots=0.01,
            max_concurrent=1,
            hedge=False,
            cool_down_bars=0,
        ),
        code=None,
    )


def _make_tier(tier: TierName = TierName.green) -> TierReport:
    return TierReport(
        tier=tier,
        min_score=92.0,
        walk_forward=93.0,
        mc_bootstrap=92.0,
        robustness=94.0,
        size_mult=1.0,
        experimental=False,
        paper_only=False,
    )


def _make_validation(passed: bool = True) -> ValidationResult:
    stages = [
        StageResult(stage=1, name="schema_lint", passed=True, reason="ok"),
    ]
    tier = _make_tier() if passed else None
    return ValidationResult(
        passed=passed,
        stages=stages,
        report=tier,
        manifest_name="test-algo",
    )


def _make_trace() -> GenerationTrace:
    return GenerationTrace(mode="fast", model="llama3.1:8b", seed=42)


class FakeAlgoGen:
    """In-memory algo_gen facade for tests. No LLM or disk I/O."""

    def __init__(
        self,
        *,
        manifest: AlgoManifest | None = None,
        validation: ValidationResult | None = None,
        trace: GenerationTrace | None = None,
        generate_exc: Exception | None = None,
        validate_exc: Exception | None = None,
        backtest_result: dict | None = None,
        backtest_exc: Exception | None = None,
        promote_exc: Exception | None = None,
        retire_exc: Exception | None = None,
    ) -> None:
        self._manifest = manifest or _make_manifest()
        self._validation = validation if validation is not None else _make_validation()
        self._trace = trace or _make_trace()
        self._generate_exc = generate_exc
        self._validate_exc = validate_exc
        self._backtest_result = backtest_result or {
            "equity_curve": [10000.0, 10050.0, 10100.0],
            "metrics": {
                "total_trades": 25,
                "net_profit": 100.0,
                "sharpe": 1.2,
                "max_drawdown_pct": 5.0,
                "win_rate": 0.6,
            },
        }
        self._backtest_exc = backtest_exc
        self._promote_exc = promote_exc
        self._retire_exc = retire_exc
        # Track calls
        self.generate_calls: list[tuple] = []
        self.validate_calls: list[tuple] = []
        self.backtest_calls: list[tuple] = []
        self.promote_calls: list[tuple] = []
        self.retire_calls: list[tuple] = []

    def generate(self, brief: str, effort: str, seed: int):
        self.generate_calls.append((brief, effort, seed))
        if self._generate_exc:
            raise self._generate_exc
        return self._manifest, self._trace

    def validate(self, manifest, seed: int) -> ValidationResult:
        self.validate_calls.append((manifest, seed))
        if self._validate_exc:
            raise self._validate_exc
        return self._validation

    def backtest(self, manifest, *, symbol: str = "EURUSD", n_bars=None, seed: int = 42) -> dict:
        self.backtest_calls.append((manifest, symbol, n_bars, seed))
        if self._backtest_exc:
            raise self._backtest_exc
        return self._backtest_result

    def promote(self, tier, manifest) -> Any:
        self.promote_calls.append((tier, manifest))
        if self._promote_exc:
            raise self._promote_exc
        return MagicMock()

    def retire(self, manifest) -> None:
        self.retire_calls.append((manifest,))
        if self._retire_exc:
            raise self._retire_exc


@pytest.fixture
def broker() -> PaperBroker:
    b = PaperBroker(balance=10_000.0)
    b.connect()
    yield b
    b.disconnect()


@pytest.fixture
def fake_ag() -> FakeAlgoGen:
    return FakeAlgoGen()


@pytest.fixture
def app_with_ag(broker: PaperBroker, fake_ag: FakeAlgoGen):
    # Clear the in-memory store between tests
    from algoforge.dashboard.algo_gen_routes import _STORE
    _STORE.clear()
    return make_app(broker, algo_gen=fake_ag)


@pytest.fixture
def client_with_ag(app_with_ag):
    with TestClient(app_with_ag, raise_server_exceptions=True) as c:
        yield c


@pytest.fixture
def app_no_ag(broker: PaperBroker):
    from algoforge.dashboard.algo_gen_routes import _STORE
    _STORE.clear()
    return make_app(broker, algo_gen=None)


@pytest.fixture
def client_no_ag(app_no_ag):
    with TestClient(app_no_ag, raise_server_exceptions=False) as c:
        yield c


def _generate_algo(client: TestClient) -> dict:
    """POST /api/algos/generate and return JSON."""
    resp = client.post(
        "/api/algos/generate",
        json={"brief": "trend following EURUSD", "effort": "fast"},
    )
    assert resp.status_code == 200, resp.text
    return resp.json()


# ---------------------------------------------------------------------------
# Helper: iterate SSE endpoint body_iterator
# ---------------------------------------------------------------------------

def _get_stream_endpoint(app):
    """Return the async handler for /api/algos/generate/stream."""
    for route in app.routes:
        if getattr(route, "path", None) == "/api/algos/generate/stream":
            return route.endpoint
    raise AssertionError("/api/algos/generate/stream route not found")


async def _collect_sse(endpoint, body: dict, max_chunks: int = 50, timeout: float = 5.0) -> list[str]:
    """Call the SSE endpoint and collect raw data lines."""
    from algoforge.dashboard.algo_gen_routes import GenerateBody
    body_model = GenerateBody(**body)
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
# Tests: GET /api/algos (list)
# ---------------------------------------------------------------------------

class TestListAlgos:

    def test_empty_list(self, client_with_ag: TestClient) -> None:
        resp = client_with_ag.get("/api/algos")
        assert resp.status_code == 200
        assert resp.json() == []

    def test_list_after_generate(self, client_with_ag: TestClient) -> None:
        _generate_algo(client_with_ag)
        resp = client_with_ag.get("/api/algos")
        assert resp.status_code == 200
        data = resp.json()
        assert len(data) == 1
        entry = data[0]
        assert "id" in entry
        assert entry["name"] == "test-algo"
        assert entry["status"] == "generated"

    def test_list_returns_tier_when_available(self, client_with_ag: TestClient) -> None:
        _generate_algo(client_with_ag)
        resp = client_with_ag.get("/api/algos")
        data = resp.json()
        # Tier should be the string value from TierName.green
        assert data[0]["tier"] == "green"


# ---------------------------------------------------------------------------
# Tests: POST /api/algos/generate (non-streaming)
# ---------------------------------------------------------------------------

class TestGenerate:

    def test_generate_happy_path(self, client_with_ag: TestClient, fake_ag: FakeAlgoGen) -> None:
        resp = client_with_ag.post(
            "/api/algos/generate",
            json={"brief": "momentum EURUSD", "effort": "fast"},
        )
        assert resp.status_code == 200
        data = resp.json()
        assert "id" in data
        assert "manifest" in data
        assert data["manifest"]["name"] == "test-algo"
        assert "validation" in data
        assert "tier" in data
        # Verify generate was called with the right args
        assert fake_ag.generate_calls[0] == ("momentum EURUSD", "fast", 42)

    def test_generate_with_seed(self, client_with_ag: TestClient, fake_ag: FakeAlgoGen) -> None:
        resp = client_with_ag.post(
            "/api/algos/generate",
            json={"brief": "test brief", "effort": "balanced", "seed": 99},
        )
        assert resp.status_code == 200
        assert fake_ag.generate_calls[0][2] == 99  # seed passed through

    def test_generate_returns_validation_passed(self, client_with_ag: TestClient) -> None:
        resp = client_with_ag.post(
            "/api/algos/generate",
            json={"brief": "test brief", "effort": "fast"},
        )
        data = resp.json()
        assert data["validation"]["passed"] is True
        assert data["tier"]["tier"] == "green"

    def test_generate_generation_error_422(self, broker: PaperBroker) -> None:
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        ag = FakeAlgoGen(generate_exc=GenerationError("schema invalid after 2 attempts"))
        app = make_app(broker, algo_gen=ag)
        with TestClient(app, raise_server_exceptions=False) as c:
            resp = c.post(
                "/api/algos/generate",
                json={"brief": "broken", "effort": "fast"},
            )
        assert resp.status_code == 422
        detail = resp.json().get("detail", resp.json())
        assert detail.get("error") == "schema_invalid"

    def test_generate_timeout_gives_504(self, broker: PaperBroker) -> None:
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        ag = FakeAlgoGen(generate_exc=GenerationError("LLM timeout after 2 attempts: timeout"))
        app = make_app(broker, algo_gen=ag)
        with TestClient(app, raise_server_exceptions=False) as c:
            resp = c.post(
                "/api/algos/generate",
                json={"brief": "test", "effort": "fast"},
            )
        assert resp.status_code == 504

    def test_generate_unreachable_gives_502(self, broker: PaperBroker) -> None:
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        ag = FakeAlgoGen(generate_exc=GenerationError("connection refused: unreachable"))
        app = make_app(broker, algo_gen=ag)
        with TestClient(app, raise_server_exceptions=False) as c:
            resp = c.post(
                "/api/algos/generate",
                json={"brief": "test", "effort": "fast"},
            )
        assert resp.status_code == 502

    def test_generate_stores_algo_in_registry(self, client_with_ag: TestClient) -> None:
        resp = client_with_ag.post(
            "/api/algos/generate",
            json={"brief": "test", "effort": "fast"},
        )
        algo_id = resp.json()["id"]
        # Fetch manifest directly
        manifest_resp = client_with_ag.get(f"/api/algos/{algo_id}/manifest")
        assert manifest_resp.status_code == 200
        assert manifest_resp.json()["name"] == "test-algo"


# ---------------------------------------------------------------------------
# Tests: POST /api/algos/generate/stream (SSE)
# ---------------------------------------------------------------------------

class TestGenerateStream:

    def test_stream_event_order(self, broker: PaperBroker, fake_ag: FakeAlgoGen) -> None:
        """SSE: iterate body_iterator to avoid TestClient deadlock."""
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        app = make_app(broker, algo_gen=fake_ag)
        endpoint = _get_stream_endpoint(app)

        raw_chunks = asyncio.run(
            _collect_sse(
                endpoint,
                {"brief": "test brief", "effort": "fast"},
                max_chunks=20,
                timeout=5.0,
            )
        )

        joined = "".join(raw_chunks)
        # All expected stages must appear
        assert '"stage": "prompt"' in joined or '"stage":"prompt"' in joined
        assert '"stage": "manifest"' in joined or '"stage":"manifest"' in joined
        assert '"stage": "validation"' in joined or '"stage":"validation"' in joined
        assert '"stage": "tier"' in joined or '"stage":"tier"' in joined
        assert "[DONE]" in joined

    def test_stream_stage_ordering(self, broker: PaperBroker, fake_ag: FakeAlgoGen) -> None:
        """Prompt must come before manifest, validation, tier, DONE."""
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        app = make_app(broker, algo_gen=fake_ag)
        endpoint = _get_stream_endpoint(app)

        raw_chunks = asyncio.run(
            _collect_sse(
                endpoint,
                {"brief": "test brief", "effort": "fast"},
                max_chunks=20,
                timeout=5.0,
            )
        )

        joined = "".join(raw_chunks)
        # Find positions of each stage
        pos_prompt = joined.find("prompt")
        pos_manifest = joined.find("manifest")
        pos_validation = joined.find("validation")
        pos_tier = joined.find('"stage": "tier"') if '"stage": "tier"' in joined else joined.find('"stage":"tier"')
        pos_done = joined.find("[DONE]")

        assert pos_prompt >= 0, "prompt stage missing"
        assert pos_manifest > pos_prompt, "manifest must come after prompt"
        assert pos_validation > pos_manifest, "validation must come after manifest"
        assert pos_done > 0, "[DONE] missing"

    def test_stream_generation_error_emits_error_event(self, broker: PaperBroker) -> None:
        """When generation fails, an error event is emitted (no crash)."""
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        ag = FakeAlgoGen(generate_exc=GenerationError("failed"))
        app = make_app(broker, algo_gen=ag)
        endpoint = _get_stream_endpoint(app)

        raw_chunks = asyncio.run(
            _collect_sse(
                endpoint,
                {"brief": "test", "effort": "fast"},
                max_chunks=20,
                timeout=5.0,
            )
        )

        joined = "".join(raw_chunks)
        assert "error" in joined

    def test_stream_abort_cleans_up(self, broker: PaperBroker, fake_ag: FakeAlgoGen) -> None:
        """Close the iterator early; should not raise."""
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        app = make_app(broker, algo_gen=fake_ag)
        endpoint = _get_stream_endpoint(app)

        async def _run():
            from algoforge.dashboard.algo_gen_routes import GenerateBody
            body = GenerateBody(brief="test", effort="fast")
            resp = await endpoint(body=body)
            collected: list[str] = []
            async for chunk in resp.body_iterator:
                if isinstance(chunk, bytes):
                    chunk = chunk.decode()
                collected.append(chunk)
                if len(collected) >= 1:
                    break
            try:
                await resp.body_iterator.aclose()
            except Exception:
                pass
            return collected

        result = asyncio.run(_run())
        assert len(result) >= 1


# ---------------------------------------------------------------------------
# Tests: POST /api/algos/{algo_id}/backtest
# ---------------------------------------------------------------------------

class TestBacktest:

    def test_backtest_happy_path(self, client_with_ag: TestClient) -> None:
        gen = _generate_algo(client_with_ag)
        algo_id = gen["id"]

        resp = client_with_ag.post(
            f"/api/algos/{algo_id}/backtest",
            json={"symbol": "EURUSD"},
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["algo_id"] == algo_id
        assert data["symbol"] == "EURUSD"
        assert isinstance(data["equity_curve"], list)
        assert "metrics" in data
        assert "total_trades" in data["metrics"]

    def test_backtest_not_found(self, client_with_ag: TestClient) -> None:
        resp = client_with_ag.post(
            "/api/algos/nonexistent-id/backtest",
            json={"symbol": "EURUSD"},
        )
        assert resp.status_code == 404
        detail = resp.json().get("detail", resp.json())
        assert detail.get("error") == "not_found"

    def test_backtest_with_bars_param(self, client_with_ag: TestClient, fake_ag: FakeAlgoGen) -> None:
        gen = _generate_algo(client_with_ag)
        algo_id = gen["id"]

        resp = client_with_ag.post(
            f"/api/algos/{algo_id}/backtest",
            json={"symbol": "GBPUSD", "bars": 500, "seed": 7},
        )
        assert resp.status_code == 200
        # Verify args were passed
        call = fake_ag.backtest_calls[0]
        assert call[1] == "GBPUSD"   # symbol
        assert call[2] == 500         # n_bars
        assert call[3] == 7           # seed

    def test_backtest_sandbox_failure_returns_422(self, broker: PaperBroker) -> None:
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        ag = FakeAlgoGen(backtest_exc=RuntimeError("sandbox security violation"))
        app = make_app(broker, algo_gen=ag)
        with TestClient(app, raise_server_exceptions=False) as c:
            resp_gen = c.post(
                "/api/algos/generate",
                json={"brief": "test", "effort": "fast"},
            )
            algo_id = resp_gen.json()["id"]
            resp = c.post(
                f"/api/algos/{algo_id}/backtest",
                json={"symbol": "EURUSD"},
            )
        assert resp.status_code == 422


# ---------------------------------------------------------------------------
# Tests: POST /api/algos/{algo_id}/promote
# ---------------------------------------------------------------------------

class TestPromote:

    def test_promote_happy_path(self, client_with_ag: TestClient) -> None:
        gen = _generate_algo(client_with_ag)
        algo_id = gen["id"]

        resp = client_with_ag.post(f"/api/algos/{algo_id}/promote")
        assert resp.status_code == 200
        data = resp.json()
        assert data["algo_id"] == algo_id
        assert data["status"] == "promoted"

    def test_promote_not_found(self, client_with_ag: TestClient) -> None:
        resp = client_with_ag.post("/api/algos/nonexistent-id/promote")
        assert resp.status_code == 404

    def test_promote_promotion_error_returns_422(self, broker: PaperBroker) -> None:
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        ag = FakeAlgoGen(promote_exc=PromotionError("Red tier cannot be promoted"))
        app = make_app(broker, algo_gen=ag)
        with TestClient(app, raise_server_exceptions=False) as c:
            resp_gen = c.post(
                "/api/algos/generate",
                json={"brief": "test", "effort": "fast"},
            )
            algo_id = resp_gen.json()["id"]
            resp = c.post(f"/api/algos/{algo_id}/promote")
        assert resp.status_code == 422

    def test_promote_updates_status(self, client_with_ag: TestClient) -> None:
        gen = _generate_algo(client_with_ag)
        algo_id = gen["id"]

        client_with_ag.post(f"/api/algos/{algo_id}/promote")

        # Verify status updated in list
        algos = client_with_ag.get("/api/algos").json()
        matching = [a for a in algos if a["id"] == algo_id]
        assert matching[0]["status"] == "promoted"


# ---------------------------------------------------------------------------
# Tests: POST /api/algos/{algo_id}/retire
# ---------------------------------------------------------------------------

class TestRetire:

    def test_retire_happy_path(self, client_with_ag: TestClient) -> None:
        gen = _generate_algo(client_with_ag)
        algo_id = gen["id"]

        resp = client_with_ag.post(f"/api/algos/{algo_id}/retire")
        assert resp.status_code == 200
        data = resp.json()
        assert data["algo_id"] == algo_id
        assert data["status"] == "retired"

    def test_retire_not_found(self, client_with_ag: TestClient) -> None:
        resp = client_with_ag.post("/api/algos/nonexistent-id/retire")
        assert resp.status_code == 404

    def test_retire_updates_status(self, client_with_ag: TestClient) -> None:
        gen = _generate_algo(client_with_ag)
        algo_id = gen["id"]

        client_with_ag.post(f"/api/algos/{algo_id}/retire")

        algos = client_with_ag.get("/api/algos").json()
        matching = [a for a in algos if a["id"] == algo_id]
        assert matching[0]["status"] == "retired"


# ---------------------------------------------------------------------------
# Tests: GET /api/algos/{algo_id}/manifest
# ---------------------------------------------------------------------------

class TestGetManifest:

    def test_get_manifest_happy_path(self, client_with_ag: TestClient) -> None:
        gen = _generate_algo(client_with_ag)
        algo_id = gen["id"]

        resp = client_with_ag.get(f"/api/algos/{algo_id}/manifest")
        assert resp.status_code == 200
        data = resp.json()
        assert data["name"] == "test-algo"
        assert data["schema_version"] == "1.0"
        assert "indicators" in data
        assert "entries" in data
        assert "exits" in data
        assert "risk" in data

    def test_get_manifest_not_found(self, client_with_ag: TestClient) -> None:
        resp = client_with_ag.get("/api/algos/nonexistent-id/manifest")
        assert resp.status_code == 404
        detail = resp.json().get("detail", resp.json())
        assert detail.get("error") == "not_found"


# ---------------------------------------------------------------------------
# Tests: algo_gen disabled (algo_gen=None) → 503
# ---------------------------------------------------------------------------

class TestAlgoGenDisabled:

    def test_list_returns_503(self, client_no_ag: TestClient) -> None:
        resp = client_no_ag.get("/api/algos")
        assert resp.status_code == 503
        detail = resp.json().get("detail", resp.json())
        assert detail.get("error") == "algo_gen_disabled"

    def test_generate_returns_503(self, client_no_ag: TestClient) -> None:
        resp = client_no_ag.post(
            "/api/algos/generate",
            json={"brief": "test", "effort": "fast"},
        )
        assert resp.status_code == 503
        detail = resp.json().get("detail", resp.json())
        assert detail.get("error") == "algo_gen_disabled"

    def test_stream_returns_503(self, client_no_ag: TestClient) -> None:
        resp = client_no_ag.post(
            "/api/algos/generate/stream",
            json={"brief": "test", "effort": "fast"},
        )
        assert resp.status_code == 503
        detail = resp.json().get("detail", resp.json())
        assert detail.get("error") == "algo_gen_disabled"

    def test_backtest_returns_503(self, client_no_ag: TestClient) -> None:
        resp = client_no_ag.post(
            "/api/algos/some-id/backtest",
            json={"symbol": "EURUSD"},
        )
        assert resp.status_code == 503

    def test_promote_returns_503(self, client_no_ag: TestClient) -> None:
        resp = client_no_ag.post("/api/algos/some-id/promote")
        assert resp.status_code == 503

    def test_retire_returns_503(self, client_no_ag: TestClient) -> None:
        resp = client_no_ag.post("/api/algos/some-id/retire")
        assert resp.status_code == 503

    def test_manifest_returns_503(self, client_no_ag: TestClient) -> None:
        resp = client_no_ag.get("/api/algos/some-id/manifest")
        assert resp.status_code == 503


# ---------------------------------------------------------------------------
# Tests: Bearer auth honored when AF_DASHBOARD_TOKEN set
# ---------------------------------------------------------------------------

class TestAuth:

    def test_generate_requires_token_when_set(self, broker: PaperBroker, fake_ag: FakeAlgoGen) -> None:
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        app = make_app(broker, algo_gen=fake_ag, token="test-secret")
        with TestClient(app, raise_server_exceptions=False) as c:
            # Without token → 401
            resp = c.post(
                "/api/algos/generate",
                json={"brief": "test", "effort": "fast"},
            )
            assert resp.status_code == 401

    def test_generate_succeeds_with_valid_bearer(self, broker: PaperBroker, fake_ag: FakeAlgoGen) -> None:
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        app = make_app(broker, algo_gen=fake_ag, token="test-secret")
        with TestClient(app, raise_server_exceptions=True) as c:
            resp = c.post(
                "/api/algos/generate",
                json={"brief": "test", "effort": "fast"},
                headers={"Authorization": "Bearer test-secret"},
            )
            assert resp.status_code == 200

    def test_list_requires_token_when_set(self, broker: PaperBroker, fake_ag: FakeAlgoGen) -> None:
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        app = make_app(broker, algo_gen=fake_ag, token="test-secret")
        with TestClient(app, raise_server_exceptions=False) as c:
            resp = c.get("/api/algos")
            assert resp.status_code == 401

    def test_auth_with_query_param(self, broker: PaperBroker, fake_ag: FakeAlgoGen) -> None:
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        app = make_app(broker, algo_gen=fake_ag, token="test-secret")
        with TestClient(app, raise_server_exceptions=True) as c:
            resp = c.get("/api/algos?token=test-secret")
            assert resp.status_code == 200

    def test_stream_auth_401_without_token(self, broker: PaperBroker, fake_ag: FakeAlgoGen) -> None:
        """401 is raised synchronously before SSE starts — TestClient is safe here."""
        from algoforge.dashboard.algo_gen_routes import _STORE
        _STORE.clear()
        app = make_app(broker, algo_gen=fake_ag, token="test-secret")
        with TestClient(app, raise_server_exceptions=False) as c:
            resp = c.post(
                "/api/algos/generate/stream",
                json={"brief": "test", "effort": "fast"},
            )
            assert resp.status_code == 401
