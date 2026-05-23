# LLM Setup (S2)

AlgoForge's S2 layer adds a local LLM via [Ollama](https://ollama.com)
across all four implementations (Python reference, C, C++, JS). This
document covers installation, configuration, and verification.

For the spec (prompt templates, wire format, fixture protocol), see
**[llm_spec.md](llm_spec.md)**.

---

## 1. Install Ollama

### macOS

```bash
brew install ollama
ollama serve &        # starts the local server on :11434
```

### Linux

```bash
curl -fsSL https://ollama.com/install.sh | sh
sudo systemctl enable --now ollama
```

### Windows

Download the installer from <https://ollama.com/download>. The Windows
service binds `127.0.0.1:11434` by default.

### Verify

```bash
curl http://localhost:11434/api/tags
```

Should return JSON.

## 2. Pull the default model

```bash
ollama pull llama3.1:8b
```

(~4.7 GB.) For the embedding use case:

```bash
ollama pull nomic-embed-text
```

(~275 MB.)

## 3. Environment variables

All four implementations read the same env vars:

| Variable | Default | Notes |
|---|---|---|
| `AF_LLM_PROVIDER` | `ollama` | reserved for future providers |
| `AF_LLM_HOST` | `http://localhost:11434` | base URL |
| `AF_LLM_MODEL` | `llama3.1:8b` | default chat/completion model |
| `AF_LLM_EMBED_MODEL` | `nomic-embed-text` | default embedding model |
| `AF_LLM_TIMEOUT` | `60` | seconds |
| `AF_LLM_SEED` | `42` | seed sent to Ollama for determinism |
| `AF_LLM_TEMPERATURE` | `0.2` | default temperature |
| `AF_LLM_MAX_TOKENS` | `512` | default `num_predict` |

## 4. Verify per-language

### Python (reference)

```bash
cd python
pip install -e .[dashboard]
python -m algoforge.llm health
python -m algoforge.llm chat "Explain a long EURUSD breakout in one sentence."
```

### JS

```bash
cd js
node src/llm/cli.js health
node src/llm/cli.js chat "Summarize a trending market in one sentence."
```

### C

```bash
cd c
make bin
# Requires libcurl. If curl-config isn't found, build falls back to the
# stub transport (no network) — fine for tests but not live use.
```

### C++

```bash
cd cpp
./scripts/build.sh
# Same libcurl requirement as the C build.
```

## 5. Dashboard chat panel

The Python dashboard exposes the chat panel automatically when an
`LLMProvider` is wired into `make_app(llm=...)`. With Ollama running and
env vars set, launching the dashboard:

```bash
cd python
python -m algoforge.dashboard --port 8000
```

reveals a collapsible LLM Chat panel in the bottom-right corner. The
panel auto-detects health on load:

- **Online** (green): live chat, three quick-action buttons, streaming
  via SSE.
- **Offline** (red): Ollama unreachable — start `ollama serve` and click
  Reconnect.
- **Disabled**: dashboard was constructed without an `llm` argument.

## 6. Endpoints (Python dashboard)

| Method | Path | Behavior |
|---|---|---|
| GET | `/api/llm/health` | `{"status":"ok","host":...,"model":...}` or 503 |
| GET | `/api/llm/models` | `{"models":[...]}` |
| POST | `/api/llm/chat` | non-streaming chat |
| POST | `/api/llm/chat/stream` | SSE: `data: <token>\n\n` lines, ends with `data: [DONE]` |

## 7. Building without libcurl (C / C++)

If `curl-config` is unavailable, the C Makefile defines `-DAF_NO_LIBCURL`
and links against an in-process stub transport. The library still builds
and tests pass (mock transport drives them), but real Ollama calls are
disabled. Install libcurl (`brew install curl` or
`apt-get install libcurl4-openssl-dev`) for live use.

## 8. Cross-language parity

All four ports validate against the **same 16 canonical fixtures** at
`tests/fixtures/llm/*.json`. Each fixture contains:

- `input` — typed input struct
- `request` — expected outbound HTTP request (method, path, body)
- `response` — canned upstream response

If you add a new fixture, every language's test for that fixture must
either pass or be skipped — never silently diverge.

## 9. Troubleshooting

| Symptom | Likely cause |
|---|---|
| `502 Bad Gateway` from `/api/llm/chat` | Ollama unreachable — `ollama serve` |
| `504 Gateway Timeout` | `AF_LLM_TIMEOUT` too low for the model |
| `503 llm_disabled` | dashboard built with `llm=None` — wire a provider in |
| `model_missing` error | run `ollama pull <model>` |
| Tests fail with byte-level prompt diffs | a prompt template was edited — fixtures must be regenerated from the Python reference and committed |
