"""Canonical fixture loader for algo_gen tests.

Mirrors algoforge.llm.fixtures patterns.

Fixtures live in: tests/fixtures/algo_gen/ (relative to repo root).

Public API:
    load_bars(fixture_name: str) -> list[Bar]
    load_manifest(fixture_name: str) -> dict
    load_expected_tier_report(fixture_name: str) -> dict

    # LLM generation fixtures (Agent 2 additions)
    load_llm_response(fixture_name: str) -> dict | list[dict]
    MultiTurnMockTransport — plays back ordered {request, response} turns
"""
from __future__ import annotations

import json
import os
from typing import Any

from ..types import Bar

# Path to fixtures directory (relative to repo root)
def _fixture_dir() -> str:
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__))
    )))
    return os.path.join(repo_root, "tests", "fixtures", "algo_gen")


def _load_json(path: str) -> Any:
    """Load a JSON file and return its content."""
    with open(path, "r") as f:
        return json.load(f)


def load_bars(fixture_name: str) -> list[Bar]:
    """Load a bar fixture and return a list of Bar objects.

    Parameters
    ----------
    fixture_name:
        Fixture file name without path, e.g. ``"bars_eurusd_h1_2y.json"``.
    """
    path = os.path.join(_fixture_dir(), fixture_name)
    data = _load_json(path)

    if isinstance(data, dict) and "bars" in data:
        raw_bars = data["bars"]
    else:
        raw_bars = data

    return [
        Bar(
            timestamp = b["timestamp"],
            open      = b["open"],
            high      = b["high"],
            low       = b["low"],
            close     = b["close"],
            volume    = b["volume"],
            spread    = b.get("spread", 0.0),
        )
        for b in raw_bars
    ]


def load_manifest(fixture_name: str) -> dict:
    """Load a manifest fixture and return the raw dict.

    Parameters
    ----------
    fixture_name:
        Fixture file name, e.g. ``"manifest_trend_follow_green.json"``.
    """
    path = os.path.join(_fixture_dir(), fixture_name)
    return _load_json(path)


def load_expected_tier_report(fixture_name: str) -> dict:
    """Load an expected TierReport fixture.

    Parameters
    ----------
    fixture_name:
        Fixture file name, e.g. ``"tier_report_trend_follow_green.json"``.
    """
    path = os.path.join(_fixture_dir(), fixture_name)
    return _load_json(path)


# ── LLM response fixtures (Agent 2 additions) ─────────────────────────────────

def load_llm_response(fixture_name: str) -> Any:
    """Load a canned LLM generation transcript.

    Fixture format:
      - Single-turn (fast mode): a dict with keys ``request`` and ``response``.
      - Multi-turn (balanced/max modes): a list of dicts, each with keys
        ``request`` and ``response``.  The ``MultiTurnMockTransport`` plays
        them back in order.

    Parameters
    ----------
    fixture_name:
        File name, e.g. ``"llm_response_trend_fast.json"``.
        The ``.json`` suffix is optional.
    """
    if not fixture_name.endswith(".json"):
        fixture_name = fixture_name + ".json"
    path = os.path.join(_fixture_dir(), fixture_name)
    if not os.path.exists(path):
        raise FileNotFoundError(f"LLM response fixture not found: {path}")
    return _load_json(path)


class MultiTurnMockTransport:
    """Replays an ordered list of {request, response} turns for multi-call tests.

    Mirrors the interface of ``algoforge.llm.fixtures.MockTransport`` but
    supports ordered multi-turn transcripts.  The fixture file may be either:

      - A *list* of ``{"request": {...}, "response": {...}}`` dicts — played
        back in order across successive ``.request()`` calls.
      - A single ``{"request": {...}, "response": {...}}`` dict — treated as a
        one-turn transcript (delegates to the same replay logic).

    After all turns are exhausted, further calls raise ``StopIteration``.

    Parameters
    ----------
    fixture:
        The parsed JSON fixture (list or single dict).
    """

    def __init__(self, fixture: Any) -> None:
        # Normalise to a list
        if isinstance(fixture, dict):
            turns = [fixture]
        else:
            turns = list(fixture)

        # Pre-encode each response body so .request() just pops and returns
        self._turns: list[tuple[int, bytes]] = []
        for turn in turns:
            resp = turn["response"]
            status: int = resp["status"]
            resp_body = resp["body"]
            if isinstance(resp_body, list):
                raw = b"\n".join(
                    json.dumps(chunk).encode("utf-8") for chunk in resp_body
                )
            else:
                raw = json.dumps(resp_body).encode("utf-8")
            self._turns.append((status, raw))

        self._index = 0
        self.requests_made: list[dict] = []

    def request(
        self,
        method: str,
        url: str,
        body: bytes | None,
        headers: dict[str, str],
        timeout: float,
    ) -> tuple[int, bytes]:
        """Return the next pre-encoded response from the transcript."""
        parsed_body: Any = None
        if body is not None:
            try:
                parsed_body = json.loads(body)
            except Exception:
                parsed_body = body.decode("utf-8", errors="replace")

        self.requests_made.append({
            "method": method,
            "url": url,
            "body": parsed_body,
        })

        if self._index >= len(self._turns):
            raise StopIteration(
                f"MultiTurnMockTransport exhausted after {len(self._turns)} turn(s)"
            )

        status, raw = self._turns[self._index]
        self._index += 1
        return status, raw

    @property
    def turns_remaining(self) -> int:
        return len(self._turns) - self._index

    @property
    def turns_consumed(self) -> int:
        return self._index
