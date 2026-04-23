#include "LogicSystem.h"
#include "StatusGrpcClient.h"
#include "MysqlMgr.h"
#include "core.h"
#include "RedisMgr.h"
#include "UserMgr.h"
#include "ChatGrpcClient.h"
#include <string>
#include "CServer.h"
using namespace std;

LogicSystem::LogicSystem():_b_stop(false), _p_server(nullptr){
	RegisterCallBacks();
	_worker_thread = std::thread (&LogicSystem::DealMsg, this);
}

LogicSystem::~LogicSystem(){
	_b_stop = true;
	_consume.notify_one();
	_worker_thread.join();
}

void LogicSystem::PostMsgToQue(shared_ptr < LogicNode> msg) {
	std::unique_lock<std::mutex> unique_lk(_mutex);
	_msg_que.push(msg);
	if (_msg_que.size() == 1) {
		unique_lk.unlock();
		_consume.notify_one();
	}
}


void LogicSystem::SetServer(std::shared_ptr<CServer> pserver) {
	_p_server = pserver;
}


void LogicSystem::DealMsg() {
	for (;;) {
		std::unique_lock<std::mutex> unique_lk(_mutex);
		while (_msg_que.empty() && !_b_stop) {
			_consume.wait(unique_lk);
		}

		if (_b_stop ) {
			while (!_msg_que.empty()) {
				auto msg_node = _msg_que.front();
				cout << "recv_msg id  is " << msg_node->_recvnode->_msg_id << endl;
				auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
				if (call_back_iter == _fun_callbacks.end()) {
					_msg_que.pop();
					continue;
				}
				call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
					std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
				_msg_que.pop();
			}
			break;
		}

		auto msg_node = _msg_que.front();
		cout << "recv_msg id  is " << msg_node->_recvnode->_msg_id << endl;
		auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
		if (call_back_iter == _fun_callbacks.end()) {
			_msg_que.pop();
			std::cout << "msg id [" << msg_node->_recvnode->_msg_id << "] handler not found" << std::endl;
			continue;
		}
		call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id, 
			std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
		_msg_que.pop();
	}
}

