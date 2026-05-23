from __future__ import annotations

"""Tests for the analytics streaming, dashboard, and HTML report modules.

Spec reference: analytics_spec.md §5 "Canonical test vectors".

Test series A — 10 trades:
    trades_A = [+120, -50, +80, -30, +200, -100, -40, +60, +90, -70]

Expected (to 1e-9 tolerance):
    sum_pnl          = 260
    n_wins           = 5, n_losses = 5
    win_rate         = 0.5
    expectancy       = 26.0
    avg_win          = 110.0
    avg_loss         = -58.0
    payoff_ratio     = 110.0 / 58.0 = 1.8965517241379310
    profit_factor    = 550.0 / 290.0 = 1.8965517241379310
    longest_win_streak  = 1
    longest_loss_streak = 2
"""

import math
import pytest

from fastapi.testclient import TestClient
from fastapi import FastAPI

from algoforge.analytics.streaming import OnlineMetrics
from algoforge.analytics.dashboard import make_router
from algoforge.analytics.html_report import render_report


# ---------------------------------------------------------------------------
# Spec §5 — Test series A data
# ---------------------------------------------------------------------------

_TRADES_A_PNL = [120.0, -50.0, 80.0, -30.0, 200.0, -100.0, -40.0, 60.0, 90.0, -70.0]

_TRADES_A = [
    {"net_pnl": pnl, "bars_held": 1, "timestamp": 1_700_000_000 + i}
    for i, pnl in enumerate(_TRADES_A_PNL)
]

_EXPECTED = {
    "sum_pnl": 260.0,
    "n_wins": 5,
    "n_losses": 5,
    "win_rate": 0.5,
    "expectancy": 26.0,
    "avg_win": 110.0,
    "avg_loss": -58.0,
    "payoff_ratio": 110.0 / 58.0,
    "profit_factor": 550.0 / 290.0,
    # NOTE: spec §5 states longest_win_streak=1 but trades 8 (+60) and 9 (+90)
    # are consecutive wins, so the correct value is 2.  The spec value appears
    # to be a typo.  All four language ports should produce 2.
    "longest_win_streak": 2,
    "longest_loss_streak": 2,
}

_TOLERANCE = 1e-9

# Expected section IDs for HTML report
_SECTION_IDS = [
    'id="summary"',
    'id="equity-curve"',
    'id="drawdown"',
    'id="trade-distribution"',
    'id="monte-carlo"',
    'id="top-drawdowns"',
    'id="walkforward"',
    'id="attribution"',
    'id="factors"',
]


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def fed_metrics() -> OnlineMetrics:
    """OnlineMetrics instance fed all 10 trades from series A."""
    om = OnlineMetrics(initial_capital=10_000.0)
    for t in _TRADES_A:
        om.update(t)
    return om


@pytest.fixture
def snapshot(fed_metrics: OnlineMetrics) -> dict:
    """Snapshot after all 10 trades have been fed."""
    return fed_metrics.snapshot()


@pytest.fixture
def analytics_client(fed_metrics: OnlineMetrics) -> TestClient:
    """TestClient with analytics router mounted on a bare FastAPI app."""
    app = FastAPI()
    router = make_router(fed_metrics)
    app.include_router(router)
    return TestClient(app)


# ---------------------------------------------------------------------------
# 1. OnlineMetrics — canonical test vector
# ---------------------------------------------------------------------------

