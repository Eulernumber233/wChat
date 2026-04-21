#include "chatpage.h"
#include "ui_chatpage.h"
#include "chatitembase.h"
#include "TextBubble.h"
#include "picturebubble.h"
#include "usermgr.h"
#include "tcpmgr.h"
#include "filemgr.h"
#include "localdb.h"
#include <QUuid>
#include <QPointer>
#include <QFile>
#include <QPixmap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QScrollArea>
#include <QFileDialog>
#include <QStandardPaths>
#include "fluenticon.h"
ChatPage::ChatPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ChatPage)
{
    ui->setupUi(this);
    ui->send_btn->SetState("normal","hover","press");
    ui->send_btn->setCursor(Qt::PointingHandCursor);
    ui->emo_lb->SetState("normal","hover","press","normal","hover","press");
    ui->file_lb->SetState("normal","hover","press","normal","hover","press");

    // Toolbar icons: Fluent Icons (installed on Win10+).
    FIC::applyIconFont(ui->emo_lb,  18); ui->emo_lb->setText(QString(FIC::Emoji));
    FIC::applyIconFont(ui->file_lb, 18); ui->file_lb->setText(QString(FIC::Picture));

    // New UI: emoji panel is not implemented — hide its trigger.
    ui->emo_lb->hide();

    // file_lb becomes a dedicated "choose image" entry: clicking it opens a
    // native file picker limited to image formats, and inserts the picked
    // images into the message edit as image tokens (reusing the existing
    // "paste image" path already supported by MessageTextEdit).
    ui->file_lb->setCursor(Qt::PointingHandCursor);
    ui->file_lb->setToolTip(QStringLiteral("选择图片"));
    connect(ui->file_lb, &ClickedLabel::clicked, this, [this]() {
        static const QString kImgFilter =
            QStringLiteral("图片 (*.png *.jpg *.jpeg *.gif *.bmp *.webp)");
        QString start = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        QStringList paths = QFileDialog::getOpenFileNames(
            this, QStringLiteral("选择图片"), start, kImgFilter);
        for (const QString& p : paths) {
            if (p.isEmpty()) continue;
            QPixmap pm(p);
            if (pm.isNull()) continue;
            ui->chatEdit->insertImageFromPath(p);
        }
    });

    connect(ui->chatEdit,&MessageTextEdit::send,this,&ChatPage::on_send_btn_clicked);

    setupAiPanel();
}

ChatPage::~ChatPage()
{
    delete ui;
}