void LogicSystem::RegisterCallBacks() {
	_fun_callbacks[MSG_CHAT_LOGIN] = std::bind(&LogicSystem::LoginHandler, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_SEARCH_USER_REQ] = std::bind(&LogicSystem::SearchInfo, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_ADD_FRIEND_REQ] = std::bind(&LogicSystem::AddFriendApply, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_AUTH_FRIEND_REQ] = std::bind(&LogicSystem::AuthFriendApply, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_TEXT_CHAT_MSG_REQ] = std::bind(&LogicSystem::DealChatTextMsg, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_HEART_BEAT_REQ] = std::bind(&LogicSystem::HeartBeatHandler, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_FILE_UPLOAD_REQ] = std::bind(&LogicSystem::FileUploadReqHandler, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	// STAGE-C: lazy-loading history handlers
	_fun_callbacks[ID_PULL_CONV_SUMMARY_REQ] = std::bind(&LogicSystem::PullConvSummaryHandler, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
	_fun_callbacks[ID_PULL_MESSAGES_REQ] = std::bind(&LogicSystem::PullMessagesHandler, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
	_fun_callbacks[ID_GET_DOWNLOAD_TOKEN_REQ] = std::bind(&LogicSystem::GetDownloadTokenHandler, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
}

bool LogicSystem::ValidateSession(std::shared_ptr<CSession> session) {
	// 已经在关闭流程中的 session 直接拒绝，不再重复踢
	if (session->IsClosing()) return false;

	int uid = session->GetUserId();
	if (uid <= 0) return false;

	std::string uid_str = std::to_string(uid);
	std::string stored_sid;
	bool ok = RedisMgr::GetInstance()->Get(
		USER_SESSION_PREFIX + uid_str, stored_sid);

	if (!ok) {
		// Redis 读取失败（可能是临时不可达），放行，不踢人
		return true;
	}

	if (stored_sid != session->GetSessionId()) {
		// session 已被新登录顶替：发通知并关闭 socket
		// 不调 ClearSession——socket 关闭后 async_read EOF 回调会自动清理 _sessions 和 Redis
		std::cout << "ValidateSession: session invalidated for uid=" << uid << std::endl;
		Json::Value kick;
		kick["error"] = ErrorCodes::Success;
		kick["reason"] = "session_replaced";
		session->SendAndClose(kick.toStyledString(), ID_NOTIFY_KICK_USER);
		int uid_int = session->GetUserId();
		UserMgr::GetInstance()->RmvUserSession(uid_int, session->GetSessionId());
		return false;
	}
	return true;
}

void LogicSystem::LoginHandler(shared_ptr<CSession> session, const short &msg_id, const string &msg_data) {
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	auto uid = root["uid"].asInt();
	auto token = root["token"].asString();
	std::cout << "user login uid is  " << uid << " user token  is "
		<< token << std::endl;

	Json::Value  rtvalue;
	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, MSG_CHAT_LOGIN_RSP);
		});


	std::string uid_str = std::to_string(uid);
	std::string token_key = USERTOKENPREFIX + uid_str;
	std::string token_value = "";
	bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
	if (!success) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return ;
	}

	if (token_value != token) {
		rtvalue["error"] = ErrorCodes::TokenInvalid;
		return ;
	}

	rtvalue["error"] = ErrorCodes::Success;


	std::string base_key = USER_BASE_INFO + uid_str;
	auto user_info = std::make_shared<UserInfo>();
	bool b_base = GetBaseInfo(base_key, uid, user_info);
	if (!b_base) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}
	rtvalue["uid"] = uid;
	rtvalue["pwd"] = user_info->pwd;
	rtvalue["name"] = user_info->name;
	rtvalue["email"] = user_info->email;
	rtvalue["sex"] = user_info->sex;
	rtvalue["icon"] = user_info->icon;

	std::vector<std::shared_ptr<ApplyInfo>> apply_list;
	auto b_apply = GetFriendApplyInfo(uid, apply_list);
	if (b_apply) {
		for (auto& apply : apply_list) {
			Json::Value obj;
			obj["name"] = apply->_name;
			obj["uid"] = apply->_uid;
			obj["icon"] = apply->_icon;
			obj["nick"] = apply->_nick;
			obj["sex"] = apply->_sex;
			obj["certification"] = apply->_certification;
			obj["status"] = apply->_status;
			rtvalue["apply_list"].append(obj);
		}
	}

	// STAGE-C: friend list ships only the base profile. Chat history is
	// loaded lazily by the client via ID_PULL_CONV_SUMMARY_REQ + per-peer
	// ID_PULL_MESSAGES_REQ, and file download tokens are minted on demand
	// via ID_GET_DOWNLOAD_TOKEN_REQ.
	std::vector<std::shared_ptr<UserInfo>> friend_list;
	bool b_friend_list = GetFriendList(uid, friend_list);
	for (auto& friend_ele : friend_list) {
		Json::Value obj;
		obj["name"] = friend_ele->name;
		obj["uid"] = friend_ele->uid;
		obj["icon"] = friend_ele->icon;
		obj["nick"] = friend_ele->nick;
		obj["sex"] = friend_ele->sex;
		obj["back"] = friend_ele->back;
		rtvalue["friend_list"].append(obj);
	}
	auto& cfg = ConfigMgr::Inst();
	auto server_name = cfg.GetValue("SelfServer", "Name");

	// ===== Anti-duplicate-login: distributed lock + kick-before-write =====
	{
		auto lock_key = LOCK_PREFIX + uid_str;
		auto identifier = RedisMgr::GetInstance()->acquireLock(lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
		Defer defer_lock([&identifier, &lock_key]() {
			RedisMgr::GetInstance()->releaseLock(lock_key, identifier);
		});

		if (identifier.empty()) {
			// 获取锁失败（超时）：说明有并发登录正在处理，本次放弃
			// 客户端收到错误码后可以重试登录
			std::cout << "LoginHandler: failed to acquire lock for uid=" << uid << std::endl;
			rtvalue["error"] = ErrorCodes::RPCFailed;
			return;
		}

		// Check if this uid is already logged in somewhere
		std::string old_server;
		auto uid_ip_key = USERIPPREFIX + uid_str;
		bool b_ip = RedisMgr::GetInstance()->Get(uid_ip_key, old_server);

		if (b_ip && !old_server.empty()) {
			if (old_server == server_name) {
				// Same-server kick: 发送踢人通知 + 从 _sessions 中移除旧 session
				auto old_session = UserMgr::GetInstance()->GetSession(uid);
				if (old_session) {
					std::cout << "LoginHandler: kicking old session (same server) uid=" << uid << std::endl;
					Json::Value kick_notify;
					kick_notify["error"] = ErrorCodes::Success;
					kick_notify["reason"] = "duplicate_login";
					std::string old_sid = old_session->GetSessionId();
					old_session->SendAndClose(kick_notify.toStyledString(), ID_NOTIFY_KICK_USER);
					// 只清 UserMgr；_sessions map 的清理由 socket 关闭后的 EOF 回调自动完成
					UserMgr::GetInstance()->RmvUserSession(uid, old_sid);
				}
			}
			else {
				// Cross-server kick via gRPC (3s timeout)
				std::cout << "LoginHandler: kicking old session (cross-server: "
					<< old_server << ") uid=" << uid << std::endl;
				KickUserReq kick_req;
				kick_req.set_uid(uid);
				kick_req.set_reason("duplicate_login");
				auto kick_rsp = ChatGrpcClient::GetInstance()->NotifyKickUser(old_server, kick_req);
				if (kick_rsp.error() != 0) {
					std::cout << "LoginHandler: cross-server kick failed, proceeding anyway uid="
						<< uid << std::endl;
				}
			}
		}

		// Kick done (or attempted); now register the new session
		session->SetUserId(uid);
		session->RefreshHeartbeat();
		std::string ipkey = USERIPPREFIX + uid_str;
		RedisMgr::GetInstance()->Set(ipkey, server_name);
		UserMgr::GetInstance()->SetUserSession(uid, session);
		std::string uid_session_key = USER_SESSION_PREFIX + uid_str;
		RedisMgr::GetInstance()->Set(uid_session_key, session->GetSessionId());
	}

	return;
}

