#include "usermgr.h"
#include "filemgr.h"
UserMgr::~UserMgr()
{
}

void UserMgr::SetToken(QString token)
{
    _token = token;
}
UserMgr::UserMgr()
    :_user_info(nullptr),_chat_loaded(0),_contact_loaded(0)
{
}

int UserMgr::GetUid()
{
    return _user_info->_uid;
}

QString UserMgr::GetName()
{
    return _user_info->_name;
}

std::vector<std::shared_ptr<ApplyInfo> > UserMgr::GetApplyList()
{
    return _apply_list;
}

bool UserMgr::AlreadyApply(int uid)
{
    for(auto&apply:_apply_list){
        if(apply->_uid==uid){
            return true;
        }
    }
    return false;
}


std::shared_ptr<UserInfo> UserMgr::GetUserInfo()
{
    return _user_info;
}

void UserMgr::AddApplyList(std::shared_ptr<ApplyInfo> app)
{
    _apply_list.push_back(app);
}



void UserMgr::SetUserInfo(std::shared_ptr<UserInfo> user_info)
{
    _user_info = user_info;
}



void UserMgr::AppendApplyList(QJsonArray array)
{
    // 遍历 QJsonArray 并输出每个元素
    for (const QJsonValue &value : array) {
        auto name = value["name"].toString();
        auto certification = value["certification"].toString();
        auto icon = value["icon"].toString();
        auto nick = value["nick"].toString();
        auto sex = value["sex"].toInt();
        auto uid = value["uid"].toInt();
        auto status = value["status"].toInt();
        auto info = std::make_shared<ApplyInfo>(uid, name,
                                                certification, icon, nick, sex, status);
        _apply_list.push_back(info);
    }
}



bool UserMgr::CheckFriendById(int uid)
{
    auto iter=_friend_map.find(uid);
    if(iter==_friend_map.end()){
        return false;
    }
    return true;
}



void UserMgr::AddFriend(std::shared_ptr<AuthRsp> auth_rsp)
{
    auto friend_info=std::make_shared<FriendInfo>(auth_rsp);
    _friend_map[friend_info->_uid]=friend_info;
}



void UserMgr::AddFriend(std::shared_ptr<AuthInfo> auth_info)
{
    auto friend_info=std::make_shared<FriendInfo>(auth_info);
    _friend_map[friend_info->_uid]=friend_info;
}



std::shared_ptr<FriendInfo> UserMgr::GetFriendById(int uid)
{
    auto find_it=_friend_map.find(uid);
    if(find_it==_friend_map.end()){
        return nullptr;
    }
    return *find_it;
}



void UserMgr::AppendFriendList(QJsonArray array) {
    // 遍历 QJsonArray 并输出每个元素
    int my_uid=UserMgr::GetUid();
    for (const QJsonValue& value : array) {
        auto name = value["name"].toString();
        auto desc = value["desc"].toString();
        auto icon = value["icon"].toString();
        auto nick = value["nick"].toString();
        auto sex = value["sex"].toInt();
        auto uid = value["uid"].toInt();
        auto back = value["back"].toString();

        auto info = std::make_shared<FriendInfo>(uid, name,
                                                 nick, icon, sex, desc, back);
        qDebug()<<"----- name:"<<name<<"  icon:"<<icon<<"--------"<<Qt::endl;

        // 解析该好友的聊天记录数组 (text + file 混合,按 msg_type 分支)
        QJsonArray messages = value["text_array"].toArray();
        for (const QJsonValue& message : messages) {
            QJsonObject msgObj = message.toObject();
            int sender_id = msgObj["sender_id"].toInt();
            int receiver_id = msgObj["receiver_id"].toInt();
            int msg_type = msgObj.value("msg_type").toInt(MSG_TYPE_TEXT);
            int msg_db_id = msgObj.value("msg_db_id").toInt(0);
            QString contentStr = msgObj["content"].toString();
            QByteArray contentBytes = contentStr.toUtf8();
            QJsonDocument doc = QJsonDocument::fromJson(contentBytes);
            if (doc.isNull()) {
                qDebug() << "chat history: content JSON parse failed, skip";
                continue;
            }

            if (msg_type == MSG_TYPE_TEXT) {
                // text content is stored as a JSON array of {msgid, content}
                if (!doc.isArray()) {
                    qDebug() << "chat history: text content is not array, skip";
                    continue;
                }
                QJsonArray jsonArray = doc.array();
                for (const QJsonValue& v : jsonArray) {
                    if (!v.isObject()) continue;
                    QJsonObject jsonObj = v.toObject();
                    QString content = jsonObj["content"].toString();
                    QString msgid = jsonObj["msgid"].toString();
                    auto td = std::make_shared<TextChatData>(msgid, content, sender_id, receiver_id);
                    td->_msg_db_id = msg_db_id;
                    info->_chat_msgs.push_back(td);
                }
            } else {
                // file content is a single JSON object
                if (!doc.isObject()) {
                    qDebug() << "chat history: file content is not object, skip";
                    continue;
                }
                QJsonObject fileObj = doc.object();
                auto fd = std::make_shared<TextChatData>(
                    fileObj["msgid"].toString(),
                    msg_type,
                    sender_id,
                    receiver_id,
                    fileObj["file_id"].toString(),
                    fileObj["file_name"].toString(),
                    static_cast<qint64>(fileObj["file_size"].toDouble())
                );
                fd->_msg_db_id = msg_db_id;
                fd->_file_host = msgObj.value("file_host").toString();
                fd->_file_port = msgObj.value("file_port").toString();
                fd->_file_token = msgObj.value("file_token").toString();

                // STAGE-B: reuse the per-user on-disk cache if this file was
                // already downloaded in a previous session. Lets ChatPage's
                // Case 1 path render instantly without a re-download.
                QString cached = FileMgr::GetInstance()->GetCachedPath(
                    fd->_file_id, fd->_file_name);
                if (!cached.isEmpty()) {
                    fd->_local_path = cached;
                }

                info->_chat_msgs.push_back(fd);
            }
        }
        _friend_list.push_back(info);
        _friend_map.insert(uid, info);
    }
}