void ChatPage::paintEvent(QPaintEvent *event)
{
    // QStyleOptionFrame opt;
    // opt.initFrom(this);
    // QPainter p(this);
    // style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}


void ChatPage::on_send_btn_clicked()
{
    if (_user_info == nullptr) {
        qDebug() << "friend_info is empty";
        return;
    }

    auto user_info = UserMgr::GetInstance()->GetUserInfo();
    MessageTextEdit * pTextEdit = ui->chatEdit;
    ChatRole role = ChatRole::Self;
    QString userName = user_info->_name;
    QString userIcon = user_info->_icon;

    const QVector<MsgInfo>& msgList = pTextEdit->getMsgList();
    QJsonObject textObj;
    QJsonArray textArray;
    int txt_size = 0;

    for(int i=0; i<msgList.size(); ++i)
    {
        //消息内容长度不合规就跳过
        if(msgList[i].content.length() > 1024){
            continue;
        }

        QString type = msgList[i].msgFlag;
        ChatItemBase *pChatItem = new ChatItemBase(role);
        pChatItem->setUserName(userName);
        pChatItem->setUserIcon(QPixmap(userIcon));
        QWidget *pBubble = nullptr;

        if(type == "text")
        {
            //生成唯一id
            QUuid uuid = QUuid::createUuid();
            //转为字符串
            QString uuidString = uuid.toString();

            pBubble = new TextBubble(role, msgList[i].content);
            if(txt_size + msgList[i].content.length()> 1024){
                textObj["fromuid"] = user_info->_uid;
                textObj["touid"] = _user_info->_uid;
                textObj["text_array"] = textArray;
                QJsonDocument doc(textObj);
                QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
                //发送并清空之前累计的文本列表
                txt_size = 0;
                textArray = QJsonArray();
                textObj = QJsonObject();
                //发送tcp请求给chat server
                emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, jsonData);
            }

            //将bubble和uid绑定，以后可以等网络返回消息后设置是否送达
            //_bubble_map[uuidString] = pBubble;
            txt_size += msgList[i].content.length();
            QJsonObject obj;
            QByteArray utf8Message = msgList[i].content.toUtf8();
            obj["content"] = QString::fromUtf8(utf8Message);
            obj["msgid"] = uuidString;
            textArray.append(obj);
            auto txt_msg = std::make_shared<TextChatData>(uuidString, obj["content"].toString(),
                                                          user_info->_uid, _user_info->_uid);
            emit sig_append_send_chat_msg(txt_msg);
        }
        else if(type == "image" || type == "file")
        {
            // Show image bubble locally immediately
            if(type == "image") {
                pBubble = new PictureBubble(QPixmap(msgList[i].content), role);
            }

            // Send file upload request to ChatServer
            QString local_path = msgList[i].content;
            QFileInfo fi(local_path);
            int file_type = (type == "image") ? 0 : 1;

            QJsonObject fileReq;
            fileReq["fromuid"] = user_info->_uid;
            fileReq["touid"] = _user_info->_uid;
            fileReq["file_name"] = fi.fileName();
            fileReq["file_size"] = static_cast<double>(fi.size());
            fileReq["file_type"] = file_type;

            QJsonDocument fileDoc(fileReq);
            QByteArray fileData = fileDoc.toJson(QJsonDocument::Compact);
            emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_FILE_UPLOAD_REQ, fileData);

            // Store local_path in FIFO queue for retrieval when upload RSP arrives
            _pending_file_paths.enqueue(local_path);
        }
        //发送消息
        if(pBubble != nullptr)
        {
            pChatItem->setWidget(pBubble);
            ui->chat_data_list->appendChatItem(pChatItem);
        }
    }

    // Only send text message if there's actual text content
    if (!textArray.isEmpty()) {
        textObj["text_array"] = textArray;
        textObj["fromuid"] = user_info->_uid;
        textObj["touid"] = _user_info->_uid;
        QJsonDocument doc(textObj);
        QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
        emit TcpMgr::GetInstance()->sig_send_data(ReqId::ID_TEXT_CHAT_MSG_REQ, jsonData);
    }
}



void ChatPage::SetUserInfo(std::shared_ptr<UserInfo> user_info)
{
    _user_info = user_info;
    ui->title_lb->setText(_user_info->_name);

    // M2: show AI button only when AgentServer is available
    if (_ai_toggle_btn) {
        _ai_toggle_btn->setVisible(UserMgr::GetInstance()->HasAgent());
    }
    // hide panel when switching conversations
    if (_ai_panel) {
        _ai_panel->setVisible(false);
    }

    // STAGE-C: first paint from LocalDb (instant). Then fire one
    // ID_PULL_MESSAGES_REQ to reconcile with the server. The response
    // lands in ChatDialog::slot_pull_messages_rsp, which must call
    // RefreshFromLocalDb — NOT SetUserInfo — to avoid retriggering
    // another pull request in an infinite loop.
    RefreshFromLocalDb();

    const int kPageSize = 30;
    QJsonObject req;
    req["uid"] = UserMgr::GetInstance()->GetUid();
    req["peer_uid"] = user_info->_uid;
    req["before_msg_db_id"] = 0; // 0 = newest page
    req["limit"] = kPageSize;
    QByteArray data = QJsonDocument(req).toJson(QJsonDocument::Compact);
    emit TcpMgr::GetInstance()->sig_send_data(
        ReqId::ID_PULL_MESSAGES_REQ, data);
}

void ChatPage::RefreshFromLocalDb()
{
    if (!_user_info) return;
    ui->chat_data_list->removeAllItem();

    const int kPageSize = 30;
    const int peer_uid = _user_info->_uid;
    const int self_uid = UserMgr::GetInstance()->GetUid();

    auto rows = LocalDb::Inst().LoadRecent(peer_uid, kPageSize);

    // Suppress per-row repaints while we batch-append messages. Without
    // this, every AppendChatMsg triggers a layout/paint cycle, and each
    // freshly constructed TextBubble paints its default-styled state for
    // a brief moment before its stylesheet + height kick in. The user
    // sees a flicker of "white box / black text" boxes scrolling past.
    // Disabling updates ensures only ONE paint after the loop completes.
    ui->chat_data_list->setUpdatesEnabled(false);
    for (const auto& row : rows) {
        auto tds = LocalDb::RowToTextChatData(row, self_uid);
        for (const auto& td : tds) AppendChatMsg(td);
    }
    ui->chat_data_list->setUpdatesEnabled(true);
    ui->chat_data_list->update();
}

