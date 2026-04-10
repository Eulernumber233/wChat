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
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>

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
#include <fstream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// =====================================================================
// FileServer frame format (different from ChatServer):
//
//   +----------+-----------+-----------+------------------+
//   | msg_id   | body_len  | offset    | payload          |
//   | 2 bytes  | 4 bytes   | 8 bytes   | up to 64 KB      |
//   | network  | network   | network   | JSON or binary   |
//   +----------+-----------+-----------+------------------+
//
// Total header size = 2 + 4 + 8 = 14 bytes
// =====================================================================

#define FSVR_HEAD_ID_LEN      2
#define FSVR_HEAD_BODYLEN_LEN 4
#define FSVR_HEAD_OFFSET_LEN  8
#define FSVR_HEAD_TOTAL_LEN   (FSVR_HEAD_ID_LEN + FSVR_HEAD_BODYLEN_LEN + FSVR_HEAD_OFFSET_LEN)

#define FSVR_MAX_CHUNK_SIZE   (64 * 1024)
#define FSVR_MAX_RECV_BUF     (FSVR_HEAD_TOTAL_LEN + FSVR_MAX_CHUNK_SIZE + 64)

#define MAX_RECVQUE  10000
#define MAX_SENDQUE  1000

enum FSVR_MSG_IDS {
	ID_FSVR_AUTH_REQ  = 2001,
	ID_FSVR_AUTH_RSP  = 2002,
	ID_FSVR_DATA      = 2003,
	ID_FSVR_DONE      = 2004,
};

enum FileStatus {
	FILE_REGISTERED = 0,
	FILE_UPLOADING  = 1,
	FILE_UPLOADED   = 2,
	FILE_FAILED     = 3,
};

enum FileType {
	FTYPE_IMAGE = 0,
	FTYPE_FILE  = 1,
	FTYPE_AUDIO = 2,
};

#define FILE_UPLOAD_TOKEN_PREFIX    "file_upload_token:"
#define FILE_DOWNLOAD_TOKEN_PREFIX  "file_download_token:"
#define FILE_UPLOAD_PROGRESS_PREFIX "file_upload_progress:"

class Defer {
public:
	Defer(std::function<void()> func) : func_(func) {}
	~Defer() { func_(); }
private:
	std::function<void()> func_;
};