bool UserMgr::IsLoadChatFin() {
    if (_chat_loaded >= _friend_list.size()) {
        return true;
    }

    return false;
}

void UserMgr::UpdateChatLoadedCount() {
    int begin = _chat_loaded;
    int end = begin + CHAT_COUNT_PER_PAGE;

    if (begin >= _friend_list.size()) {
        return ;
    }

    if (end > _friend_list.size()) {
        _chat_loaded = _friend_list.size();
        return ;
    }

    _chat_loaded = end;
}


std::vector<std::shared_ptr<FriendInfo>> UserMgr::GetChatListPerPage() {

    std::vector<std::shared_ptr<FriendInfo>> friend_list;
    int begin = _chat_loaded;
    int end = begin + CHAT_COUNT_PER_PAGE;

    if (begin >= _friend_list.size()) {
        return friend_list;
    }

    if (end > _friend_list.size()) {
        friend_list = std::vector<std::shared_ptr<FriendInfo>>(_friend_list.begin() + begin, _friend_list.end());
        return friend_list;
    }


    friend_list = std::vector<std::shared_ptr<FriendInfo>>(_friend_list.begin() + begin, _friend_list.begin()+ end);
    return friend_list;
}


std::vector<std::shared_ptr<FriendInfo>> UserMgr::GetConListPerPage() {
    std::vector<std::shared_ptr<FriendInfo>> friend_list;
    int begin = _contact_loaded;
    int end = begin + CHAT_COUNT_PER_PAGE;

    if (begin >= _friend_list.size()) {
        return friend_list;
    }

    if (end > _friend_list.size()) {
        friend_list = std::vector<std::shared_ptr<FriendInfo>>(_friend_list.begin() + begin, _friend_list.end());
        return friend_list;
    }


    friend_list = std::vector<std::shared_ptr<FriendInfo>>(_friend_list.begin() + begin, _friend_list.begin() + end);
    return friend_list;
}



void UserMgr::UpdateContactLoadedCount() {
    int begin = _contact_loaded;
    int end = begin + CHAT_COUNT_PER_PAGE;

    if (begin >= _friend_list.size()) {
        return;
    }

    if (end > _friend_list.size()) {
        _contact_loaded = _friend_list.size();
        return;
    }

    _contact_loaded = end;
}

bool UserMgr::IsLoadConFin()
{
    if (_contact_loaded >= _friend_list.size()) {
        return true;
    }

    return false;
}



void UserMgr::AppendFriendChatMsg(int friend_id, std::vector<std::shared_ptr<TextChatData>>msgs)
{
    auto find_iter = _friend_map.find(friend_id);
    if(find_iter == _friend_map.end()){
        qDebug()<<"append friend uid  " << friend_id << " not found";
        return;
    }

    find_iter.value()->AppendChatMsgs(msgs);
}

