#include "ChatServiceImpl.h"
#include "RedisMgr.h"
#include "MysqlMgr.h"
#include "UserMgr.h"

ChatServiceImpl::ChatServiceImpl()
{
}

Status ChatServiceImpl::NotifyAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* reply)
{
	auto touid = request->touid();
	auto session = UserMgr::GetInstance()->GetSession(touid);

	Defer defer([request, reply]() {
		reply->set_error(ErrorCodes::Success);
		reply->set_applyuid(request->applyuid());
		reply->set_touid(request->touid());
		});

	if (session == nullptr) {
		return Status::OK;
	}

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["applyuid"] = request->applyuid();
	rtvalue["name"] = request->name();
	rtvalue["icon"] = request->icon();
	rtvalue["sex"] = request->sex();
	rtvalue["nick"] = request->nick();
	rtvalue["certification"] = request->certification();

	std::string return_str = rtvalue.toStyledString();

	session->Send(return_str, ID_NOTIFY_ADD_FRIEND_REQ);
	return Status::OK;
}

Status ChatServiceImpl::NotifyAuthFriend(ServerContext* context, const AuthFriendReq* request, AuthFriendRsp* reply)
{
	auto touid = request->touid();
	auto fromuid = request->fromuid();
	auto session = UserMgr::GetInstance()->GetSession(touid);

	Defer defer([request, reply]() {
		reply->set_error(ErrorCodes::Success);
		reply->set_fromuid(request->fromuid());
		reply->set_touid(request->touid());
		});

	if (session == nullptr) {
		return Status::OK;
	}

	Json::Value  notify;
	notify["error"] = ErrorCodes::Success;
	notify["fromuid"] = request->fromuid();
	notify["touid"] = request->touid();
	std::shared_ptr<FriendInfo> friend_info = MysqlMgr::GetInstance()->GetFriendBaseInfo(touid, fromuid);
	if (friend_info != nullptr) {
		notify["back"] = friend_info->back;
	}
	else {
		notify["error"] = ErrorCodes::UidInvalid;
	}
	std::shared_ptr<UserInfo>user_info = std::make_shared<UserInfo>();
	std::string base_key = USER_BASE_INFO + std::to_string(fromuid);
	bool b_info = GetBaseInfo(base_key, fromuid, user_info);
	if (b_info) {
		notify["name"] = user_info->name;
		notify["sex"] = user_info->sex;
		notify["icon"] = user_info->icon;
	}
	else {
		notify["error"] = ErrorCodes::UidInvalid;
	}


	std::string return_str = notify.toStyledString();
	session->Send(return_str, ID_NOTIFY_AUTH_FRIEND_REQ);
	return Status::OK;
}

Status ChatServiceImpl::NotifyTextChatMsg(::grpc::ServerContext* context, const TextChatMsgReq* request, TextChatMsgRsp* reply)
{
	std::cout << "------using text grpc -----\n";
	auto touid = request->touid();
	auto session = UserMgr::GetInstance()->GetSession(touid);
	reply->set_error(ErrorCodes::Success);

	if (session == nullptr) {
		return Status::OK;
	}

	// Check if this is a cross-server file message (marked by HandleFileUploadDone)
	if (request->textmsgs_size() == 1 && request->textmsgs(0).msgid() == "__file_msg__") {
		// msgcontent contains the complete file notification JSON,
		// forward it with ID_FILE_MSG_NOTIFY so client handles it as a file
		std::string file_notify = request->textmsgs(0).msgcontent();
		session->Send(file_notify, ID_FILE_MSG_NOTIFY);
		std::cout << "Forwarded file msg via ID_FILE_MSG_NOTIFY to uid=" << touid << std::endl;
		return Status::OK;
	}

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["fromuid"] = request->fromuid();
	rtvalue["touid"] = request->touid();

	Json::Value text_array;
	for (auto& msg : request->textmsgs()) {
		Json::Value element;
		element["content"] = msg.msgcontent();
		element["msgid"] = msg.msgid();
		text_array.append(element);
	}
	rtvalue["text_array"] = text_array;

	std::string return_str = rtvalue.toStyledString();

	session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
	return Status::OK;
}

void ChatServiceImpl::RegisterServer(std::shared_ptr<CServer> pServer) {

}

bool ChatServiceImpl::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo) {
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		userinfo->uid = root["uid"].asInt();
		userinfo->name = root["name"].asString();
		userinfo->pwd = root["pwd"].asString();
		userinfo->email = root["email"].asString();
		userinfo->nick = root["nick"].asString();
		userinfo->desc = root["desc"].asString();
		userinfo->sex = root["sex"].asInt();
		userinfo->icon = root["icon"].asString();
		std::cout << "user login uid is  " << userinfo->uid << " name  is "
			<< userinfo->name << " pwd is " << userinfo->pwd << " email is " << userinfo->email << std::endl;
	}
	else {
		std::shared_ptr<UserInfo> user_info = nullptr;
		user_info = MysqlMgr::GetInstance()->GetUser(uid);
		if (user_info == nullptr) {
			return false;
		}

		userinfo = user_info;

		Json::Value redis_root;
		redis_root["uid"] = uid;
		redis_root["pwd"] = userinfo->pwd;
		redis_root["name"] = userinfo->name;
		redis_root["email"] = userinfo->email;
		redis_root["nick"] = userinfo->nick;
		redis_root["desc"] = userinfo->desc;
		redis_root["sex"] = userinfo->sex;
		redis_root["icon"] = userinfo->icon;
		RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
	}

	return true;
}