void LogicSystem::SearchInfo(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data)
{
	if (!ValidateSession(session)) return;
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	auto uid_str = root["uid"].asString();
	std::cout << "user SearchInfo uid is  " << uid_str << endl;

	Json::Value  rtvalue;

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_SEARCH_USER_RSP);
		});

	bool b_digit = isPureDigit(uid_str);
	if (b_digit) {
		GetUserByUid(uid_str, rtvalue);
	}
	else {
		GetUserByName(uid_str, rtvalue);
	}
	return;
}

void LogicSystem::AddFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data)
{
	if (!ValidateSession(session)) return;
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	auto uid = root["uid"].asInt();
	auto certification = root["certification"].asString();
	auto bakname = root["bakname"].asString();
	auto touid = root["touid"].asInt();
	std::cout << "user login uid is  " << uid << " applyname  is "
		<< certification << " bakname is " << bakname << " touid is " << touid << endl;

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_ADD_FRIEND_RSP);
		});

	MysqlMgr::GetInstance()->AddFriendApply(uid, touid, bakname, certification);

	auto to_str = std::to_string(touid);
	auto to_ip_key = USERIPPREFIX + to_str;
	std::string to_ip_value = "";
	bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
	if (!b_ip) {
		return;
	}


	auto& cfg = ConfigMgr::Inst();
	auto self_name = cfg["SelfServer"]["Name"];


	std::string base_key = USER_BASE_INFO + std::to_string(uid);
	auto apply_info = std::make_shared<UserInfo>();
	bool b_info = GetBaseInfo(base_key, uid, apply_info);

	if (to_ip_value == self_name) {
		auto session = UserMgr::GetInstance()->GetSession(touid);
		if (session) {
			Json::Value  notify;
			notify["error"] = ErrorCodes::Success;
			notify["applyuid"] = uid;
			notify["certification"] = certification;
			if (b_info) {
				notify["name"] = apply_info->name;
				notify["icon"] = apply_info->icon;
				notify["sex"] = apply_info->sex;
				notify["nick"] = apply_info->nick;
			}
			std::string return_str = notify.toStyledString();
			session->Send(return_str, ID_NOTIFY_ADD_FRIEND_REQ);
		}
		return ;
	}

	
	AddFriendReq add_req;
	add_req.set_applyuid(uid);
	add_req.set_touid(touid);
	add_req.set_certification(certification);
	if (b_info) {
		add_req.set_name(apply_info->name);
		add_req.set_icon(apply_info->icon);
		add_req.set_sex(apply_info->sex);
		add_req.set_nick(apply_info->nick);
	}

	ChatGrpcClient::GetInstance()->NotifyAddFriend(to_ip_value,add_req);
}

