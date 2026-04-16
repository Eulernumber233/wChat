# wChat AgentServer

LLM-powered smart reply suggestion service for the wChat IM system.
Independent Python microservice; calls no C++ services this milestone
(data comes from `MockBackend` + JSON fixtures).

## Quick start

Requires **Python 3.12** (3.11 will also work but pyproject pins 3.12
for type-system features).

```bash
cd wChat_AgentServer
python -m venv .venv
# Windows PowerShell
.venv\Scripts\Activate.ps1
# or bash
source .venv/bin/activate

pip install -e ".[dev]"

# Configure secrets
cp .env.example .env
# edit .env and put your DEEPSEEK_API_KEY

cp config/agent.ini.example config/agent.ini
# edit config/agent.ini if you want non-default ports / options
```

## Running the server

```bash
uvicorn app.main:app --reload --port 8200
```

Endpoints:

| Method | Path                          | Status           |
|--------|-------------------------------|------------------|
| GET    | `/agent/health`               | implemented      |
| GET    | `/agent/presets`              | implemented      |
| POST   | `/agent/suggest_reply`        | implemented      |
| POST   | `/agent/refine`               | implemented      |
| GET    | `/agent/suggest_reply/stream` | **501 reserved** |

OpenAPI UI: `http://localhost:8200/docs`.

## Manual smoke test

With the server running:

```bash
curl -X POST http://localhost:8200/agent/suggest_reply \
  -H "Content-Type: application/json" \
  -d '{
    "self_uid": 1001,
    "peer_uid": 2002,
    "recent_messages": [
      {"msg_db_id":"101","from_uid":2002,"to_uid":1001,"msg_type":1,"content":"在吗","send_time":1713150000,"direction":0},
      {"msg_db_id":"102","from_uid":1001,"to_uid":2002,"msg_type":1,"content":"在的 咋了","send_time":1713150030,"direction":1},
      {"msg_db_id":"103","from_uid":2002,"to_uid":1001,"msg_type":1,"content":"能借我五千块不 下月还","send_time":1713150080,"direction":0}
    ],
    "preset_id": "polite_decline",
    "num_candidates": 3
  }'
```

Follow up with refine:

```bash
curl -X POST http://localhost:8200/agent/refine \
  -H "Content-Type: application/json" \
  -d '{"session_id":"<from-previous-response>","candidate_index":0,"instruction":"再简短些"}'
```

## Testing

```bash
pytest               # unit + api tests with FakeLLM, no DeepSeek calls
pytest -v tests/test_agent_flow.py
```

All tests use a scripted `FakeLLM` (see [tests/conftest.py](tests/conftest.py)).
No DeepSeek quota is consumed by the suite.

## Architecture

```
app/
  api/         HTTP routes (FastAPI)
  agent/       LangGraph nodes + compiled graph (AgentService)
  tools/       Tool registry (get_chat_history / get_friend_profile /
               get_relationship_summary / search_past_similar stub)
  llm/         LLMProvider abstraction + DeepSeek implementation
  presets/     Scenario preset loader (config/presets.yaml)
  memory/      Short-term session store (in-memory dict; Redis in M2)
  rpc/         Placeholder for gRPC client to ChatServer (see rpc/README.md)
  config/      Settings loader (.env + agent.ini)
  schemas/     Pydantic contracts (aligned with wChat_client TextChatData)
```

Flow of a request:

```
POST /agent/suggest_reply
        │
        ▼
  AgentService.suggest_reply
        │
        ▼
  LangGraph compiled once per (self_uid, peer_uid):
    analyze_intent  → one LLM call, decides which tools to invoke
      │
      ▼
    fetch_profile  → (conditional, no-op if not requested)
    fetch_history  → (conditional)
    fetch_summary  → (conditional)
      │
      ▼
    generate       → one LLM call in JSON mode, emits N candidates
        │
        ▼
  MemoryStore.put(session_id, state_snapshot)
        │
        ▼
  SuggestReplyResponse
```

## What's deferred to Milestone 2 (network integration)

- Actual gRPC client to ChatServer (`app/rpc/agent_data_client.py` is a
  placeholder). See [app/rpc/README.md](app/rpc/README.md) for proto additions.
- SSE streaming endpoint (plumbing present in `app/llm/streaming.py`,
  route returns 501 for now).
- Auth: `Authorization: Bearer <token>` is accepted but not validated.
- Redis-backed session memory + rate limiting.
- Real `get_relationship_summary` (currently returns a stub string).
- RAG (`search_past_similar` returns empty list when disabled).

## Adding a preset

Edit [config/presets.yaml](config/presets.yaml). No code changes needed.
Restart the server to pick it up (no hot-reload, matches C++-side
ConfigMgr convention).

## Changing the LLM

Swap providers by implementing `app/llm/base.py::LLMProvider` and wiring
it in [app/api/deps.py](app/api/deps.py). DeepSeek is OpenAI-compatible,
so most OpenAI-compatible providers (Moonshot, Qwen, etc.) can reuse
`DeepSeekProvider` with a different base_url.

## Where the DeepSeek key goes

Two options, `.env` takes priority:

1. **`.env`** (recommended, gitignored):
   ```
   DEEPSEEK_API_KEY=sk-xxxxx
   ```
2. **`config/agent.ini`** (also gitignored):
   ```ini
   [LLM]
   ApiKey=sk-xxxxx
   ```

Never commit either file. Templates (`.env.example`, `config/agent.ini.example`)
are safe to commit.
