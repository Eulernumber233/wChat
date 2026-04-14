# wChat — 基于分布式架构的即时通讯系统

wChat 是一个使用 C++ / Qt 开发的分布式即时通讯系统，包含图形界面客户端与多个职责分离的后端服务。系统支持用户注册登录、邮箱验证码、密码找回、好友搜索与添加、实时消息收发以及历史消息持久化等核心 IM 场景，并面向高并发与分布式协同进行了设计。

## 技术栈

- **客户端**：C++ / Qt（信号槽、QSS 样式、自定义控件）
- **服务端**：C++ / Boost.Asio（异步网络）、HTTP、TCP 长连接
- **服务间通信**：gRPC + Protobuf
- **数据存储**：MySQL（持久化）、Redis（缓存 / Token / 在线状态）
- **邮件服务**：Node.js（验证码下发）
- **常用模式**：单例、线程池、连接池、IO 多线程模型

## 项目结构

```
wChat/
├── wChat_client/                 Qt 客户端
└── wChat_server/
    ├── wChat_server_gate/        网关服务器（HTTP 接入、登录注册）
    ├── wChat_StatusServer/       状态服务器（Token 分发、聊天服务器调度）
    ├── wChat_VarifyServer/       验证码服务器（Node.js 实现的邮件服务）
    ├── wChat_server_tcp/         聊天服务器（单一代码库，通过不同配置启动多个实例）
    │   ├── configs/
    │   │   ├── chatserver1.ini   实例 1 配置
    │   │   └── chatserver2.ini   实例 2 配置
    │   └── start_cluster.bat     一键启动多实例集群
    └── wchatmysql.sql            数据库脚本
```

## 服务端架构

系统由四类相互协作的服务组成：

| 服务 | 职责 |
| --- | --- |
| **GateServer** | 接入客户端 HTTP 请求，处理注册 / 登录 / 找回密码等业务，向后端服务转发 gRPC 调用 |
| **StatusServer** | 校验登录状态，生成 Token，按负载为客户端分配合适的聊天服务器 IP / 端口 |
| **VarifyServer** | Node.js 实现的邮件验证码服务，生成验证码并写入 Redis、通过 SMTP 发送 |
| **ChatServer (tcp)** | 与客户端建立 TCP 长连接，处理好友申请、消息收发等实时业务；**同一份可执行文件通过不同配置文件启动多个实例**实现水平扩展，跨实例消息通过 gRPC 互转 |

各 C++ 服务复用同一套基础组件：

- `CServer` — 基于 Boost.Asio 的异步监听器
- `AsioIOContextPool` — 按 CPU 核心数构建的 IO 上下文池，多线程调度
- `LogicSystem` — 网络层与业务层解耦的逻辑线程 / 任务队列
- `MysqlMgr` / `RedisMgr` — 基于连接池的数据库访问单例
- `ChatGrpcClient` / `ChatServiceImpl` — 跨聊天服务器的 gRPC 通信
- `UserMgr` — 维护本机在线用户与会话的映射
- `ConfigMgr` — 加载 `.ini` 配置

## 数据库设计

| 表 | 主要作用 |
| --- | --- |
| `user` | 用户基础信息（uid、name、email、pwd 等） |
| `friend` | 好友关系（按 `self_id` 建立索引） |
| `chat_conversations` | 会话表，按 `(user_a_id, user_b_id)` 联合索引快速定位会话 |
| `chat_messages` | 聊天记录，外键关联 `chat_conversations.id`，按 `conversation_id` 索引 |

## 客户端设计

客户端基于 Qt Widgets，主要由三个对话框组成：`LoginDialog`（登录）、`RegistDialog`（注册）、`ResetDialog`（找回密码）和 `ChatDialog`（主界面）。主界面分为：

- **侧边导航栏**：会话列表 / 联系人列表 / 申请列表的切换，支持新消息红点提示
- **会话列表 / 联系人列表**：好友与会话条目，支持搜索框模糊查找
- **聊天区**：消息气泡、图片气泡、消息分段发送、历史记录加载

