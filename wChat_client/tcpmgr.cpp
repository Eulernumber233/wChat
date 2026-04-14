#include "tcpmgr.h"
#include "apppaths.h"
#include "filemgr.h"
#include "localdb.h"
#include <QtEndian>

TcpMgr::TcpMgr():_host(""),_port(0),_b_recv_pending(false),_message_id(0),_message_len(0),_kick_pending(false),_last_heartbeat_rsp_ms(0)
{
    // 建立连接回调
    QObject::connect(&_socket, &QTcpSocket::connected, [&]() {
        qDebug() << "Connected to server!";
        // 连接建立后发送消息
        emit sig_con_success(true);
    });
    // 断开连接回调
    QObject::connect(&_socket, &QTcpSocket::disconnected, [&]() {
        qDebug() << "Disconnected from server.";
        if (!_kick_pending) {
            // 非踢人导致的断线，通知 UI 层
            emit sig_connection_lost();
        }
    });
    // 读信号回调
    QObject::connect(&_socket, &QTcpSocket::readyRead, this, &TcpMgr::slot_read_data);
    // 写信号回调
    QObject::connect(this, &TcpMgr::sig_send_data, this, &TcpMgr::slot_send_data);
    // 异常回调
    QObject::connect(&_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                    [&](QAbstractSocket::SocketError socketError){
                        Q_UNUSED(socketError)
                         qDebug() << "Error:" << _socket.errorString() ;
                         switch (socketError) {
                         case QTcpSocket::ConnectionRefusedError:
                             qDebug() << "Connection Refused!";
                             emit sig_con_success(false);
                             break;
                         case QTcpSocket::RemoteHostClosedError:
                             qDebug() << "Remote Host Closed Connection!";
                             if (!_kick_pending) {
                                 emit sig_connection_lost();
                             }
                             break;
                         case QTcpSocket::HostNotFoundError:
                             qDebug() << "Host Not Found!";
                             emit sig_con_success(false);
                             break;
                         case QTcpSocket::SocketTimeoutError:
                             qDebug() << "Connection Timeout!";
                             emit sig_con_success(false);
                             break;
                         case QTcpSocket::NetworkError:
                             qDebug() << "Network Error!";
                             break;
                         default:
                             qDebug() << "Other Error!";
                             break;
                         }
                     });

    initHandlers();
}

void TcpMgr::slot_tcp_connect(ServerInfo si)
{
    qDebug()<< "receive tcp connect signal";
    // 尝试连接到服务器
    qDebug() << "Connecting to server...";
    _kick_pending = false; // 重置踢人标志
    _last_heartbeat_rsp_ms = QDateTime::currentMSecsSinceEpoch(); // 初始化为当前时间
    _host = si.Host;
    _port = static_cast<uint16_t>(si.Port.toUInt());
    _socket.connectToHost(si.Host, _port);
}

void TcpMgr::slot_read_data()
{
    _buffer.append(_socket.readAll());

    forever {
        // 解析头部
        if(!_b_recv_pending){
            // 检查缓冲区中的数据是否足够解析出一个消息头（消息ID 2B + 消息长度 2B）
            if (_buffer.size() < static_cast<int>(sizeof(quint16) * 2)) {
                return;
            }

            // 直接从 buffer 按大端字节序读取 msg_id 和 msg_len
            _message_id = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(_buffer.constData()));
            _message_len = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(_buffer.constData() + sizeof(quint16)));

            // 移除头部 4 字节
            _buffer = _buffer.mid(sizeof(quint16) * 2);

            qDebug() << "Message ID:" << _message_id << ", Length:" << _message_len;
        }

        // buffer 剩余长度是否满足消息体长度
        if(_buffer.size() < _message_len){
            _b_recv_pending = true;
            return;
        }

        _b_recv_pending = false;
        QByteArray messageBody = _buffer.mid(0, _message_len);
        qDebug() << "receive body msg is " << messageBody ;

        _buffer = _buffer.mid(_message_len);
        handleMsg(ReqId(_message_id),_message_len, messageBody);
    }
}

void TcpMgr::slot_send_data(ReqId reqId, QByteArray dataBytes)
{
    uint16_t id = reqId;

    // 计算长度（使用网络字节序转换）
    quint16 len = static_cast<quint16>(dataBytes.size());
    // 创建一个QByteArray用于存储要发送的所有数据
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    // 设置数据流使用网络字节序
    out.setByteOrder(QDataStream::BigEndian);
    // 写入ID和长度
    out << id << len;
    // 添加字符串数据
    block.append(dataBytes);
    // 发送数据
    _socket.write(block);
}