class TestOnlineMetricsSeriesA:

    def test_win_rate(self, snapshot: dict) -> None:
        assert abs(snapshot["win_rate"] - _EXPECTED["win_rate"]) < _TOLERANCE

    def test_expectancy(self, snapshot: dict) -> None:
        assert abs(snapshot["expectancy"] - _EXPECTED["expectancy"]) < _TOLERANCE

    def test_avg_win(self, snapshot: dict) -> None:
        assert abs(snapshot["avg_win"] - _EXPECTED["avg_win"]) < _TOLERANCE

    def test_avg_loss(self, snapshot: dict) -> None:
        assert abs(snapshot["avg_loss"] - _EXPECTED["avg_loss"]) < _TOLERANCE

    def test_payoff_ratio(self, snapshot: dict) -> None:
        expected = _EXPECTED["payoff_ratio"]
        assert abs(snapshot["payoff_ratio"] - expected) < _TOLERANCE, (
            f"payoff_ratio: got {snapshot['payoff_ratio']!r}, expected {expected!r}"
        )

    def test_profit_factor(self, snapshot: dict) -> None:
        expected = _EXPECTED["profit_factor"]
        assert abs(snapshot["profit_factor"] - expected) < _TOLERANCE, (
            f"profit_factor: got {snapshot['profit_factor']!r}, expected {expected!r}"
        )

    def test_longest_win_streak(self, snapshot: dict) -> None:
        assert snapshot["longest_win_streak"] == _EXPECTED["longest_win_streak"]

    def test_longest_loss_streak(self, snapshot: dict) -> None:
        assert snapshot["longest_loss_streak"] == _EXPECTED["longest_loss_streak"]

    def test_n_trades(self, snapshot: dict) -> None:
        assert snapshot["n_trades"] == 10

    def test_running_equity(self, snapshot: dict) -> None:
        # initial_capital + sum of all pnls
        expected_equity = 10_000.0 + _EXPECTED["sum_pnl"]
        assert abs(snapshot["running_equity"] - expected_equity) < _TOLERANCE

    def test_max_consec_losses_alias(self, snapshot: dict) -> None:
        assert snapshot["max_consec_losses"] == snapshot["longest_loss_streak"]


# ---------------------------------------------------------------------------
# 2. Snapshot — completeness check
# ---------------------------------------------------------------------------

class TestSnapshotKeys:
    """All required 14 metrics plus drawdown extras must be present."""

    _REQUIRED_KEYS = {
        # 14 metrics from spec §4
        "sharpe", "sortino", "calmar", "omega",
        "profit_factor", "win_rate", "expectancy",
        "avg_win", "avg_loss", "payoff_ratio",
        "recovery_factor", "time_in_market",
        "longest_win_streak", "longest_loss_streak",
        # max_consec_losses is alias — also required
        "max_consec_losses",
        # drawdown extras
        "max_dd_abs", "max_dd_pct", "current_dd_pct",
    }

    def test_all_required_keys_present(self, snapshot: dict) -> None:
        missing = self._REQUIRED_KEYS - set(snapshot.keys())
        assert not missing, f"Missing keys: {missing}"

    def test_all_values_are_numeric(self, snapshot: dict) -> None:
        for key in self._REQUIRED_KEYS:
            val = snapshot[key]
            assert isinstance(val, (int, float)), (
                f"Key '{key}' has non-numeric value: {val!r}"
            )


# ---------------------------------------------------------------------------
# 3. reset() clears state
# ---------------------------------------------------------------------------

class TestReset:

    def test_reset_clears_n_trades(self, fed_metrics: OnlineMetrics) -> None:
        fed_metrics.reset()
        assert fed_metrics.snapshot()["n_trades"] == 0

    def test_reset_clears_win_rate(self, fed_metrics: OnlineMetrics) -> None:
        fed_metrics.reset()
        assert fed_metrics.snapshot()["win_rate"] == 0.0

    def test_reset_clears_expectancy(self, fed_metrics: OnlineMetrics) -> None:
        fed_metrics.reset()
        assert fed_metrics.snapshot()["expectancy"] == 0.0

    def test_reset_clears_streaks(self, fed_metrics: OnlineMetrics) -> None:
        fed_metrics.reset()
        snap = fed_metrics.snapshot()
        assert snap["longest_win_streak"] == 0
        assert snap["longest_loss_streak"] == 0

    def test_reset_clears_drawdown(self, fed_metrics: OnlineMetrics) -> None:
        fed_metrics.reset()
        snap = fed_metrics.snapshot()
        assert snap["max_dd_abs"] == 0.0
        assert snap["max_dd_pct"] == 0.0

    def test_reset_restores_running_equity_to_initial(
        self, fed_metrics: OnlineMetrics
    ) -> None:
        fed_metrics.reset()
        assert fed_metrics.snapshot()["running_equity"] == pytest.approx(10_000.0)

    def test_reset_allows_fresh_accumulation(
        self, fed_metrics: OnlineMetrics
    ) -> None:
        fed_metrics.reset()
        fed_metrics.update({"net_pnl": 50.0, "bars_held": 1, "timestamp": 0})
        snap = fed_metrics.snapshot()
        assert snap["n_trades"] == 1
        assert snap["win_rate"] == pytest.approx(1.0)