void ChatPage::AppendChatMsg(std::shared_ptr<TextChatData> msg)
{
    auto self_info = UserMgr::GetInstance()->GetUserInfo();

    // Resolve role + display name/icon
    ChatRole role;
    QString displayName;
    QString displayIcon;
    if (msg->_from_uid == self_info->_uid) {
        role = ChatRole::Self;
        displayName = self_info->_name;
        displayIcon = self_info->_icon;
    } else {
        role = ChatRole::Other;
        auto friend_info = UserMgr::GetInstance()->GetFriendById(msg->_from_uid);
        if (friend_info == nullptr) return;
        displayName = friend_info->_name;
        displayIcon = friend_info->_icon;
    }

    ChatItemBase* pChatItem = new ChatItemBase(role);
    pChatItem->setUserName(displayName);
    pChatItem->setUserIcon(QPixmap(displayIcon));

    if (!msg->IsFile()) {
        // Plain text path — unchanged.
        pChatItem->setWidget(new TextBubble(role, msg->_msg_content));
        ui->chat_data_list->appendChatItem(pChatItem);
        return;
    }

    // --- File message path ---

    // Case 1: already downloaded in a previous session or realtime recv,
    // and the local file still exists → render image immediately.
    if (msg->_msg_type == MSG_TYPE_IMAGE
        && !msg->_local_path.isEmpty()
        && QFile::exists(msg->_local_path))
    {
        pChatItem->setWidget(new PictureBubble(QPixmap(msg->_local_path), role));
        ui->chat_data_list->appendChatItem(pChatItem);
        return;
    }

    // Case 2: need to download. Build a placeholder bubble, append the item,
    // then kick off FileMgr and swap in a PictureBubble when done.
    QString typeLabel;
    switch (msg->_msg_type) {
    case MSG_TYPE_IMAGE: typeLabel = QStringLiteral("[图片] "); break;
    case MSG_TYPE_FILE:  typeLabel = QStringLiteral("[文件] "); break;
    case MSG_TYPE_AUDIO: typeLabel = QStringLiteral("[语音] "); break;
    default:             typeLabel = QStringLiteral("[媒体] "); break;
    }
    QString placeholderText = typeLabel + msg->_file_name
        + QStringLiteral(" (图片下载中...)");
    pChatItem->setWidget(new TextBubble(role, placeholderText));
    ui->chat_data_list->appendChatItem(pChatItem);

    // Non-image types (file / audio) are out of scope for stage A image
    // rendering; just leave the placeholder bubble visible.
    if (msg->_msg_type != MSG_TYPE_IMAGE) {
        return;
    }

    // Avoid re-issuing a download if one is already in flight for this msg.
    if (msg->_download_pending) {
        return;
    }
    msg->_download_pending = true;

    // QPointer tracks the chat item and becomes null if the item is destroyed
    // (e.g. user switched chats and ChatView::removeAllItem ran).
    QPointer<ChatItemBase> itemGuard(pChatItem);
    QString target_file_id = msg->_file_id;

    // Shared tail: once we have a valid download token, start FileMgr and
    // wait for sig_download_done to swap the bubble in.
    auto start_download_when_authed = [this, msg, itemGuard, role, target_file_id]() {
        auto file_data = std::make_shared<FileChatData>(
            msg->_msg_id, msg->_file_id, msg->_file_name,
            msg->_file_size, 0 /*image*/, msg->_from_uid, msg->_to_uid);
        file_data->_file_host = msg->_file_host;
        file_data->_file_port = msg->_file_port;
        file_data->_file_token = msg->_file_token;

        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(FileMgr::GetInstance().get(), &FileMgr::sig_download_done,
                        this, [itemGuard, msg, target_file_id, role, conn]
                        (QString dl_file_id, QString local_path, int error) {
            if (dl_file_id != target_file_id) return;
            QObject::disconnect(*conn);

            msg->_download_pending = false;
            if (error != 0) {
                qDebug() << "history image download failed: file_id=" << target_file_id
                         << " error=" << error;
                return;
            }
            msg->_local_path = local_path;
            if (!itemGuard) return;
            itemGuard->setWidget(new PictureBubble(QPixmap(local_path), role));
        });

        FileMgr::GetInstance()->StartDownload(file_data);
    };

    // Fast path: we already have routing info (e.g. realtime file_msg_notify
    // delivered the token inline). Skip the extra roundtrip.
    if (!msg->_file_token.isEmpty() && !msg->_file_host.isEmpty()) {
        start_download_when_authed();
        return;
    }

    // STAGE-C: history file message with no token (loaded from LocalDb).
    // Ask the server to mint a one-shot download token on demand.
    auto tok_conn = std::make_shared<QMetaObject::Connection>();
    *tok_conn = connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_download_token_rsp,
                        this, [msg, target_file_id, start_download_when_authed, tok_conn]
                        (QString rsp_file_id, QString host, QString port,
                         QString token, int error) {
        if (rsp_file_id != target_file_id) return;
        QObject::disconnect(*tok_conn);

        if (error != 0 || token.isEmpty()) {
            qDebug() << "get download token failed: file_id=" << target_file_id
                     << " error=" << error;
            msg->_download_pending = false;
            return;
        }
        msg->_file_host = host;
        msg->_file_port = port;
        msg->_file_token = token;
        start_download_when_authed();
    });

    QJsonObject req;
    req["uid"] = UserMgr::GetInstance()->GetUid();
    req["file_id"] = msg->_file_id;
    QByteArray data = QJsonDocument(req).toJson(QJsonDocument::Compact);
    emit TcpMgr::GetInstance()->sig_send_data(
        ReqId::ID_GET_DOWNLOAD_TOKEN_REQ, data);
}

