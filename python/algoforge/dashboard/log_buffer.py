"""In-memory ring buffer that captures algoforge log records.

Subclasses :class:`logging.Handler` so it can be attached directly to
the ``algoforge`` logger hierarchy.
"""
from __future__ import annotations

import logging
from collections import deque
from datetime import datetime, timezone
from typing import Any


class LogRingBuffer(logging.Handler):
    """Fixed-capacity deque that stores the last *capacity* log records.

    Each stored record is a plain dict with keys:
        ts        -- ISO 8601 UTC timestamp string (seconds precision)
        level     -- logging level name (e.g. ``"INFO"``)
        component -- value of ``record.component`` if present, else ``record.name``
        msg       -- formatted message string
    """

    def __init__(self, capacity: int = 500) -> None:
        super().__init__()
        self._capacity = capacity
        self._buf: deque[dict[str, Any]] = deque(maxlen=capacity)

    # ------------------------------------------------------------------
    # logging.Handler interface
    # ------------------------------------------------------------------

    def emit(self, record: logging.LogRecord) -> None:
        ts = datetime.fromtimestamp(record.created, tz=timezone.utc).strftime(
            "%Y-%m-%dT%H:%M:%SZ"
        )
        component = getattr(record, "component", record.name)
        try:
            msg = record.getMessage()
            if record.exc_info:
                msg += "\n" + self.formatException(record.exc_info)
        except Exception:  # noqa: BLE001
            msg = str(record.msg)
        self._buf.append(
            {
                "ts": ts,
                "level": record.levelname,
                "component": component,
                "msg": msg,
            }
        )

    # ------------------------------------------------------------------
    # Public helpers
    # ------------------------------------------------------------------

    def tail(self, n: int) -> list[dict[str, Any]]:
        """Return the last *n* records in chronological order."""
        records = list(self._buf)
        return records[-n:] if n < len(records) else records

    def install(self) -> None:
        """Attach this handler to the ``algoforge`` root logger."""
        logging.getLogger("algoforge").addHandler(self)
