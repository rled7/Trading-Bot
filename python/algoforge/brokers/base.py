"""RestBroker — shared base for all live REST broker adapters (S5).

Implements the cross-cutting plumbing every REST broker needs so that each
concrete adapter (OANDA, Alpaca, IBKR, Binance, Coinbase) only has to express
*that broker's* endpoints and payload shapes:

  * connection lifecycle + state          (connect / disconnect / is_connected)
  * paper-vs-live base-URL resolution      (LIVE_URL / PAPER_URL class attrs)
  * authenticated JSON HTTP over stdlib    (_get / _post / _request)
  * canonical <-> broker symbol mapping    (_to_broker_symbol / _from_broker_symbol)

HTTP uses the standard library ``urllib`` only — AlgoForge ships with zero hard
dependencies (see pyproject ``dependencies = []``), matching ``llm/ollama.py``.

The fourteen IBroker trading methods default to raising NotImplementedError
here so a freshly-scaffolded adapter is importable and instantiable while its
methods are filled in incrementally (one S5 sub-task per broker).
"""
from __future__ import annotations

import json
import urllib.error
import urllib.parse
import urllib.request
from typing import Any, Optional

from ..broker import (AccountInfo, IBroker, Order, OrderType, Position,
                      SymbolInfo, Tick)
from ..logger import get_logger
from ..types import Bar
from .config import BrokerConfig

log = get_logger("brokers")


class BrokerError(Exception):
    """Raised for transport / API errors. ``code`` is a connect() return code."""

    def __init__(self, message: str, *, status: int = 0, code: int = 1):
        super().__init__(message)
        self.status = status   # HTTP status, if any
        self.code = code       # non-zero connect()/order return code


