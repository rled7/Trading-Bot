# AlgoForge LLM Layer — Cross-Language Specification (S2)

This document is the **single source of truth** for the S2 local-LLM layer.
All four language implementations (C, C++, Python, JS) must speak the same
wire protocol (Ollama HTTP), expose the same provider interface, and produce
identical results when replaying the canonical fixture suite.

The spec is binding. Implementations consult the spec, not each other.

Default model: **`llama3.1:8b`** (configurable via env).

---

## 1. Scope

| Module | C / C++ / JS / Py | Notes |
|---|---|---|
| `LLMProvider` interface | ✓ all 4 | `complete`, `chat`, `embed`, `health`, `list_models` |
| `OllamaProvider` impl | ✓ all 4 | HTTP client against the Ollama REST API |
| `trade_rationale` use case | ✓ all 4 | Generates a 1–3 sentence explanation for a fired signal |
| `backtest_commentary` use case | ✓ all 4 | Generates a narrative summary of a backtest result |
| `news_sentiment` use case | ✓ all 4 | Returns `{sentiment, score, confidence}` for a headline |
| `strategy_describe` use case | ✓ all 4 | Generates a natural-language description of a strategy spec |
| Fixture-replay test harness | ✓ all 4 | Same JSON fixture files in `tests/fixtures/llm/` |
| CLI | ✓ all 4 | `rationale` / `commentary` / `sentiment` / `describe` subcommands |
| Dashboard chat surface | Py only | FastAPI endpoints + static UI panel |

**Non-goals (deferred):** training, fine-tuning, RAG/vector stores, multi-turn
conversation memory persistence, tool use / function calling. The chat panel
in the dashboard is single-turn (history is client-side only).

---

## 2. Module layout

### Python (reference)
```
python/algoforge/llm/
  __init__.py              (re-exports)
  types.py                 (dataclasses: ChatMessage, CompletionRequest, …)
  provider.py              (LLMProvider abstract base)
  ollama.py                (OllamaProvider)
  use_cases.py             (trade_rationale, backtest_commentary, …)
  prompts.py               (prompt templates — single source of strings)
  fixtures.py              (fixture loader/recorder helpers)
  __main__.py              (CLI)
python/tests/test_llm.py
python/tests/fixtures/llm/*.json   (shared canonical fixtures)
```

### C
```
c/include/af_llm.h          (single header, all structs + prototypes)
c/src/llm.c                 (OllamaProvider + use cases)
c/tests/test_llm.c
```
HTTP client: libcurl (already used elsewhere in `c/`? if not, link it as a
new dependency in the Makefile). JSON: hand-rolled in `c/src/llm.c` — same
style as existing C code, no external JSON dep.

### C++
```
cpp/include/core/llm.hpp    (namespace algoforge::llm)
cpp/src/llm/
  ollama.cpp
  use_cases.cpp
  prompts.cpp
cpp/tests/test_llm.cpp
```
HTTP: libcurl via C++ wrapper. JSON: hand-rolled, kept minimal.

### JS
```
js/src/llm/
  index.js
  provider.js
  ollama.js
  useCases.js
  prompts.js
  fixtures.js
js/test/test_llm.test.js
```
HTTP: Node 18+ built-in `fetch`. JSON: native. No new dependencies.

### Shared fixtures (binary-identical across languages)
```
tests/fixtures/llm/
  complete_basic.json         (request + response pair)
  chat_basic.json
  chat_streaming.json         (array of NDJSON-decoded chunks)
  embed_basic.json
  health_ok.json
  health_down.json            (simulated: 503)
  list_models.json
  trade_rationale_long.json
  trade_rationale_short.json
  backtest_commentary_winning.json
  backtest_commentary_losing.json
  news_sentiment_bullish.json
  news_sentiment_bearish.json
  news_sentiment_neutral.json
  strategy_describe_breakout.json
  strategy_describe_meanrev.json
```

Each fixture file has this shape:
```json
{
  "name": "complete_basic",
  "request": { "method": "POST", "path": "/api/generate", "body": { … } },
  "response": { "status": 200, "body": { … } }
}
```

Tests load these fixtures and feed them to a `MockTransport` that asserts the
request matches and returns the canned response.

---

## 3. Provider interface

The contract every language implements (Python signature shown; other
languages use the idiomatic equivalent — see §6).

