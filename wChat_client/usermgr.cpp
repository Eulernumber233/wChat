#include "usermgr.h"
#include "filemgr.h"
UserMgr::~UserMgr()
{
}

void UserMgr::SetToken(QString token)
{
    _token = token;
}

void UserMgr::SetAgentEndpoint(const QString& host, int port)
{
    _agent_host = host;
    _agent_port = port;
}
UserMgr::UserMgr()
    :_user_info(nullptr),_chat_loaded(0),_contact_loaded(0)
{
}

int UserMgr::GetUid()
{
    if (!_user_info) return 0;
    return _user_info->_uid;
}

QString UserMgr::GetName()
{
    if (!_user_info) return "";
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



void UserMgr::ApplyConvSummaries(const QJsonArray& summaries) {
    for (const QJsonValue& v : summaries) {
        QJsonObject o = v.toObject();
        int peer_uid = o["peer_uid"].toInt();
        auto it = _friend_map.find(peer_uid);
        if (it == _friend_map.end()) {
            qDebug() << "ApplyConvSummaries: unknown peer_uid=" << peer_uid;
            continue;
        }
        auto& fi = it.value();
        fi->_last_msg_db_id = static_cast<qint64>(o["last_msg_db_id"].toDouble());
        fi->_last_msg_type  = o["last_msg_type"].toInt();
        fi->_last_msg       = o["last_msg_preview"].toString();
        fi->_last_msg_time  = static_cast<qint64>(o["last_msg_time"].toDouble());
        fi->_unread_count   = o["unread_count"].toInt();
    }
}

void UserMgr::AppendFriendList(QJsonArray array) {
    // STAGE-C: friend entries from login rsp contain only the base profile.
    // Chat history is loaded lazily via ID_PULL_CONV_SUMMARY then
    // ID_PULL_MESSAGES when the user opens a conversation.
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

void UserMgr::Reset()
{
    _token.clear();
    _agent_host.clear();
    _agent_port = 0;
    _user_info = nullptr;
    _apply_list.clear();
    _friend_map.clear();
    _friend_list.clear();
    _chat_loaded = 0;
    _contact_loaded = 0;
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

