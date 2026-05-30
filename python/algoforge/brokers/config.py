"""Broker configuration + environment-variable credential loading.

Credentials follow the existing AF_* / MT5_* env convention. For a broker
named ``oanda`` the recognised variables are::

    AF_OANDA_API_KEY        # bearer token / key id
    AF_OANDA_API_SECRET     # secret (HMAC brokers: Binance/Coinbase)
    AF_OANDA_ACCOUNT        # account id, where the API is account-scoped
    AF_OANDA_BASE_URL       # optional explicit override of the REST host
    AF_OANDA_PAPER          # "1"/"true" forces paper, "0"/"false" forces live

Nothing here makes a network call; it only assembles credentials.
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field
from typing import Optional


def _env_bool(value: Optional[str]) -> Optional[bool]:
    if value is None or value == "":
        return None
    return value.strip().lower() in ("1", "true", "yes", "on")


@dataclass
class BrokerConfig:
    """Resolved credentials + endpoint selection for one broker."""

    name:       str
    paper:      bool = True
    base_url:   str = ""              # empty => adapter picks LIVE/PAPER default
    api_key:    str = ""
    api_secret: str = ""
    account_id: str = ""
    extra:      dict[str, str] = field(default_factory=dict)

    @classmethod
    def from_env(cls, name: str, *, paper: bool = True,
                 environ: Optional[dict[str, str]] = None) -> "BrokerConfig":
        """Build a config from ``AF_<NAME>_*`` environment variables.

        The ``paper`` argument is the default; an explicit ``AF_<NAME>_PAPER``
        env var overrides it so deployments can flip live/paper without code.
        """
        env = environ if environ is not None else os.environ
        prefix = f"AF_{name.upper()}_"

        env_paper = _env_bool(env.get(prefix + "PAPER"))
        resolved_paper = env_paper if env_paper is not None else paper

        return cls(
            name=name,
            paper=resolved_paper,
            base_url=env.get(prefix + "BASE_URL", ""),
            api_key=env.get(prefix + "API_KEY", ""),
            api_secret=env.get(prefix + "API_SECRET", ""),
            account_id=env.get(prefix + "ACCOUNT", ""),
        )

    def require(self, *fields: str) -> None:
        """Raise ValueError if any named credential field is empty.

        Adapters call this in ``connect()`` so misconfiguration fails loudly
        and early rather than as an opaque 401 mid-session.
        """
        missing = [f for f in fields if not getattr(self, f, "")]
        if missing:
            pretty = ", ".join(f"AF_{self.name.upper()}_{f.upper()}" for f in missing)
            raise ValueError(
                f"{self.name}: missing required credential(s): {pretty}"
            )