void LogicSystem::AuthFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data) {
	if (!ValidateSession(session)) return;

	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	auto uid = root["fromuid"].asInt();
	auto touid = root["touid"].asInt();
	auto back_name = root["back"].asString();
	std::cout << "from " << uid << " auth friend to " << touid << std::endl;

	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	auto user_info = std::make_shared<UserInfo>();

	std::string base_key = USER_BASE_INFO + std::to_string(touid);
	bool b_info = GetBaseInfo(base_key, touid, user_info);
	if (b_info) {
		rtvalue["name"] = user_info->name;
		rtvalue["icon"] = user_info->icon;
		rtvalue["back"] = back_name;
		rtvalue["sex"] = user_info->sex;
		rtvalue["uid"] = touid;
	}
	else {
		rtvalue["error"] = ErrorCodes::UidInvalid;
	}


	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_AUTH_FRIEND_RSP);
		});

	MysqlMgr::GetInstance()->AuthFriendApply(uid, touid);

	MysqlMgr::GetInstance()->AddFriend(uid, touid,back_name);


	auto to_str = std::to_string(touid);
	auto to_ip_key = USERIPPREFIX + to_str;
	std::string to_ip_value = "";
	bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
	if (!b_ip) {
		return;
	}

	auto& cfg = ConfigMgr::Inst();
	auto self_name = cfg["SelfServer"]["Name"];
	if (to_ip_value == self_name) {
		auto session = UserMgr::GetInstance()->GetSession(touid);
		if (session) {
			Json::Value  notify;
			notify["error"] = ErrorCodes::Success;
			notify["fromuid"] = uid;
			notify["touid"] = touid;
			std::shared_ptr<FriendInfo> friend_info = MysqlMgr::GetInstance()->GetFriendBaseInfo(touid, uid);
			if (friend_info != nullptr) {
				notify["back"] = friend_info->back;
			}
			else {
				notify["error"] = ErrorCodes::UidInvalid;
			}
			std::string base_key = USER_BASE_INFO + std::to_string(uid);
			b_info = GetBaseInfo(base_key, uid, user_info);
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
		}

		return ;
	}


	AuthFriendReq auth_req;
	auth_req.set_fromuid(uid);
	auth_req.set_touid(touid);

	ChatGrpcClient::GetInstance()->NotifyAuthFriend(to_ip_value, auth_req);
}

void LogicSystem::DealChatTextMsg(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data) {
	if (!ValidateSession(session)) return;
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	auto uid = root["fromuid"].asInt();
	auto touid = root["touid"].asInt();

	const Json::Value  arrays = root["text_array"];
	
	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["text_array"] = arrays;
	rtvalue["fromuid"] = uid;
	rtvalue["touid"] = touid;

	Defer defer([this, &rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_TEXT_CHAT_MSG_RSP);
		});

	// Persist and capture the inserted row id so the client can store it
	// in its local SQLite mirror (STAGE-C). Cross-server forwarding still
	// goes through the existing proto which has no msg_db_id field, so the
	// receiver on a different ChatServer will get 0 and fall back to the
	// next ID_PULL_MESSAGES_REQ to pick up the real id.
	int inserted_id = MysqlMgr::GetInstance()->AddMessage(uid, touid, arrays.toStyledString());
	rtvalue["msg_db_id"] = inserted_id;

	auto to_str = std::to_string(touid);
	auto to_ip_key = USERIPPREFIX + to_str;
	std::string to_ip_value = "";
	bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
	if (!b_ip) {
		return;
	}

	auto& cfg = ConfigMgr::Inst();
	auto self_name = cfg["SelfServer"]["Name"];
	if (to_ip_value == self_name) {
		auto session = UserMgr::GetInstance()->GetSession(touid);
		if (session) {
			std::string return_str = rtvalue.toStyledString();
			session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
		}

		return ;
	}


	TextChatMsgReq text_msg_req;
	text_msg_req.set_fromuid(uid);
	text_msg_req.set_touid(touid);
	for (const auto& txt_obj : arrays) {
		auto content = txt_obj["content"].asString();
		auto msgid = txt_obj["msgid"].asString();
		std::cout << "content is " << content << std::endl;
		std::cout << "msgid is " << msgid << std::endl;
		auto *text_msg = text_msg_req.add_textmsgs();
		text_msg->set_msgid(msgid);
		text_msg->set_msgcontent(content);
	}

	ChatGrpcClient::GetInstance()->NotifyTextChatMsg(to_ip_value, text_msg_req, rtvalue);
}

void LogicSystem::HeartBeatHandler(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data) {
	// 已被踢的 session 不应刷新心跳（否则僵尸 session 永远不会超时）
	if (!ValidateSession(session)) return;
	// 刷新心跳时间戳
	session->RefreshHeartbeat();
	Json::Value  rtvalue;
	rtvalue["error"] = ErrorCodes::Success;
	session->Send(rtvalue.toStyledString(), ID_HEARTBEAT_RSP);
}

