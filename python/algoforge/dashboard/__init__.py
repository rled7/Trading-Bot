"""AlgoForge dashboard package.

Exports:
    make_app      -- FastAPI application factory.
    LogRingBuffer -- In-memory ring buffer logging handler.
"""
from __future__ import annotations

from .log_buffer import LogRingBuffer
from .server import make_app

__all__ = ["make_app", "LogRingBuffer"]
