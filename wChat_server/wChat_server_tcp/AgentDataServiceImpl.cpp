#include "AgentDataServiceImpl.h"
#include "RedisMgr.h"
#include "MysqlMgr.h"
#include <json/json.h>
#include <iostream>

Status AgentDataServiceImpl::GetChatHistory(ServerContext* /*context*/,
    const GetChatHistoryReq* request, GetChatHistoryRsp* response) {

    const int self_uid = request->self_uid();
    const int peer_uid = request->peer_uid();
    int       limit    = request->limit();
    const int64_t before = request->before_msg_db_id();
    const std::string token = request->auth_token();

    if (self_uid <= 0 || peer_uid <= 0) {
        response->set_error(ErrorCodes::Error_Json);
        return Status::OK;
    }
    if (limit <= 0 || limit > 200) {
        limit = 30;
    }

    // ---- auth ----
    if (!VerifyToken(self_uid, token)) {
        response->set_error(ErrorCodes::TokenInvalid);
        return Status::OK;
    }

    // ---- fetch page via the existing, battle-tested SQL ----
    Json::Value messages_json(Json::arrayValue);
    bool has_more = false;
    if (!MysqlMgr::GetInstance()->GetMessagesPage(self_uid, peer_uid, before,
            limit, messages_json, has_more)) {
        response->set_error(ErrorCodes::Error_Json);
        return Status::OK;
    }

    // ---- map JSON rows -> proto rows ----
    // Rows from GetMessagesPage carry sender_id / receiver_id. The proto
    // exposes from_uid / to_uid + a `direction` int derived against
    // self_uid. We also convert msg_db_id to int64: jsoncpp limits us on
    // write (asDouble only), but the underlying SQL column is BIGINT so
    // the proto field (which is int64) is the source of truth on the
    // wire; Python side re-exports it as string for JSON safety.
    for (const auto& m : messages_json) {
        auto* row = response->add_messages();
        row->set_msg_db_id(static_cast<int64_t>(m.get("msg_db_id", 0).asDouble()));
        int from = m.get("sender_id", 0).asInt();
        int to   = m.get("receiver_id", 0).asInt();
        row->set_from_uid(from);
        row->set_to_uid(to);
        row->set_msg_type(m.get("msg_type", 1).asInt());
        row->set_content(m.get("content", "").asString());
        row->set_send_time(static_cast<int64_t>(m.get("send_time", 0).asDouble()));
        row->set_direction(from == self_uid ? 1 : 0);
    }
    response->set_has_more(has_more);
    response->set_error(ErrorCodes::Success);
    return Status::OK;
}

Status AgentDataServiceImpl::GetFriendProfile(ServerContext* /*context*/,
    const GetFriendProfileReq* request, GetFriendProfileRsp* response) {

    const int self_uid = request->self_uid();
    const int peer_uid = request->peer_uid();
    const std::string token = request->auth_token();

    if (self_uid <= 0 || peer_uid <= 0) {
        response->set_error(ErrorCodes::Error_Json);
        return Status::OK;
    }

    if (!VerifyToken(self_uid, token)) {
        response->set_error(ErrorCodes::TokenInvalid);
        return Status::OK;
    }

    //  不校验好友关系
    // We don't enforce friendship here on purpose: the Agent is a
    // read-only helper and the caller already proved they know peer_uid
    // (they have it in chat history). If this turns out to leak too
    // much, add a `GetFriendBaseInfo(self_uid, peer_uid)` check here.
    std::shared_ptr<UserInfo> info; 
    if (!LoadBaseInfo(peer_uid, info) || info == nullptr) {
        response->set_error(ErrorCodes::UidInvalid);
        return Status::OK;
    }

    response->set_error(ErrorCodes::Success);
    response->set_uid(info->uid);
    response->set_name(info->name);
    response->set_nick(info->nick);
    response->set_sex(info->sex);
    response->set_desc(info->desc);
    response->set_icon(info->icon);
    return Status::OK;
}

bool AgentDataServiceImpl::VerifyToken(int self_uid, const std::string& token) {
    if (token.empty()) return false;
    std::string key = std::string(USERTOKENPREFIX) + std::to_string(self_uid);
    std::string stored;
    if (!RedisMgr::GetInstance()->Get(key, stored)) {
        // no such token — this also covers "redis down": fail closed
        return false;
    }
    return stored == token;
}

bool AgentDataServiceImpl::LoadBaseInfo(int uid, std::shared_ptr<UserInfo>& userinfo) {
    const std::string base_key = std::string(USER_BASE_INFO) + std::to_string(uid);
    std::string info_str;
    if (RedisMgr::GetInstance()->Get(base_key, info_str)) {
        Json::Reader reader;
        Json::Value root;
        if (!reader.parse(info_str, root)) return false;
        userinfo = std::make_shared<UserInfo>();
        userinfo->uid   = root["uid"].asInt();
        userinfo->name  = root["name"].asString();
        userinfo->pwd   = root["pwd"].asString();
        userinfo->email = root["email"].asString();
        userinfo->nick  = root["nick"].asString();
        userinfo->desc  = root["desc"].asString();
        userinfo->sex   = root["sex"].asInt();
        userinfo->icon  = root["icon"].asString();
        return true;
    }

    auto from_db = MysqlMgr::GetInstance()->GetUser(uid);
    if (from_db == nullptr) return false;
    userinfo = from_db;

    Json::Value redis_root;
    redis_root["uid"]   = uid;
    redis_root["pwd"]   = userinfo->pwd;
    redis_root["name"]  = userinfo->name;
    redis_root["email"] = userinfo->email;
    redis_root["nick"]  = userinfo->nick;
    redis_root["desc"]  = userinfo->desc;
    redis_root["sex"]   = userinfo->sex;
    redis_root["icon"]  = userinfo->icon;
    RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
    return true;
}
