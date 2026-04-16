# RPC Layer — Deferred to Milestone 2

This directory holds the future gRPC client that bridges AgentServer and
ChatServer (C++). It is **not implemented yet** — the agent currently
runs against `app/tools/mock_backend.py`.

## Planned proto additions

To be added to `proto/message.proto` (shared by all C++ services):

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

## HTTP API auth (also deferred)

AgentServer's own HTTP auth is stubbed. The plan:
- Client sends `Authorization: Bearer <token>` (reuses `utoken_<uid>` issued
  by StatusServer at login).
- AgentServer has its own Redis connection to validate the token before
  routing the request — avoids an extra RPC hop per call.
