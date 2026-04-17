# RPC Layer — Milestone 2

This directory holds the gRPC client that bridges AgentServer and
ChatServer (C++). 当前为占位，M1 期间所有数据走
`app/tools/mock_backend.py`；M2 按步骤接入。

## 为什么走 RPC 而不是直连 MySQL

已经对齐的架构决策（项目根 [CLAUDE.md](../../../CLAUDE.md) §13.2）：

- **权限边界统一**：`utoken_<uid>` 校验、"user A 能否读 user B 的消息"这类
  策略已在 ChatServer 里实现，Agent 直连 DB 意味着要再实现一遍。
- **热缓存亲和**：`ubaseinfo_<uid>` 是 ChatServer 维护的热缓存，绕过它直查
  MySQL 没意义。
- **schema 漂移风险**：项目已有 4 份 `MysqlDao` C++ 副本（§11.1），再加一
  份 Python 副本只会更糟。
- **Agent 仍需直连 Redis**：仅用于 token 校验和会话记忆/限流，不碰业务数据。
  这不矛盾——Redis 的这些 key 本质是"鉴权态"而非"业务态"。

## Planned proto additions

两处改动要一起做（都加到 `proto/message.proto`）：

### (1) 新 service：`AgentDataService`

ChatServer 侧实现（复用现有 `MysqlDao::GetMessagesPage` 和 Redis
`ubaseinfo_<uid>`）。端口复用 ChatServer 现有的 gRPC 端口（50055 / 50056），
`main.cc` 注册多一个 service 即可。

```proto
service AgentDataService {
  rpc GetChatHistory   (GetChatHistoryReq)   returns (GetChatHistoryRsp);
  rpc GetFriendProfile (GetFriendProfileReq) returns (GetFriendProfileRsp);
}

message GetChatHistoryReq {
  int32 self_uid = 1;
  int32 peer_uid = 2;
  int32 limit = 3;
  // int64 on the wire; C++ side converts via string due to jsoncpp int64
  // limitation on downstream JSON paths
  int64 before_msg_db_id = 4;
  string auth_token = 5;   // Redis utoken_<uid> verified by ChatServer
}

message GetChatHistoryRsp {
  int32 error = 1;
  repeated ChatMessageRow messages = 2;
  bool has_more = 3;
}

message ChatMessageRow {
  int64  msg_db_id = 1;
  int32  from_uid = 2;
  int32  to_uid = 3;
  int32  msg_type = 4;
  string content = 5;
  int64  send_time = 6;
  int32  direction = 7;    // 0 incoming, 1 outgoing (relative to self_uid)
}

message GetFriendProfileReq {
  int32 self_uid = 1;
  int32 peer_uid = 2;
  string auth_token = 3;
}

message GetFriendProfileRsp {
  int32 error = 1;
  int32 uid = 2;
  string name = 3;
  string nick = 4;
  int32 sex = 5;
  string desc = 6;
  string icon = 7;
}
```

### (2) 扩展现有 `GetChatServerRsp`（StatusServer 登录响应）

客户端登录时一次性拿到 Agent 地址，不走 Gate 反代、不硬编码。

```proto
message GetChatServerRsp {
  int32  error     = 1;
  string host      = 2;   // ChatServer TCP host（已有）
  string port      = 3;   // ChatServer TCP port（已有）
  string token     = 4;   // utoken_<uid>（已有）
  string agent_host = 5;  // 新增：AgentServer HTTP host
  int32  agent_port = 6;  // 新增：AgentServer HTTP port
}
```

StatusServer 读 `[AgentServer]` 配置段下发，客户端 `HttpMgr`
登录响应解析后存进 `UserMgr`，后续 AI 请求直连 `http://agent_host:agent_port/agent/...`。

## ChatServer-side wiring (C++, future)

New methods on `ChatServiceImpl` (or a sibling `AgentDataServiceImpl` — preferred
to keep peer-notify RPCs separate from data-fetch RPCs):

- `GetChatHistory`  → `MysqlDao::GetMessagesPage` (already exists, see
  `wChat_server/wChat_server_tcp/MysqlDao.h:257`)
- `GetFriendProfile` → `RedisMgr.Get("ubaseinfo_<uid>")` with MySQL fallback

Token validation must reuse the existing `utoken_<uid>` Redis check from
`LoginHandler`.

## Python-side TODO list

When this milestone is picked up:

1. Run protoc for Python:
   ```
   cd proto
   python -m grpc_tools.protoc -I. --python_out=../wChat_AgentServer/app/rpc/gen \
       --grpc_python_out=../wChat_AgentServer/app/rpc/gen message.proto
   ```
2. Implement `GrpcAgentDataClient` in `agent_data_client.py`.
3. Switch `app/main.py` wiring from `MockBackend` to `GrpcAgentDataClient`
   when `settings.backend.mode == "grpc"`.
4. Add request auth: client sends `Authorization: Bearer <token>`, AgentServer
   forwards the token to ChatServer via gRPC metadata.

## HTTP API auth（M2 Step 1 同步实现）

- Client 发请求带 `Authorization: Bearer <token>`（复用 StatusServer 登录时
  发的 `utoken_<uid>`）。
- AgentServer 有**自己的 Redis 连接**直接校验 token：`GET utoken_<self_uid>`
  比对 Bearer。**不走 ChatServer RPC**——Agent 也不想为了鉴权多一跳。
- 请求 body 里 `self_uid` 必须与 token 对应的 uid 一致，否则 403。
- 数据路径的鉴权由 ChatServer 自己做：Agent 调 `AgentDataService` 时，把
  token 放进 gRPC metadata（key `auth_token`），ChatServer 端再次校验
  `Redis utoken_<self_uid>` 一致。
