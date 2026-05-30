"""OANDA v20 REST adapter (S5).

API docs: https://developer.oanda.com/rest-live-v20/introduction/
Auth:   Bearer token (AF_OANDA_API_KEY), account-scoped (AF_OANDA_ACCOUNT).
Symbols: underscore form, e.g. EUR_USD.

Status: scaffold. Trading methods inherited from RestBroker raise
NotImplementedError until the OANDA S5 sub-task fills them in.
"""
from __future__ import annotations

from .base import RestBroker


class OandaBroker(RestBroker):
    BROKER_NAME = "oanda"
    LIVE_URL = "https://api-fxtrade.oanda.com/v3"
    PAPER_URL = "https://api-fxpractice.oanda.com/v3"

    def _on_connect(self) -> None:
        self._config.require("api_key", "account_id")

    def _auth_headers(self) -> dict[str, str]:
        return {"Authorization": f"Bearer {self._config.api_key}"}

    def _to_broker_symbol(self, symbol: str) -> str:
        # EURUSD / EUR/USD -> EUR_USD
        s = symbol.replace("/", "").replace("_", "")
        return f"{s[:3]}_{s[3:]}" if len(s) == 6 else symbol

    def _from_broker_symbol(self, symbol: str) -> str:
        return symbol.replace("_", "")