网络通信由两个单例类管理：

- `HttpMgr` — 与 GateServer 之间的 HTTP/JSON 请求
- `TcpMgr` — 与 ChatServer 之间的 TCP 长连接、消息分发

本地数据管理：

- `UserMgr` — 缓存当前登录用户的个人信息、好友列表、申请列表
- `LocalDb` — 基于 SQLite 的本地消息持久化（`local_messages` / `local_files` / `sync_state` 三表），按用户隔离存储在 `<AppData>/wChat/users/<uid>/db/chat.sqlite`
- `FileMgr` — 文件上传 / 下载管理，本地缓存目录 `<AppData>/wChat/users/<uid>/files/`
- `AppPaths` — 按登录用户 uid 隔离的本地目录管理

## 主要业务流程

系统已实现以下完整端到端流程，关键路径串联了 HTTP、gRPC、TCP、MySQL、Redis 多个组件：

1. **登录** — 客户端 HTTP 登录 → GateServer 校验密码 → gRPC 请求 StatusServer 分配 ChatServer 与 Token → 客户端 TCP 连接 ChatServer → 服务端校验 Token、加载好友列表（不含历史消息）→ 进入主界面 → 客户端立即请求会话摘要（`ID_PULL_CONV_SUMMARY`）
2. **注册** — 邮箱验证码（VarifyServer + Redis）→ 用户信息写入 MySQL（存储过程生成 uid）
3. **找回密码** — 邮箱验证码校验 → 更新 MySQL 中的密码
4. **搜索好友** — Redis 缓存优先 + MySQL 回源 + 缓存回写
5. **添加好友 / 同意申请** — 同服直接通过本地 Session 推送，跨服通过 gRPC 转发到对方所在的 ChatServer
6. **消息发送** — 消息持久化到 MySQL，再通过 Session 或 gRPC 推送到接收方所在的 ChatServer
7. **历史消息懒加载** — 打开会话时先从本地 SQLite 读取最近 30 条消息（瞬时渲染），再通过 `ID_PULL_MESSAGES` 向服务端拉取增量并写入本地库；历史文件消息通过 `ID_GET_DOWNLOAD_TOKEN` 按需获取一次性下载凭证后从 FileServer 下载
8. **图片收发** — 发送方通过 ChatServer 获取上传凭证后直连 FileServer 上传，源文件同时 copy 到本地缓存；FileServer 通过 gRPC 通知 ChatServer 写入消息记录并推送接收方；接收方自动下载并替换占位气泡为图片

## 构建与运行

> 该项目主要在 Windows + Visual Studio + Qt 环境下开发。

依赖：

- Qt 5/6（客户端）
- Boost（>= 1.78，服务端 Asio / Beast）
- gRPC + Protobuf
- MySQL Connector/C++
- hiredis（Redis 客户端）
- Node.js（VarifyServer）

大致步骤：

1. 使用 `wChat_server/wchatmysql.sql` 初始化 MySQL 数据库；
2. 启动 Redis；
3. 在 `wChat_VarifyServer/` 下 `npm install` 后启动 Node 服务；
4. 编译并启动 `GateServer`、`StatusServer`、一个或多个 `ChatServer`；
5. 编译并启动 `wChat_client`，使用注册账号登录即可。

各服务的端口、数据库地址、Redis 地址、邮件账号等请在对应模块的 `config.ini` 中配置。

## 项目状态

当前实现已覆盖账户体系、好友体系、文字与图片消息收发、历史消息懒加载与本地持久化的完整链路，并支持多个 ChatServer 实例之间通过 gRPC 互通。客户端通过 SQLite 实现消息本地镜像，登录时仅拉取会话摘要，打开会话时按需分页加载，离线期间收到的消息在下次登录后自动补齐。后续可扩展方向包括：消息送达回执 / 已读状态、大文件分片与断点续传、群聊等。
