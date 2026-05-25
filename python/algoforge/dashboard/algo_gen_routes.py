"""FastAPI router for /api/algos/* endpoints.

Wired in by make_app(..., algo_gen=...).  When algo_gen is None every
route returns 503 {"error": "algo_gen_disabled"}.

Error → HTTP mapping (mirroring llm_routes pattern):
  GenerationError (LLM timeout)  → 504
  GenerationError (unreachable)  → 502
  schema_invalid / ValidationError → 422
  sandbox_failed                  → 422
  not_found                       → 404
  algo_gen disabled               → 503

Public API:
    AlgoGenFacade   — wraps algo_gen functions for the router
    make_algo_gen_router(algo_gen) -> APIRouter
"""
from __future__ import annotations

import asyncio
import dataclasses
import json
import os
import tempfile
import uuid
from typing import Any, AsyncGenerator

from fastapi import APIRouter, HTTPException
from fastapi.responses import StreamingResponse
from pydantic import BaseModel

from algoforge.algo_gen.generator import GenerationError, generate_fast, generate_balanced, generate_max
from algoforge.algo_gen.promote import PromotionError, promote as _promote_fn
from algoforge.algo_gen.validator import validate as _validate_fn
from algoforge.algo_gen.schema import validate_manifest as _validate_manifest_fn
from algoforge.algo_gen.types import TierReport, ValidationResult, AlgoManifest
from algoforge.backtest import BTConfig, BacktestEngine
from algoforge.algo_gen.runtime import ManifestAlgo
from algoforge.logger import get_logger

log = get_logger("dashboard.algo_gen")


# ---------------------------------------------------------------------------
# AlgoGenFacade — high-level wrapper for the router
# ---------------------------------------------------------------------------

class AlgoGenFacade:
    """Wraps the algo_gen module functions for use by the dashboard router.

    Parameters
    ----------
    llm_provider:
        LLMProvider instance (Ollama or compatible).
    registry_dir:
        Directory where promoted manifests are written.
    bars:
        Pre-loaded bars for validation/backtest (list[Bar]).
    """

    def __init__(
        self,
        llm_provider: Any,
        registry_dir: str | None = None,
        bars: list | None = None,
    ) -> None:
        self._llm = llm_provider
        self._registry_dir = registry_dir or tempfile.mkdtemp(prefix="af_registry_")
        self._bars = bars or []

    def generate(self, brief: str, effort: str, seed: int) -> tuple[AlgoManifest, Any]:
        """Generate a manifest using the specified effort level."""
        effort = effort.lower()
        if effort == "fast":
            return generate_fast(brief, self._llm, seed=seed)
        elif effort == "balanced":
            return generate_balanced(brief, self._llm, seed=seed)
        elif effort == "max":
            return generate_max(brief, self._llm, seed=seed)
        else:
            raise ValueError(f"Unknown effort level: {effort!r}. Must be 'fast', 'balanced', or 'max'.")

    def validate(self, manifest: AlgoManifest, seed: int) -> ValidationResult:
        """Run the 5-stage validation pipeline on a manifest."""
        import dataclasses as _dc
        manifest_dict = _dc.asdict(manifest)
        # Normalise nested dataclasses
        manifest_dict["indicators"] = [
            {"id": ind["id"], "kind": ind["kind"], "params": ind["params"]}
            for ind in manifest_dict["indicators"]
        ]
        manifest_dict["entries"] = [
            {"side": e["side"], "when": e["when"]}
            for e in manifest_dict["entries"]
        ]
        manifest_dict["exits"] = [
            {k: v for k, v in ex.items() if v is not None}
            for ex in manifest_dict["exits"]
        ]
        return _validate_fn(manifest_dict, self._bars, seed)

    def backtest(
        self,
        manifest: AlgoManifest,
        *,
        symbol: str = "EURUSD",
        n_bars: int | None = None,
        seed: int = 42,
    ) -> dict[str, Any]:
        """Run a paper backtest and return equity curve + metrics dict."""
        bars = self._bars
        if n_bars is not None and bars:
            bars = bars[:n_bars]

        if not bars:
            raise ValueError("No bar data available for backtest")

        algo = ManifestAlgo(manifest)
        cfg = BTConfig(initial_balance=10_000.0)
        engine = BacktestEngine(algo, cfg)
        result = engine.run(bars, symbol=symbol, tf=3600)

        return {
            "equity_curve": list(result.equity_curve),
            "metrics": {
                "total_trades": result.total_trades,
                "net_profit": round(result.net_profit, 4),
                "sharpe": round(result.sharpe_ratio, 4),
                "max_drawdown_pct": round(result.max_drawdown_pct, 4),
                "win_rate": round(result.win_rate, 4) if hasattr(result, "win_rate") else None,
            },
        }

    def promote(self, tier: TierReport, manifest: AlgoManifest) -> Any:
        """Promote manifest to the active registry."""
        return _promote_fn(tier, manifest, self._registry_dir, confirm=True)

    def retire(self, manifest: AlgoManifest) -> None:
        """Retire a manifest from the active registry (set is_enabled=false)."""
        import json as _json
        fname = os.path.join(self._registry_dir, f"{manifest.name}.json")
        if not os.path.exists(fname):
            # Not in registry yet — treat as no-op
            return
        with open(fname) as f:
            d = _json.load(f)
        d["is_enabled"] = False
        with open(fname, "w") as f:
            _json.dump(d, f, indent=2)

