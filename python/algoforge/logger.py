"""Structured JSON logger for AlgoForge.

All log records are emitted as single-line JSON objects:
    {"ts": "2026-05-19T12:00:00Z", "level": "INFO", "component": "broker", "msg": "..."}

Usage
-----
    from algoforge.logger import init_logger, get_logger, log_info

    init_logger(level='DEBUG', component='mybot')
    log = get_logger('risk')
    log.info('guard state changed')

    # Or use the module-level convenience wrappers:
    log_info('starting up', component='main')
"""
from __future__ import annotations

import json
import logging
import sys
from datetime import datetime, timezone
from typing import Optional


# ---------------------------------------------------------------------------
# JSON formatter
# ---------------------------------------------------------------------------

class JsonFormatter(logging.Formatter):
    """Formats log records as single-line JSON objects."""

    def format(self, record: logging.LogRecord) -> str:
        ts = datetime.fromtimestamp(record.created, tz=timezone.utc).strftime(
            '%Y-%m-%dT%H:%M:%SZ'
        )
        component = getattr(record, 'component', record.name)
        msg = record.getMessage()
        if record.exc_info:
            msg += '\n' + self.formatException(record.exc_info)
        return json.dumps(
            {
                'ts': ts,
                'level': record.levelname,
                'component': component,
                'msg': msg,
            },
            ensure_ascii=False,
        )


# ---------------------------------------------------------------------------
# Module-level state
# ---------------------------------------------------------------------------

_logger: Optional[logging.Logger] = None
_component: str = 'algoforge'


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def init_logger(level: str = 'INFO', component: str = 'algoforge') -> logging.Logger:
    """Initialise (or re-initialise) the shared AlgoForge logger.

    Parameters
    ----------
    level:
        Standard logging level name: ``'DEBUG'``, ``'INFO'``, ``'WARNING'``,
        ``'ERROR'``, ``'CRITICAL'``.
    component:
        Default component tag written to JSON output when none is supplied
        by the caller.

    Returns
    -------
    logging.Logger
        The configured root ``'algoforge'`` logger.
    """
    global _logger, _component
    _component = component

    # stdout handler — all levels
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(JsonFormatter())

    # stderr handler — ERROR and above only
    err_handler = logging.StreamHandler(sys.stderr)
    err_handler.setFormatter(JsonFormatter())
    err_handler.setLevel(logging.ERROR)

    logger = logging.getLogger('algoforge')
    logger.handlers.clear()
    logger.addHandler(handler)
    logger.addHandler(err_handler)
    logger.setLevel(getattr(logging, level.upper(), logging.INFO))
    logger.propagate = False

    _logger = logger
    return logger


def get_logger(component: str = '') -> logging.LoggerAdapter:
    """Return a :class:`logging.LoggerAdapter` tagged with *component*.

    Lazily initialises the logger at ``INFO`` level if :func:`init_logger`
    has not been called yet.

    Parameters
    ----------
    component:
        Tag written to the ``"component"`` field of each JSON record.
        Defaults to the value passed to the last :func:`init_logger` call.
    """
    global _logger
    if _logger is None:
        init_logger()
    return logging.LoggerAdapter(_logger, {'component': component or _component})  # type: ignore[arg-type]


# ---------------------------------------------------------------------------
# Convenience wrappers
# ---------------------------------------------------------------------------

def log_info(msg: str, component: str = '') -> None:
    """Log *msg* at INFO level."""
    get_logger(component).info(msg)


def log_warn(msg: str, component: str = '') -> None:
    """Log *msg* at WARNING level."""
    get_logger(component).warning(msg)


def log_error(msg: str, component: str = '') -> None:
    """Log *msg* at ERROR level."""
    get_logger(component).error(msg)


def log_debug(msg: str, component: str = '') -> None:
    """Log *msg* at DEBUG level."""
    get_logger(component).debug(msg)