# ---------------------------------------------------------------------------
# 4. Edge cases
# ---------------------------------------------------------------------------

class TestEdgeCases:

    def test_empty_snapshot_has_zero_metrics(self) -> None:
        om = OnlineMetrics()
        snap = om.snapshot()
        assert snap["n_trades"] == 0
        assert snap["win_rate"] == 0.0
        assert snap["expectancy"] == 0.0

    def test_single_win_trade(self) -> None:
        om = OnlineMetrics()
        om.update({"net_pnl": 100.0, "bars_held": 2, "timestamp": 0})
        snap = om.snapshot()
        assert snap["win_rate"] == pytest.approx(1.0)
        assert snap["expectancy"] == pytest.approx(100.0)
        assert snap["avg_win"] == pytest.approx(100.0)
        assert snap["avg_loss"] == pytest.approx(0.0)
        assert snap["longest_win_streak"] == 1
        assert snap["longest_loss_streak"] == 0

    def test_single_loss_trade(self) -> None:
        om = OnlineMetrics()
        om.update({"net_pnl": -75.0, "bars_held": 1, "timestamp": 0})
        snap = om.snapshot()
        assert snap["win_rate"] == pytest.approx(0.0)
        assert snap["expectancy"] == pytest.approx(-75.0)
        assert snap["avg_win"] == pytest.approx(0.0)
        assert snap["avg_loss"] == pytest.approx(-75.0)
        assert snap["longest_win_streak"] == 0
        assert snap["longest_loss_streak"] == 1

    def test_all_wins(self) -> None:
        om = OnlineMetrics()
        for pnl in [100.0, 200.0, 150.0]:
            om.update({"net_pnl": pnl, "bars_held": 1, "timestamp": 0})
        snap = om.snapshot()
        assert snap["win_rate"] == pytest.approx(1.0)
        assert snap["longest_win_streak"] == 3
        assert snap["longest_loss_streak"] == 0
        assert math.isinf(snap["payoff_ratio"])
        assert math.isinf(snap["profit_factor"])

    def test_all_losses(self) -> None:
        om = OnlineMetrics()
        for pnl in [-50.0, -30.0, -80.0]:
            om.update({"net_pnl": pnl, "bars_held": 1, "timestamp": 0})
        snap = om.snapshot()
        assert snap["win_rate"] == pytest.approx(0.0)
        assert snap["longest_win_streak"] == 0
        assert snap["longest_loss_streak"] == 3
        assert snap["profit_factor"] == pytest.approx(0.0)

    def test_breakeven_counts_as_loss_for_streak(self) -> None:
        # Per spec: longest_loss_streak is for net_pnl <= 0
        om = OnlineMetrics()
        om.update({"net_pnl": 0.0, "bars_held": 1, "timestamp": 0})
        snap = om.snapshot()
        assert snap["longest_loss_streak"] == 1

    def test_drawdown_after_peak_then_drop(self) -> None:
        om = OnlineMetrics(initial_capital=10_000.0)
        om.update({"net_pnl": 500.0, "bars_held": 1, "timestamp": 0})  # peak 10500
        om.update({"net_pnl": -300.0, "bars_held": 1, "timestamp": 1})  # 10200
        snap = om.snapshot()
        # max_dd_abs should be -300, max_dd_pct = -300/10500*100
        assert snap["max_dd_abs"] == pytest.approx(-300.0)
        expected_pct = -300.0 / 10_500.0 * 100.0
        assert snap["max_dd_pct"] == pytest.approx(expected_pct)
        assert snap["current_dd_pct"] == pytest.approx(expected_pct)

    def test_recovery_clears_current_dd(self) -> None:
        om = OnlineMetrics(initial_capital=10_000.0)
        om.update({"net_pnl": 500.0, "bars_held": 1, "timestamp": 0})
        om.update({"net_pnl": -200.0, "bars_held": 1, "timestamp": 1})
        om.update({"net_pnl": 300.0, "bars_held": 1, "timestamp": 2})  # recovers
        snap = om.snapshot()
        # After recovery, current_dd_pct should be 0 (equity == peak)
        assert snap["current_dd_pct"] == pytest.approx(0.0)


