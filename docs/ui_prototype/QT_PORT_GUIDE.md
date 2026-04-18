# wChat UI · HTML 原型 → Qt 移植说明

> 本指南说明如何把本目录下的 HTML/CSS 原型迁移到 Qt (Widgets 或 Quick) 上，尽量只用**代码实现**（不依赖贴图），对齐 `Client_UI_Requirements.md` 的全部必保留功能。

---

## 0. 原型目录

```
ui_prototype/
├── index.html                原型入口
├── css/theme.css             所有样式 (变量/组件/布局)
├── pages/
│   ├── login.html            登录 430×560
│   ├── register.html         注册 430×640
│   ├── reset-password.html   找回密码 430×640
│   ├── main.html             主界面 1180×760 · 三列
│   └── dialogs.html          申请/同意/搜索结果/加载/下线提示
└── QT_PORT_GUIDE.md          本文件
```

双击 `index.html` 即可预览；所有样式定义在 `css/theme.css` 的 CSS 变量里，改色只需要改 `:root` 一段。

---

## 1. 设计核心概念 → Qt 对应

| 原型概念 | 原型实现 | Qt 推荐对应 |
|---|---|---|
| **外层半透明边框** (灰白+轻微粉) | `.app-frame` 用 `rgba + backdrop-filter` | 无边框窗口 (`Qt::FramelessWindowHint`) + `setAttribute(Qt::WA_TranslucentBackground)`，在 `paintEvent` 里画圆角半透明矩形 |
| **内层不透明正式界面** | `.app-frame__inner` 白色圆角 | 外层 widget 里嵌一个 `QWidget` 设置纯色 QSS 背景 + border-radius |
| **列间半透明间隔** | `.gutter` 宽 10px 的半透明条 | 在 `QHBoxLayout` 里放 3 个 `QFrame`，中间夹两个 10px 宽、半透明 QSS 背景的 `QFrame` |
| **灰底透粉主色** | CSS 变量 `--pink-*` / `--gray-*` | 全局 QSS + `QPalette`，或集中在一份 `theme.qss` 里 |
| **按钮/输入/气泡** | CSS class | 继承 `QPushButton`/`QLineEdit`/自定义 `QLabel` 气泡 widget，或者直接 QSS objectName 精准选择 |
| **三列主界面** | flex 横向布局 | `QHBoxLayout` + `QStackedWidget` (列表有 3 种模式、详情有聊天/资料/空 3 态) |
| **列表懒加载 & 消息气泡** | 纯静态 DOM | `QListView` + `QAbstractListModel` + 自定义 `QStyledItemDelegate`；或 `QScrollArea` + 手动追加 widget |
| **弹窗** | `.modal-mask` 遮罩 | `QDialog` (独立 modal) 或 自绘半透明浮层 |
| **Toast** | 绝对定位 div | 自定义无边框 `QWidget`，`QTimer` 控制 2-3 秒淡出 |

---

## 2. 无边框+半透明窗口骨架 (MainWindow)

CSS 里 `.app-frame` 的"外层半透明灰白 + 内层正式界面"在 Qt 里通过**三层 widget 嵌套**实现：

```cpp
// FramelessShell 是一个 QWidget 基类, 登录/主界面都继承它
class FramelessShell : public QWidget {
public:
    FramelessShell(QWidget* parent=nullptr) : QWidget(parent) {
        setWindowFlag(Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);          // 允许圆角外裁剪

        auto outer = new QVBoxLayout(this);
        outer->setContentsMargins(10,10,10,10);              // 对应 .app-frame padding:10
        outer->setSpacing(0);

        // 顶栏 (标题+最小化/关闭)
        titleBar_ = new TitleBar(this);
        outer->addWidget(titleBar_);

        // 内层正式界面
        inner_ = new QWidget(this);
        inner_->setObjectName("AppFrameInner");
        outer->addWidget(inner_, 1);
    }

    QWidget* contentArea() { return inner_; }                // 子类往这里放内容

protected:
    void paintEvent(QPaintEvent*) override {
        // 画外层半透明圆角矩形 (.app-frame 的视觉)
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor glass(255,255,255, 90);                       // rgba(255,255,255,0.35)
        p.setBrush(glass);
        p.setPen(QPen(QColor(255,255,255,165), 1));          // rgba(255,255,255,0.65)
        p.drawRoundedRect(rect().adjusted(0,0,-1,-1), 28, 28);
    }

    // 支持拖拽窗口
    void mousePressEvent(QMouseEvent* e) override { if (e->button()==Qt::LeftButton) drag_ = e->globalPos() - frameGeometry().topLeft(); }
    void mouseMoveEvent (QMouseEvent* e) override { if (e->buttons()&Qt::LeftButton)  move(e->globalPos() - drag_); }

private:
    TitleBar* titleBar_ = nullptr;
    QWidget*  inner_    = nullptr;
    QPoint    drag_;
};
```