QString ChatPage::PopPendingFilePath() {
    if (_pending_file_paths.isEmpty()) return "";
    return _pending_file_paths.dequeue();
}

void ChatPage::AppendImageBubble(const QString& image_path, ChatRole role,
                                  const QString& name, const QString& icon) {
    ChatItemBase* pChatItem = new ChatItemBase(role);
    pChatItem->setUserName(name);
    pChatItem->setUserIcon(QPixmap(icon));
    PictureBubble* pBubble = new PictureBubble(QPixmap(image_path), role);
    pChatItem->setWidget(pBubble);
    ui->chat_data_list->appendChatItem(pChatItem);
}

// ================================================================
// AI Suggestion Panel (M2 Step 7)
// ================================================================

// Cherry-pink AI action button style (inline used for per-candidate buttons).
static const char* AI_BTN_STYLE =
    "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
    "  stop:0 #e89bb4, stop:1 #c25978); color: white; border: none;"
    "  border-radius: 4px; font-size: 11px; padding: 2px 6px; letter-spacing: 1px; }"
    "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
    "  stop:0 #d97a95, stop:1 #c25978); }"
    "QPushButton:disabled { background: #dcd8e0; }";

void ChatPage::setupAiPanel()
{
    auto* toolLayout = qobject_cast<QHBoxLayout*>(ui->tool_wid->layout());
    if (!toolLayout) return;

    _ai_toggle_btn = new QPushButton(ui->tool_wid);
    _ai_toggle_btn->setObjectName("ai_toggle_btn");
    _ai_toggle_btn->setFixedSize(56, 26);
    _ai_toggle_btn->setCursor(Qt::PointingHandCursor);
    _ai_toggle_btn->setToolTip(QStringLiteral("AI 智能回复建议"));
    _ai_toggle_btn->setText(QString(FIC::Sparkle) + QStringLiteral(" AI"));
    // Use the icon font so the spark glyph renders as a true icon.
    {
        QFont f = _ai_toggle_btn->font();
        FIC::applyIconFont(_ai_toggle_btn, 12);
        QFont mix = _ai_toggle_btn->font();
        _ai_toggle_btn->setFont(mix);
    }
    // Visual style is provided by the global stylesheet (#ai_toggle_btn).
    _ai_toggle_btn->setVisible(false);
    toolLayout->insertWidget(toolLayout->count() - 1, _ai_toggle_btn);
    connect(_ai_toggle_btn, &QPushButton::clicked, this, &ChatPage::onAiToggleClicked);

    _ai_panel = new QWidget(this);
    _ai_panel->setObjectName("ai_panel");
    _ai_panel->setAttribute(Qt::WA_StyledBackground, true);
    // Visual style comes from the global stylesheet (#ai_panel).
    _ai_panel->setVisible(false);

    auto* panelLayout = new QVBoxLayout(_ai_panel);
    panelLayout->setContentsMargins(6, 4, 6, 4);
    panelLayout->setSpacing(4);

    // row 1: preset + custom prompt + request + close
    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(4);

    _ai_preset_combo = new QComboBox;
    _ai_preset_combo->setFixedSize(90, 26);
    _ai_preset_combo->addItem(QString::fromUtf8("自动"), "");
    _ai_preset_combo->addItem(QString::fromUtf8("礼貌拒绝"), "polite_decline");
    _ai_preset_combo->addItem(QString::fromUtf8("关心安慰"), "comfort");
    _ai_preset_combo->addItem(QString::fromUtf8("幽默化解"), "humor_deflect");
    _ai_preset_combo->addItem(QString::fromUtf8("正式商务"), "formal_business");
    _ai_preset_combo->addItem(QString::fromUtf8("暧昧试探"), "flirty");
    topRow->addWidget(_ai_preset_combo);

    _ai_custom_prompt = new QLineEdit;
    _ai_custom_prompt->setFixedHeight(26);
    _ai_custom_prompt->setPlaceholderText(QString::fromUtf8("补充背景（可选）如：我们昨天刚吵过架 / 他是我领导"));
    _ai_custom_prompt->setStyleSheet(
        "QLineEdit { font-size: 11px; padding: 2px 8px; border: 1px solid #ece7ee;"
        "  border-radius: 6px; background: #ffffff; }"
        "QLineEdit:focus { border: 1px solid #e89bb4; }"
    );
    topRow->addWidget(_ai_custom_prompt, 1);

    _ai_request_btn = new QPushButton(QString::fromUtf8("获取建议"));
    _ai_request_btn->setFixedSize(76, 26);
    _ai_request_btn->setStyleSheet(AI_BTN_STYLE);
    topRow->addWidget(_ai_request_btn);

    auto* closeBtn = new QPushButton(QStringLiteral("✕"));
    closeBtn->setFixedSize(26, 26);
    closeBtn->setStyleSheet(
        "QPushButton { background: transparent; color: #8a838f; border: none;"
        "  font-weight: bold; font-size: 12px; }"
        "QPushButton:hover { color: #c25978; }"
    );
    topRow->addWidget(closeBtn);

    panelLayout->addLayout(topRow);

    _ai_status_label = new QLabel;
    _ai_status_label->setStyleSheet("QLabel { color: #8a838f; font-size: 11px; padding-left: 4px; }");
    _ai_status_label->setVisible(false);
    panelLayout->addWidget(_ai_status_label);

    _ai_candidates_container = new QWidget;
    _ai_candidates_layout = new QVBoxLayout(_ai_candidates_container);
    _ai_candidates_layout->setContentsMargins(0, 0, 0, 0);
    _ai_candidates_layout->setSpacing(3);
    panelLayout->addWidget(_ai_candidates_container);

    auto* mainLayout = qobject_cast<QVBoxLayout*>(ui->chat_data_wid->layout());
    if (mainLayout) {
        int toolIdx = mainLayout->indexOf(ui->tool_wid);
        mainLayout->insertWidget(toolIdx + 1, _ai_panel);
    }

    connect(_ai_request_btn, &QPushButton::clicked, this, &ChatPage::onAiRequestClicked);
    connect(closeBtn, &QPushButton::clicked, [this]() { _ai_panel->setVisible(false); });
}