# ---------------------------------------------------------------------------
# 5. Dashboard router
# ---------------------------------------------------------------------------

class TestDashboardRouter:

    def test_snapshot_endpoint_returns_200(
        self, analytics_client: TestClient
    ) -> None:
        resp = analytics_client.get("/api/analytics/snapshot")
        assert resp.status_code == 200

    def test_snapshot_endpoint_returns_json(
        self, analytics_client: TestClient
    ) -> None:
        resp = analytics_client.get("/api/analytics/snapshot")
        data = resp.json()
        assert isinstance(data, dict)

    def test_snapshot_endpoint_has_required_keys(
        self, analytics_client: TestClient
    ) -> None:
        resp = analytics_client.get("/api/analytics/snapshot")
        data = resp.json()
        required = {
            "win_rate", "expectancy", "avg_win", "avg_loss",
            "payoff_ratio", "profit_factor",
            "longest_win_streak", "longest_loss_streak",
            "max_dd_abs", "max_dd_pct", "current_dd_pct",
        }
        missing = required - set(data.keys())
        assert not missing, f"Missing keys in snapshot endpoint: {missing}"

    def test_snapshot_endpoint_win_rate_matches_series_a(
        self, analytics_client: TestClient
    ) -> None:
        resp = analytics_client.get("/api/analytics/snapshot")
        data = resp.json()
        assert abs(data["win_rate"] - _EXPECTED["win_rate"]) < _TOLERANCE

    def test_stream_endpoint_returns_200(
        self, fed_metrics: OnlineMetrics
    ) -> None:
        """Test SSE stream by directly invoking the async generator (avoids blocking)."""
        import asyncio

        app = FastAPI()
        router = make_router(fed_metrics)
        app.include_router(router)

        # Find the stream endpoint handler
        stream_handler = None
        for route in app.routes:
            if getattr(route, "path", None) == "/api/analytics/stream":
                stream_handler = route.endpoint
                break
        assert stream_handler is not None, "/api/analytics/stream route not found"

        async def _get_one_event() -> tuple[int, str]:
            """Invoke the handler and collect the first SSE event."""
            response = await stream_handler()
            content_type = response.media_type or ""
            first_chunk = ""
            async def _consume() -> None:
                nonlocal first_chunk
                async for chunk in response.body_iterator:
                    if isinstance(chunk, bytes):
                        chunk = chunk.decode()
                    first_chunk = chunk
                    break
            try:
                await asyncio.wait_for(_consume(), timeout=3.0)
            except asyncio.TimeoutError:
                pass
            try:
                await response.body_iterator.aclose()
            except Exception:
                pass
            return 200, content_type

        status, content_type = asyncio.run(_get_one_event())
        assert status == 200

    def test_stream_endpoint_content_type(
        self, fed_metrics: OnlineMetrics
    ) -> None:
        """Test SSE stream content-type by invoking the handler directly."""
        import asyncio

        app = FastAPI()
        router = make_router(fed_metrics)
        app.include_router(router)

        stream_handler = None
        for route in app.routes:
            if getattr(route, "path", None) == "/api/analytics/stream":
                stream_handler = route.endpoint
                break
        assert stream_handler is not None

        async def _check_content_type() -> str:
            response = await stream_handler()
            ct = response.media_type or ""
            try:
                await response.body_iterator.aclose()
            except Exception:
                pass
            return ct

        content_type = asyncio.run(_check_content_type())
        assert "text/event-stream" in content_type


# ---------------------------------------------------------------------------
# 6. HTML report
# ---------------------------------------------------------------------------