内层 (`#AppFrameInner`) 的样式通过 QSS：

```css
#AppFrameInner {
    background: #ffffff;
    border-radius: 20px;
}
```

> **想要真正的背景模糊**(`backdrop-filter: blur`)：Qt 没有内置等价 API。近似做法：
> - Windows：调用 `DwmEnableBlurBehindWindow` / `SetWindowCompositionAttribute`(ACCENT_ENABLE_ACRYLICBLURBEHIND)
> - 不想碰 Win API：直接用半透明纯色即可，视觉差异不大。

---

## 3. 配色与 QSS 总开关

把 CSS 变量搬成一份 `theme.qss` (在 `QApplication` 启动时 `setStyleSheet(loadFile("theme.qss"))`)：

```css
/* theme.qss ─ 樱语红粉主题 */

/* ---- 基本背景 ---- */
QWidget { font-family: "PingFang SC","Microsoft YaHei"; font-size: 14px; color: #26222b; }

#AppFrameInner { background: #ffffff; border-radius: 20px; }

/* ---- 输入框 ---- */
QLineEdit, QTextEdit {
    background: #ffffff;
    border: 1px solid #ece7ee;
    border-radius: 10px;
    padding: 6px 12px;
    selection-background-color: #f6cfdd;
}
QLineEdit:focus, QTextEdit:focus { border: 1px solid #e89bb4; }

/* ---- 主按钮 ---- */
QPushButton#BtnPrimary {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #e89bb4, stop:1 #c25978);
    color: white;
    border: none;
    border-radius: 10px;
    padding: 8px 18px;
    font-weight: 500;
}
QPushButton#BtnPrimary:hover  { /* 略微加深 */ }
QPushButton#BtnPrimary:pressed{ padding-top: 9px; }

/* ---- 幽灵按钮 ---- */
QPushButton#BtnGhost {
    background: transparent;
    color: #c25978;
    border: 1px solid #f6cfdd;
    border-radius: 10px;
    padding: 8px 18px;
}
QPushButton#BtnGhost:hover { background: #fdf2f6; }

/* ---- 侧边栏导航按钮 ---- */
QPushButton#NavBtn            { background: transparent; border-radius: 10px; padding: 8px; }
QPushButton#NavBtn:hover      { background: #fbe4ec; }
QPushButton#NavBtn[active="true"] {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #e89bb4, stop:1 #c25978);
    color: white;
}

/* ---- 列表条目 ---- */
QListView {
    background: #ffffff;
    border: none;
    outline: none;
}
QListView::item         { padding: 10px; border-left: 3px solid transparent; }
QListView::item:hover   { background: #fdf2f6; }
QListView::item:selected{ background: #fbe4ec; border-left: 3px solid #d97a95; color: #26222b; }

/* ---- 间隔条 ---- */
QFrame#Gutter { background: rgba(248,236,243,0.45); }
```

> Qt QSS 的 `qlineargradient` 可以完全还原 CSS 的 `linear-gradient(135deg,...)`，所以渐变头像、渐变按钮都不需要图片。

---

## 4. 登录/注册/找回密码

所有三个界面都是 `FramelessShell` + `QVBoxLayout`：