class RestBroker(IBroker):
    """Base class for HTTP/REST broker adapters.

    Subclasses set ``BROKER_NAME``, ``LIVE_URL`` and ``PAPER_URL`` class
    attributes, override ``_auth_headers`` (and symbol mapping where the broker
    uses a non-standard ticker convention), then implement the trading methods.
    """

    BROKER_NAME: str = "rest"
    LIVE_URL: str = ""
    PAPER_URL: str = ""

    #: default request timeout in seconds
    TIMEOUT: float = 15.0

    def __init__(self, config: Optional[BrokerConfig] = None):
        self._config = config or BrokerConfig(name=self.BROKER_NAME)
        self._connected = False
        self._base_url = ""

    # ------------------------------------------------------------------ #
    # Connection lifecycle
    # ------------------------------------------------------------------ #
    def connect(self) -> int:
        if self._connected:
            return 0
        self._base_url = self._resolve_base_url()
        if not self._base_url:
            log.error("%s: no base URL configured", self.BROKER_NAME)
            return 1
        try:
            self._on_connect()
        except (BrokerError, ValueError) as exc:
            log.error("%s: connect failed: %s", self.BROKER_NAME, exc)
            return getattr(exc, "code", 1) or 1
        self._connected = True
        log.info("%s: connected (%s) -> %s", self.BROKER_NAME,
                 "paper" if self._config.paper else "live", self._base_url)
        return 0

    def disconnect(self) -> None:
        self._connected = False

    def is_connected(self) -> bool:
        return self._connected

    def broker_name(self) -> str:
        return self.BROKER_NAME

    # ------------------------------------------------------------------ #
    # Hooks for subclasses
    # ------------------------------------------------------------------ #
    def _resolve_base_url(self) -> str:
        """Explicit config override wins; otherwise pick paper/live default."""
        if self._config.base_url:
            return self._config.base_url.rstrip("/")
        url = self.PAPER_URL if self._config.paper else self.LIVE_URL
        return url.rstrip("/")

    def _on_connect(self) -> None:
        """Validate credentials / ping the API. Default: no-op.

        Override to call ``self._config.require(...)`` and hit a lightweight
        endpoint so bad credentials surface at connect() time.
        """

    def _auth_headers(self) -> dict[str, str]:
        """Return per-request auth headers. Default: none (override)."""
        return {}

    def _to_broker_symbol(self, symbol: str) -> str:
        """Canonical AlgoForge symbol -> this broker's convention. Identity by default."""
        return symbol

    def _from_broker_symbol(self, symbol: str) -> str:
        """This broker's convention -> canonical AlgoForge symbol. Identity by default."""
        return symbol

    # ------------------------------------------------------------------ #
    # HTTP helpers (stdlib urllib)
    # ------------------------------------------------------------------ #
    def _get(self, path: str, *, params: Optional[dict[str, Any]] = None,
             headers: Optional[dict[str, str]] = None) -> Any:
        return self._request("GET", path, params=params, headers=headers)

    def _post(self, path: str, *, body: Optional[dict[str, Any]] = None,
              params: Optional[dict[str, Any]] = None,
              headers: Optional[dict[str, str]] = None) -> Any:
        return self._request("POST", path, params=params, body=body, headers=headers)

    def _request(self, method: str, path: str, *,
                 params: Optional[dict[str, Any]] = None,
                 body: Optional[dict[str, Any]] = None,
                 headers: Optional[dict[str, str]] = None) -> Any:
        """Issue an authenticated JSON request and return the decoded body.

        Raises ``BrokerError`` on HTTP >= 400, network failure, or invalid JSON.
        """
        url = path if path.startswith("http") else f"{self._base_url}/{path.lstrip('/')}"
        if params:
            query = urllib.parse.urlencode(
                {k: v for k, v in params.items() if v is not None})
            url = f"{url}?{query}"

        data: Optional[bytes] = None
        req_headers = {"Accept": "application/json"}
        req_headers.update(self._auth_headers())
        if headers:
            req_headers.update(headers)
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            req_headers.setdefault("Content-Type", "application/json")

        req = urllib.request.Request(url, data=data, method=method,
                                     headers=req_headers)
        try:
            with urllib.request.urlopen(req, timeout=self.TIMEOUT) as resp:
                raw = resp.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", "replace") if exc.fp else ""
            raise BrokerError(
                f"{self.BROKER_NAME} {method} {path} -> HTTP {exc.code}: {detail[:300]}",
                status=exc.code, code=2,
            ) from exc
        except urllib.error.URLError as exc:
            raise BrokerError(
                f"{self.BROKER_NAME} {method} {path} -> network error: {exc.reason}",
                code=3,
            ) from exc

        if not raw:
            return {}
        try:
            return json.loads(raw)
        except json.JSONDecodeError as exc:
            raise BrokerError(
                f"{self.BROKER_NAME} {method} {path} -> invalid JSON: {raw[:200]}",
                code=4,
            ) from exc

    # ------------------------------------------------------------------ #
    # IBroker trading surface — implemented per broker in S5 sub-tasks.
    # ------------------------------------------------------------------ #
    def _todo(self, method: str) -> NotImplementedError:
        return NotImplementedError(
            f"S5: {self.BROKER_NAME}.{method}() not yet implemented")

    def get_account(self) -> AccountInfo:
        raise self._todo("get_account")

    def get_tick(self, symbol: str) -> Tick:
        raise self._todo("get_tick")

    def get_symbol_info(self, symbol: str) -> SymbolInfo:
        raise self._todo("get_symbol_info")

    def get_bars(self, symbol: str, tf: int, count: int) -> list[Bar]:
        raise self._todo("get_bars")

    def place_order(self, symbol: str, order_type: OrderType,
                    lots: float, price: float = 0.0,
                    sl: float = 0.0, tp: float = 0.0,
                    magic: int = 0, comment: str = "") -> Order:
        raise self._todo("place_order")

    def close_position(self, ticket: int, lots: float = 0.0) -> int:
        raise self._todo("close_position")

    def modify_position(self, ticket: int, sl: float, tp: float) -> int:
        raise self._todo("modify_position")

    def cancel_order(self, ticket: int) -> int:
        raise self._todo("cancel_order")

    def get_positions(self) -> list[Position]:
        raise self._todo("get_positions")

    def get_orders(self) -> list[Order]:
        raise self._todo("get_orders")

    def get_position(self, ticket: int) -> Optional[Position]:
        raise self._todo("get_position")
