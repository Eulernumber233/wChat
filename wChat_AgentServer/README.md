# wChat AgentServer

wChat IM 系统的 AI 智能回复辅助子系统。独立 Python 微服务，给 Qt 客户端的
`ChatPage` 提供"分析对方意图 → 按需拉取上下文 → 生成多条候选回复"的 AI 辅助。

本服务**不直连 MySQL**——业务数据通过 gRPC 调 ChatServer 的 `AgentDataService`
拿，复用主服务的 token 校验和 Redis 热缓存；只**直连 Redis** 做 Bearer Token
校验、会话记忆、日限流。客户端经由 StatusServer 在登录时拿到的
`agent_host/agent_port` 直连本服务。

| 关系 | 协议 | 用途 |
|---|---|---|
| Client ↔ AgentServer | HTTP + SSE | 提建议、润色、流式推送 |
| AgentServer ↔ ChatServer | gRPC `AgentDataService` | 取聊天历史、好友资料 |
| AgentServer ↔ Redis | 直连 | 校验 `utoken_<uid>`、会话记忆、限流计数 |
| AgentServer ↔ DeepSeek | HTTP (OpenAI 兼容) | LLM 推理 |

支持两种后端模式：`mock`（fixture 驱动，离线开发）/ `grpc`（生产联调，接 ChatServer）。
切换只需 `config/agent.ini` 里改一行 `[Backend] Mode=`。

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

| Method | Path                          | 说明 |
|--------|-------------------------------|------|
| GET    | `/agent/health`               | 健康检查 |
| GET    | `/agent/presets`              | 列出所有场景预设 |
| POST   | `/agent/suggest_reply`        | 主入口：生成 N 条候选回复 |
| POST   | `/agent/refine`               | 对某条候选发起润色迭代 |
| POST   | `/agent/suggest_reply/stream` | SSE 流式变体，token 级增量推送 |

除 `/agent/health` 外所有端点都要求请求头：
- `Authorization: Bearer <token>` — 对应 Redis `utoken_<uid>`
- `X-Self-Uid: <uid>` — 和 body 里的 `self_uid` 双向校验

OpenAPI UI: `http://localhost:8200/docs`.

## Manual smoke test

先在 Redis 里塞一个测试 token（绕过真实登录流程）：

```bash
redis-cli -p 6380 -a 123456 SET utoken_1001 test-tok
```

然后发建议请求（注意 `Authorization` + `X-Self-Uid` 两个 header）：

```bash
curl -X POST http://localhost:8200/agent/suggest_reply \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer test-tok" \
  -H "X-Self-Uid: 1001" \
  -d '{
    "self_uid": 1001,
    "peer_uid": 2002,
    "recent_messages": [
      {"msg_db_id":"101","from_uid":2002,"to_uid":1001,"msg_type":1,"content":"在吗","send_time":1713150000,"direction":0},
      {"msg_db_id":"102","from_uid":1001,"to_uid":2002,"msg_type":1,"content":"在的 咋了","send_time":1713150030,"direction":1},
      {"msg_db_id":"103","from_uid":2002,"to_uid":1001,"msg_type":1,"content":"能借我五千块不 下月还","send_time":1713150080,"direction":0}
    ],
    "preset_id": "polite_decline",
    "custom_prompt": "他是我高中同学,关系一般",
    "num_candidates": 3
  }'
```

润色某条候选：

```bash
curl -X POST http://localhost:8200/agent/refine \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer test-tok" \
  -H "X-Self-Uid: 1001" \
  -d '{"session_id":"<from-previous-response>","candidate_index":0,"instruction":"再简短些"}'
```

SSE 流式（用 `curl -N` 保持连接观察事件序列 `intent → candidate_delta*N → candidate_done*N → done`）：

```bash
curl -N -X POST http://localhost:8200/agent/suggest_reply/stream \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer test-tok" \
  -H "X-Self-Uid: 1001" \
  -d '{...同上...}'
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
  api/         HTTP routes (FastAPI) — suggest / refine / stream / presets / health
  agent/       LangGraph nodes + graph builders + AgentService facade
  tools/       Tool registry + ChatBackend Protocol + MockBackend
  rpc/         GrpcAgentDataClient + generated proto stubs (gen/)
  llm/         LLMProvider abstraction + DeepSeek implementation + streaming utils
  presets/     Scenario preset loader (config/presets.yaml)
  memory/      Session store — in-memory dict or Redis-backed (settings switch)
  security/    Bearer token auth (Redis-validated) + daily rate limit
  config/      Settings loader (.env + agent.ini)
  schemas/     Pydantic contracts (aligned with wChat_client TextChatData)
  redis_client.py    Process-wide async Redis singleton
```

Blocking flow (`POST /agent/suggest_reply`):

```
HTTP POST → require_auth (Redis 校验 utoken_<uid>) → rate_limit check
    │
    ▼
AgentService.suggest_reply(req, preset, auth_token=...)
    │  (每请求新建 tool_factory → ToolRegistry，tools 各自携带本次 token)
    ▼
LangGraph full pipeline:
  analyze_intent      → LLM call: returns intent + which tools needed
      │
      ▼
  fetch_profile       → conditional: GrpcAgentDataClient.fetch_profile(token)
  fetch_history       → conditional: GrpcAgentDataClient.fetch_history(token)
  fetch_summary       → conditional: LLM-backed relationship summary (stub)
      │
      ▼
  generate_candidates → LLM call (JSON mode): emits strategy + N candidates
    │
    ▼
MemoryStore.put(session_id, state_snapshot) [Redis TTL 1h 或 in-memory]
    │
    ▼
SuggestReplyResponse { session_id, candidates, intent_analysis, strategy, ... }
```

Streaming variant (`POST /agent/suggest_reply/stream`) runs a `build_prep_graph`
(analyze_intent + fetch_*) blocking, emits `intent` SSE event, then
streams the generate LLM call token-by-token as `candidate_delta` events,
finally emits parsed `candidate_done` events and a `done` event.

## 实现状态（M2 全部完成）

7 步实施计划全部上线，51/51 测试用例通过：

| Step | 说明 |
|---|---|
| 1 | 鉴权（Redis Bearer Token）+ Redis 会话记忆 + 日限流 |
| 2 | Proto 扩展：`AgentDataService` + `GetChatServerRsp.agent_host/agent_port` |
| 3 | ChatServer `AgentDataServiceImpl`：`GetChatHistory` + `GetFriendProfile` |
| 4 | StatusServer 下发 AgentServer 地址，Gate 透传，客户端 `UserMgr` 保存 |
| 5 | `GrpcAgentDataClient` 接替 `MockBackend` |
| 6 | SSE 流式端点 |
| 7 | Qt 客户端 `ChatPage` AI 面板：场景预设 + 背景补充 + 候选采用 / 润色 |

未来扩展方向：
- 真 `get_relationship_summary`（当前为 stub）
- RAG `search_past_similar`（当前返回空）
- 独立 AgentServer 集群与更丰富的调度策略

详细设计见项目根 [CLAUDE.md](../CLAUDE.md) §13。

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