```cpp
LoginDialog::LoginDialog(QWidget* parent) : FramelessShell(parent) {
    resize(430, 560);                                        // 对应 .auth-shell

    auto body = new QWidget(contentArea());
    body->setStyleSheet(
        "background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 white, stop:1 #fdf2f6);");

    auto v = new QVBoxLayout(body);
    v->setContentsMargins(40, 30, 40, 36);
    v->setSpacing(18);

    // Logo
    auto logo = new QLabel("w", body);
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedSize(64, 64);
    logo->setStyleSheet("background: qlineargradient(...); border-radius: 32px;"
                        "color: white; font-size: 26px; font-weight: 600;");
    v->addWidget(logo, 0, Qt::AlignHCenter);

    // 邮箱 / 密码
    emailEdit_ = new QLineEdit; emailEdit_->setPlaceholderText("邮箱");
    pwdEdit_   = new QLineEdit; pwdEdit_->setPlaceholderText("密码 (6-15 位)");
    pwdEdit_->setEchoMode(QLineEdit::Password);
    v->addWidget(emailEdit_);
    v->addWidget(pwdEdit_);

    // 错误提示行
    errLabel_ = new QLabel; errLabel_->setStyleSheet("color:#d9534f;font-size:12px;");
    v->addWidget(errLabel_);

    // 登录按钮
    auto loginBtn = new QPushButton("登  录");
    loginBtn->setObjectName("BtnPrimary");
    loginBtn->setFixedHeight(42);
    v->addWidget(loginBtn);

    // 辅助链接
    auto links = new QHBoxLayout;
    links->addWidget(new QLabel("<a href='#forgot'>忘记密码？</a>"));
    links->addStretch();
    links->addWidget(new QLabel("<a href='#reg'>注册新账号</a>"));
    v->addLayout(links);

    auto host = new QVBoxLayout(contentArea());
    host->setContentsMargins(0,0,0,0);
    host->addWidget(body);

    connect(loginBtn, &QPushButton::clicked, this, &LoginDialog::doLogin);
}
```

对照 `Client_UI_Requirements.md`：
- **L1/L2 输入** → `QLineEdit`
- **L3 提交** → `loginBtn` 槽里调用 `GateClient::login()`
- **L4/L5 跳转** → `QLabel` 的 `linkActivated` 信号
- **L6 错误提示** → `errLabel_->setText(...)`
- **R7 显示/隐藏密码**: 在 `QLineEdit` 右侧加一个 `QPushButton`（父窗切 `setEchoMode(Normal/Password)`），或者直接用 `QLineEdit::addAction(icon, QLineEdit::TrailingPosition)`。
- **R6/P5 获取验证码冷却**: `QPushButton` + `QTimer` 1s tick, `setText("58s")`, `setEnabled(false)`。

---

## 5. 主聊天页 (三列)

### 5.1 外层骨架

```cpp
class ChatDialog : public FramelessShell {
    ChatDialog(QWidget* parent=nullptr) : FramelessShell(parent) {
        resize(1180, 760);

        auto h = new QHBoxLayout(contentArea());
        h->setContentsMargins(0,0,0,0);
        h->setSpacing(0);

        sidebar_     = new SidebarWidget;   sidebar_->setFixedWidth(66);
        auto gutter1 = new QFrame;          gutter1->setObjectName("Gutter"); gutter1->setFixedWidth(10);
        listStack_   = new ListPanelStack;  listStack_->setFixedWidth(300);   // 3 个模式的堆叠
        auto gutter2 = new QFrame;          gutter2->setObjectName("Gutter"); gutter2->setFixedWidth(10);
        detailStack_ = new DetailPanelStack;                                   // 聊天/资料/空 的堆叠

        h->addWidget(sidebar_);
        h->addWidget(gutter1);
        h->addWidget(listStack_);
        h->addWidget(gutter2);
        h->addWidget(detailStack_, 1);

        // 模式切换: 侧边栏 3 个按钮 → listStack_/detailStack_ setCurrentIndex
        connect(sidebar_, &SidebarWidget::modeChanged, this, &ChatDialog::onModeChanged);
        connect(listStack_, &ListPanelStack::conversationClicked, this, [this](int uid){
            detailStack_->showChat(uid);
        });
        // ...
    }
};
```

- `ListPanelStack` 内部是 `QStackedWidget`，装 **会话列表 / 联系人列表 / 申请列表** 三页。
- `DetailPanelStack` 装 **聊天面板 / 好友资料 / 空态** 三页。
- 两个"半透明间隔条"就是两个 `QFrame#Gutter`。

### 5.2 会话列表 / 联系人列表

推荐 `QListView` + `QAbstractListModel`：

```cpp
class ConversationModel : public QAbstractListModel {
    enum Roles {
        AvatarRole = Qt::UserRole + 1, NameRole, LastMsgRole,
        TimeRole, UnreadRole, PeerUidRole,
    };
    QVector<ConvItem> items_;
    // data() 返回角色值, rowCount() 返回条数
    // fetchMore() + canFetchMore() 实现 M13 下拉分页
};

class ConvItemDelegate : public QStyledItemDelegate {
    // paint() 里: 画圆形头像 → 画名字 → 画时间 → 画最后一条消息 → 画未读 badge
};
```

为了快速出效果，也可以直接用 `QScrollArea + QVBoxLayout` 塞自定义 `ConversationRow` widget。字段少时 (几十条内) 视觉差异不大；条目多或滚动时 `QListView + delegate` 更流畅。

