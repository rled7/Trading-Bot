"""Telegram alerting for AlgoForge.

:class:`TelegramAlerter` sends messages to a Telegram chat via the
Bot API.  Credentials are accepted either as constructor arguments or
from the environment variables ``TELEGRAM_TOKEN`` and
``TELEGRAM_CHAT_ID``.  When neither source provides valid credentials
the alerter silently disables itself — all ``send``/``alert*`` calls
return ``False`` without raising.

Usage
-----
    from algoforge.telegram import TelegramAlerter

    tg = TelegramAlerter()                  # reads env vars
    tg.alert('CRITICAL', 'Trading halted')
    tg.alert_guard_state('RED', 'Daily loss limit reached')
    tg.alert_trade('EURUSD', 'LONG', 0.10, 1.08500, 1.08200, 1.09100)

Only the Python standard library is used (``urllib.request`` + ``json``).
"""
from __future__ import annotations

import json
import os
import urllib.error
import urllib.parse
import urllib.request
from typing import Optional


class TelegramAlerter:
    """Send Telegram messages via the Bot API using the standard library only.

    Parameters
    ----------
    bot_token:
        Telegram bot token.  Falls back to the ``TELEGRAM_TOKEN``
        environment variable when empty.
    chat_id:
        Target chat / channel id.  Falls back to ``TELEGRAM_CHAT_ID``
        when empty.
    """

    BASE_URL = 'https://api.telegram.org/bot{token}/sendMessage'

    def __init__(self, bot_token: str = '', chat_id: str = '') -> None:
        self._token   = bot_token   or os.getenv('TELEGRAM_TOKEN',   '')
        self._chat_id = chat_id     or os.getenv('TELEGRAM_CHAT_ID', '')
        self._enabled = bool(self._token and self._chat_id)

    # ------------------------------------------------------------------
    # Properties
    # ------------------------------------------------------------------

    @property
    def enabled(self) -> bool:
        """``True`` when both token and chat-id are configured."""
        return self._enabled

    # ------------------------------------------------------------------
    # Core send
    # ------------------------------------------------------------------

    def send(self, message: str, parse_mode: str = '') -> bool:
        """Send *message* to the configured chat.

        Parameters
        ----------
        message:
            Text body.  Plain text by default; pass ``'MarkdownV2'`` or
            ``'HTML'`` as *parse_mode* to enable formatting.
        parse_mode:
            Optional Telegram parse mode string.

        Returns
        -------
        bool
            ``True`` on HTTP 200, ``False`` when disabled or on any error.
        """
        if not self._enabled:
            return False

        url = self.BASE_URL.format(token=self._token)
        payload: dict = {'chat_id': self._chat_id, 'text': message}
        if parse_mode:
            payload['parse_mode'] = parse_mode

        try:
            data = json.dumps(payload).encode('utf-8')
            req  = urllib.request.Request(
                url,
                data=data,
                headers={'Content-Type': 'application/json'},
            )
            with urllib.request.urlopen(req, timeout=10) as resp:
                return resp.status == 200
        except Exception:
            return False

    # ------------------------------------------------------------------
    # Convenience alert methods
    # ------------------------------------------------------------------

    def alert(self, level: str, msg: str) -> bool:
        """Send a level-tagged alert message.

        The message is formatted as ``'[LEVEL] msg'``, e.g.
        ``'[CRITICAL] Trading halted: daily loss limit reached'``.

        Parameters
        ----------
        level:
            Severity label (e.g. ``'INFO'``, ``'WARN'``, ``'CRITICAL'``).
            Automatically uppercased.
        msg:
            Alert body.
        """
        return self.send(f'[{level.upper()}] {msg}')

    def alert_guard_state(self, state_name: str, reason: str) -> bool:
        """Send a guard-state transition notification.

        Parameters
        ----------
        state_name:
            One of ``'GREEN'``, ``'YELLOW'``, or ``'RED'`` (case-insensitive).
        reason:
            Human-readable explanation of the transition.
        """
        emoji_map = {'GREEN': '✅', 'YELLOW': '⚠️', 'RED': '\U0001f6a8'}
        emoji = emoji_map.get(state_name.upper(), '❓')
        return self.send(f'{emoji} AlgoForge Guard: {state_name}\n{reason}')

    def alert_trade(
        self,
        symbol:    str,
        direction: str,
        lots:      float,
        entry:     float,
        sl:        float,
        tp:        float,
    ) -> bool:
        """Send a trade-placed notification.

        Parameters
        ----------
        symbol:
            Instrument name (e.g. ``'EURUSD'``).
        direction:
            ``'LONG'`` or ``'SHORT'`` (case-insensitive).
        lots:
            Position size in lots.
        entry:
            Fill / entry price.
        sl:
            Stop-loss price.
        tp:
            Take-profit price.
        """
        arrow = '\U0001f4c8' if direction.upper() == 'LONG' else '\U0001f4c9'
        msg = (
            f'{arrow} Trade Placed\n'
            f'Symbol: {symbol}\n'
            f'Direction: {direction}\n'
            f'Lots: {lots:.2f}\n'
            f'Entry: {entry:.5f}  SL: {sl:.5f}  TP: {tp:.5f}'
        )
        return self.send(msg)
