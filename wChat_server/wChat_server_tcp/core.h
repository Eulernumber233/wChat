#pragma once
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid.hpp>

#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>

#include <grpc/grpc.h>
#include <grpcpp/server_builder.h>

#include "hiredis.h"

#include "jdbc/mysql_driver.h"
#include "jdbc/mysql_connection.h"
#include "jdbc/cppconn/prepared_statement.h"
#include "jdbc/cppconn/resultset.h"
#include "jdbc/cppconn/statement.h"
#include "jdbc/cppconn/exception.h"

#include "Singleton.h"
#include <iostream>
#include <csignal>
//#include <assert>
#include <queue>
#include <vector>
#include <stack>
#include <string>
#include <map>
#include <unordered_map>
#include <functional>
#include <thread>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>

#define CODEPREFIX "code_"

#define MAX_LENGTH  1024*2
#define HEAD_TOTAL_LEN 4
#define HEAD_ID_LEN 2
#define HEAD_DATA_LEN 2
#define MAX_RECVQUE  10000
#define MAX_SENDQUE 1000




namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

enum ErrorCodes {
	Success = 0,
	Error_Json = 1001,
	RPCFailed = 1002,
	VarifyExpired = 1003,
	VarifyCodeErr = 1004,
	UserExist = 1005,
	PasswdErr = 1006,
	EmailNotMatch = 1007,
	PasswdUpFailed = 1008,
	PasswdInvalid = 1009,
	TokenInvalid = 1010,   //TokenʧЧ
	UidInvalid = 1011,
};

enum MSG_IDS {
	MSG_CHAT_LOGIN = 1005,
	MSG_CHAT_LOGIN_RSP = 1006,
	ID_SEARCH_USER_REQ = 1007,
	ID_SEARCH_USER_RSP = 1008,
	ID_ADD_FRIEND_REQ = 1009,
	ID_ADD_FRIEND_RSP = 1010,
	ID_NOTIFY_ADD_FRIEND_REQ = 1011,
	ID_AUTH_FRIEND_REQ = 1013,
	ID_AUTH_FRIEND_RSP = 1014,
	ID_NOTIFY_AUTH_FRIEND_REQ = 1015,
	ID_TEXT_CHAT_MSG_REQ = 1017,
	ID_TEXT_CHAT_MSG_RSP = 1018,
	ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1019,
	ID_NOTIFY_OFF_LINE_REQ = 1021,
	ID_HEART_BEAT_REQ = 1023,
	ID_HEARTBEAT_RSP = 1024,

	// STAGE-C: lazy-loading history
	ID_PULL_CONV_SUMMARY_REQ  = 1025,
	ID_PULL_CONV_SUMMARY_RSP  = 1026,
	ID_PULL_MESSAGES_REQ      = 1027,
	ID_PULL_MESSAGES_RSP      = 1028,
	ID_GET_DOWNLOAD_TOKEN_REQ = 1029,
	ID_GET_DOWNLOAD_TOKEN_RSP = 1030,
	ID_NOTIFY_KICK_USER       = 1031, // Server→Client: 你被踢下线了（重复登录等）

	// File coordination
	ID_FILE_UPLOAD_REQ      = 1101,
	ID_FILE_UPLOAD_RSP      = 1102,
	ID_FILE_NOTIFY_COMPLETE = 1103,
	ID_FILE_MSG_NOTIFY      = 1105,
};

// chat_messages.msg_type global convention.
// MUST stay in sync with wChat_client/global.h MsgType and docs/FileServer_Design.md.
enum MsgType {
	MSG_TYPE_TEXT  = 1, // text:  content = {"msgid":"...","content":"..."}
	MSG_TYPE_IMAGE = 2, // image: content = {"msgid":"...","file_id":"...","file_name":"...","file_size":N}
	MSG_TYPE_FILE  = 3, // file:  same shape as image
	MSG_TYPE_AUDIO = 4, // audio: same shape as image
};

class Defer {
public:
	Defer(std::function<void()>func) :func_(func) {
	}
	~Defer() {
		func_();
	}
private:
	std::function<void()>func_;
};




#define USERIPPREFIX  "uip_"
#define USERTOKENPREFIX  "utoken_"
#define IPCOUNTPREFIX  "ipcount_"
#define USER_BASE_INFO "ubaseinfo_"
#define LOGIN_COUNT  "logincount"
#define NAME_INFO  "nameinfo_"
#define LOCK_PREFIX "lock_"
#define USER_SESSION_PREFIX "usession_"
#define LOCK_COUNT "lockcount"
#define FILE_UPLOAD_TOKEN_PREFIX    "file_upload_token:"
#define FILE_DOWNLOAD_TOKEN_PREFIX  "file_download_token:"

// 分布式锁超时参数（秒）
#define LOCK_TIME_OUT     5   // 锁自动过期时间（防死锁）
#define ACQUIRE_TIME_OUT  3   // 获取锁最大等待时间