void TcpMgr::handleMsg(ReqId id, int len, QByteArray data)
{
    auto find_iter =  _handlers.find(id);
    if(find_iter == _handlers.end()){
        qDebug()<< "not found id ["<< id << "] to handle";
        return ;
    }

    find_iter.value()(id,len,data);
}
void TcpMgr::CloseConnection(){
    _socket.close();
}

TcpMgr::~TcpMgr(){

}
void TcpMgr::initHandlers()
{
    //auto self = shared_from_this();
    _handlers.insert(ID_CHAT_LOGIN_RSP, [this](ReqId id, int len, QByteArray data){
        qDebug()<< "handle id is "<< id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        // 检查转换是否成功
        if(jsonDoc.isNull()){
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }
        QJsonObject jsonObj = jsonDoc.object();
        if(!jsonObj.contains("error")){
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Login Failed, err is Json Parse Err" << err ;
            emit sig_login_failed(err);
            return;
        }
        int err = jsonObj["error"].toInt();
        if(err != ErrorCodes::SUCCESS){
            qDebug() << "Login Failed, err is " << err ;
            emit sig_login_failed(err);
            return;
        }
        auto uid = jsonObj["uid"].toInt();
        auto name = jsonObj["name"].toString();
        auto nick = jsonObj["nick"].toString();
        auto icon = jsonObj["icon"].toString();
        qDebug() << "-------------------------------";
        qDebug() << "-------------------------------";
        qDebug() << "-------------------------------";
        qDebug() << icon;
        qDebug() << "-------------------------------";
        qDebug() << "-------------------------------";
        qDebug() << "-------------------------------";
        auto sex = jsonObj["sex"].toInt();
        auto certification = jsonObj["certification"].toString();
        auto user_info = std::make_shared<UserInfo>(uid, name, nick, icon, sex,"",certification);
        UserMgr::GetInstance()->SetUserInfo(user_info);
        UserMgr::GetInstance()->SetToken(jsonObj["token"].toString());

        // STAGE-B: bind per-user paths before parsing history so that
        // UserMgr::AppendFriendList can probe the per-user file cache
        // while building TextChatData records.
        AppPaths::SetCurrentUser(uid);
        FileMgr::GetInstance()->Init();

        // STAGE-C: open per-user SQLite before parsing friend list.
        if (!LocalDb::Inst().Open()) {
            qWarning() << "LocalDb::Open failed for uid=" << uid;
        }

        //申请列表
        if(jsonObj.contains("apply_list")){
            UserMgr::GetInstance()->AppendApplyList(jsonObj["apply_list"].toArray());
        }
        //添加好友列表 (STAGE-C: friend profile only — no embedded history)
        if (jsonObj.contains("friend_list")) {
            UserMgr::GetInstance()->AppendFriendList(jsonObj["friend_list"].toArray());
        }
        qDebug()<<"hhhhh success login !!!!"<<Qt::endl;
        emit sig_switch_chatdlg();

        // STAGE-C: fire off conversation summary request immediately so the
        // chat list can show last-msg previews and unread badges without
        // waiting for the user to click any peer.
        {
            QJsonObject pullObj;
            pullObj["uid"] = uid;
            QByteArray pullData = QJsonDocument(pullObj).toJson(QJsonDocument::Compact);
            emit sig_send_data(ReqId::ID_PULL_CONV_SUMMARY_REQ, pullData);
        }
    });
    // 搜索回复
    _handlers.insert(ID_SEARCH_USER_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Search Failed, err is Json Parse Err" << err;

            emit sig_user_search(nullptr);
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Search Failed, err is " << err;
            emit sig_user_search(nullptr);
            return;
        }
        auto search_info =  std::make_shared<SearchInfo>(jsonObj["uid"].toInt(), jsonObj["name"].toString(),
            jsonObj["nick"].toString(), jsonObj["desc"].toString(),jsonObj["sex"].toInt(), jsonObj["icon"].toString());
        qDebug()<<"--search reslt: name:"<<search_info->_name<<"  uid:"<<search_info->_uid
            <<"  icon :" <<search_info->_icon <<"----"<<Qt::endl;
        emit sig_user_search(search_info);
    });
    // 自己是否成功发出申请的回复
    _handlers.insert(ID_ADD_FRIEND_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Add Friend Failed, err is Json Parse Err" << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Add Friend Failed, err is " << err;
            return;
        }

        qDebug() << "Add Friend Success " ;
    });
    // 处理对方发来的申请
    _handlers.insert(ID_NOTIFY_ADD_FRIEND_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            emit sig_user_search(nullptr);
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << " Failed, err is " << err;
            emit sig_user_search(nullptr);
            return;
        }

        int from_uid = jsonObj["applyuid"].toInt();
        QString name = jsonObj["name"].toString();
        QString icon = jsonObj["icon"].toString();
        QString nick = jsonObj["nick"].toString();
        QString back = jsonDoc["back"].toString();
        QString certification = jsonDoc["certification"].toString();
        int sex = jsonObj["sex"].toInt();

        auto apply_info = std::make_shared<AddFriendApply>(
            from_uid, name, certification,
            icon, nick, sex);

        emit sig_friend_apply(apply_info);
    });
    // 对方同意自己的申请
    _handlers.insert(ID_NOTIFY_AUTH_FRIEND_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();
        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Auth Friend Failed, err is " << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Auth Friend Failed, err is " << err;
            return;
        }

        int from_uid = jsonObj["fromuid"].toInt();
        QString name = jsonObj["name"].toString();
        QString back = jsonObj["back"].toString();
        QString icon = jsonObj["icon"].toString();
        int sex = jsonObj["sex"].toInt();

        auto auth_info = std::make_shared<AuthInfo>(from_uid,name,
                                                    back, icon, sex);

        emit sig_add_auth_friend(auth_info);
    });
    // 自己是否成功同意对方发出的申请的回复
    _handlers.insert(ID_AUTH_FRIEND_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Auth Friend Failed, err is Json Parse Err" << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Auth Friend Failed, err is " << err;
            return;
        }

        auto name = jsonObj["name"].toString();
        auto back = jsonObj["back"].toString();
        auto icon = jsonObj["icon"].toString();
        auto sex = jsonObj["sex"].toInt();
        auto uid = jsonObj["uid"].toInt();
        auto rsp = std::make_shared<AuthRsp>(uid, name, back, icon, sex);
        emit sig_auth_rsp(rsp);

        qDebug() << "Auth Friend Success " ;
    });
    _handlers.insert(ID_TEXT_CHAT_MSG_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Chat Msg Rsp Failed, err is Json Parse Err" << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Chat Msg Rsp Failed, err is " << err;
            return;
        }

        qDebug() << "Receive Text Chat Rsp Success " ;
        // STAGE-C: emit an echo carrying msg_db_id + arrays so ChatDialog
        // can persist the outgoing message into LocalDb.
        qint64 msg_db_id = static_cast<qint64>(jsonObj.value("msg_db_id").toDouble());
        auto echo = std::make_shared<TextChatMsg>(jsonObj["fromuid"].toInt(),
                                                  jsonObj["touid"].toInt(),
                                                  jsonObj["text_array"].toArray(),
                                                  msg_db_id);
        emit sig_text_chat_msg_rsp(echo);
    });
    _handlers.insert(ID_NOTIFY_TEXT_CHAT_MSG_REQ, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << " data is " << data;
        // 将QByteArray转换为QJsonDocument
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        // 检查转换是否成功
        if (jsonDoc.isNull()) {
            qDebug() << "Failed to create QJsonDocument.";
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        if (!jsonObj.contains("error")) {
            int err = ErrorCodes::ERR_JSON;
            qDebug() << "Notify Chat Msg Failed, err is Json Parse Err" << err;
            return;
        }

        int err = jsonObj["error"].toInt();
        if (err != ErrorCodes::SUCCESS) {
            qDebug() << "Notify Chat Msg Failed, err is " << err;
            return;
        }

        qDebug() << "Receive Text Chat Notify Success " ;
        qint64 msg_db_id = static_cast<qint64>(jsonObj.value("msg_db_id").toDouble());
        auto msg_ptr = std::make_shared<TextChatMsg>(jsonObj["fromuid"].toInt(),
                                                     jsonObj["touid"].toInt(),
                                                     jsonObj["text_array"].toArray(),
                                                     msg_db_id);
        emit sig_text_chat_msg(msg_ptr);
    });

    // === File transfer handlers ===

    // ID_FILE_UPLOAD_RSP (1102): ChatServer responds with file_id + token + FileServer addr
    _handlers.insert(ID_FILE_UPLOAD_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        int err = obj["error"].toInt();
        QString file_id = obj["file_id"].toString();
        QString file_token = obj["file_token"].toString();
        QString host = obj["host"].toString();
        QString port = obj["port"].toString();
        // local_path was stored in a pending map; for now, retrieve via property
        QString local_path = obj.value("_local_path").toString(); // won't be in server response
        emit sig_file_upload_rsp(file_id, file_token, host, port, local_path, err);
    });

    // ID_FILE_NOTIFY_COMPLETE (1103): ChatServer confirms upload done + msg sent
    _handlers.insert(ID_FILE_NOTIFY_COMPLETE, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        int err = obj["error"].toInt();
        QString file_id = obj["file_id"].toString();

        // STAGE-C: emit enriched echo so ChatDialog can mirror this file
        // message into LocalDb. Only if the server provided msg_db_id
        // (which means the write to chat_messages succeeded). On failure
        // we still fire sig_file_notify_complete below so ChatDialog can
        // clean up any pending copy entry.
        qint64 msg_db_id = static_cast<qint64>(obj.value("msg_db_id").toDouble());
        if (err == 0 && msg_db_id > 0) {
            auto fd = std::make_shared<FileChatData>(
                obj["msgid"].toString(),
                file_id,
                obj["file_name"].toString(),
                static_cast<qint64>(obj["file_size"].toDouble()),
                obj["file_type"].toInt(),
                obj["fromuid"].toInt(),
                obj["touid"].toInt()
            );
            fd->_msg_db_id = msg_db_id;
            emit sig_file_upload_persisted(fd);
        }
        emit sig_file_notify_complete(file_id, err);
    });

    // ID_FILE_MSG_NOTIFY (1105): ChatServer tells us someone sent a file message
    _handlers.insert(ID_FILE_MSG_NOTIFY, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        int err = obj["error"].toInt();
        if (err != 0) return;

        auto file_data = std::make_shared<FileChatData>(
            obj["msgid"].toString(),
            obj["file_id"].toString(),
            obj["file_name"].toString(),
            static_cast<qint64>(obj["file_size"].toDouble()),
            obj["file_type"].toInt(),
            obj["fromuid"].toInt(),
            obj["touid"].toInt()
        );
        file_data->_msg_db_id = static_cast<qint64>(obj.value("msg_db_id").toDouble());
        file_data->_file_host = obj["file_host"].toString();
        file_data->_file_port = obj["file_port"].toString();
        file_data->_file_token = obj["file_token"].toString();

        emit sig_file_msg_notify(file_data);
    });

    // === lazy-loading history handlers ===

    // ID_PULL_CONV_SUMMARY_RSP (1026)
    _handlers.insert(ID_PULL_CONV_SUMMARY_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        if (obj["error"].toInt() != 0) {
            qWarning() << "PullConvSummary rsp error:" << obj["error"].toInt();
            return;
        }
        QJsonArray summaries = obj["summaries"].toArray();
        emit sig_conv_summary(summaries);
    });

    // ID_PULL_MESSAGES_RSP (1028)
    _handlers.insert(ID_PULL_MESSAGES_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        if (obj["error"].toInt() != 0) {
            qWarning() << "PullMessages rsp error:" << obj["error"].toInt();
            return;
        }
        int peer_uid = obj["peer_uid"].toInt();
        QJsonArray msgs = obj["messages"].toArray();
        bool has_more = obj["has_more"].toBool();
        emit sig_pull_messages_rsp(peer_uid, msgs, has_more);
    });

    // ID_GET_DOWNLOAD_TOKEN_RSP (1030)
    _handlers.insert(ID_GET_DOWNLOAD_TOKEN_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        int err = obj["error"].toInt();
        QString file_id = obj["file_id"].toString();
        QString host = obj["file_host"].toString();
        QString port = obj["file_port"].toString();
        QString token = obj["file_token"].toString();
        emit sig_download_token_rsp(file_id, host, port, token, err);
    });

    // ID_NOTIFY_KICK_USER (1031) — 被踢下线通知
    _handlers.insert(ID_NOTIFY_KICK_USER, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        QJsonObject obj = QJsonDocument::fromJson(data).object();
        QString reason = obj["reason"].toString();
        qDebug() << "Received kick notification, reason:" << reason;
        _kick_pending = true;
        emit sig_kick_user(reason);
    });

    // ID_HEARTBEAT_RSP (1024) — 心跳回复：刷新最后响应时间戳
    _handlers.insert(ID_HEARTBEAT_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len); Q_UNUSED(data);
        _last_heartbeat_rsp_ms = QDateTime::currentMSecsSinceEpoch();
    });
}
