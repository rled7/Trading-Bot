from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
from typing import Iterator

from algoforge.llm.provider import LLMError, LLMProvider
from algoforge.llm.types import (
    ChatChunk,
    ChatMessage,
    ChatRequest,
    ChatResponse,
    CompletionRequest,
    CompletionResponse,
    EmbedRequest,
    EmbedResponse,
    HealthStatus,
    ModelInfo,
)

_DEFAULT_HOST = "http://localhost:11434"
_DEFAULT_MODEL = "llama3.1:8b"
_DEFAULT_EMBED_MODEL = "nomic-embed-text"
_DEFAULT_TIMEOUT = 60.0
_DEFAULT_SEED = 42
_DEFAULT_TEMPERATURE = 0.2
_DEFAULT_MAX_TOKENS = 512


class _Transport:
    def request(
        self,
        method: str,
        url: str,
        body: bytes | None,
        headers: dict[str, str],
        timeout: float,
    ) -> tuple[int, bytes]:
        req = urllib.request.Request(url, data=body, headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return resp.status, resp.read()
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read()
        except TimeoutError as exc:
            raise LLMError("timeout", str(exc)) from exc
        except OSError as exc:
            raise LLMError("unreachable", str(exc)) from exc


class OllamaProvider(LLMProvider):
    def __init__(
        self,
        host: str = _DEFAULT_HOST,
        model: str = _DEFAULT_MODEL,
        embed_model: str = _DEFAULT_EMBED_MODEL,
        timeout_s: float = _DEFAULT_TIMEOUT,
        seed: int | None = _DEFAULT_SEED,
        temperature: float = _DEFAULT_TEMPERATURE,
        max_tokens: int | None = _DEFAULT_MAX_TOKENS,
        transport: object | None = None,
    ) -> None:
        self._host = host.rstrip("/")
        self._model = model
        self._embed_model = embed_model
        self._timeout_s = timeout_s
        self._seed = seed
        self._temperature = temperature
        self._max_tokens = max_tokens
        self._transport: _Transport = transport if transport is not None else _Transport()

    @classmethod
    def from_env(cls) -> "OllamaProvider":
        host = os.environ.get("AF_LLM_HOST", _DEFAULT_HOST)
        model = os.environ.get("AF_LLM_MODEL", _DEFAULT_MODEL)
        embed_model = os.environ.get("AF_LLM_EMBED_MODEL", _DEFAULT_EMBED_MODEL)
        timeout_s = float(os.environ.get("AF_LLM_TIMEOUT", str(_DEFAULT_TIMEOUT)))
        seed_raw = os.environ.get("AF_LLM_SEED", str(_DEFAULT_SEED))
        seed: int | None = int(seed_raw) if seed_raw else None
        temperature = float(os.environ.get("AF_LLM_TEMPERATURE", str(_DEFAULT_TEMPERATURE)))
        max_tokens_raw = os.environ.get("AF_LLM_MAX_TOKENS", str(_DEFAULT_MAX_TOKENS))
        max_tokens: int | None = int(max_tokens_raw) if max_tokens_raw else None
        return cls(
            host=host,
            model=model,
            embed_model=embed_model,
            timeout_s=timeout_s,
            seed=seed,
            temperature=temperature,
            max_tokens=max_tokens,
        )

    def _do_request(
        self,
        method: str,
        path: str,
        body: dict | None,
        timeout: float,
    ) -> dict:
        url = self._host + path
        encoded: bytes | None = None
        headers: dict[str, str] = {}
        if body is not None:
            encoded = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"
        status, raw = self._transport.request(method, url, encoded, headers, timeout)
        if status >= 400:
            raise LLMError("http", f"HTTP {status}", status=status)
        try:
            return json.loads(raw)
        except json.JSONDecodeError as exc:
            raise LLMError("decode", f"JSON decode error: {exc}") from exc

    def complete(self, req: CompletionRequest) -> CompletionResponse:
        options: dict = {
            "temperature": req.temperature,
        }
        if req.max_tokens is not None:
            options["num_predict"] = req.max_tokens
        if req.stop:
            options["stop"] = list(req.stop)
        if req.seed is not None:
            options["seed"] = req.seed
        body = {
            "model": req.model,
            "prompt": req.prompt,
            "stream": False,
            "options": options,
        }
        data = self._do_request("POST", "/api/generate", body, req.timeout_s)
        return CompletionResponse(
            text=data["response"],
            model=data["model"],
            prompt_tokens=data.get("prompt_eval_count", 0),
            completion_tokens=data.get("eval_count", 0),
            total_duration_ns=data.get("total_duration", 0),
            finish_reason=data.get("done_reason", "stop"),
        )

    def chat(self, req: ChatRequest) -> ChatResponse:
        options: dict = {
            "temperature": req.temperature,
        }
        if req.max_tokens is not None:
            options["num_predict"] = req.max_tokens
        if req.seed is not None:
            options["seed"] = req.seed
        body = {
            "model": req.model,
            "messages": [{"role": m.role, "content": m.content} for m in req.messages],
            "stream": False,
            "options": options,
        }
        data = self._do_request("POST", "/api/chat", body, req.timeout_s)
        msg = data["message"]
        return ChatResponse(
            message=ChatMessage(role=msg["role"], content=msg["content"]),
            model=data["model"],
            prompt_tokens=data.get("prompt_eval_count", 0),
            completion_tokens=data.get("eval_count", 0),
            total_duration_ns=data.get("total_duration", 0),
            finish_reason=data.get("done_reason", "stop"),
        )

    def chat_stream(self, req: ChatRequest) -> Iterator[ChatChunk]:
        options: dict = {
            "temperature": req.temperature,
        }
        if req.max_tokens is not None:
            options["num_predict"] = req.max_tokens
        if req.seed is not None:
            options["seed"] = req.seed
        body = {
            "model": req.model,
            "messages": [{"role": m.role, "content": m.content} for m in req.messages],
            "stream": True,
            "options": options,
        }
        url = self._host + "/api/chat"
        encoded = json.dumps(body).encode("utf-8")
        headers = {"Content-Type": "application/json"}
        status, raw = self._transport.request("POST", url, encoded, headers, req.timeout_s)
        if status >= 400:
            raise LLMError("http", f"HTTP {status}", status=status)
        for line in raw.splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                chunk = json.loads(line)
            except json.JSONDecodeError as exc:
                raise LLMError("decode", f"JSON decode error in stream: {exc}") from exc
            done = chunk.get("done", False)
            msg = chunk.get("message", {})
            delta = msg.get("content", "")
            finish_reason = chunk.get("done_reason") if done else None
            yield ChatChunk(delta=delta, done=done, finish_reason=finish_reason)

    def embed(self, req: EmbedRequest) -> EmbedResponse:
        inputs = req.input if isinstance(req.input, list) else [req.input]
        embeddings: list[list[float]] = []
        for text in inputs:
            body = {"model": req.model, "prompt": text}
            data = self._do_request("POST", "/api/embeddings", body, self._timeout_s)
            embeddings.append(data["embedding"])
        return EmbedResponse(embeddings=embeddings, model=req.model)

    def list_models(self) -> list[ModelInfo]:
        data = self._do_request("GET", "/api/tags", None, self._timeout_s)
        models = []
        for m in data.get("models", []):
            models.append(
                ModelInfo(
                    name=m["name"],
                    size_bytes=m.get("size", 0),
                    modified_at=m.get("modified_at", ""),
                )
            )
        return models

    def health(self) -> HealthStatus:
        url = self._host + "/"
        headers: dict[str, str] = {}
        try:
            status, _ = self._transport.request("GET", url, None, headers, self._timeout_s)
            if status >= 400:
                return HealthStatus(
                    ok=False,
                    model_loaded=False,
                    model=self._model,
                    error=f"root endpoint returned HTTP {status}",
                )
        except LLMError as exc:
            return HealthStatus(ok=False, model_loaded=False, model=self._model, error=exc.message)

        try:
            models = self.list_models()
        except LLMError as exc:
            return HealthStatus(ok=False, model_loaded=False, model=self._model, error=exc.message)

        model_names = {m.name for m in models}
        model_loaded = self._model in model_names
        return HealthStatus(ok=True, model_loaded=model_loaded, model=self._model, error=None)
