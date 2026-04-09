# CLAUDE.md — wChat 项目记忆

> 本文件供 Claude Code 在每次新会话开始时自动加载。目的：让你（Claude）在不重新阅读全部代码的前提下，快速掌握项目背景、模块边界、跨服务调用链与关键约定，避免陷入"局部修改 / 全局失配"的问题。
>
> 维护原则：当架构、协议、模块职责发生变化时同步更新本文件；纯实现细节（函数体、字段名）不要写进来——读代码即可。

---

## 1. 项目一句话定位

wChat 是一个 **C++ / Qt 实现的分布式即时通讯系统**。客户端是 Qt Widgets 桌面 App，后端由 4 类服务组成（HTTP 网关 / 状态调度 / 邮件验证码 / 多实例聊天服务器），服务间用 **gRPC** 互通，数据落地在 **MySQL**，热数据 / Token / 在线状态在 **Redis**。

技术栈：C++17、Qt5/6、Boost.Asio (Beast for HTTP)、gRPC + Protobuf、MySQL Connector/C++、hiredis、Node.js（仅 VarifyServer）、jsoncpp。

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
| ChatServer（多实例） | TCP + gRPC | 实例 1：TCP 8090 / RPC 50055；实例 2：TCP 8091 / RPC 50056；… | 与客户端长连接，处理好友 / 消息业务。同时作为 gRPC server 接收其他 ChatServer 的转发请求。同一份可执行文件通过 `configs/chatserverN.ini` 启动多个实例。`[PeerServer]` 小节列出其他对端。 |

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

> **改协议时必须同步三处**：客户端 `global.h` 的枚举、ChatServer 的 `LogicSystem::RegisterCallBacks()` 注册、proto 文件（如果涉及跨服 gRPC 字段）。

---

## 7. 数据库 & Redis 约定

**MySQL 表（schema 默认 `wchatmysql`）**：

- `user(uid, name, email, pwd, icon, ...)` — 用户主表，注册走存储过程 `reg_user`
- `friend(self_id, friend_id, back, ...)` — 好友关系，按 `self_id` 索引；一对好友插入两行（双向）
- `chat_conversations(id, user_a_id, user_b_id, last_msg_id, update_time)` — 联合索引 `(user_a_id, user_b_id)`
- `chat_messages(id, conversation_id, sender_id, receiver_id, content, msg_type, status, send_time, read_time)` — 外键到 `chat_conversations.id`，按 `conversation_id` 索引
- `friend_apply(from_uid, to_uid, status, back, ...)` — 申请表，`status=1` 表示已同意

**Redis 常用 key 前缀**（出现在多个服务的逻辑函数里，搜代码时按这些前缀找）：

- `usertokenprefix_<uid>` — 登录 token，登录后由 StatusServer 写入，由 ChatServer 校验
- `useripprefix_<uid>` — 该 uid 当前所在的 ChatServer 名字，**这是跨服路由的关键**
- `userbaseinfo_<uid>` — 用户基础信息缓存（搜索好友 / 加载好友列表时优先查此处）
- `code_prefix<email>` — 邮件验证码（VarifyServer 写、Gate 读）

> 改任何"按 uid 找服务器"的逻辑时，记得 `useripprefix_` 这条线：登录时写入、消息转发时读取、下线时清理。

---

## 8. 跨服务调用链速查（修改任何业务前先看对应链）

### 8.1 登录
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
       ├─ MysqlMgr 加载 base_info / friend_list / apply_list / 历史消息
       ├─ Redis 写 useripprefix_<uid> = self_server_name
       ├─ UserMgr::SetUserSession(uid, session)
       └─ 回包 ID_CHAT_LOGIN_RSP
Client ── 跳转主界面 ChatDialog
```

### 8.2 文本消息发送（含跨服务器路由）
```
Client A: ChatPage::on_send_btn_clicked
  └─TcpMgr 发 ID_TEXT_CHAT_MSG_REQ {fromuid, touid, msg}─▶
ChatServer A::LogicSystem::DealChatTextMsg
  ├─ MysqlMgr::AddMessage（先持久化）
  ├─ Redis Get useripprefix_<touid>
  ├─ 同服(==self_name)? ──▶ UserMgr::GetSession(touid)->Send(ID_NOTIFY_TEXT_CHAT_MSG_REQ)
  └─ 跨服?            ──▶ ChatGrpcClient::NotifyTextChatMsg(peer_addr, ...)
                              └─▶ ChatServer B::ChatServiceImpl::NotifyTextChatMsg
                                     └─ UserMgr::GetSession(touid)->Send(ID_NOTIFY_TEXT_CHAT_MSG_REQ)
  └─ 回包 ID_TEXT_CHAT_MSG_RSP 给 Client A