// =====================================================================
// ID_FILE_UPLOAD_REQ handler
// Client sends: {fromuid, touid, file_name, file_size, file_type, md5}
// ChatServer responds: {error, file_id, file_token, host, port}
// =====================================================================
void LogicSystem::FileUploadReqHandler(std::shared_ptr<CSession> session,
	const short& msg_id, const string& msg_data) {
	if (!ValidateSession(session)) return;
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);

	int fromuid = root["fromuid"].asInt();
	int touid = root["touid"].asInt();
	std::string file_name = root["file_name"].asString();
	int64_t file_size = static_cast<int64_t>(root["file_size"].asDouble());
	int file_type = root["file_type"].asInt();
	std::string md5 = root.get("md5", "").asString();

	// Derive mime_type from file_type (simplified)
	std::string mime_type = "application/octet-stream";
	if (file_type == 0) { // image
		auto ext_pos = file_name.rfind('.');
		if (ext_pos != string::npos) {
			auto ext = file_name.substr(ext_pos + 1);
			if (ext == "jpg" || ext == "jpeg") mime_type = "image/jpeg";
			else if (ext == "png") mime_type = "image/png";
			else if (ext == "gif") mime_type = "image/gif";
			else mime_type = "image/" + ext;
		}
	}

	Json::Value rsp;
	rsp["error"] = 0;

	// 1. Generate file_id
	auto uuid = boost::uuids::random_generator()();
	std::string file_id = boost::uuids::to_string(uuid);

	// 2. Generate file_token (one-time upload authorization)
	auto token_uuid = boost::uuids::random_generator()();
	std::string file_token = boost::uuids::to_string(token_uuid);

	// 3. Register in chat_files (status=0: registered)
	bool b_reg = MysqlMgr::GetInstance()->RegisterFile(file_id, fromuid,
		file_name, file_size, file_type, mime_type);
	if (!b_reg) {
		rsp["error"] = 1;
		rsp["msg"] = "db register failed";
		session->Send(rsp.toStyledString(), ID_FILE_UPLOAD_RSP);
		return;
	}

	// 4. Write upload token to Redis with file metadata
	Json::Value token_data;
	token_data["file_id"] = file_id;
	token_data["uid"] = fromuid;
	token_data["touid"] = touid;
	token_data["file_size"] = root["file_size"]; // preserve original
	token_data["file_name"] = file_name;
	std::string token_key = std::string(FILE_UPLOAD_TOKEN_PREFIX) + file_token;
	RedisMgr::GetInstance()->Set(token_key, token_data.toStyledString());
	RedisMgr::GetInstance()->Expire(token_key, 600); // 10 min TTL

	// 4b. Store fromuid/touid metadata for HandleFileUploadDone to retrieve later
	Json::Value file_meta;
	file_meta["fromuid"] = fromuid;
	file_meta["touid"] = touid;
	file_meta["file_type"] = file_type;
	std::string meta_key = "file_meta:" + file_id;
	RedisMgr::GetInstance()->Set(meta_key, file_meta.toStyledString());
	RedisMgr::GetInstance()->Expire(meta_key, 3600); // 1 hour, cleaned up after upload done

	// 5. Read FileServer address from config
	auto& cfg = ConfigMgr::Inst();
	std::string fs_host = cfg["FileServer"]["Host"];
	std::string fs_port = cfg["FileServer"]["Port"];

	// 6. Return to client
	rsp["file_id"] = file_id;
	rsp["file_token"] = file_token;
	rsp["host"] = fs_host;
	rsp["port"] = fs_port;

	std::cout << "FileUploadReq: uid=" << fromuid << " file=" << file_name
		<< " size=" << file_size << " -> file_id=" << file_id << std::endl;

	session->Send(rsp.toStyledString(), ID_FILE_UPLOAD_RSP);
}