```python
class LLMProvider(Protocol):
    def health(self) -> HealthStatus: ...
    def list_models(self) -> list[ModelInfo]: ...
    def complete(self, req: CompletionRequest) -> CompletionResponse: ...
    def chat(self, req: ChatRequest) -> ChatResponse: ...
    def chat_stream(self, req: ChatRequest) -> Iterator[ChatChunk]: ...
    def embed(self, req: EmbedRequest) -> EmbedResponse: ...
```

### Types

```python
@dataclass(frozen=True)
class CompletionRequest:
    model: str
    prompt: str
    temperature: float = 0.2
    max_tokens: int | None = None    # maps to num_predict
    stop: list[str] = ()
    seed: int | None = None
    timeout_s: float = 60.0

@dataclass(frozen=True)
class CompletionResponse:
    text: str
    model: str
    prompt_tokens: int
    completion_tokens: int
    total_duration_ns: int
    finish_reason: str   # "stop" | "length" | "error"

@dataclass(frozen=True)
class ChatMessage:
    role: str            # "system" | "user" | "assistant"
    content: str

@dataclass(frozen=True)
class ChatRequest:
    model: str
    messages: list[ChatMessage]
    temperature: float = 0.2
    max_tokens: int | None = None
    seed: int | None = None
    timeout_s: float = 60.0

@dataclass(frozen=True)
class ChatResponse:
    message: ChatMessage
    model: str
    prompt_tokens: int
    completion_tokens: int
    total_duration_ns: int
    finish_reason: str

@dataclass(frozen=True)
class ChatChunk:
    delta: str           # incremental text since last chunk
    done: bool
    finish_reason: str | None

@dataclass(frozen=True)
class EmbedRequest:
    model: str
    input: str | list[str]

@dataclass(frozen=True)
class EmbedResponse:
    embeddings: list[list[float]]
    model: str

@dataclass(frozen=True)
class HealthStatus:
    ok: bool
    model_loaded: bool
    model: str | None
    error: str | None    # populated when ok=False

@dataclass(frozen=True)
class ModelInfo:
    name: str
    size_bytes: int
    modified_at: str     # ISO-8601 string from Ollama
```

### Error model

A single exception type (or error code in C) `LLMError` with fields:
- `kind`: `"unreachable" | "timeout" | "http" | "decode" | "model_missing"`
- `message`: human-readable
- `status`: HTTP status (if applicable)

In C, `af_llm_error_t` is a struct returned via out-param; all functions
return `int 0/non-zero` for success/failure (consistent with existing C code).

---

## 4. Ollama wire format

Default base URL: `http://localhost:11434`. Override via `AF_LLM_HOST`.

### 4.1 `POST /api/generate`
Request:
```json
{
  "model": "llama3.1:8b",
  "prompt": "Why did EURUSD break out?",
  "stream": false,
  "options": {
    "temperature": 0.2,
    "num_predict": 256,
    "stop": ["</answer>"],
    "seed": 42
  }
}
```
Response (200):
```json
{
  "model": "llama3.1:8b",
  "response": "EURUSD broke out because…",
  "done": true,
  "done_reason": "stop",
  "prompt_eval_count": 12,
  "eval_count": 84,
  "total_duration": 1234567890
}
```

### 4.2 `POST /api/chat`
Request:
```json
{
  "model": "llama3.1:8b",
  "messages": [
    {"role": "system", "content": "You are a trading assistant."},
    {"role": "user",   "content": "Explain ATR."}
  ],
  "stream": false,
  "options": { "temperature": 0.2, "num_predict": 256, "seed": 42 }
}
```
Response (200):
```json
{
  "model": "llama3.1:8b",
  "message": {"role": "assistant", "content": "ATR is…"},
  "done": true,
  "done_reason": "stop",
  "prompt_eval_count": 18,
  "eval_count": 120,
  "total_duration": 2345678901
}
```

### 4.3 `POST /api/chat` (stream=true)
NDJSON stream — one JSON object per line. Each non-final chunk:
```json
{"model":"llama3.1:8b","message":{"role":"assistant","content":"A"},"done":false}
```
Final chunk:
```json
{"model":"llama3.1:8b","message":{"role":"assistant","content":""},"done":true,"done_reason":"stop","prompt_eval_count":18,"eval_count":120,"total_duration":2345678901}
```

