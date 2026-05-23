"""Tests that the dashboard static assets (HTML/JS/CSS) are served at their
expected paths.  These are purely backend tests — no frontend harness required.
"""
from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

from algoforge.dashboard import make_app
from algoforge.paper_broker import PaperBroker


@pytest.fixture
def client():
    broker = PaperBroker(balance=10_000.0)
    broker.connect()
    app = make_app(broker)
    with TestClient(app) as c:
        yield c
    broker.disconnect()


def test_index_html_served(client: TestClient) -> None:
    """GET / returns 200 and HTML content."""
    resp = client.get("/")
    assert resp.status_code == 200
    assert "text/html" in resp.headers.get("content-type", "")


def test_static_css_served(client: TestClient) -> None:
    """GET /static/style.css returns 200."""
    resp = client.get("/static/style.css")
    assert resp.status_code == 200


def test_static_js_served(client: TestClient) -> None:
    """GET /static/app.js returns 200."""
    resp = client.get("/static/app.js")
    assert resp.status_code == 200


def test_html_contains_chat_panel(client: TestClient) -> None:
    """Index HTML includes the LLM chat panel markup."""
    resp = client.get("/")
    assert resp.status_code == 200
    body = resp.text
    assert "llm-chat-panel" in body
    assert "llm-chat-box" in body
    assert "llm-toggle-btn" in body


def test_js_contains_chat_logic(client: TestClient) -> None:
    """app.js contains the LLM chat implementation."""
    resp = client.get("/static/app.js")
    assert resp.status_code == 200
    src = resp.text
    assert "llmCheckHealth" in src
    assert "llmSendMessage" in src
    assert "/api/llm/chat/stream" in src
    assert "AbortController" in src