// =====================================================================
// Called by FileServiceImpl when FileServer reports upload done via gRPC.
// This is the point where the chat message is created and the receiver
// is notified — ONLY after the file is fully stored on disk.
// =====================================================================
void LogicSystem::HandleFileUploadDone(const std::string& file_id,
	const std::string& file_path, const std::string& md5,
	int rpc_fromuid) {

	std::cout << "HandleFileUploadDone: file_id=" << file_id
		<< " path=" << file_path
		<< " rpc_fromuid=" << rpc_fromuid << std::endl;

	// FileServer has already updated chat_files to status=2 via UpdateFileComplete
	// before calling this gRPC. Do NOT update here to avoid MySQL lock contention.

	// 1. Get file info from DB to build the chat message
	std::string db_path, file_name;
	int64_t file_size = 0;
	if (!MysqlMgr::GetInstance()->GetFileInfo(file_id, db_path, file_name, file_size)) {
		std::cerr << "HandleFileUploadDone: file not found in DB: " << file_id << std::endl;
		return;
	}

	// We need fromuid and touid. These were stored in the upload token,
	// but that token is already deleted. We need to retrieve them from chat_files.
	// For now, we'll query chat_files for uploader_uid + look up touid from a
	// supplementary Redis key we set during FileUploadReqHandler.
	// Let's use a simple approach: store fromuid/touid in Redis with file_id as key.
	std::string meta_key = "file_meta:" + file_id;
	std::string meta_str;
	if (!RedisMgr::GetInstance()->Get(meta_key, meta_str)) {
		std::cerr << "HandleFileUploadDone: file_meta not found for " << file_id << std::endl;
		return;
	}
	Json::Reader reader;
	Json::Value meta;
	reader.parse(meta_str, meta);
	int fromuid = meta["fromuid"].asInt();
	int touid = meta["touid"].asInt();
	int file_type = meta["file_type"].asInt();
	RedisMgr::GetInstance()->Del(meta_key); // clean up

	// Defence-in-depth: if the gRPC caller supplied a fromuid, it must match
	// what we stored in file_meta. A mismatch would mean the file_id space
	// got crossed (e.g. a stale/forged notify), so we bail out rather than
	// persist a chat message under the wrong sender.
	if (rpc_fromuid != 0 && rpc_fromuid != fromuid) {
		std::cerr << "HandleFileUploadDone: fromuid mismatch for " << file_id
			<< " (rpc=" << rpc_fromuid << " meta=" << fromuid << "), abort"
			<< std::endl;
		return;
	}

	// 3. Build message content JSON
	auto msg_uuid = boost::uuids::random_generator()();
	Json::Value content;
	content["msgid"] = boost::uuids::to_string(msg_uuid);
	content["file_id"] = file_id;
	content["file_name"] = file_name;
	content["file_size"] = static_cast<double>(file_size);
	content["file_type"] = file_type;

	// msg_type: map file_type(0/1/2) -> MsgType(IMAGE/FILE/AUDIO) in core.h
	int msg_type = MSG_TYPE_IMAGE + file_type;

	// 4. Write chat_messages
	int inserted_msg_db_id = MysqlMgr::GetInstance()->AddFileMessage(
		fromuid, touid, msg_type, content.toStyledString());

	// 5. Notify sender: upload complete (carry msg_db_id for LocalDb mirror)
	auto sender_session = UserMgr::GetInstance()->GetSession(fromuid);
	if (sender_session) {
		Json::Value notify_sender;
		notify_sender["error"] = 0;
		notify_sender["file_id"] = file_id;
		notify_sender["msg_db_id"] = inserted_msg_db_id;
		notify_sender["msg_type"] = msg_type;
		notify_sender["file_name"] = file_name;
		notify_sender["file_size"] = static_cast<double>(file_size);
		notify_sender["file_type"] = file_type;
		notify_sender["msgid"] = content["msgid"];
		notify_sender["fromuid"] = fromuid;
		notify_sender["touid"] = touid;
		sender_session->Send(notify_sender.toStyledString(), ID_FILE_NOTIFY_COMPLETE);
	}

	// 6. Notify receiver (same logic as text message routing)
	auto to_str = std::to_string(touid);
	auto to_ip_key = std::string(USERIPPREFIX) + to_str;
	std::string to_ip_value;
	bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
	if (!b_ip) {
		// Receiver offline, will get the file message on next login
		std::cout << "Receiver " << touid << " offline, file msg saved to DB" << std::endl;
		return;
	}

	// Generate download token for receiver
	auto dl_token_uuid = boost::uuids::random_generator()();
	std::string dl_token = boost::uuids::to_string(dl_token_uuid);
	Json::Value dl_token_data;
	dl_token_data["file_id"] = file_id;
	dl_token_data["uid"] = touid;
	std::string dl_key = std::string(FILE_DOWNLOAD_TOKEN_PREFIX) + dl_token;
	RedisMgr::GetInstance()->Set(dl_key, dl_token_data.toStyledString());
	RedisMgr::GetInstance()->Expire(dl_key, 600);

	// Build notification for receiver
	auto& cfg = ConfigMgr::Inst();
	Json::Value notify;
	notify["error"] = 0;
	notify["fromuid"] = fromuid;
	notify["touid"] = touid;
	notify["msg_type"] = msg_type;
	notify["msg_db_id"] = inserted_msg_db_id;
	notify["msgid"] = content["msgid"];
	notify["file_id"] = file_id;
	notify["file_name"] = file_name;
	notify["file_size"] = static_cast<double>(file_size);
	notify["file_type"] = file_type;
	notify["file_host"] = cfg["FileServer"]["Host"];
	notify["file_port"] = cfg["FileServer"]["Port"];
	notify["file_token"] = dl_token;

	auto self_name = cfg["SelfServer"]["Name"];
	if (to_ip_value == self_name) {
		// Same server
		auto recv_session = UserMgr::GetInstance()->GetSession(touid);
		if (recv_session) {
			recv_session->Send(notify.toStyledString(), ID_FILE_MSG_NOTIFY);
		}
	}
	else {
		// Cross-server: reuse existing TextChatMsg gRPC for simplicity
		// Pack file info into TextChatMsgReq
		TextChatMsgReq req;
		req.set_fromuid(fromuid);
		req.set_touid(touid);
		auto* text_msg = req.add_textmsgs();
		text_msg->set_msgid("__file_msg__"); // special marker
		text_msg->set_msgcontent(notify.toStyledString());
		ChatGrpcClient::GetInstance()->NotifyTextChatMsg(to_ip_value, req, notify);
	}
}

