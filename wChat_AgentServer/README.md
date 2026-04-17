# wChat AgentServer

LLM-powered smart reply suggestion service for the wChat IM system.
Independent Python microservice. 本服务**不直连 MySQL**——业务数据通过
gRPC 调 ChatServer 的 `AgentDataService` 拿；只**直连 Redis** 做 token
校验和会话记忆。客户端经由 StatusServer 在登录时拿到的 `agent_host/port`
直连本服务。

| 关系 | 协议 | 用途 |
|---|---|---|
| Client ↔ AgentServer | HTTP + SSE | 提建议、润色、流式 |
| AgentServer ↔ ChatServer | gRPC `AgentDataService` | 取聊天历史、好友资料 |
| AgentServer ↔ Redis | 直连 | 校验 `utoken_<uid>`、会话记忆、限流计数 |
| AgentServer ↔ DeepSeek | HTTP | LLM 推理 |

M1 阶段以 `MockBackend` + fixture 替代 gRPC，可完全离线跑通。M2 进行中，
分步骤接入真实网络链路，详见项目根 [CLAUDE.md](../CLAUDE.md) §13.9。

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
| POST   | `/agent/suggest_reply/stream` | implemented (SSE) |

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

## Milestone 2 路线（已对齐架构，分步推进）

按这个顺序做，每步独立可跑测试：

1. **鉴权 + Redis memory + 限流**（纯 Python，C++ 侧零改动）
   - `Authorization: Bearer <token>` → Agent 直连 Redis 查 `utoken_<uid>`
   - `MemoryStore` Redis 实现（TTL 1h，key `agent_session:<sid>`）
   - `agent_quota_<uid>_<date>` INCR + 日末过期
2. **proto 扩展**：[proto/message.proto](../proto/message.proto) 新增 `AgentDataService` + 给 `GetChatServerRsp` 加 `agent_host/agent_port`
3. **ChatServer 实现 `AgentDataService`**（C++）：`GetChatHistory` 复用 `MysqlDao::GetMessagesPage`；`GetFriendProfile` Redis → MySQL 兜底
4. **StatusServer 下发 Agent 地址** + 客户端登录链路保存
5. **Python `GrpcAgentDataClient`** 接替 `MockBackend`（接口形状已对齐，deps.py 一行切换）
6. **SSE 流式**：`/agent/suggest_reply/stream` 从 501 改成真正流式（LangGraph `astream`）
7. **客户端 `ChatPage` AI 面板 UI**

未来但不急：
- 真 `get_relationship_summary`（当前是 stub）
- RAG `search_past_similar`（当前返回空列表）

详细拆解见项目根 [CLAUDE.md](../CLAUDE.md) §13.9。

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