### 4.4 `POST /api/embeddings`
Request:
```json
{"model": "nomic-embed-text", "prompt": "EURUSD broke out"}
```
Response:
```json
{"embedding": [0.123, -0.456, …]}
```
For batched input (list of strings), each is sent as a separate request and
the results are gathered into `embeddings: [[…], […]]` in input order.

### 4.5 `GET /api/tags` (list models) → `ModelInfo[]`
```json
{
  "models": [
    {"name": "llama3.1:8b", "size": 4661211808, "modified_at": "2024-..."}
  ]
}
```

### 4.6 Health check
There is no dedicated endpoint. Health is implemented as:
1. `GET /` — Ollama returns "Ollama is running" with 200.
2. `GET /api/tags` — confirm reachable + parse model list.
3. `model_loaded = (req.model in list_models())`.

`HealthStatus.ok = (root reachable) and (list_models successful)`.

---

## 5. Environment variables

| Var | Default | Meaning |
|---|---|---|
| `AF_LLM_PROVIDER` | `ollama` | only `ollama` for now; reserved for future |
| `AF_LLM_HOST` | `http://localhost:11434` | base URL |
| `AF_LLM_MODEL` | `llama3.1:8b` | default model |
| `AF_LLM_EMBED_MODEL` | `nomic-embed-text` | embedding model |
| `AF_LLM_TIMEOUT` | `60` | seconds |
| `AF_LLM_SEED` | `42` | RNG seed passed to Ollama for determinism |
| `AF_LLM_TEMPERATURE` | `0.2` | default temperature |
| `AF_LLM_MAX_TOKENS` | `512` | default `num_predict` |

All four languages must read these same names.

---

## 6. Per-language API conventions

### Python (snake_case)
```python
from algoforge.llm import OllamaProvider, ChatRequest, ChatMessage
p = OllamaProvider.from_env()
r = p.chat(ChatRequest(model="llama3.1:8b", messages=[ChatMessage("user","hi")]))
print(r.message.content)
```

### JS (camelCase, ESM)
```js
import { OllamaProvider } from 'algoforge/llm';
const p = OllamaProvider.fromEnv();
const r = await p.chat({
  model: 'llama3.1:8b',
  messages: [{ role: 'user', content: 'hi' }],
});
console.log(r.message.content);
```

### C (snake_case, prefixed)
```c
af_llm_provider_t p;
af_llm_provider_from_env(&p);
af_llm_chat_request_t req = {…};
af_llm_chat_response_t resp;
af_llm_error_t err;
if (af_llm_chat(&p, &req, &resp, &err) != 0) { /* handle */ }
af_llm_chat_response_free(&resp);
af_llm_provider_free(&p);
```

### C++ (`namespace algoforge::llm`)
```cpp
auto p = algoforge::llm::OllamaProvider::from_env();
auto r = p.chat({.model="llama3.1:8b",
                 .messages={{"user","hi"}}});
std::cout << r.message.content;
```

---

## 7. Use cases

Each use case is a function that takes structured input, builds a prompt
from a canonical template (see `prompts.py`), calls `provider.chat(...)`,
parses the response, and returns a structured output.

The prompt templates are **byte-identical** across all four languages.
They live in:
- `python/algoforge/llm/prompts.py` (string constants)
- `c/src/llm.c` (`static const char *AF_PROMPT_*`)
- `cpp/src/llm/prompts.cpp`
- `js/src/llm/prompts.js`

### 7.1 `trade_rationale`
**Input:**
```python
@dataclass(frozen=True)
class TradeRationaleInput:
    symbol: str
    side: str                    # "long" | "short"
    entry_price: float
    stop_loss: float
    take_profit: float
    indicators: dict[str, float] # e.g. {"rsi":68.2, "atr":0.0014}
    patterns: list[str]          # e.g. ["BullishFlag", "Hammer"]
    timeframe: str               # "M15" | "H1" | …
```
**Template (system + user):**
```
SYSTEM: You are an experienced trading assistant. Given a fired signal, explain in 1–3 plain sentences why this trade triggered. Be concrete; reference the indicator values and patterns. Do not give financial advice.

USER: SIGNAL
symbol={symbol}
side={side}
timeframe={timeframe}
entry={entry_price}
sl={stop_loss}
tp={take_profit}
indicators={indicators_json_sorted}
patterns={patterns_csv}

Explain.
```
`indicators_json_sorted` is JSON with keys sorted alphabetically for
determinism. `patterns_csv` is comma-joined, in input order.

