"""Alpaca REST adapter (S5).

API docs: https://docs.alpaca.markets/reference/
Auth:   APCA-API-KEY-ID + APCA-API-SECRET-KEY headers.
Paper:  https://paper-api.alpaca.markets  | Live: https://api.alpaca.markets
Symbols: equities AAPL; crypto BTC/USD.

Status: scaffold. Trading methods inherited from RestBroker raise
NotImplementedError until the Alpaca S5 sub-task fills them in.
"""
from __future__ import annotations

from .base import RestBroker


class AlpacaBroker(RestBroker):
    BROKER_NAME = "alpaca"
    LIVE_URL = "https://api.alpaca.markets/v2"
    PAPER_URL = "https://paper-api.alpaca.markets/v2"

    def _on_connect(self) -> None:
        self._config.require("api_key", "api_secret")

    def _auth_headers(self) -> dict[str, str]:
        return {
            "APCA-API-KEY-ID": self._config.api_key,
            "APCA-API-SECRET-KEY": self._config.api_secret,
        }
