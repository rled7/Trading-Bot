"""Binance Spot REST adapter (S5).

API docs: https://developers.binance.com/docs/binance-spot-api-docs
Auth:   X-MBX-APIKEY header + HMAC-SHA256 signature over the query string
        (signed using AF_BINANCE_API_SECRET) on SIGNED endpoints.
Live:   https://api.binance.com | Testnet: https://testnet.binance.vision
Symbols: concatenated, e.g. BTCUSDT.

Status: scaffold. The HMAC signing helper must be added by the Binance S5
sub-task; trading methods inherited from RestBroker raise NotImplementedError
until implemented.
"""
from __future__ import annotations

from .base import RestBroker


class BinanceBroker(RestBroker):
    BROKER_NAME = "binance"
    LIVE_URL = "https://api.binance.com"
    PAPER_URL = "https://testnet.binance.vision"

    def _on_connect(self) -> None:
        self._config.require("api_key", "api_secret")

    def _auth_headers(self) -> dict[str, str]:
        # Public-data headers; SIGNED endpoints additionally need an HMAC
        # `signature` query param — added by the adapter's signing helper.
        return {"X-MBX-APIKEY": self._config.api_key}

    def _to_broker_symbol(self, symbol: str) -> str:
        return symbol.replace("/", "").replace("_", "").upper()
