"""AlgoForge live broker adapters (S5).

Public surface:

    from algoforge.brokers import make_broker, available_brokers
    broker = make_broker("oanda", paper=True)

Concrete adapters subclass ``RestBroker`` and are imported lazily by the
registry; import them directly only when you need the class itself.
"""
from __future__ import annotations

from .base import BrokerError, RestBroker
from .config import BrokerConfig
from .registry import available_brokers, make_broker

__all__ = [
    "make_broker",
    "available_brokers",
    "RestBroker",
    "BrokerError",
    "BrokerConfig",
]