void ChatPage::onAiToggleClicked()
{
    _ai_panel->setVisible(!_ai_panel->isVisible());
}

QJsonArray ChatPage::buildRecentMessagesJson(int limit)
{
    QJsonArray arr;
    if (!_user_info) return arr;

    int self_uid = UserMgr::GetInstance()->GetUid();
    int peer_uid = _user_info->_uid;
    auto rows = LocalDb::Inst().LoadRecent(peer_uid, limit);
    for (const auto& row : rows) {
        auto tds = LocalDb::RowToTextChatData(row, self_uid);
        for (const auto& td : tds) {
            if (td->_msg_type != MSG_TYPE_TEXT) continue;
            QJsonObject m;
            m["msg_db_id"]  = QString::number(td->_msg_db_id);
            m["from_uid"]   = td->_from_uid;
            m["to_uid"]     = td->_to_uid;
            m["msg_type"]   = td->_msg_type;
            m["content"]    = td->_msg_content;
            m["send_time"]  = static_cast<double>(td->_send_time);
            m["direction"]  = (td->_from_uid == self_uid) ? 1 : 0;
            arr.append(m);
        }
    }
    return arr;
}

QNetworkRequest ChatPage::makeAgentRequest(const QString& path)
{
    QString host  = UserMgr::GetInstance()->GetAgentHost();
    int port      = UserMgr::GetInstance()->GetAgentPort();
    QString token = UserMgr::GetInstance()->GetToken();
    int self_uid  = UserMgr::GetInstance()->GetUid();

    QUrl url(QString("http://%1:%2%3").arg(host).arg(port).arg(path));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    req.setRawHeader("X-Self-Uid", QString::number(self_uid).toUtf8());
    return req;
}