Client B: TcpMgr 收 ID_NOTIFY_TEXT_CHAT_MSG_REQ → emit sig_text_chat_msg → ChatDialog 更新 list/page
```

> **修改消息流时的检查清单**（避免改一处坏全局）：
> 1. Client `ChatPage` 构造的 JSON 字段是否变？→ 同步改 ChatServer 解析。
> 2. ID_TEXT_CHAT_MSG_REQ 的字段是否变？→ 同步改 `ChatGrpcClient` / `ChatServiceImpl` / `message.proto`，否则跨服转发会丢字段。
> 3. 持久化字段是否变？→ 同步改 `MysqlDao::AddMessage` SQL 与 `chat_messages` 表结构。
> 4. 接收端 `Client::TcpMgr` 的 `handlers[ID_NOTIFY_TEXT_CHAT_MSG_REQ]` 是否需要更新解析。
> 5. 历史消息加载路径（登录时 `GetMessages`）是否也需要兼容新字段。

### 8.3 加好友 / 同意好友
- 加好友：`Client → ChatServer A (AddFriendApply) → MysqlMgr 写 friend_apply → Redis 查对方所在服务器 → 同服 Session 推 / 跨服 gRPC NotifyAddFriend → Client B 收到 ID_NOTIFY_ADD_FRIEND_REQ → ApplyFriendPage 显示`
- 同意：`Client B → ChatServer (AuthFriendApply) → MysqlMgr 更新 friend_apply 状态 + AddFriend 双向插入 + 创建 chat_conversations → 路由通知邀请方 → 双方 ChatDialog/ContactList 同时更新`

### 8.4 注册 / 找回密码 / 验证码
- 验证码：`Client → Gate(/get_varifycode) → gRPC → VarifyServer(Node.js) → Redis SET email→code (TTL) → nodemailer 发邮件`
- 注册：`Client → Gate(/user_register) → 校验 Redis 验证码 → MysqlMgr::RegUser（调用存储过程 reg_user）`
- 找回密码：`Client → Gate(/reset_pwd) → 校验验证码 → MysqlMgr::UpdatePwd`

---

## 9. 修改代码时的 "全局观" 速查

| 修改类型 | 必须同步检查的地方 |
|---|---|
| **新增 / 修改一个 ReqId** | ① `wChat_client/global.h` 枚举 ② Client `TcpMgr::handlers` ③ ChatServer `LogicSystem::RegisterCallBacks` 与对应处理函数 |
| **修改 message.proto** | 编辑 `proto/message.proto` → 运行 `proto/generate_pb.bat` 重新生成 → 重新编译所有 C++ 服务即可（proto 已统一，无需逐个同步） |
| **新增 / 修改 MySQL 字段** | `wchatmysql.sql` + 用到该表的 `MysqlDao::*` SQL（多个服务都有自己的副本） + 字段消费方（结构体 `UserInfo` / `TextChatData` / 等） |
| **修改 Redis key 含义** | 写入方 + 所有读出方（grep `usertokenprefix_` / `useripprefix_` / `userbaseinfo_` / `code_prefix`） |
| **修改任意公共类**（CServer / LogicSystem / Mgr 等） | 4 个 C++ 服务下的同名文件**通常需要同步**——确认是否所有服务都有相同需求；不要假设是共享代码 |
| **新增"按 uid 推消息"的业务** | 模板：先 `Redis Get useripprefix_<uid>` → 同服走 `UserMgr::GetSession` → 跨服走 `ChatGrpcClient` + `ChatServiceImpl` 加新方法（要扩 proto） |
| **客户端新增界面与服务端交互** | HTTP 业务找 `HttpMgr` + Gate 的 `LogicSystem::_post_handlers`；TCP 业务找 `TcpMgr::sig_send_data` + ChatServer 的 `LogicSystem::_fun_callbacks` |

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
5. **uid → 服务器映射的清理**：用户下线时需要清掉 `useripprefix_<uid>`，否则会导致跨服转发指向已离线的会话。改下线 / 心跳超时逻辑时要注意。
6. **客户端登录是阻塞式 gRPC**：Gate 调 Status 时是同步等待的，Status 响应慢会拖累 Gate 线程，`StatusServer` 不要塞重逻辑。

---

## 12. 你（Claude）应当优先采取的行为

- **回答"这块怎么工作的？" 类问题**：优先按 §6（ReqId）→ §8（调用链）→ 具体源文件 的顺序定位，不要一上来 grep 全仓库。
- **回答"怎么改 X？" 类问题**：先在 §9 找到对应"修改类型"清单，确认要同步的所有点，再动手。
- **当用户描述一个 bug**：先识别它属于哪条调用链（§8），从两端往中间收，而不是逐文件阅读。
- **不要假定文件路径**：本文件写就时是真实状态，但代码可能演进。给出修改建议前，对涉及的关键文件做一次 Read 校验。
- **不要假设服务间是共享代码**：见 §11.1。改公共组件时一定要 grep 一遍是哪几个服务用到。
- **当 LabReport 与代码冲突时**：以代码为准，并提示用户更新 `wChat_LabReport.txt`。LabReport 是设计原稿，代码是事实。
- **保持本文件简短**：只记"高层 + 易错点"。具体函数签名、字段名变了不必更新这里——让代码自己说话。
