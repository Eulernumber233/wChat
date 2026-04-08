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

#include "hiredis.h"

#include "jdbc/mysql_driver.h"
#include "jdbc/mysql_connection.h"
#include "jdbc/cppconn/prepared_statement.h"
#include "jdbc/cppconn/resultset.h"
#include "jdbc/cppconn/statement.h"
#include "jdbc/cppconn/exception.h"

#include "Singleton.h"
#include <iostream>
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

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

enum ErrorCodes {
	Success = 0,
	Error_Json = 1001,
	RPCFailed = 1002,      //RPC请求错误
	VarifyExpired = 1003,  //验证码过期
	VarifyCodeErr = 1004,  //验证码错误
	UserExist = 1005,      //用户已存在 
	PasswdErr = 1006,      //密码错误
	EmailNotMatch = 1007,  //邮箱不匹配
	PasswdUpFailed = 1008, //更新密码失败
	PasswdInvalid = 1009,   //密码更新失败
	TokenInvalid = 1010,   //Token失效
	UidInvalid = 1011,  //uid无效
};

// 确保不确定何时析构的变量析构
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
#define LOCK_COUNT "lockcount"


const std::string head_photo_head = ":/asserts/head_";
const std::string head_photo_tail = ".jpg";