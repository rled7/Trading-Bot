from __future__ import annotations

import json
from pathlib import Path

from algoforge.llm.provider import LLMError

_FIXTURES_DIR = Path(__file__).parent.parent.parent.parent / "tests" / "fixtures" / "llm"


def load_fixture(name: str) -> dict:
    path = _FIXTURES_DIR / f"{name}.json"
    if not path.exists():
        raise FileNotFoundError(f"fixture not found: {path}")
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


class MockTransport:
    def __init__(self, fixture: dict) -> None:
        self._fixture = fixture
        self.last_request: dict | None = None

    def request(
        self,
        method: str,
        url: str,
        body: bytes | None,
        headers: dict[str, str],
        timeout: float,
    ) -> tuple[int, bytes]:
        parsed_body: dict | None = None
        if body is not None:
            parsed_body = json.loads(body)
        self.last_request = {
            "method": method,
            "url": url,
            "body": parsed_body,
        }
        resp = self._fixture["response"]
        status: int = resp["status"]
        resp_body = resp["body"]
        if isinstance(resp_body, list):
            raw = b"\n".join(json.dumps(chunk).encode("utf-8") for chunk in resp_body)
        else:
            raw = json.dumps(resp_body).encode("utf-8")
        return status, raw
