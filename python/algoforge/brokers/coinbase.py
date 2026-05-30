"""Coinbase Advanced Trade REST adapter (S5).

API docs: https://docs.cdp.coinbase.com/advanced-trade/docs/welcome
Auth:   JWT (ES256) bearer per request, signed with the CDP API key/secret.
Base:   https://api.coinbase.com/api/v3/brokerage
Symbols: hyphenated product ids, e.g. BTC-USD.

Status: scaffold. The JWT signing helper must be added by the Coinbase S5
sub-task; trading methods inherited from RestBroker raise NotImplementedError
until implemented.
"""
from __future__ import annotations

from .base import RestBroker


class CoinbaseBroker(RestBroker):
    BROKER_NAME = "coinbase"
    LIVE_URL = "https://api.coinbase.com/api/v3/brokerage"
    # Coinbase has no separate paper host; sandbox is opt-in per-account.
    PAPER_URL = "https://api.coinbase.com/api/v3/brokerage"

    def _on_connect(self) -> None:
        self._config.require("api_key", "api_secret")

    def _to_broker_symbol(self, symbol: str) -> str:
        s = symbol.replace("/", "-").replace("_", "-").upper()
        return s if "-" in s or len(s) != 6 else f"{s[:3]}-{s[3:]}"