# ---------------------------------------------------------------------------
# In-memory store for generated manifests
# ---------------------------------------------------------------------------

_STORE: dict[str, dict[str, Any]] = {}  # algo_id -> record


def _new_record(
    manifest: Any,
    validation: ValidationResult | None,
    tier: TierReport | None,
) -> dict[str, Any]:
    algo_id = str(uuid.uuid4())
    record: dict[str, Any] = {
        "id": algo_id,
        "manifest": manifest,
        "validation": validation,
        "tier": tier,
        "status": "generated",
    }
    _STORE[algo_id] = record
    return record


def _manifest_to_dict(manifest: Any) -> dict[str, Any]:
    """Convert AlgoManifest dataclass to serialisable dict."""
    if dataclasses.is_dataclass(manifest):
        d = dataclasses.asdict(manifest)
        return d
    return dict(manifest)


def _validation_to_dict(vr: ValidationResult) -> dict[str, Any]:
    return dataclasses.asdict(vr)


def _tier_to_dict(tr: TierReport) -> dict[str, Any]:
    d = dataclasses.asdict(tr)
    # TierName enum → string
    if hasattr(d.get("tier"), "value"):
        d["tier"] = d["tier"].value
    return d


# ---------------------------------------------------------------------------
# Request / response models
# ---------------------------------------------------------------------------

class GenerateBody(BaseModel):
    brief: str
    effort: str = "fast"   # "fast" | "balanced" | "max"
    seed: int | None = None


class BacktestBody(BaseModel):
    symbol: str = "EURUSD"
    bars: int | None = None
    seed: int | None = None


# ---------------------------------------------------------------------------
# Router factory
# ---------------------------------------------------------------------------

