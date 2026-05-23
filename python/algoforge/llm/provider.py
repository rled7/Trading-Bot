from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Iterator

from algoforge.llm.types import (
    ChatRequest,
    ChatResponse,
    ChatChunk,
    CompletionRequest,
    CompletionResponse,
    EmbedRequest,
    EmbedResponse,
    HealthStatus,
    ModelInfo,
)


class LLMError(Exception):
    def __init__(self, kind: str, message: str, status: int | None = None) -> None:
        super().__init__(message)
        self.kind = kind
        self.message = message
        self.status = status


class LLMProvider(ABC):
    @abstractmethod
    def health(self) -> HealthStatus: ...

    @abstractmethod
    def list_models(self) -> list[ModelInfo]: ...

    @abstractmethod
    def complete(self, req: CompletionRequest) -> CompletionResponse: ...

    @abstractmethod
    def chat(self, req: ChatRequest) -> ChatResponse: ...

    @abstractmethod
    def chat_stream(self, req: ChatRequest) -> Iterator[ChatChunk]: ...

    @abstractmethod
    def embed(self, req: EmbedRequest) -> EmbedResponse: ...