**M6 未读红点**: 在侧边栏申请按钮 `paintEvent` 里根据 `badgeCount_ > 0` 额外画一个红色小圆。

### 5.3 聊天面板 (消息气泡)

`QScrollArea` 包着一个 `QVBoxLayout` 逐条 `addWidget`，每个消息是一个 `MessageBubble`:

```cpp
class MessageBubble : public QWidget {
    // 布局: [头像] [气泡 QLabel + 附件区]
    // isMe 为 true 时 setLayoutDirection(Qt::RightToLeft) 或者手动反向布局
    // 气泡背景: QSS 或 paintEvent 画圆角矩形
};
```

- **C2 文本气泡**: `QLabel`，`setWordWrap(true)`，QSS 给 `background + border-radius + padding`。我方用渐变粉色、对方用白色。
- **C9 图片**: `QLabel` + `QPixmap::scaled`，或 `ClickableLabel` 支持点击放大。
- **C10 文件**: 自定义小 widget：左图标 + 中 (文件名+大小) + 右 (下载/打开)，点击时走 FileServer 下载流程。
- **C3 懒加载**:
    - 进入会话 → 先用本地 `QSqlite` (或你项目已有的本地缓存) 填充最近 30 条。
    - 再向 ChatServer 拉取最新页 → 合并到视图。
    - 监听 `QScrollBar::valueChanged`, 滚到 0 时 `loadOlder()`。
- **C4 实时接收**: 客户端已有的 `TcpMgr` / `ChatDialog` 的信号 → 槽里 `appendBubble()`。
- **C5/C6/C7/C8 输入区**: `QTextEdit` (多行) + 下面 `QToolButton` (图片/文件/表情/AI)。Enter 发送、Shift+Enter 换行通过 `eventFilter` 实现。

### 5.4 好友资料页 (profile)

`QWidget` 手工布局即可，渐变封面用 QSS `background: qlineargradient(...)`。
**F2 "发送消息"** → 点击后 `emit requestChat(peerUid)`，`ChatDialog` 切到聊天 tab 并定位该会话。

### 5.5 申请列表

每条申请一个 `ApplyRow` widget：
- 未处理 → "同意" 按钮 `#BtnPrimary`，点击后打开 **同意好友对话框 (D9-D12)**。
- 已处理 → 灰色 "已添加"，`setEnabled(false)`。
- **G1 红点**: `ApplyManager` 单例维护 `pendingCount_`，变动时 `emit pendingCountChanged(n)`，侧边栏按钮和列表 group label 同时响应。

---

## 6. 弹窗 / 二级页面

统一用 `QDialog` (模态) 或 `QFrame` 浮层：

```cpp
class AddFriendDialog : public QDialog {
    // 输入理由 / 备注 / 标签 chip
    // 点击 "发送申请" → emit submitted(reason, remark, tags)
};
```

- 遮罩层可以不画 (Qt 的 modal dialog 默认用 OS 焦点反馈)。若想要类似原型那种毛玻璃遮罩，自绘一个半透明 `QWidget`，`resize` 到主窗口大小并置顶。
- **D6/D10 标签 chip**: 一个 `QCheckBox` 子类，关掉默认外观，QSS 画成圆角 chip (参考 `.chip` / `.chip.is-selected`)。
- **D13 加载态**: 一个带 `QMovie` (gif) 或自绘 `QTimer + paintEvent` 旋转圆的小 `QDialog`；点击"搜索"时 `show()`，收到结果 `accept()`。

---

## 7. 全局通知 / 状态事件

和现有客户端的信号体系对齐：

| 事件 (需求) | 推荐 Qt 信号源 | UI 反馈 |
|---|---|---|
| G1 新好友申请 | `ApplyManager::applyArrived` | 侧边栏红点 + 申请列表追加条目 + 右上角 Toast |
| G2 申请被同意 | `FriendManager::friendAdded` | 联系人列表 + 会话列表追加；Toast "xxx 已通过你的好友申请" |
| G3 新消息 (不在当前会话) | `TcpMgr::messageArrived` | 会话条目 unread++ + 最后消息预览更新 |
| G4 被踢下线 | `TcpMgr::kickOff` | 弹出"账号在其他设备登录"对话框 → 倒计时 → `emit logout()` → 回登录页 |
| G5 网络断开 | `TcpMgr::disconnected` (心跳超时) | 同 G4，文案改"连接已断开" |
| G6 心跳保活 | `HeartBeatTimer` (30s) | 无 UI |

