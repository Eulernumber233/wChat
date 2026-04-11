#include "chatpage.h"
#include "ui_chatpage.h"
#include "chatitembase.h"
#include "TextBubble.h"
#include "picturebubble.h"
#include "usermgr.h"
#include "tcpmgr.h"
#include "filemgr.h"
#include <QUuid>
#include <QPointer>
#include <QFile>
#include <QPixmap>
ChatPage::ChatPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ChatPage)
{
    ui->setupUi(this);
    //设置按钮样式
    ui->send_btn->SetState("normal","hover","press");

    //设置图标样式
    ui->emo_lb->SetState("normal","hover","press","normal","hover","press");
    ui->file_lb->SetState("normal","hover","press","normal","hover","press");

    connect(ui->chatEdit,&MessageTextEdit::send,this,&ChatPage::on_send_btn_clicked);
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
    //设置ui界面
    ui->title_lb->setText(_user_info->_name);
    ui->chat_data_list->removeAllItem();
    for(auto & msg : user_info->_chat_msgs){
        AppendChatMsg(msg);
    }
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

    // No routing info means the server didn't mint a download token — nothing
    // we can do client-side. Leave placeholder.
    if (msg->_file_token.isEmpty() || msg->_file_host.isEmpty()) {
        return;
    }

    // Avoid re-issuing a download if one is already in flight for this msg
    // (can happen if the user rapidly switches away and back).
    //
    // STAGE-A-NEXT: known UX gap — if the user switches chats while the
    // download is in flight and comes back before it finishes, the new
    // placeholder is shown but won't refresh until the user opens the chat
    // once more (because the first lambda's itemGuard is null by then and
    // this second entry sees _download_pending=true). The right fix needs
    // a per-msg list of waiting bubbles; deferring until SQLite-era rework.
    if (msg->_download_pending) {
        return;
    }
    msg->_download_pending = true;

    // QPointer tracks the chat item and becomes null if the item is destroyed
    // (e.g. user switched chats and ChatView::removeAllItem ran).
    QPointer<ChatItemBase> itemGuard(pChatItem);
    QString target_file_id = msg->_file_id;

    // Build a FileChatData for FileMgr::StartDownload.
    auto file_data = std::make_shared<FileChatData>(
        msg->_msg_id, msg->_file_id, msg->_file_name,
        msg->_file_size, 0 /*image*/, msg->_from_uid, msg->_to_uid);
    file_data->_file_host = msg->_file_host;
    file_data->_file_port = msg->_file_port;
    file_data->_file_token = msg->_file_token;

    // One-shot connection: disconnect as soon as our file_id matches, so
    // unrelated downloads in the same session don't pile up handlers.
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
            // Leave placeholder. STAGE-A-NEXT: could show "[下载失败]".
            return;
        }
        msg->_local_path = local_path;

        // If the user already left this chat, the ChatItemBase is gone; the
        // next time they open the conversation AppendChatMsg will hit Case 1
        // (local path exists) and render immediately. So we just bail here.
        if (!itemGuard) return;

        itemGuard->setWidget(new PictureBubble(QPixmap(local_path), role));
    });

    FileMgr::GetInstance()->StartDownload(file_data);
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