void ChatPage::clearCandidatesUi()
{
    QLayoutItem* item;
    while ((item = _ai_candidates_layout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    _ai_candidate_texts.clear();
}

void ChatPage::renderCandidates(const QJsonArray& candidates)
{
    clearCandidatesUi();
    for (int i = 0; i < candidates.size(); ++i) {
        QJsonObject c = candidates[i].toObject();
        QString style   = c["style"].toString();
        QString content = c["content"].toString();
        _ai_candidate_texts.append(content);

        auto* row = new QWidget;
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(2, 2, 2, 2);
        rowLayout->setSpacing(4);

        auto* label = new QLabel(QString("[%1] %2").arg(style, content));
        label->setWordWrap(true);
        label->setStyleSheet(
            "QLabel { font-size: 12px; color: #26222b; padding: 6px 8px;"
            "  background: #ffffff; border: 1px solid #f6cfdd; border-radius: 6px; }");
        rowLayout->addWidget(label, 1);

        auto* refineBtn = new QPushButton(QString::fromUtf8("润色"));
        refineBtn->setFixedSize(44, 24);
        refineBtn->setStyleSheet(AI_BTN_STYLE);
        int idx = i;
        connect(refineBtn, &QPushButton::clicked, [this, idx]() {
            onCandidateRefineClicked(idx);
        });
        rowLayout->addWidget(refineBtn);

        auto* useBtn = new QPushButton(QString::fromUtf8("采用"));
        useBtn->setFixedSize(44, 24);
        useBtn->setStyleSheet(AI_BTN_STYLE);
        connect(useBtn, &QPushButton::clicked, [this, idx]() {
            onCandidateUseClicked(idx);
        });
        rowLayout->addWidget(useBtn);

        _ai_candidates_layout->addWidget(row);
    }
}

void ChatPage::onAiRequestClicked()
{
    if (!UserMgr::GetInstance()->HasAgent()) {
        _ai_status_label->setText(QString::fromUtf8("AI 服务不可用"));
        _ai_status_label->setVisible(true);
        return;
    }
    if (!_user_info) return;

    auto recentMsgs = buildRecentMessagesJson(10);
    if (recentMsgs.isEmpty()) {
        _ai_status_label->setText(QString::fromUtf8("没有可分析的文本消息"));
        _ai_status_label->setVisible(true);
        return;
    }

    _ai_request_btn->setEnabled(false);
    _ai_status_label->setText(QString::fromUtf8("正在生成建议..."));
    _ai_status_label->setVisible(true);
    _ai_custom_prompt->setPlaceholderText(QString::fromUtf8("补充背景（可选）如：我们昨天刚吵过架 / 他是我领导"));
    clearCandidatesUi();

    int self_uid = UserMgr::GetInstance()->GetUid();
    QString presetId = _ai_preset_combo->currentData().toString();
    QString customPrompt = _ai_custom_prompt->text().trimmed();

    QJsonObject body;
    body["self_uid"]         = self_uid;
    body["peer_uid"]         = _user_info->_uid;
    body["recent_messages"]  = recentMsgs;
    body["num_candidates"]   = 3;
    if (!presetId.isEmpty())
        body["preset_id"] = presetId;
    if (!customPrompt.isEmpty())
        body["custom_prompt"] = customPrompt;

    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    auto* reply = _ai_nam.post(makeAgentRequest("/agent/suggest_reply"), data);
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        onAiReplyFinished(reply);
    });
}