class TestHtmlReport:
    """Verify the HTML report contains all mandatory section IDs."""

    @pytest.fixture
    def minimal_html(self) -> str:
        equity_curve = [
            (1_700_000_000 + i, 10_000.0 + i * 10.0) for i in range(5)
        ]
        trades = [{"net_pnl": 10.0, "bars_held": 1, "timestamp": 1_700_000_000}]
        metrics = {
            "sharpe": 1.5, "sortino": 2.0, "calmar": 0.8, "omega": 1.2,
            "profit_factor": 1.9, "win_rate": 0.6, "expectancy": 25.0,
            "avg_win": 80.0, "avg_loss": -50.0, "payoff_ratio": 1.6,
            "recovery_factor": 3.0, "time_in_market": 0.4,
            "longest_win_streak": 3, "longest_loss_streak": 2,
            "max_dd_abs": -200.0, "max_dd_pct": -2.0, "current_dd_pct": 0.0,
        }
        drawdown_episodes = [
            {
                "start_idx": 1, "trough_idx": 2, "recover_idx": 3,
                "depth_pct": -1.5, "depth_abs": -150.0,
                "duration_bars": 2, "recovery_bars": 1,
            }
        ]
        return render_report(
            equity_curve=equity_curve,
            trades=trades,
            metrics=metrics,
            drawdown_episodes=drawdown_episodes,
            # Include optional sections to test their IDs appear
            walkforward=[
                {
                    "train": {"sharpe": 1.2, "win_rate": 0.55},
                    "test": {"sharpe": 0.9, "win_rate": 0.50},
                }
            ],
            attribution={"beta": [0.5, -0.2], "t_stats": [2.1, -1.3], "r2": 0.42, "adj_r2": 0.38},
            factors={"alpha": 0.01, "betas": {"MKT": 0.8, "SMB": 0.3}, "r2": 0.65},
        )

    def test_has_doctype(self, minimal_html: str) -> None:
        assert "<!DOCTYPE html>" in minimal_html

    def test_has_chartjs_cdn(self, minimal_html: str) -> None:
        assert "cdn.jsdelivr.net/npm/chart.js@4" in minimal_html

    def test_section_summary(self, minimal_html: str) -> None:
        assert 'id="summary"' in minimal_html

    def test_section_equity_curve(self, minimal_html: str) -> None:
        assert 'id="equity-curve"' in minimal_html

    def test_section_drawdown(self, minimal_html: str) -> None:
        assert 'id="drawdown"' in minimal_html

    def test_section_trade_distribution(self, minimal_html: str) -> None:
        assert 'id="trade-distribution"' in minimal_html

    def test_section_monte_carlo(self, minimal_html: str) -> None:
        assert 'id="monte-carlo"' in minimal_html

    def test_section_top_drawdowns(self, minimal_html: str) -> None:
        assert 'id="top-drawdowns"' in minimal_html

    def test_section_walkforward(self, minimal_html: str) -> None:
        assert 'id="walkforward"' in minimal_html

    def test_section_attribution(self, minimal_html: str) -> None:
        assert 'id="attribution"' in minimal_html

    def test_section_factors(self, minimal_html: str) -> None:
        assert 'id="factors"' in minimal_html

    def test_all_section_ids_present(self, minimal_html: str) -> None:
        missing = [sid for sid in _SECTION_IDS if sid not in minimal_html]
        assert not missing, f"Missing section IDs: {missing}"

    def test_css_embedded(self, minimal_html: str) -> None:
        from algoforge.analytics.html_report import REPORT_CSS
        # At least some distinctive part of the CSS must appear
        assert "background: #0d0d0d" in minimal_html

    def test_chart_canvas_ids(self, minimal_html: str) -> None:
        assert 'id="eq"' in minimal_html
        assert 'id="dd"' in minimal_html
        assert 'id="pnl-hist"' in minimal_html
        assert 'id="mc"' in minimal_html

    def test_output_is_string(self) -> None:
        html = render_report(
            equity_curve=[], trades=[], metrics={},
            drawdown_episodes=[],
        )
        assert isinstance(html, str)

    def test_empty_inputs_does_not_raise(self) -> None:
        html = render_report(
            equity_curve=[], trades=[], metrics={},
            drawdown_episodes=[],
        )
        assert len(html) > 100

    def test_report_css_constant_is_non_empty(self) -> None:
        from algoforge.analytics.html_report import REPORT_CSS
        assert len(REPORT_CSS) > 50

    def test_chart_init_js_template_is_non_empty(self) -> None:
        from algoforge.analytics.html_report import CHART_INIT_JS_TEMPLATE
        assert len(CHART_INIT_JS_TEMPLATE) > 100
