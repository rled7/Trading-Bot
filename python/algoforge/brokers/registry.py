"""Broker registry — single factory entry point for constructing brokers.

``make_broker("oanda", paper=True)`` resolves credentials from the environment
(``AF_OANDA_*``) and returns a connected-capable adapter. The legacy "paper"
and "mt5" backends are routed here too so callers have one construction path.

Adapter modules are imported lazily so that, e.g., selecting "paper" never
imports the live REST adapters (and MT5's optional 'live' dependency stays
optional).
"""
from __future__ import annotations

from typing import Any, Callable, Optional

from ..broker import IBroker
from .config import BrokerConfig

# name -> "module:ClassName" for lazy import of the RestBroker adapters.
_REST_ADAPTERS: dict[str, str] = {
    "oanda":    "algoforge.brokers.oanda:OandaBroker",
    "alpaca":   "algoforge.brokers.alpaca:AlpacaBroker",
    "ibkr":     "algoforge.brokers.ibkr:IbkrBroker",
    "binance":  "algoforge.brokers.binance:BinanceBroker",
    "coinbase": "algoforge.brokers.coinbase:CoinbaseBroker",
}

#: legacy non-REST backends, kept for one unified construction path
_LEGACY = ("paper", "mt5")


def available_brokers() -> list[str]:
    """All selectable broker names, including legacy paper/mt5."""
    return list(_LEGACY) + list(_REST_ADAPTERS)


def _load(spec: str) -> type:
    module_name, _, cls_name = spec.partition(":")
    import importlib
    module = importlib.import_module(module_name)
    return getattr(module, cls_name)


def make_broker(name: str, *, paper: bool = True,
                config: Optional[BrokerConfig] = None,
                environ: Optional[dict[str, str]] = None,
                balance: float = 10_000.0) -> IBroker:
    """Construct a broker by name.

    Parameters
    ----------
    name:    one of ``available_brokers()`` (case-insensitive).
    paper:   default paper/live mode for REST adapters (env may override).
    config:  pre-built BrokerConfig; if omitted, loaded from ``AF_<NAME>_*``.
    balance: starting balance for the legacy paper broker.
    """
    key = name.strip().lower()

    if key == "paper":
        from .paper_compat import make_paper
        return make_paper(balance)
    if key == "mt5":
        from .paper_compat import make_mt5
        return make_mt5()

    if key not in _REST_ADAPTERS:
        choices = ", ".join(available_brokers())
        raise ValueError(f"unknown broker {name!r}; choose one of: {choices}")

    cls = _load(_REST_ADAPTERS[key])
    cfg = config or BrokerConfig.from_env(key, paper=paper, environ=environ)
    return cls(cfg)