void ChatPage::onAiReplyFinished(QNetworkReply* reply)
{
    _ai_request_btn->setEnabled(true);
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errDetail;
        if (httpStatus == 429)
            errDetail = QString::fromUtf8("今日使用次数已达上限");
        else if (httpStatus == 401)
            errDetail = QString::fromUtf8("认证失败，请重新登录");
        else if (httpStatus == 404)
            errDetail = QString::fromUtf8("预设不存在");
        else
            errDetail = QString::fromUtf8("请求失败: ") + reply->errorString();
        _ai_status_label->setText(errDetail);
        _ai_status_label->setVisible(true);
        return;
    }

    QByteArray raw = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject()) {
        _ai_status_label->setText(QString::fromUtf8("响应解析失败"));
        return;
    }
    QJsonObject obj = doc.object();

    _ai_session_id = obj["session_id"].toString();
    QString intent = obj["intent_analysis"].toString();

    _ai_status_label->setText(QString::fromUtf8("意图分析: ") + intent);
    _ai_status_label->setVisible(true);

    renderCandidates(obj["candidates"].toArray());

    _ai_custom_prompt->clear();
    _ai_custom_prompt->setPlaceholderText(QString::fromUtf8("修改指令（如：其实我想答应但不想太主动 / 再简短些）"));
}

void ChatPage::onCandidateUseClicked(int index)
{
    if (index < 0 || index >= _ai_candidate_texts.size()) return;
    ui->chatEdit->setText(_ai_candidate_texts[index]);
    _ai_panel->setVisible(false);
}

void ChatPage::onCandidateRefineClicked(int index)
{
    if (index < 0 || index >= _ai_candidate_texts.size()) return;
    if (_ai_session_id.isEmpty()) {
        _ai_status_label->setText(QString::fromUtf8("请先获取建议"));
        _ai_status_label->setVisible(true);
        return;
    }

    // simple input: reuse the custom_prompt lineEdit as the refine instruction
    QString instruction = _ai_custom_prompt->text().trimmed();
    if (instruction.isEmpty()) {
        _ai_status_label->setText(QString::fromUtf8("请在提示词框中输入润色指令，如「再简短些」"));
        _ai_status_label->setVisible(true);
        return;
    }

    _ai_request_btn->setEnabled(false);
    _ai_status_label->setText(
        QString::fromUtf8("正在润色第 %1 条...").arg(index + 1));
    _ai_status_label->setVisible(true);

    QJsonObject body;
    body["session_id"]      = _ai_session_id;
    body["candidate_index"] = index;
    body["instruction"]     = instruction;

    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    auto* reply = _ai_nam.post(makeAgentRequest("/agent/refine"), data);

    connect(reply, &QNetworkReply::finished, [this, reply, index]() {
        onRefineReplyFinished(reply, index);
    });
}

void ChatPage::onRefineReplyFinished(QNetworkReply* reply, int index)
{
    _ai_request_btn->setEnabled(true);
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString err;
        if (httpStatus == 404)
            err = QString::fromUtf8("会话已过期，请重新获取建议");
        else
            err = QString::fromUtf8("润色失败: ") + reply->errorString();
        _ai_status_label->setText(err);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) return;

    QJsonObject c = doc.object();
    QString style   = c["style"].toString();
    QString content = c["content"].toString();

    if (index >= 0 && index < _ai_candidate_texts.size()) {
        _ai_candidate_texts[index] = content;
    }

    // update the label in the UI row
    auto* rowWidget = _ai_candidates_layout->itemAt(index)
                          ? _ai_candidates_layout->itemAt(index)->widget()
                          : nullptr;
    if (rowWidget) {
        auto* label = rowWidget->findChild<QLabel*>();
        if (label)
            label->setText(QString("[%1] %2").arg(style, content));
    }

    _ai_status_label->setText(
        QString::fromUtf8("第 %1 条已润色").arg(index + 1));
    _ai_custom_prompt->clear();
}