bool LogicSystem::isPureDigit(const std::string& str)
{
	if (str.empty()) return false;
	for (unsigned char c : str) {
		if (c < 48 || c > 57) {
			return false;
		}
	}
	return true;
}

void LogicSystem::GetUserByUid(std::string uid_str, Json::Value& rtvalue)
{
	rtvalue["error"] = ErrorCodes::Success;

	std::string base_key = USER_BASE_INFO + uid_str;

	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		auto uid = root["uid"].asInt();
		auto name = root["name"].asString();
		auto nick = root["nick"].asString();
		auto desc = root["desc"].asString();
		auto sex = root["sex"].asInt();
		auto icon = root["icon"].asString();
		std::cout << "user  uid is  " << uid << " name  is "
			<< name << " icon is " << icon << std::endl;

		rtvalue["uid"] = uid;
		rtvalue["name"] = name;
		rtvalue["nick"] = nick;
		rtvalue["desc"] = desc;
		rtvalue["sex"] = sex;
		rtvalue["icon"] = icon;
		return;
	}

	auto uid = std::stoi(uid_str);
	std::shared_ptr<UserInfo> user_info = nullptr;
	user_info = MysqlMgr::GetInstance()->GetUser(uid);
	if (user_info == nullptr) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	Json::Value redis_root;
	redis_root["uid"] = user_info->uid;
	redis_root["pwd"] = user_info->pwd;
	redis_root["name"] = user_info->name;
	redis_root["email"] = user_info->email;
	redis_root["nick"] = user_info->nick;
	redis_root["desc"] = user_info->desc;
	redis_root["sex"] = user_info->sex;
	redis_root["icon"] = user_info->icon;

	RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());

	rtvalue["uid"] = user_info->uid;
	rtvalue["name"] = user_info->name;
	rtvalue["nick"] = user_info->nick;
	rtvalue["desc"] = user_info->desc;
	rtvalue["sex"] = user_info->sex;
	rtvalue["icon"] = user_info->icon;
}

void LogicSystem::GetUserByName(std::string name, Json::Value& rtvalue)
{
	rtvalue["error"] = ErrorCodes::Success;

	std::string base_key = NAME_INFO + name;

	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		auto uid = root["uid"].asInt();
		auto name = root["name"].asString();
		auto nick = root["nick"].asString();
		auto desc = root["desc"].asString();
		auto sex = root["sex"].asInt();
		auto icon = root["icon"].asString();
		std::cout << "user  uid is  " << uid << " name  is "
			<< name << " icon is " << icon << std::endl;

		rtvalue["uid"] = uid;
		rtvalue["name"] = name;
		rtvalue["nick"] = nick;
		rtvalue["desc"] = desc;
		rtvalue["sex"] = sex;
		rtvalue["icon"] = icon;
		return;
	}

	std::shared_ptr<UserInfo> user_info = nullptr;
	user_info = MysqlMgr::GetInstance()->GetUser(name);
	if (user_info == nullptr) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	Json::Value redis_root;
	redis_root["uid"] = user_info->uid;
	redis_root["pwd"] = user_info->pwd;
	redis_root["name"] = user_info->name;
	redis_root["email"] = user_info->email;
	redis_root["nick"] = user_info->nick;
	redis_root["desc"] = user_info->desc;
	redis_root["sex"] = user_info->sex;
	redis_root["icon"] = user_info->icon;

	RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
	
	rtvalue["uid"] = user_info->uid;
	rtvalue["name"] = user_info->name;
	rtvalue["nick"] = user_info->nick;
	rtvalue["desc"] = user_info->desc;
	rtvalue["sex"] = user_info->sex;
	rtvalue["icon"] = user_info->icon;
}

