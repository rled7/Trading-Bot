"""Interactive Brokers adapter (S5) — Client Portal Web API.

API docs: https://www.interactivebrokers.com/api/doc.html (Client Portal)
Transport: a locally-running Client Portal Gateway proxies an authenticated
session; requests hit https://localhost:5000/v1/api. There is no API-key
header — the gateway holds the session, so AF_IBKR_BASE_URL should point at
the gateway and AF_IBKR_ACCOUNT selects the account.

Status: scaffold. Trading methods inherited from RestBroker raise
NotImplementedError until the IBKR S5 sub-task fills them in.
"""
from __future__ import annotations

from .base import RestBroker


class IbkrBroker(RestBroker):
    BROKER_NAME = "ibkr"
    # Both point at the local gateway; AF_IBKR_BASE_URL overrides if relocated.
    LIVE_URL = "https://localhost:5000/v1/api"
    PAPER_URL = "https://localhost:5000/v1/api"

    def _on_connect(self) -> None:
        self._config.require("account_id")
        # NOTE: agent must confirm the gateway session is authenticated
        # (GET /iserver/auth/status) before trading.