**Output:**
```python
@dataclass(frozen=True)
class TradeRationaleOutput:
    explanation: str   # 1–3 sentences
    model: str
    tokens: int        # eval_count
```

### 7.2 `backtest_commentary`
**Input:**
```python
@dataclass(frozen=True)
class BacktestCommentaryInput:
    strategy_name: str
    period: str                  # "2024-01-01 → 2024-06-30"
    total_return: float
    sharpe: float
    sortino: float
    max_drawdown: float
    win_rate: float
    trade_count: int
    best_trade: float
    worst_trade: float
```
**Template:**
```
SYSTEM: You are an experienced quant analyst. Given a backtest summary, write a 3–5 sentence narrative covering: overall result, risk-adjusted performance, drawdown behavior, and one concrete improvement suggestion. Plain prose, no bullet points.

USER: BACKTEST
strategy={strategy_name}
period={period}
total_return={total_return:.4f}
sharpe={sharpe:.4f}
sortino={sortino:.4f}
max_drawdown={max_drawdown:.4f}
win_rate={win_rate:.4f}
trade_count={trade_count}
best_trade={best_trade:.4f}
worst_trade={worst_trade:.4f}

Comment.
```
**Output:**
```python
@dataclass(frozen=True)
class BacktestCommentaryOutput:
    commentary: str
    model: str
    tokens: int
```

### 7.3 `news_sentiment`
**Input:** `headline: str`, optional `body: str`
**Template:**
```
SYSTEM: You are a financial news sentiment classifier. Given a headline (and optional body), respond with exactly one JSON object: {"sentiment":"bullish|bearish|neutral","score":-1.0..1.0,"confidence":0.0..1.0,"reason":"<one short sentence>"}. No prose outside the JSON.

USER: HEADLINE: {headline}
BODY: {body_or_empty}
```
**Output:**
```python
@dataclass(frozen=True)
class NewsSentimentOutput:
    sentiment: str        # "bullish" | "bearish" | "neutral"
    score: float          # -1.0 .. 1.0
    confidence: float     # 0.0 .. 1.0
    reason: str
```
Parser: extract the first JSON object from the response (regex
`\{[^}]*\}` with a recursion-aware scanner — see §9). On parse failure,
raise `LLMError(kind="decode")`.

### 7.4 `strategy_describe`
**Input:**
```python
@dataclass(frozen=True)
class StrategyDescribeInput:
    name: str
    indicators_used: list[str]
    entry_rules: list[str]
    exit_rules: list[str]
    risk_rules: list[str]
    timeframe: str
```
**Template:**
```
SYSTEM: You are a trading strategy documenter. Given a structured strategy spec, write a 2–4 sentence plain-English description suitable for a strategy catalog. Do not invent rules.

USER: STRATEGY
name={name}
timeframe={timeframe}
indicators={indicators_csv}
entry_rules={entry_rules_csv}
exit_rules={exit_rules_csv}
risk_rules={risk_rules_csv}

Describe.
```
**Output:**
```python
@dataclass(frozen=True)
class StrategyDescribeOutput:
    description: str
    model: str
    tokens: int
```

---

## 8. CLI surface

Each language ships a CLI with subcommands. Same name, same flags.

### Python (`python -m algoforge.llm`)
```
algoforge.llm rationale  --signal signal.json    [--model M] [--out out.txt]
algoforge.llm commentary --backtest results.json [--model M] [--out out.txt]
algoforge.llm sentiment  --headline "…" [--body "…"]
algoforge.llm describe   --strategy strategy.json
algoforge.llm health
algoforge.llm models
```
Exit codes: `0` ok, `1` user error, `2` LLM unreachable, `3` decode error.

### JS (`node js/src/llm/cli.js` or `npm run llm -- …`)
Same subcommands.

### C (`c/build/af_llm`) and C++ (`cpp/build/Release/af_llm_cli`)
Same subcommands.

---

## 9. Determinism, JSON extraction, parsing

- All chat calls include `seed=42` and `temperature=0.2` by default. With the
  same model + seed + prompt, Ollama is approximately deterministic; tests
  do not depend on model output content — they replay fixtures.
