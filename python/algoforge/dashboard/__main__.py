"""Entry point for the AlgoForge dashboard server.

Usage::

    python -m algoforge.dashboard [--host 0.0.0.0] [--port 8000] \\
        [--broker paper|mt5] [--balance 10000]

Environment
-----------
    AF_DASHBOARD_TOKEN   Bearer token for API auth.  Unset = no auth.
"""
from __future__ import annotations

import argparse
import os
import signal
import sys

from algoforge.logger import get_logger, init_logger

from .log_buffer import LogRingBuffer
from .server import make_app


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m algoforge.dashboard",
        description="AlgoForge dashboard HTTP server",
    )
    parser.add_argument("--host", default="0.0.0.0", help="Bind host (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8000, help="Bind port (default: 8000)")
    parser.add_argument(
        "--broker",
        choices=["paper", "mt5"],
        default="paper",
        help="Broker backend (default: paper)",
    )
    parser.add_argument(
        "--balance",
        type=float,
        default=10_000.0,
        help="Starting balance for paper broker (default: 10000)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> None:
    args = _parse_args(argv)

    # Logger
    init_logger(level="INFO", component="dashboard")
    log = get_logger("dashboard")

    # Log ring buffer
    buf = LogRingBuffer(capacity=500)
    buf.install()

    # Auth token
    token: str | None = os.environ.get("AF_DASHBOARD_TOKEN") or None

    # Broker
    if args.broker == "mt5":
        try:
            from algoforge.mt5_broker import MT5Broker  # type: ignore[import]
            broker = MT5Broker()
        except ImportError:
            log.error("MT5Broker requires the 'live' optional dependency group. "
                      "Install with: pip install algoforge[live]")
            sys.exit(1)
    else:
        from algoforge.paper_broker import PaperBroker
        broker = PaperBroker(balance=args.balance)

    rc = broker.connect()
    if rc != 0:
        log.error(f"Broker connect() failed with code {rc}")
        sys.exit(1)

    log.info(
        f"Broker connected: {broker.broker_name()} | "
        f"host={args.host} port={args.port} auth={'yes' if token else 'no'}"
    )

    app = make_app(broker, token=token, log_buffer=buf)

    # Graceful shutdown on SIGINT
    def _shutdown(signum: int, frame: object) -> None:
        log.info("Received signal, disconnecting broker...")
        broker.disconnect()
        sys.exit(0)

    signal.signal(signal.SIGINT, _shutdown)
    signal.signal(signal.SIGTERM, _shutdown)

    try:
        import uvicorn
    except ImportError:
        log.error("uvicorn is required. Install with: pip install algoforge[dashboard]")
        sys.exit(1)

    uvicorn.run(app, host=args.host, port=args.port, log_config=None)


if __name__ == "__main__":
    main()
