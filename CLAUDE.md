# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> 本文件供 Claude Code 在每次新会话开始时自动加载。目的：让你（Claude）在不重新阅读全部代码的前提下，快速掌握项目背景、模块边界、跨服务调用链与关键约定，避免陷入"局部修改 / 全局失配"的问题。
>
> 维护原则：当架构、协议、模块职责发生变化时同步更新本文件；纯实现细节（函数体、字段名）不要写进来——读代码即可。

---

## 1. 项目一句话定位

wChat 是一个 **C++ / Qt 实现的分布式即时通讯系统**。客户端是 Qt Widgets 桌面 App，后端由 4 类服务组成（HTTP 网关 / 状态调度 / 邮件验证码 / 多实例聊天服务器），服务间用 **gRPC** 互通，数据落地在 **MySQL**，热数据 / Token / 在线状态在 **Redis**。

技术栈：C++17、Qt5/6、Boost.Asio (Beast for HTTP)、gRPC + Protobuf、MySQL Connector/C++、hiredis、Node.js（仅 VarifyServer）、jsoncpp。

**外部库版本**（选型和 API 兼容性有关时参照）：
- **Boost** 1.81.0（头文件路径 `boost-1_88`，实际版本 1.81）
- **jsoncpp**（libjson）— 旧版，**不支持** `Json::Int64` / `asInt64()`，大整数需要走 `asDouble()` 或字符串中转
- **MySQL Connector/C++** 8.3.0 (winx64)
- **gRPC** + **Protobuf** — 本地编译版本位于 `D:\library\grpc\`

设计模式高频出现：**单例（CRTP `Singleton<T>`）**、**线程池 + IO 上下文池**、**连接池**、**生产者-消费者（LogicSystem 任务队列）**。

---

## 2. 顶层目录结构

```
wChat/
├── proto/                        统一的 protobuf 定义（唯一源头）
│   ├── message.proto             所有 service / message 定义
│   ├── message.pb.h/cc           protoc 生成的 C++ 序列化代码
│   ├── message.grpc.pb.h/cc      protoc 生成的 gRPC stub 代码
│   └── generate_pb.bat           改完 proto 后一键重新生成
├── wChat_client/                 Qt 客户端（VS 工程 + .pro 双轨）
├── wChat_server/
│   ├── wChat_server_gate/        GateServer：HTTP 接入
│   ├── wChat_StatusServer/       StatusServer：Token / ChatServer 调度
│   ├── wChat_VarifyServer/       VarifyServer：Node.js 邮件验证码
│   ├── wChat_server_tcp/         ChatServer：单一代码库，通过不同配置启动多个实例
│   │   ├── configs/
│   │   │   ├── chatserver1.ini   实例 1 配置（TCP 8090 / gRPC 50055）
│   │   │   └── chatserver2.ini   实例 2 配置（TCP 8091 / gRPC 50056）
│   │   └── start_cluster.bat     一键启动多实例集群的脚本
│   ├── wchatmysql.sql            数据库脚本（含存储过程 reg_user）
│   └── wChat_server_diagram.png  架构图
├── docs/
│   └── FileServer_Design.md      文件服务器设计文档（完整协议、流程、实施计划）
├── wChat_LabReport.txt           实验报告（最权威的设计说明，有疑问先查这里）
├── wChat_LabReport.docx          同上 Word 版
├── README.md                     面向人类的项目介绍
└── CLAUDE.md                     本文件
```

> **重要**：3 个 C++ 服务的目录里仍然有大量**同名同构文件**（`CServer.*`、`AsioIOContextPool.*`、`LogicSystem.*`、`MysqlMgr.*`、`RedisMgr.*`、`ConfigMgr.*`、`Singleton.h`）。它们是**各自独立的拷贝**，不是共享代码——修改一处不会同步到其他服务，必要时需要逐个同步。这是当前项目的一个已知现实，改公共类时务必注意。
>
> **Proto 已统一**：`message.proto` 及其生成物（`message.pb.*`、`message.grpc.pb.*`）已合并到项目根目录 `proto/`，所有 C++ 服务通过 vcxproj / PropertySheet.props 的相对路径 `..\..\proto` 引用同一份文件，VarifyServer (Node.js) 也通过 `proto.js` 中的相对路径指向 `proto/message.proto`。改 proto 后只需运行 `proto/generate_pb.bat` 一次即可。
>
> **ChatServer 横向扩展**：过去的 `wChat_server_tcp_1` / `wChat_server_tcp_2` 已合并为单一目录 `wChat_server_tcp`。多实例通过"同一份可执行文件 + 不同 `configs/*.ini`"启动——`main(argc, argv)` 把 `argv[1]` 作为配置文件路径传给 `ConfigMgr::SetConfigPath()`；若不传参数则回退到 `./config.ini`（保留旧行为）。要加实例，只需要在 `configs/` 下新增一个 ini 并在 `start_cluster.bat` 里追加一行 `start` 即可。

---

## 3. 服务职责与端口（默认配置）

| 服务 | 主要协议 | 默认端口 | 职责 |
|---|---|---|---|
| GateServer | HTTP (Beast) | 8080 | 注册 / 登录 / 找回密码 / 获取验证码的 HTTP 入口。无状态，所有业务靠 gRPC 转发到 Status / Varify，靠 MySQL/Redis 落地。 |
| StatusServer | gRPC | 50052 | 接收 Gate 的 `GetChatServer` 请求，按负载分配某个 ChatServer 的 (Host, Port)，生成与 uid 绑定的 Token 写入 Redis。 |
| VarifyServer (Node.js) | gRPC | 50051 | 收到 `GetVarifyCode` → 生成 4 位验证码 → 写 Redis（带 TTL）→ 用 nodemailer 发邮件。 |
| ChatServer（多实例） | TCP + gRPC | 实例 1：TCP 8090 / RPC 50055；实例 2：TCP 8091 / RPC 50056；… | 与客户端长连接，处理好友 / 消息业务。同时作为 gRPC server 接收其他 ChatServer 的转发请求 + 为 AgentServer 提供 `AgentDataService`（读历史/读资料）。同一份可执行文件通过 `configs/chatserverN.ini` 启动多个实例。`[PeerServer]` 小节列出其他对端。 |
| FileServer（规划中） | TCP + gRPC | TCP 8100 | 文件上传 / 下载。ChatServer 生成一次性 file_token 授权上传下载。完成设计见 `docs/FileServer_Design.md`。 |
| AgentServer（M1 完成，M2 接线中） | HTTP (FastAPI) + SSE | 8200 | Python 独立服务。给客户端 `ChatPage` 提供 AI 智能回复建议。自身**不直连 MySQL**，通过 gRPC 调 ChatServer 的 `AgentDataService` 取上下文；**直连 Redis** 校验 `utoken_<uid>` + 做会话记忆/限流。详见 §13。 |

> 端口、密码、数据库名都在各服务的 ini 里。ChatServer 实例样例见 `wChat_server/wChat_server_tcp/configs/chatserver1.ini`；Gate / Status / Varify 的配置仍在各自目录下的 `config.ini`。

---

## 4. 公共服务端组件（每个 C++ 服务都有同名同构副本）

| 类 | 角色 | 关键点 |
|---|---|---|
| `CServer` | 异步监听器 | `boost::asio::ip::tcp::acceptor` + `async_accept` 递归自调用；每接到连接就从 `AsioIOContextPool` 拿一个 io_context 构造 Session。 |
| `AsioIOContextPool` | IO 多线程池 | 单例。按 CPU 核心数创建 N 个 `io_context`，每个跑在独立线程，配 `executor_work_guard` 保活。`GetIOContext()` 取引用。 |
| `LogicSystem` | 业务消息队列 | 单例。**网络线程 → 投递任务 → 业务线程消费**。构造时启动单独 `_worker_thread` 跑 `DealMsg()`，用 `std::condition_variable` 等队列。`RegisterCallBacks()` 在构造时建立 `msg_id → 处理函数` 的 map。 |
| `MysqlMgr` / `MysqlDao` | DB 访问单例 | 通过 `MySqlPool` 取连接；对外暴露 `CheckPwd / RegUser / AddFriend / GetMessages` 等。 |
| `RedisMgr` | 缓存访问单例 | 通过 `RedisConPool` 取 hiredis 连接；常用 key 前缀见 §7。 |
| `ConfigMgr` | 配置读取 | 解析 `config.ini` → `map<string, SectionInfo>`。不是单例，但通常以全局对象使用。 |
| `ChatGrpcClient` / `ChatServiceImpl` | 跨 ChatServer 通信 | Client 维护 `ChatConPool`（grpc stub 池）；Service 实现 proto 中定义的 `NotifyAddFriend / NotifyAuthFriend / NotifyTextChatMsg` 等接口，用于把请求送到目标用户所在的另一台 ChatServer。 |
| `UserMgr`（仅 ChatServer 有） | 在线会话表 | 单例。`unordered_map<uid, shared_ptr<CSession>>`，用于本机内"按 uid 直接推消息"。 |
| `StatusGrpcClient` / `VerifyGrpcClient` | gRPC 客户端 | Gate / 其他服务调用 Status / Varify 用的 stub 单例，内部也有连接池。 |

---

## 5. 客户端核心结构

入口：`main.cpp` → `MainWindow` → `LoginDialog` / `RegistDialog` / `ResetDialog` ↔ `ChatDialog`（主界面）。

关键单例（都在客户端 `wChat_client/`）：

| 类 | 职责 |
|---|---|
| `HttpMgr` | 与 GateServer 通信。`PostHttpReq(url, json, ReqId, Modules)` 发请求，QNetworkReply 完成后发 `sig_http_finish(ReqId, json, ErrorCode, Modules)`，由各对话框接收并按 `Modules` / `ReqId` 路由。 |
| `TcpMgr` | 与 ChatServer 长连接。统一信号 `sig_send_data(ReqId, QByteArray)` 发数据；接收时按 `ReqId` 在 `handlers` map 里查处理函数，再 emit 业务信号（`sig_text_chat_msg / sig_add_auth_friend / ...`）。 |
| `UserMgr` | 缓存当前登录用户的：个人信息、好友列表、申请列表、各好友的聊天记录。**所有跨界面的数据共享都走它**，不要在 UI 类里另存一份。 |

主界面 `ChatDialog` 内部的关键组合：

- 侧边导航：`StateWidget`（chat / contact / apply 三个 tab，带红点提示）
- 中部列表：搜索框 `CustomizeEdit` + `SearchList` / `ChatUserList` / `ContactUserList` / `ApplyFriendList`
- 右侧：`ChatPage`（聊天主面板）/ `ApplyFriendPage`（申请管理）/ `FriendInfoPage`
- 消息显示：`ChatView` 承载 `ChatItemBase`，每个 item 内嵌 `TextBubble` / `PictureBubble`

---

## 6. 协议层 —— ReqId 总表（**最关键的全局参考**）

来自 `wChat_client/global.h`，ChatServer 端必须保持一致：

| ID | 名称 | 方向 | 走 HTTP/TCP |
|---|---|---|---|
| 1001 | `ID_GET_VARIFY_CODE` | C → Gate | HTTP |
| 1002 | `ID_REG_USER` | C → Gate | HTTP |
| 1003 | `ID_RESET_PWD` | C → Gate | HTTP |
| 1004 | `ID_LOGIN_USER` | C → Gate | HTTP |
| 1005 | `ID_CHAT_LOGIN` | C → ChatServer | TCP（首包带 uid+token） |
| 1006 | `ID_CHAT_LOGIN_RSP` | ChatServer → C | TCP |
| 1007 / 1008 | 搜索好友 请求 / 回包 | C ↔ ChatServer | TCP |
| 1009 / 1010 | 添加好友 请求 / 回包 | C ↔ ChatServer | TCP |
| 1011 | `ID_NOTIFY_ADD_FRIEND_REQ` | ChatServer → C（被申请方） | TCP |
| 1013 / 1014 | 同意好友 请求 / 回包 | C ↔ ChatServer | TCP |
| 1015 | `ID_NOTIFY_AUTH_FRIEND_REQ` | ChatServer → C（邀请方） | TCP |
| 1017 / 1018 | 文本消息 请求 / 回包 | C ↔ ChatServer | TCP |
| 1019 | `ID_NOTIFY_TEXT_CHAT_MSG_REQ` | ChatServer → C（接收方） | TCP |
| 1021 | `ID_NOTIFY_OFF_LINE_REQ` | ChatServer → C | TCP |
| 1023 / 1024 | 心跳 请求 / 回复 | C ↔ ChatServer | TCP |
| 1025 / 1026 | `ID_PULL_CONV_SUMMARY` 请求 / 回包 | C ↔ ChatServer | TCP（登录后自动拉一次，返回每个会话的 last_msg/unread） |
| 1027 / 1028 | `ID_PULL_MESSAGES` 请求 / 回包 | C ↔ ChatServer | TCP（打开会话时拉 30 条，或滚屏加载更老一页） |
| 1029 / 1030 | `ID_GET_DOWNLOAD_TOKEN` 请求 / 回包 | C ↔ ChatServer | TCP（历史文件消息的按需下载凭证） |
| 1031 | `ID_NOTIFY_KICK_USER` | ChatServer → C | TCP（被踢下线通知，携带 `reason`；详见 §8.6） |
| 1101 / 1102 | 文件上传 请求 / 回包 | C ↔ ChatServer | TCP |
| 1103 | `ID_FILE_NOTIFY_COMPLETE` | ChatServer → C（发送方） | TCP（上传完成 + 带 msg_db_id 让发送方写 LocalDb） |
| 1105 | `ID_FILE_MSG_NOTIFY` | ChatServer → C（接收方） | TCP |

> **改协议时必须同步三处**：客户端 `global.h` 的枚举、ChatServer 的 `LogicSystem::RegisterCallBacks()` 注册、proto 文件（如果涉及跨服 gRPC 字段）。

---

## 7. 数据库 & Redis 约定

**MySQL 表（schema 默认 `wchatmysql`）**：

- `user(uid, name, email, pwd, icon, ...)` — 用户主表，注册走存储过程 `reg_user`
- `friend(self_id, friend_id, back, ...)` — 好友关系，按 `self_id` 索引；一对好友插入两行（双向）
- `chat_conversations(id, user_a_id, user_b_id, last_msg_id, update_time)` — 联合索引 `(user_a_id, user_b_id)`
- `chat_messages(id, conversation_id, sender_id, receiver_id, content, msg_type, status, send_time, read_time)` — 外键到 `chat_conversations.id`，按 `conversation_id` 索引
- `friend_apply(from_uid, to_uid, status, back, ...)` — 申请表，`status=1` 表示已同意

**客户端本地 SQLite**（阶段 C 引入，路径 `<AppDataLocation>/wChat/users/<uid>/db/chat.sqlite`，通过 `LocalDb` 单例访问）：

- `local_messages(msg_db_id PK, peer_uid, direction, msg_type, content, send_time, status)` — 服务端 `chat_messages.id` 做主键天然去重；`content` 原样存服务端 JSON（文本是 `[{msgid,content}, ...]` 数组，文件是单对象）
- `local_files(file_id PK, file_name, file_size, file_type, local_path, download_status, md5, last_access)` — 文件元数据与本地缓存路径,`download_status=2` 表示已下载完成
- `sync_state(peer_uid PK, last_synced_msg_id)` — 每个会话已同步到的最大 `msg_db_id`

**登录后的加载顺序**:`SetUserInfo` → `FileMgr::Init()` → `LocalDb::Open()` → 发 `ID_PULL_CONV_SUMMARY_REQ`。打开某个会话时:先从 LocalDb `LoadRecent(peer, 30)` 渲染,再无条件发 `ID_PULL_MESSAGES_REQ { before_msg_db_id: 0, limit: 30 }` 拉服务端最新一页,收到响应后 `UpsertMessages` + 重建 UI。

**实时消息入本地库的条件**:只在**同服路径**下服务端会在 `ID_NOTIFY_TEXT_CHAT_MSG_REQ` / `ID_FILE_NOTIFY_COMPLETE` / `ID_FILE_MSG_NOTIFY` 的 JSON 里带 `msg_db_id` 字段,客户端 `PersistTextMsgToLocalDb` / `slot_file_upload_persisted` / `slot_file_msg_notify` 才写库。跨服(通过 `ChatGrpcClient::NotifyTextChatMsg` 转发)时 proto 没有这个字段,客户端收到 `msg_db_id=0`,**不写** LocalDb,等下次 `SetUserInfo` 触发的 `ID_PULL_MESSAGES_REQ` 把真实 id 补回来。

**`chat_messages.msg_type` 取值**（客户端 `global.h` 的 `MsgType` 与 ChatServer `core.h` 的 `MsgType` 必须同步，见 `docs/FileServer_Design.md` §2.2）：

| 值 | 含义 | content JSON 格式 |
|---|---|---|
| 1 | text  | `{"msgid":"...","content":"..."}` |
| 2 | image | `{"msgid":"...","file_id":"...","file_name":"...","file_size":N,"file_type":0}` |
| 3 | file  | 同 image，`file_type=1` |
| 4 | audio | 同 image，`file_type=2` |

**Redis 常用 key 前缀**（出现在多个服务的逻辑函数里，搜代码时按这些前缀找）：

- `utoken_<uid>` — 登录 token，登录后由 StatusServer 写入，由 ChatServer 校验
- `uip_<uid>` — 该 uid 当前所在的 ChatServer 名字，**这是跨服路由的关键**
- `usession_<uid>` — 当前活跃 session 的 UUID，**被动踢除的真相来源**（见 §8.6）
- `ubaseinfo_<uid>` — 用户基础信息缓存（搜索好友 / 加载好友列表时优先查此处）
- `lock_<uid>` — 登录互斥分布式锁（`SET NX EX`）
- `code_prefix<email>` — 邮件验证码（VarifyServer 写、Gate 读）

前缀宏定义在 ChatServer `core.h` 的 `USERIPPREFIX`/`USERTOKENPREFIX`/`USER_SESSION_PREFIX`/`USER_BASE_INFO`/`LOCK_PREFIX`。

> 改任何"按 uid 找服务器"的逻辑时，记得 `uip_` 这条线：登录时写入、消息转发时读取、下线时清理。

---

## 8. 跨服务调用链速查（修改任何业务前先看对应链）

### 8.1 登录 + 懒加载
```
Client(LoginDialog)
  └─HTTP POST /user_login─▶ GateServer::LogicSystem(post_handlers)
       ├─ MysqlMgr::CheckPwd
       └─gRPC GetChatServer(uid)─▶ StatusServer::StatusServiceImpl
              └─ 选 ChatServer + 生成 token + 写 Redis(usertokenprefix_)
       ◀───────── 返回 {host, port, token} ─────────
  ◀── HTTP 200 {host, port, token} ──
Client(TcpMgr) ──TCP connect──▶ ChatServer
  ──首包 ID_CHAT_LOGIN {uid, token}──▶ ChatServer::LogicSystem::LoginHandler
       ├─ Redis 校验 token
       ├─ MysqlMgr 加载 base_info / friend_list / apply_list（不含历史消息）
       ├─ Redis 写 useripprefix_<uid> = self_server_name
       ├─ UserMgr::SetUserSession(uid, session)
       └─ 回包 ID_CHAT_LOGIN_RSP（只含好友基本信息,不含 text_array）
Client:
  ├─ AppPaths::SetCurrentUser(uid) → FileMgr::Init() → LocalDb::Open()
  ├─ 跳转主界面 ChatDialog
  └─ 立即发 ID_PULL_CONV_SUMMARY_REQ(uid)
       └─▶ ChatServer::PullConvSummaryHandler → GetConvSummaries SQL
       ◀── ID_PULL_CONV_SUMMARY_RSP {summaries: [{peer, last_msg, unread}, ...]}
Client: UserMgr::ApplyConvSummaries (填好友的 last_msg / unread_count)

打开某个会话:
Client(ChatPage::SetUserInfo)
  ├─ LocalDb::LoadRecent(peer, 30) → 瞬时渲染（首屏）
  └─ ID_PULL_MESSAGES_REQ {peer, before=0, limit=30}
       └─▶ ChatServer::PullMessagesHandler → GetMessagesPage SQL
       ◀── ID_PULL_MESSAGES_RSP {messages, has_more}
Client: LocalDb::UpsertMessages → RefreshFromLocalDb（增量合并到本地库后重建 UI）

历史文件消息的按需下载:
ChatPage::AppendChatMsg (msg_type=IMAGE, _local_path 空)
  └─ ID_GET_DOWNLOAD_TOKEN_REQ {uid, file_id}
       └─▶ ChatServer::GetDownloadTokenHandler → UserCanAccessFile + Redis mint token
       ◀── ID_GET_DOWNLOAD_TOKEN_RSP {file_host, file_port, file_token}
  └─ FileMgr::StartDownload → FileServer TCP → 写 local_files → PictureBubble
```

### 8.2 文本消息发送（含跨服务器路由）
```
Client A: ChatPage::on_send_btn_clicked
  └─TcpMgr 发 ID_TEXT_CHAT_MSG_REQ {fromuid, touid, msg}─▶
ChatServer A::LogicSystem::DealChatTextMsg
  ├─ MysqlMgr::AddMessage（持久化,返回 msg_db_id）
  ├─ Redis Get useripprefix_<touid>
  ├─ 同服(==self_name)? ──▶ UserMgr::GetSession(touid)->Send(ID_NOTIFY_TEXT_CHAT_MSG_REQ)
  └─ 跨服?            ──▶ ChatGrpcClient::NotifyTextChatMsg(peer_addr, ...)
                              └─▶ ChatServer B::ChatServiceImpl::NotifyTextChatMsg
                                     └─ UserMgr::GetSession(touid)->Send(ID_NOTIFY_TEXT_CHAT_MSG_REQ)
  └─ 回包 ID_TEXT_CHAT_MSG_RSP（含 msg_db_id）给 Client A
Client A: sig_text_chat_msg_rsp → PersistTextMsgToLocalDb（写 local_messages）
Client B: TcpMgr 收 ID_NOTIFY_TEXT_CHAT_MSG_REQ（含 msg_db_id）→ PersistTextMsgToLocalDb → ChatDialog 更新 list/page
```

> **修改消息流时的检查清单**（避免改一处坏全局）：
> 1. Client `ChatPage` 构造的 JSON 字段是否变？→ 同步改 ChatServer 解析。
> 2. ID_TEXT_CHAT_MSG_REQ 的字段是否变？→ 同步改 `ChatGrpcClient` / `ChatServiceImpl` / `message.proto`，否则跨服转发会丢字段。
> 3. 持久化字段是否变？→ 同步改 `MysqlDao::AddMessage` SQL 与 `chat_messages` 表结构。
> 4. 接收端 `Client::TcpMgr` 的 `handlers[ID_NOTIFY_TEXT_CHAT_MSG_REQ]` 是否需要更新解析。
> 5. 历史消息加载路径（`GetMessagesPage` + 客户端 `LocalDb::RowToTextChatData`）是否也需要兼容新字段。

### 8.3 加好友 / 同意好友
- 加好友：`Client → ChatServer A (AddFriendApply) → MysqlMgr 写 friend_apply → Redis 查对方所在服务器 → 同服 Session 推 / 跨服 gRPC NotifyAddFriend → Client B 收到 ID_NOTIFY_ADD_FRIEND_REQ → ApplyFriendPage 显示`
- 同意：`Client B → ChatServer (AuthFriendApply) → MysqlMgr 更新 friend_apply 状态 + AddFriend 双向插入 + 创建 chat_conversations → 路由通知邀请方 → 双方 ChatDialog/ContactList 同时更新`

### 8.4 AI 智能回复（AgentServer 接线后）
```
① 登录阶段：客户端同时拿到 ChatServer 和 AgentServer 地址
Client → Gate(/user_login) → Status::GetChatServer
    Status 选定 ChatServer + 选定 AgentServer + 写 utoken_<uid>
    返回 {chat_host, chat_port, agent_host, agent_port, token}
Client: UserMgr 同时保存 chat_* 和 agent_* 地址

② AI 请求阶段：客户端直连 AgentServer，AgentServer 反过来调 ChatServer 拿数据
Client(ChatPage AI 面板)
  └─ HTTP POST http://<agent_host>:<agent_port>/agent/suggest_reply
       Header: Authorization: Bearer <token>
       Body  : {self_uid, peer_uid, recent_messages, preset_id, num_candidates}
       ▼
AgentServer::/agent/suggest_reply
  ├─ require_auth: Redis GET utoken_<uid> == token?（Agent 直连 Redis，不走 ChatServer）
  ├─ RateLimit: Redis INCR agent_quota_<uid>_<date>（日限）
  ├─ LangGraph: analyze_intent → fetch_profile/history/summary → generate
  │    各 fetch_* 节点按需通过 gRPC 调 ChatServer::AgentDataService
  │    └─ gRPC GetChatHistory(self_uid, peer_uid, limit, before_id, auth_token)
  │         └─▶ ChatServer::AgentDataServiceImpl
  │                ├─ 校验 metadata 里的 token == Redis utoken_<self_uid>
  │                └─ MysqlDao::GetMessagesPage（复用现有 SQL）
  │    └─ gRPC GetFriendProfile(self_uid, peer_uid, auth_token)
  │         └─▶ ChatServer: 先 Redis ubaseinfo_<peer_uid>，miss 走 MySQL
  └─ LLM 调 DeepSeek 生成 N 条候选 → MemoryStore(Redis) 存快照
       ▼
Client: 展示候选；用户选中后 fill 到输入框或直接发

③ 追问润色: 客户端拿 session_id + candidate_index + instruction → /agent/refine
   AgentServer: MemoryStore 取快照 → LLM 单点重生成 → 返回单条
```

> **为什么 Agent 不直连 MySQL**：权限边界（token 校验、好友关系校验）、热缓存（`ubaseinfo_<uid>`）都集中在 ChatServer，让 Agent 再实现一遍会产生两份漂移的副本。DB schema 变更的同步面也只剩"C++ 副本们"，不会扩散到 Python。
>
> **为什么客户端不经 Gate 反代 Agent**：AI 请求耗时秒级 + SSE 流式，Gate 反代会成为瓶颈且难透传分片。由 Status 在登录时一次性下发 `agent_host/port`，沿用现有的 ChatServer 调度模型，对称简洁。

### 8.5 注册 / 找回密码 / 验证码
- 验证码：`Client → Gate(/get_varifycode) → gRPC → VarifyServer(Node.js) → Redis SET email→code (TTL) → nodemailer 发邮件`
- 注册：`Client → Gate(/user_register) → 校验 Redis 验证码 → MysqlMgr::RegUser（调用存储过程 reg_user）`
- 找回密码：`Client → Gate(/reset_pwd) → 校验验证码 → MysqlMgr::UpdatePwd`

### 8.6 防重复登录 + 心跳检测（三层防护）

**核心不变量**：同一 uid 在整个系统中**最多一个活跃 session**。由三层机制共同保证：

**第 1 层 —— 登录时主动踢人**（LoginHandler）：
```
LoginHandler:
  ├─ 分布式锁 Redis SET lock_<uid> NX EX → acquireLock 失败则返回 RPCFailed 让客户端重试
  ├─ GET uip_<uid> → old_server
  ├─ old_server == self?
  │    ├─ YES: old_session->SendAndClose(ID_NOTIFY_KICK_USER) + UserMgr::RmvUserSession
  │    └─ NO:  gRPC ChatGrpcClient::NotifyKickUser(old_server) → 对端 ChatServiceImpl 做同样的本地清理
  ├─ SET uip_<uid> = self / SET usession_<uid> = new_sid（新 session 覆盖）
  └─ UserMgr::SetUserSession + session->RefreshHeartbeat
```

**第 2 层 —— 被动校验兜底**（`LogicSystem::ValidateSession`，所有业务 handler 入口都调）：
- 每条业务消息进来先校验 `usession_<uid>` 是否仍等于自己的 session id
- 不等 → 本 session 已被新登录顶替 → `SendAndClose(ID_NOTIFY_KICK_USER)` + `RmvUserSession`
- Redis GET 失败 → 放行（防止 Redis 抖动误踢全员）

**第 3 层 —— 心跳超时扫描**（`CServer::HeartbeatCheckLoop`）：
- 每 15 秒扫描 `_sessions`，踢掉 90 秒无心跳的僵尸 session
- 清理路径统一走 `CServer::ClearSession`

**Session 清理职责分离**（重要）：
- `UserMgr::RmvUserSession(uid, sid)` — 立即生效，按 sid 匹配防误删
- `CServer::ClearSession(sid)` — 从 `_sessions` map 清理 + Redis 清理（只在 `usession_` 仍属自己时清）
- 踢人路径**只调 RmvUserSession**，不直接调 ClearSession。`_sessions` 的清理由 socket 关闭后 `async_read` 返回 EOF 的回调触发（因此必须用 `SendAndClose` 异步关 socket，绝不能让 Send 调用对已 close 的 socket 写）
- 心跳超时和客户端断线路径则调 `ClearSession` 完成全量清理

**客户端双向心跳**：
- 客户端每 30 秒发 `ID_HEART_BEAT_REQ` (1023)；服务端 `HeartBeatHandler` 回 `ID_HEARTBEAT_RSP` (1024) 并刷新 session 心跳戳
- 客户端 `TcpMgr` 注册 1024 handler 刷新 `_last_heartbeat_rsp_ms`；`ChatDialog::sendHeartbeat` 每次发送前检查 90 秒未响应 → `CloseConnection` → `sig_connection_lost` → `MainWindow::slotBackToLogin`
- 这是应用层 keepalive，解决 TCP "半死连接"问题（拔网线后 TCP 层可能几分钟才报错）

**客户端被踢/断线 UI 流程**：
- `ID_NOTIFY_KICK_USER` → `TcpMgr::_kick_pending=true` → `sig_kick_user` → `slotBackToLogin`
- `QTcpSocket::disconnected` → 若 `_kick_pending` 则跳过（避免重复弹窗）→ 否则 `sig_connection_lost` → `slotBackToLogin`
- `slotBackToLogin` 用 `_switching_to_login` 防重入，**先切换 widget（销毁 ChatDialog 停掉心跳 timer）再 `UserMgr::Reset()`**，否则 timer 回调访问 `_user_info=nullptr` 崩溃（已加空指针保护 + timer disconnect）

**修改踢人逻辑的红线**：
1. 不要在 Send 和 Close 之间有其他代码——只能用 `CSession::SendAndClose`，它把 Close post 到 io_context 保证 async_write 先完成
2. 不要在踢人路径里直接删 Redis `uip_`/`usession_`——新 session 马上会覆盖，直接删会出现"踢旧的时刻同时把新的也清了"的时序窗口
3. 不要在 `ChatServiceImpl`/`LogicSystem` 里持有 `CServer*`——CServer 是 main 的栈对象，生命周期不匹配（之前的崩溃就是 null `_p_server` 引起的）

---

## 9. 修改代码时的 "全局观" 速查

| 修改类型 | 必须同步检查的地方 |
|---|---|
| **新增 / 修改一个 ReqId** | ① `wChat_client/global.h` 枚举 ② Client `TcpMgr::handlers` ③ ChatServer `LogicSystem::RegisterCallBacks` 与对应处理函数 |
| **修改 message.proto** | 编辑 `proto/message.proto` → 运行 `proto/generate_pb.bat` 重新生成 → 重新编译所有 C++ 服务即可（proto 已统一，无需逐个同步） |
| **新增 / 修改 MySQL 字段** | `wchatmysql.sql` + 用到该表的 `MysqlDao::*` SQL（多个服务都有自己的副本） + 字段消费方（结构体 `UserInfo` / `TextChatData` / 等） |
| **修改 Redis key 含义** | 写入方 + 所有读出方（grep `usertokenprefix_` / `useripprefix_` / `userbaseinfo_` / `code_prefix`） |
| **修改任意公共类**（CServer / LogicSystem / Mgr 等） | 4 个 C++ 服务下的同名文件**通常需要同步**——确认是否所有服务都有相同需求；不要假设是共享代码 |
| **新增"按 uid 推消息"的业务** | 模板：先 `Redis Get uip_<uid>` → 同服走 `UserMgr::GetSession` → 跨服走 `ChatGrpcClient` + `ChatServiceImpl` 加新方法（要扩 proto） |
| **客户端新增界面与服务端交互** | HTTP 业务找 `HttpMgr` + Gate 的 `LogicSystem::_post_handlers`；TCP 业务找 `TcpMgr::sig_send_data` + ChatServer 的 `LogicSystem::_fun_callbacks` |
| **新增需要用户已登录的业务 handler** | 在 ChatServer handler 入口加 `if (!ValidateSession(session)) return;`（见 §8.6 第 2 层）；否则僵尸 session 的消息会被处理 |
| **改踢人 / 断线 / 心跳逻辑** | 先读 §8.6 三条红线；改完端到端测：同服双登、跨服双登、客户端崩溃、服务端崩溃、拔网线、快速连续 3 次登录 |

---

## 10. 工程 / 构建相关惯例

- 客户端是 **VS 工程 + Qt .pro 双轨**（`wChat_client.sln` 与 `wChat_client.pro` 并存）。修改源文件后两边的工程文件都可能需要更新；ui_*.h 是 uic 生成物，不要手动改（已被 `.gitignore` 忽略）。
- 服务端 3 个 C++ 服务（Gate / Status / ChatServer）都是独立的 VS 解决方案（`*.sln`），每个目录里有 `PropertySheet.props` 引用 Boost / gRPC / MySQL Connector 的路径；改第三方库版本时需要改 props。
- 各服务依赖 `mysqlcppconn-9-vs14.dll` / `mysqlcppconn8-2-vs14.dll`（已随源码放在每个服务目录里）。
- VarifyServer 是纯 Node.js：`cd wChat_VarifyServer && npm install && node server.js`。
- **ChatServer 启动**：
  - 单实例（本地调试）：直接在 VS 里按 F5，使用默认 `config.ini`（若存在）或通过项目属性"命令参数"指定 `configs/chatserver1.ini`。
  - 多实例（集群测试）：用 `wChat_server_tcp/start_cluster.bat` 一键启动，或手动：
    ```
    x64\Debug\wChat_server_tcp.exe configs\chatserver1.ini
    x64\Debug\wChat_server_tcp.exe configs\chatserver2.ini
    ```
- **构建产物全部被 `.gitignore` 忽略**：`build/`、`debug/`、`Release/`、`x64/`、`.vs/`、`*.pro.user`、`ui_*.h`、`moc_*`、`qrc_*` 等。

---

## 11. 已知的"陷阱"与设计现实

1. **代码副本而非共享库**：4 个 C++ 服务的公共类是物理拷贝。改一个 bug 时记得问自己"另外几个要不要也改"。
2. **proto 副本不一致风险**：`message.proto` 在每个服务里都有一份，改 RPC 接口时容易漏。
3. **VarifyServer 是 Node.js**：Redis key 前缀和 C++ 服务的命名习惯略不同（Node 端在 `const.js` 里定义）。
4. **配置硬编码**：MySQL 密码 / Redis 密码直接写在 `config.ini`，不要把这些 ini 当作敏感信息暴露到公开仓库的"机密"用途——如需公开请先脱敏。
5. **uid → 服务器映射的清理**：用户下线时需要清掉 `uip_<uid>`，否则会导致跨服转发指向已离线的会话。改下线 / 心跳超时逻辑时要注意。
6. **客户端登录是阻塞式 gRPC**：Gate 调 Status 时是同步等待的，Status 响应慢会拖累 Gate 线程，`StatusServer` 不要塞重逻辑。
7. **Boost.Asio socket 已关闭后再 Send 会崩**：`CSession::Send` 入口必须检查 `_b_close`。踢人要用 `SendAndClose`（post 延迟关 socket），不要手写"Send 完直接 Close"。
8. **CRLF 换行敏感**：本仓库 C++ 源文件是 CRLF，新文件或经工具改写过的文件如果变成 LF，MSVC 某些配置下会报莫名其妙的"未声明的标识符"错。新建或批量改写后用 `unix2dos` 统一。
9. **栈对象 CServer 不能放进 shared_ptr**：main 里 `CServer s(...)` 是栈对象，`ChatServiceImpl` / `LogicSystem` 如果持有 `shared_ptr<CServer>` 会空悬（以前的 `RegisterServer` / `SetServer` 从未被调用，`_p_server` 始终是 null）。跨组件拿 CServer 指针一律改走"不依赖 CServer"的路径——让 socket 关闭后的 EOF 回调去 `ClearSession`。

---

## 12. 你（Claude）应当优先采取的行为

- **回答"这块怎么工作的？" 类问题**：优先按 §6（ReqId）→ §8（调用链）→ 具体源文件 的顺序定位，不要一上来 grep 全仓库。
- **回答"怎么改 X？" 类问题**：先在 §9 找到对应"修改类型"清单，确认要同步的所有点，再动手。
- **当用户描述一个 bug**：先识别它属于哪条调用链（§8），从两端往中间收，而不是逐文件阅读。
- **不要假定文件路径**：本文件写就时是真实状态，但代码可能演进。给出修改建议前，对涉及的关键文件做一次 Read 校验。
- **不要假设服务间是共享代码**：见 §11.1。改公共组件时一定要 grep 一遍是哪几个服务用到。
- **当 LabReport 与代码冲突时**：以代码为准，并提示用户更新 `wChat_LabReport.txt`。LabReport 是设计原稿，代码是事实。
- **保持本文件简短**：只记"高层 + 易错点"。具体函数签名、字段名变了不必更新这里——让代码自己说话。

---

## 13. AgentServer（智能回复子系统，Python 独立服务）

**进度状态（截至 2026-04-16）**：M1 完成，骨架 + Agent 核心 + 5 个 Preset + 22 个测试通过；**M2 进行中**——架构决策已对齐（见 §13.3），gRPC / SSE / 鉴权 / Redis memory / 客户端 UI 分步实现。

### 13.1 一句话定位
独立 Python 微服务 `wChat_AgentServer/`，与 4 个 C++ 服务平级。基于 LangGraph + DeepSeek，给客户端 ChatPage 提供"读取最近聊天 → 分析意图 → 生成 N 条候选回复"的 AI 辅助。

### 13.2 架构决策（M2 定稿）

以下三条是 M1 → M2 过渡时对齐的核心设计，动这三条就得重读本节：

1. **Agent 不直连 MySQL，走 ChatServer 的 gRPC `AgentDataService`**
   - 理由：权限边界（token 校验、好友可见性）和热缓存（`ubaseinfo_<uid>`）都已经在 ChatServer 里，Agent 再实现一份会产生两份漂移的副本。
   - Agent **直连 Redis** 只为一件事：校验 `utoken_<uid>` + 会话记忆/限流计数（不碰业务数据）。
   - 例外：未来 RAG 的向量库是 Agent 独有的，那部分直连无妨。

2. **客户端定位 Agent 靠 StatusServer 下发，不走 Gate 反代、不走硬编码**
   - 登录时 `Status::GetChatServer` 响应里**同时下发** `{chat_host, chat_port, agent_host, agent_port, token}`。
   - 客户端 `UserMgr` 把 `agent_*` 也存下来；`ChatPage` AI 面板直连 `http://agent_host:agent_port/agent/...`。
   - 拒绝 Gate 反代：AI 秒级耗时 + SSE 流式穿反代麻烦；拒绝客户端经 ChatServer 转发：会阻塞聊天主路径。

3. **AgentServer 独立进程、独立协议**
   - 对客户端：HTTP + SSE（快路径，不挤占 ChatServer）
   - 对 ChatServer：gRPC 只用来拿数据（慢路径，和正常聊天互不影响）
   - ChatServer 的 gRPC server 同时承载原有的 `ChatService`（跨服务转发）和新增的 `AgentDataService`（数据查询），端口复用，`main.cc` 里多注册一个 service 即可。

### 13.3 技术栈选型与原因
- **Python 3.12 + FastAPI + uvicorn** —— LLM/Agent 生态在 Python 最成熟；与 C++ 主链解耦避免拖慢 IM
- **LangGraph 0.2.76** —— 状态图描述 Agent 流程，比手写 while 循环结构清晰
- **DeepSeek API（OpenAI 兼容）** —— 复用 `openai` SDK，换 Qwen/Moonshot 只换 base_url
- **Pydantic 2** —— 所有跨层数据契约
- **MockBackend** —— 替代未来 gRPC 客户端，让 Agent 在没有 ChatServer 的情况下能完整跑通

### 13.4 目录结构（详见 [wChat_AgentServer/README.md](wChat_AgentServer/README.md)）
```
wChat_AgentServer/
├── app/
│   ├── api/        HTTP 路由（FastAPI）
│   ├── agent/      LangGraph 节点 + 编译图 + AgentService 门面
│   ├── tools/      ToolRegistry + 4 个工具 + MockBackend
│   ├── llm/        LLMProvider 抽象 + DeepSeek 实现
│   ├── presets/    场景预设加载（config/presets.yaml）
│   ├── memory/     短期会话记忆（内存 dict，M2 换 Redis）
│   ├── schemas/    Pydantic 契约（与客户端 TextChatData 字段对齐）
│   ├── config/     .env + agent.ini 加载
│   └── rpc/        gRPC 客户端占位（M2 实现，proto 草案在 rpc/README.md）
├── config/         presets.yaml + agent.ini.example
└── tests/          22 个用例，FakeLLM 不消耗 DeepSeek 额度
```

### 13.5 已实现功能
- HTTP 端点：`POST /agent/suggest_reply`、`POST /agent/refine`、`GET /agent/presets`、`GET /agent/health`（`/agent/suggest_reply/stream` 返回 501，M2 实现）
- Agent 流程：`analyze_intent → fetch_profile/history/summary（按需）→ generate_candidates`
- 5 个内置 Preset：礼貌拒绝 / 关心安慰 / 幽默化解 / 正式商务 / 暧昧试探
- 追问润色：`/refine` 基于 session 快照单点重生成
- 候选数量强制对齐（多了截断，少了占位填充）
- LLM 返回非法 JSON 的容错路径

### 13.6 数据契约对齐（必须保持）
`app/schemas/chat.py` 的 `TextChatData` 字段必须与以下三处保持一致，否则未来客户端发来的 JSON 会反序列化失败：
- 客户端 [wChat_client/userdata.h:172](wChat_client/userdata.h#L172) `TextChatData`
- 客户端 [wChat_client/global.h:89](wChat_client/global.h#L89) `MsgType` 枚举
- ChatServer [wChat_server/wChat_server_tcp/MysqlDao.h:257](wChat_server/wChat_server_tcp/MysqlDao.h#L257) `GetMessagesPage` 输出

注意 `msg_db_id` 用 **str** 在 wire 上传，因为 jsoncpp（C++ 侧）不支持 int64（见 §1）。

### 13.7 配置
- **密钥** 走 `.env`（gitignored）：`DEEPSEEK_API_KEY=sk-xxx`
- **参数** 走 `config/agent.ini`（gitignored）：端口、温度、候选数、Backend 模式等
- 优先级：env > ini > 代码默认值
- 模板：`.env.example`、`config/agent.ini.example`（提交到 git）

### 13.8 启动与测试
```powershell
cd wChat_AgentServer
.venv\Scripts\Activate.ps1          # 虚拟环境已建好
pytest                              # 22 个用例,FakeLLM 不烧钱
uvicorn app.main:app --reload --port 8200   # 启服务
```
VSCode 用户：项目根的 `.vscode/launch.json` 已配好 F5 启动项（AgentServer + Pytest）；`.vscode/settings.json` 把解释器指向 `wChat_AgentServer/.venv/`。

### 13.9 M2 实施步骤（已对齐架构，按以下顺序推进）

每步独立可测；前 2 步对 C++ 侧零侵入，风险低。

**Step 1 —— 鉴权 + Redis 会话记忆 + 限流（纯 Python）**
- `app/security/auth.py`：`require_auth` 依赖，Header `Authorization: Bearer <token>` → Agent 自己连 Redis GET `utoken_<uid>` 校验。
- `app/memory/redis_store.py`：`MemoryStore` 的 Redis 实现（key `agent_session:<sid>`，TTL 1h）；`[Memory] Backend=memory|redis` 切换。
- `app/security/rate_limit.py`：Redis INCR `agent_quota_<uid>_<YYYYMMDD>` + EXPIRE 到当日 24:00。
- 用 `fakeredis` 做单测，不依赖真 Redis。

**Step 2 —— proto 扩展 + Python stub 生成**
- [proto/message.proto](proto/message.proto) 新增 `AgentDataService`（`GetChatHistory` / `GetFriendProfile`），proto 草案见 [wChat_AgentServer/app/rpc/README.md](wChat_AgentServer/app/rpc/README.md)。
- [proto/generate_pb.bat](proto/generate_pb.bat) 追加 Python grpc 生成到 `wChat_AgentServer/app/rpc/gen/`，加 `__init__.py`、pytest ignore。
- **同时**在 `GetChatServerRsp` proto 里加 `agent_host` / `agent_port` 字段（为 Step 4 铺路）。

**Step 3 —— ChatServer 实现 `AgentDataService`（C++）**
- 新建 `wChat_server_tcp/AgentDataServiceImpl.{h,cc}`（与 `ChatServiceImpl` 分文件）。
- `GetChatHistory` 走 `MysqlDao::GetMessagesPage`（复用）；`GetFriendProfile` 先 Redis `ubaseinfo_<peer_uid>` 后 MySQL 兜底。
- Token 校验：从 gRPC metadata 读 `auth_token`，与 Redis `utoken_<self_uid>` 比对。
- `main.cc` 把 `AgentDataServiceImpl` 注册到已有的 gRPC server（端口复用 50055 / 50056）。

**Step 4 —— StatusServer 下发 `agent_host/port` + 客户端登录链路接入**
- `wChat_StatusServer` 读 `[AgentServer]` 配置段（host + port）或按 AgentServer 实例池做简单轮询。
- `StatusServiceImpl::GetChatServer` 响应多塞 `agent_host/agent_port`。
- Gate `/user_login` JSON 回包透传这两个字段。
- 客户端 `HttpMgr` 登录响应解析 + `UserMgr` 保存 `agent_host/agent_port`。

**Step 5 —— Python 侧 `GrpcAgentDataClient`**
- 实现 `app/rpc/agent_data_client.py`（`grpc.aio.insecure_channel` + metadata 带 token）。
- proto row ↔ `TextChatData` / `UserProfile` 映射；`msg_db_id` int64 → str（jsoncpp 约束，见 §1）。
- `deps.py::_backend()` 按 `settings.backend.mode` 分叉 mock / grpc；统一 `AgentDataClient` Protocol。

**Step 6 —— SSE 流式**
- 把 `suggest_stream.py` 从 501 改成 `StreamingResponse`；LangGraph `astream` 驱动；事件序列 `intent → candidate_delta×N → candidate_done×N → done`。

**Step 7 —— 客户端 `ChatPage` AI 面板 UI**
- 输入框旁加"AI 建议"按钮 → 弹面板（Preset 选择 + 候选列表 + 追问输入框）。
- 走 `QNetworkAccessManager` 直连 `http://<agent_host>:<agent_port>/agent/suggest_reply`，Header 带登录 token。

### 13.10 修改 AgentServer 时的检查清单
| 修改类型 | 必须同步检查 |
|---|---|
| **改 schemas/chat.py 字段** | 客户端 `TextChatData` + ChatServer SQL + 测试 fixtures |
| **加新 Preset** | 只改 `config/presets.yaml`,无需代码改动；重启服务生效（无热更新） |
| **加新工具** | `app/tools/` 新建文件 + `app/api/deps.py::_tool_factory` 注册 + 系统提示词的工具调用规则 |
| **换 LLM provider** | 实现 `LLMProvider` 协议 + 改 `app/api/deps.py::_agent_service` 实例化 |
| **改 Agent 图拓扑** | `app/agent/graph.py::build_graph` + 同步更新本节的"已实现功能"