- JSON extraction (for `news_sentiment`): scan the response for the first
  balanced `{…}` substring using a depth counter that respects `"` strings
  and `\"` escapes. All four languages implement the same scanner.
- All floats serialized into prompts use `"%.4f"` formatting (locale-independent).
- Dict serialization uses keys sorted alphabetically.

---

## 10. Testing

Every language has the same three test groups:

1. **Provider tests** — replay each `*_basic` fixture against a `MockTransport`
   that intercepts the HTTP call, asserts the outgoing request body matches
   the fixture's `request.body` (after key-sort normalization), and returns
   `response.body`. Verify the parsed `CompletionResponse`/`ChatResponse`
   fields.
2. **Use-case tests** — replay each use-case fixture. Verify the rendered
   prompt matches the fixture's `request.body.messages[1].content`
   byte-for-byte. Verify the parsed output structure.
3. **Error tests** — `MockTransport` returns 503 → `LLMError(kind="http")`.
   Connection refused → `LLMError(kind="unreachable")`. Timeout → `kind="timeout"`.
   Malformed JSON in sentiment response → `kind="decode"`.

The fixtures **are committed** under `tests/fixtures/llm/`. Same files
referenced by all four test suites. Numerical/string fields are byte-stable.

No live Ollama needed for tests. A separate manual smoke script
(`scripts/smoke_llm.sh`) hits a real Ollama if available.

---

## 11. Dashboard surface (Python only)

### 11.1 Endpoints
| Method | Path | Auth | Notes |
|---|---|---|---|
| GET  | `/api/llm/health` | bearer | `{ok,model_loaded,model}` |
| GET  | `/api/llm/models` | bearer | `[{name,size,modified_at},…]` |
| POST | `/api/llm/chat`   | bearer | Body: `{messages:[…], model?}` — non-streaming |
| GET  | `/api/llm/chat/stream` | bearer (header OR `?token=`) | SSE token stream. Query: `prompt`, optional `model`. |
| GET  | `/api/health` | none | Extended with `llm_connected: bool`, `llm_model: str\|null`. |

### 11.2 SSE format (chat stream)
Each event is a single `data:` line, one JSON object:
```
data: {"delta":"E", "done":false}
data: {"delta":"U", "done":false}
…
data: {"delta":"", "done":true, "finish_reason":"stop"}
```
Client closes the EventSource after `done=true`.

### 11.3 Concurrency / cancellation
- One in-flight chat per session. Overlapping request → 429.
- Client disconnect (`request.is_disconnected()`) → cancel the Ollama
  request and break the loop.
- Per-request timeout from `AF_LLM_TIMEOUT`.

### 11.4 Provider injection
`make_app(...)` grows an optional `llm: LLMProvider | None = None` parameter.
If `None`, the LLM endpoints return 503 with `{"error":"llm_disabled"}`.
The `/api/health` field `llm_connected` reflects the provider's `health()`
result (cached for 5 seconds).

### 11.5 UI (`static/`)
- Collapsible right-hand panel ("AI Assistant").
- Message list (user / assistant bubbles).
- Text input + send button.
- Quick-action buttons inject context:
  - **Explain last trade** → fetches last closed position, sends as user msg
  - **Summarize equity curve** → fetches recent bars, sends as user msg
  - **Comment on open positions** → fetches `/api/positions`, sends as user msg
- Streaming render via EventSource. Token append on each `data:` event.
- Abort button cancels the EventSource mid-stream.
- Token / endpoint persisted in `localStorage` (same as existing UI).

### 11.6 Test pattern reminder
SSE chat tests **must** iterate `StreamingResponse.body_iterator` directly
under `asyncio.run()` with `wait_for` timeouts. TestClient + ASGITransport
deadlock on infinite-style streams. Auth tests (401) can stay on TestClient
because the failure happens before the stream starts. This is the same
pattern as `/api/stream/ticks` in S3.

---

## 12. Versioning / compatibility

- Wire protocol is Ollama 0.1.x — the request/response shapes above are the
  contract. If Ollama changes, only `OllamaProvider` changes; use cases and
  the interface do not.
- Adding a use case is additive — interface stays the same.
- Future provider (e.g. `LlamaCppProvider`) implements the same
  `LLMProvider` interface and the use cases keep working unchanged.
