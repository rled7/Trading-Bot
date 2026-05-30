"""Adapters for the legacy non-REST backends so they construct via the registry.

Kept separate from registry.py purely to isolate the optional-dependency
import handling for MT5.
"""
from __future__ import annotations

from ..broker import IBroker


def make_paper(balance: float = 10_000.0) -> IBroker:
    from ..paper_broker import PaperBroker
    return PaperBroker(balance=balance)


def make_mt5() -> IBroker:
    try:
        from ..mt5_broker import MT5Broker  # type: ignore[import]
    except ImportError as exc:  # pragma: no cover - depends on optional extra
        raise ImportError(
            "MT5Broker requires the 'live' optional dependency group. "
            "Install with: pip install algoforge[live]"
        ) from exc
    return MT5Broker()