bool LogicSystem::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
{
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
		std::cout <<  " icon is " << userinfo->icon << std::endl;
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


bool LogicSystem::GetFriendApplyInfo(int to_uid, std::vector<std::shared_ptr<ApplyInfo>> &list) {
	return MysqlMgr::GetInstance()->GetApplyList(to_uid, list, 0, 10);
}

bool LogicSystem::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>>& user_list) {
	return MysqlMgr::GetInstance()->GetFriendList(self_id, user_list);
}

// =====================================================================
// STAGE-C: lazy-loading history handlers
// =====================================================================

// 拉会话摘要
// Client expects: { "uid": N }
// Server replies: { "error": 0, "summaries": [ {peer_uid, last_msg_db_id,
//                    last_msg_type, last_msg_preview, last_msg_time,
//                    unread_count}, ... ] }
void LogicSystem::PullConvSummaryHandler(std::shared_ptr<CSession> session,
	const short& msg_id, const std::string& msg_data) {
	if (!ValidateSession(session)) return;
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	int self_uid = root["uid"].asInt();

	Json::Value rtvalue;
	Defer defer([&rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_PULL_CONV_SUMMARY_RSP);
	});

	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["summaries"] = Json::Value(Json::arrayValue);
	if (!MysqlMgr::GetInstance()->GetConvSummaries(self_uid, rtvalue["summaries"])) {
		rtvalue["error"] = ErrorCodes::Error_Json;
	}
}

// Client expects: { "uid": N, "peer_uid": M, "before_msg_db_id": K, "limit": L }
// Server replies: { "error": 0, "peer_uid": M, "messages": [...], "has_more": bool }
// Messages are returned in id-descending order (newest first within the page);
// the client is expected to reverse or use id-sorted render order.
void LogicSystem::PullMessagesHandler(std::shared_ptr<CSession> session,
	const short& msg_id, const std::string& msg_data) {
	if (!ValidateSession(session)) return;
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	int self_uid = root["uid"].asInt();
	int peer_uid = root["peer_uid"].asInt();
	int64_t before = static_cast<int64_t>(root.get("before_msg_db_id", 0).asDouble());
	int limit = root.get("limit", 30).asInt();
	if (limit <= 0 || limit > 200) limit = 30;

	Json::Value rtvalue;
	Defer defer([&rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_PULL_MESSAGES_RSP);
	});

	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["peer_uid"] = peer_uid;
	rtvalue["messages"] = Json::Value(Json::arrayValue);
	bool has_more = false;
	if (!MysqlMgr::GetInstance()->GetMessagesPage(self_uid, peer_uid, before,
			limit, rtvalue["messages"], has_more)) {
		rtvalue["error"] = ErrorCodes::Error_Json;
	}
	rtvalue["has_more"] = has_more;
}

// Client expects: { "uid": N, "file_id": "..." }
// Server replies: { "error": 0, "file_id": "...", "file_host": "...",
//                   "file_port": "...", "file_token": "..." }
void LogicSystem::GetDownloadTokenHandler(std::shared_ptr<CSession> session,
	const short& msg_id, const std::string& msg_data) {
	if (!ValidateSession(session)) return;
	Json::Reader reader;
	Json::Value root;
	reader.parse(msg_data, root);
	int uid = root["uid"].asInt();
	std::string file_id = root["file_id"].asString();

	Json::Value rtvalue;
	Defer defer([&rtvalue, session]() {
		std::string return_str = rtvalue.toStyledString();
		session->Send(return_str, ID_GET_DOWNLOAD_TOKEN_RSP);
	});

	rtvalue["error"] = ErrorCodes::Success;
	rtvalue["file_id"] = file_id;

	if (file_id.empty()) {
		rtvalue["error"] = ErrorCodes::Error_Json;
		return;
	}

	// Verify the user actually participated in a chat involving this file.
	// This is the only authorisation check between client and FileServer.
	if (!MysqlMgr::GetInstance()->UserCanAccessFile(uid, file_id)) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	// Mint a fresh one-time token. 600s is enough — client requests this
	// right before calling FileMgr::StartDownload.
	auto uuid = boost::uuids::random_generator()();
	std::string dl_token = boost::uuids::to_string(uuid);
	Json::Value tok;
	tok["file_id"] = file_id;
	tok["uid"] = uid;
	std::string dl_key = std::string(FILE_DOWNLOAD_TOKEN_PREFIX) + dl_token;
	RedisMgr::GetInstance()->Set(dl_key, tok.toStyledString());
	RedisMgr::GetInstance()->Expire(dl_key, 600);

	auto& cfg = ConfigMgr::Inst();
	rtvalue["file_host"] = std::string(cfg["FileServer"]["Host"]);
	rtvalue["file_port"] = std::string(cfg["FileServer"]["Port"]);
	rtvalue["file_token"] = dl_token;
}