def make_algo_gen_router(algo_gen: Any) -> APIRouter:
    """Build and return the /api/algos router.

    Parameters
    ----------
    algo_gen:
        Object exposing the algo_gen API.  Must have attributes:
          - generate_fast / generate_balanced / generate_max
          - validate (optional)
          - promote (callable)
          - load_bars (callable)
        Pass an _AlgoGenFacade instance from make_app.
    """
    router = APIRouter(prefix="/api/algos", tags=["algo_gen"])

    # ------------------------------------------------------------------ GET /
    @router.get("")
    def list_algos() -> list[dict[str, Any]]:
        """List all generated manifests (id, name, tier, status)."""
        result = []
        for rec in _STORE.values():
            m = rec["manifest"]
            t = rec["tier"]
            result.append({
                "id": rec["id"],
                "name": getattr(m, "name", None) if m else None,
                "tier": (t.tier.value if t else None),
                "status": rec["status"],
            })
        return result

    # ---------------------------------------------------- POST /generate
    @router.post("/generate")
    def generate(body: GenerateBody) -> dict[str, Any]:
        """Non-streaming generation. Returns {manifest, validation, tier}."""
        seed = body.seed if body.seed is not None else 42
        try:
            manifest, _trace = algo_gen.generate(body.brief, body.effort, seed)
        except GenerationError as exc:
            msg = str(exc).lower()
            if "timeout" in msg:
                raise HTTPException(status_code=504, detail={"error": "llm_timeout", "detail": str(exc)})
            if "unreachable" in msg or "connection" in msg:
                raise HTTPException(status_code=502, detail={"error": "llm_unreachable", "detail": str(exc)})
            raise HTTPException(status_code=422, detail={"error": "schema_invalid", "detail": str(exc)})
        except ValueError as exc:
            raise HTTPException(status_code=422, detail={"error": "schema_invalid", "detail": str(exc)})

        # Run validation
        validation, tier = _run_validate(algo_gen, manifest, seed)

        record = _new_record(manifest, validation, tier)
        return {
            "id": record["id"],
            "manifest": _manifest_to_dict(manifest),
            "validation": _validation_to_dict(validation) if validation else None,
            "tier": _tier_to_dict(tier) if tier else None,
        }

    # ------------------------------------------------ POST /generate/stream
    @router.post("/generate/stream")
    async def generate_stream(body: GenerateBody) -> StreamingResponse:
        """SSE streaming generation.

        Emits:
          data: {"stage":"prompt"}
          data: {"stage":"manifest","manifest":{...}}
          data: {"stage":"validation","result":{...}}
          data: {"stage":"tier","tier":{...}}
          data: [DONE]
        """
        seed = body.seed if body.seed is not None else 42

        async def _sse() -> AsyncGenerator[str, None]:
            # Stage: prompt
            yield f"data: {json.dumps({'stage': 'prompt'})}\n\n"

            # Stage: manifest (run in thread to avoid blocking event loop)
            try:
                manifest, _trace = await asyncio.to_thread(
                    algo_gen.generate, body.brief, body.effort, seed
                )
            except GenerationError as exc:
                err = json.dumps({"stage": "error", "error": "generation_failed", "detail": str(exc)})
                yield f"data: {err}\n\n"
                return
            except Exception as exc:
                err = json.dumps({"stage": "error", "error": "internal", "detail": str(exc)})
                yield f"data: {err}\n\n"
                return

            manifest_dict = _manifest_to_dict(manifest)
            yield f"data: {json.dumps({'stage': 'manifest', 'manifest': manifest_dict})}\n\n"

            # Stage: validation
            validation, tier = await asyncio.to_thread(_run_validate, algo_gen, manifest, seed)
            if validation:
                val_dict = _validation_to_dict(validation)
                yield f"data: {json.dumps({'stage': 'validation', 'result': val_dict})}\n\n"

            # Stage: tier
            if tier:
                tier_dict = _tier_to_dict(tier)
                yield f"data: {json.dumps({'stage': 'tier', 'tier': tier_dict})}\n\n"

            # Store in registry
            _new_record(manifest, validation, tier)

            yield "data: [DONE]\n\n"

        return StreamingResponse(
            _sse(),
            media_type="text/event-stream",
            headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"},
        )

    # ------------------------------------------------ POST /{algo_id}/backtest
    @router.post("/{algo_id}/backtest")
    def backtest(algo_id: str, body: BacktestBody) -> dict[str, Any]:
        """Run a paper backtest on a registered algo. Returns equity curve + metrics."""
        rec = _STORE.get(algo_id)
        if rec is None:
            raise HTTPException(status_code=404, detail={"error": "not_found", "detail": f"algo_id={algo_id}"})

        manifest = rec["manifest"]
        seed = body.seed if body.seed is not None else 42

        try:
            result = algo_gen.backtest(manifest, symbol=body.symbol, n_bars=body.bars, seed=seed)
        except Exception as exc:
            msg = str(exc).lower()
            if "sandbox" in msg or "security" in msg:
                raise HTTPException(status_code=422, detail={"error": "sandbox_failed", "detail": str(exc)})
            raise HTTPException(status_code=422, detail={"error": "backtest_failed", "detail": str(exc)})

        return {
            "algo_id": algo_id,
            "symbol": body.symbol,
            "equity_curve": result.get("equity_curve", []),
            "metrics": result.get("metrics", {}),
        }

    # ------------------------------------------------- POST /{algo_id}/promote
    @router.post("/{algo_id}/promote")
    def promote_algo(algo_id: str) -> dict[str, Any]:
        """Promote a registered manifest to the active registry."""
        rec = _STORE.get(algo_id)
        if rec is None:
            raise HTTPException(status_code=404, detail={"error": "not_found", "detail": f"algo_id={algo_id}"})

        manifest = rec["manifest"]
        tier = rec["tier"]

        if tier is None:
            raise HTTPException(
                status_code=422,
                detail={"error": "schema_invalid", "detail": "No tier report; run generate first"},
            )

        try:
            algo_gen.promote(tier, manifest)
        except PromotionError as exc:
            raise HTTPException(status_code=422, detail={"error": "schema_invalid", "detail": str(exc)})
        except Exception as exc:
            raise HTTPException(status_code=500, detail={"error": "internal", "detail": str(exc)})

        rec["status"] = "promoted"
        return {"algo_id": algo_id, "status": "promoted"}

    # -------------------------------------------------- POST /{algo_id}/retire
    @router.post("/{algo_id}/retire")
    def retire_algo(algo_id: str) -> dict[str, Any]:
        """Retire an algorithm from the active registry."""
        rec = _STORE.get(algo_id)
        if rec is None:
            raise HTTPException(status_code=404, detail={"error": "not_found", "detail": f"algo_id={algo_id}"})

        try:
            algo_gen.retire(rec["manifest"])
        except Exception as exc:
            raise HTTPException(status_code=500, detail={"error": "internal", "detail": str(exc)})

        rec["status"] = "retired"
        return {"algo_id": algo_id, "status": "retired"}

    # -------------------------------------------- GET /{algo_id}/manifest
    @router.get("/{algo_id}/manifest")
    def get_manifest(algo_id: str) -> dict[str, Any]:
        """Return the full manifest JSON for a registered algo."""
        rec = _STORE.get(algo_id)
        if rec is None:
            raise HTTPException(status_code=404, detail={"error": "not_found", "detail": f"algo_id={algo_id}"})
        return _manifest_to_dict(rec["manifest"])

    return router


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _run_validate(
    algo_gen: Any,
    manifest: Any,
    seed: int,
) -> tuple[ValidationResult | None, TierReport | None]:
    """Run validation if algo_gen supports it. Returns (ValidationResult, TierReport)."""
    try:
        validation: ValidationResult = algo_gen.validate(manifest, seed)
        tier = validation.report if validation.passed else None
        return validation, tier
    except Exception as exc:
        log.warning(f"Validation skipped: {exc}")
        return None, None