Toast 做法：

```cpp
class Toast : public QWidget {
public:
    static void show(QWidget* parent, const QString& text, int ms = 2500) {
        auto* t = new Toast(parent);
        t->label_->setText(text);
        // 定位到顶部居中, 淡入
        QTimer::singleShot(ms, t, [t]{ t->deleteLater(); });
    }
};
```

---

## 8. 不用贴图也能出效果的细节

本原型刻意不依赖 `asserts/` 下的图标，方便你在 Qt 里用代码实现。关键替代方案：

| 原贴图 | 代码方案 |
|---|---|
| `head_1.jpg` / 用户头像 | `QLabel` + 渐变背景 + 姓氏首字，或 `QPainter::drawPixmap` 圆形裁剪 |
| `search.png` / 搜索图标 | Emoji `🔍`、Unicode 字符、或 `QStyle::StandardPixmap` |
| `add_friend*.png` | `QPushButton` + QSS 画圆角 "+" 字符 |
| `settings*.png` | Unicode `⚙` / ttf 图标字体 (Material Icons / FontAwesome) |
| `red_point.png` | `QPainter::drawEllipse` + 红色填充 |
| `arowdown.png` / `right_tip.png` | `▼` `▶` 字符 |
| `loading.gif` | 自绘圆环 + `QPropertyAnimation` 旋转 |

**更推荐的做法**：引入一个图标字体 (例如 `icomoon.ttf` 或 `MaterialIcons-Regular.ttf`)，`QFontDatabase::addApplicationFont`，然后在 `QLabel`/`QPushButton` 上 `setFont(iconFont)` + 填字符码点。这样所有图标都是矢量的，换色只改 `color`，缩放不失真。

---

## 9. 目录 / 文件落地建议 (对照现有客户端)

```
wChat_client/
├── styles/
│   └── theme.qss                 ← 从本原型的 css/theme.css 翻译而来
├── widgets/
│   ├── FramelessShell.*          ← §2 的基类
│   ├── TitleBar.*
│   ├── Sidebar.*
│   ├── ListPanelStack.*          ← QStackedWidget 包会话/联系人/申请
│   ├── ConversationList.*        ← QListView + 模型 + delegate
│   ├── ContactList.*
│   ├── ApplyList.*
│   ├── ChatPanel.*
│   ├── MessageBubble.*
│   ├── ProfilePanel.*
│   ├── Chip.*                    ← 标签选择
│   └── Toast.*
├── dialogs/
│   ├── LoginDialog.*
│   ├── RegistDialog.*
│   ├── ResetDialog.*
│   ├── AddFriendDialog.*
│   ├── AgreeFriendDialog.*
│   └── SearchResultDialog.*
└── main.cpp                      ← load theme.qss, show FramelessShell
```

---

## 10. 迁移顺序建议 (循序渐进)

1. **先搭骨架** — `FramelessShell` + `theme.qss`，让任意一个空窗口显示出"外层半透明、内层白色圆角"的壳。
2. **移植登录页** — 用新壳套一份 `LoginDialog`，跑通登录请求 (L1-L6)。这一步打通"UI 壳 + 现有网络层"，是最小可运行闭环。
3. **移植注册/找回** — 复用输入框、验证码冷却按钮、字段校验。
4. **主界面骨架** — 三列 + 两个 `Gutter` + 空态，不接数据也能先看版式。
5. **会话列表 → 聊天面板** — 把会话点击、消息追加、历史懒加载 (C1-C10) 全部跑通。
6. **联系人 + 资料页** — F1/F2 + M14/M15/M16。
7. **申请流程** — M17/M18/M19 + D9-D12 + 红点 G1。
8. **搜索 + 加好友** — M7-M10 + D1-D8 + D13。
9. **全局通知** — G2-G5 的 Toast / 对话框接入。
10. **细节打磨** — 阴影、悬停态、动效 (`QPropertyAnimation`)、主题变量收敛。

---

## 11. 色板速查表

```
粉  #fdf2f6  #fbe4ec  #f6cfdd  #efb7c9  #e89bb4  #d97a95  #c25978  ink #6b3a4a
灰  #f7f6f8  #eeecf1  #dcd8e0  #c3bec8  #8a838f  #4b4550  #26222b
玻璃(外框 / gutter): rgba(255,255,255,0.35) / rgba(248,236,243,0.45)
主渐变 (按钮 / 头像): linear-gradient(135deg, #e89bb4 → #c25978)
```

把这 20 个色值粘进 `theme.qss`，所有视觉就能对上。
