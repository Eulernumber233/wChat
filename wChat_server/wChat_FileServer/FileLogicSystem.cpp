#include "FileLogicSystem.h"
#include "FileSession.h"
#include "FileStorage.h"
#include "RedisMgr.h"
#include "MysqlMgr.h"
#include "ConfigMgr.h"
#include "ChatServerClient.h"

FileLogicSystem::FileLogicSystem() : _b_stop(false) {
	RegisterCallBacks();
	_worker_thread = std::thread(&FileLogicSystem::DealMsg, this);
}

FileLogicSystem::~FileLogicSystem() {
	_b_stop = true;
	_consume.notify_one();
	if (_worker_thread.joinable()) {
		_worker_thread.join();
	}
}

void FileLogicSystem::PostMsg(std::shared_ptr<FileSession> session, short msg_id,
	int64_t offset, const char* data, uint32_t len) {
	auto task = std::make_shared<FileTask>();
	task->session = session;
	task->msg_id = msg_id;
	task->offset = offset;
	if (data && len > 0) {
		task->data.assign(data, data + len);
	}
	std::unique_lock<std::mutex> lock(_mutex);
	_msg_que.push(task);
	if (_msg_que.size() == 1) {
		_consume.notify_one();
	}
}

void FileLogicSystem::DealMsg() {
	while (true) {
		std::unique_lock<std::mutex> lock(_mutex);
		while (_msg_que.empty() && !_b_stop) {
			_consume.wait(lock);
		}
		if (_b_stop) {
			// Drain remaining
			while (!_msg_que.empty()) {
				auto task = _msg_que.front();
				_msg_que.pop();
				auto it = _fun_callbacks.find(task->msg_id);
				if (it != _fun_callbacks.end()) {
					it->second(task->session, task->msg_id, task->offset,
						task->data.data(), static_cast<uint32_t>(task->data.size()));
				}
			}
			break;
		}

		auto task = _msg_que.front();
		_msg_que.pop();
		lock.unlock();

		auto it = _fun_callbacks.find(task->msg_id);
		if (it != _fun_callbacks.end()) {
			it->second(task->session, task->msg_id, task->offset,
				task->data.data(), static_cast<uint32_t>(task->data.size()));
		}
		else {
			std::cerr << "FileLogicSystem: unknown msg_id=" << task->msg_id << std::endl;
		}
	}
}

void FileLogicSystem::RegisterCallBacks() {
	_fun_callbacks[ID_FSVR_AUTH_REQ] = [this](auto s, auto id, auto off, auto d, auto l) {
		HandleAuthReq(s, id, off, d, l);
	};
	_fun_callbacks[ID_FSVR_DATA] = [this](auto s, auto id, auto off, auto d, auto l) {
		HandleData(s, id, off, d, l);
	};
}

// =====================================================================
// ID_FSVR_AUTH_REQ handler
// Payload JSON: {"file_token":"...", "file_id":"..."}
// =====================================================================
void FileLogicSystem::HandleAuthReq(std::shared_ptr<FileSession> session, short msg_id,
	int64_t offset, const char* data, uint32_t len) {

	Json::Value root;
	Json::Reader reader;
	std::string body(data, len);
	if (!reader.parse(body, root)) {
		std::cerr << "HandleAuthReq: JSON parse error" << std::endl;
		Json::Value rsp;
		rsp["error"] = 1;
		session->SendJson(ID_FSVR_AUTH_RSP, rsp.toStyledString());
		return;
	}

	std::string file_token = root["file_token"].asString();
	std::string file_id = root["file_id"].asString();

	// 1. Validate token from Redis
	std::string upload_key = std::string(FILE_UPLOAD_TOKEN_PREFIX) + file_token;
	std::string download_key = std::string(FILE_DOWNLOAD_TOKEN_PREFIX) + file_token;

	std::string token_value;
	bool is_upload = false;
	bool is_download = false;

	if (RedisMgr::GetInstance()->Get(upload_key, token_value)) {
		is_upload = true;
	}
	else if (RedisMgr::GetInstance()->Get(download_key, token_value)) {
		is_download = true;
	}
	else {
		std::cerr << "HandleAuthReq: token not found or expired" << std::endl;
		Json::Value rsp;
		rsp["error"] = 2;
		rsp["msg"] = "token invalid or expired";
		session->SendJson(ID_FSVR_AUTH_RSP, rsp.toStyledString());
		return;
	}

	// 2. Parse token value
	Json::Value token_json;
	if (!reader.parse(token_value, token_json)) {
		std::cerr << "HandleAuthReq: token JSON parse error" << std::endl;
		Json::Value rsp;
		rsp["error"] = 3;
		session->SendJson(ID_FSVR_AUTH_RSP, rsp.toStyledString());
		return;
	}

	// 3. Validate file_id matches
	if (token_json["file_id"].asString() != file_id) {
		std::cerr << "HandleAuthReq: file_id mismatch" << std::endl;
		Json::Value rsp;
		rsp["error"] = 4;
		rsp["msg"] = "file_id mismatch";
		session->SendJson(ID_FSVR_AUTH_RSP, rsp.toStyledString());
		return;
	}

	// 4. Delete token (one-time use)
	if (is_upload) {
		RedisMgr::GetInstance()->Del(upload_key);
	}
	else {
		RedisMgr::GetInstance()->Del(download_key);
	}

	// 5. Set session state
	session->file_id = file_id;
	session->is_upload = is_upload;

	Json::Value rsp;
	rsp["error"] = 0;

	if (is_upload) {
		int64_t expected_size = static_cast<int64_t>(token_json["file_size"].asDouble());
		std::string file_name = token_json.get("file_name", "unknown").asString();
		session->file_size = expected_size;
		// Remember the sender uid for later NotifyUploadDone routing.
		// The token was minted by ChatServer with uid = fromuid.
		session->fromuid = token_json.get("uid", 0).asInt();

		// Update DB status to uploading FIRST (before touching disk)
		if (!MysqlMgr::GetInstance()->UpdateFileStatus(file_id, FILE_UPLOADING)) {
			std::cerr << "HandleAuthReq: UpdateFileStatus failed for " << file_id << std::endl;
			Json::Value err_rsp;
			err_rsp["error"] = 6;
			err_rsp["msg"] = "database busy, please retry";
			session->SendJson(ID_FSVR_AUTH_RSP, err_rsp.toStyledString());
			return;
		}

		// Check resume progress
		std::string progress_key = std::string(FILE_UPLOAD_PROGRESS_PREFIX) + file_id;
		std::string progress_str;
		int64_t resume_offset = 0;
		if (RedisMgr::GetInstance()->Get(progress_key, progress_str)) {
			resume_offset = std::stoll(progress_str);
		}

		// Prepare directory on disk (file created on first WriteChunk)
		std::string relative_path = FileStorage::GetInstance()->PrepareFile(file_id, file_name);
		session->file_path = relative_path;
		session->bytes_received = resume_offset;

		rsp["offset"] = static_cast<double>(resume_offset);
		std::cout << "Upload auth OK: file_id=" << file_id
			<< " size=" << expected_size << " resume_offset=" << resume_offset << std::endl;
	}
	else {
		// Download: look up file_path from DB
		std::string file_path;
		std::string file_name;
		int64_t file_size = 0;
		if (!MysqlMgr::GetInstance()->GetFileInfo(file_id, file_path, file_name, file_size)) {
			rsp["error"] = 5;
			rsp["msg"] = "file not found in database";
			session->SendJson(ID_FSVR_AUTH_RSP, rsp.toStyledString());
			return;
		}
		session->file_path = file_path;
		session->file_size = file_size;
		rsp["file_size"] = static_cast<double>(file_size);
		rsp["file_name"] = file_name;
		std::cout << "Download auth OK: file_id=" << file_id
			<< " file_path=" << file_path << " size=" << file_size << std::endl;
	}

	session->SendJson(ID_FSVR_AUTH_RSP, rsp.toStyledString());

	// For download, start sending file data immediately after auth response
	if (is_download) {
		// Send file in chunks on a separate thread to avoid blocking logic thread
		auto self_session = session;
		std::thread([self_session]() {
			char buf[FSVR_MAX_CHUNK_SIZE];
			int64_t offset = 0;
			while (offset < self_session->file_size) {
				size_t to_read = static_cast<size_t>(
					std::min<int64_t>(FSVR_MAX_CHUNK_SIZE, self_session->file_size - offset));
				size_t bytes_read = FileStorage::GetInstance()->ReadChunk(
					self_session->file_path, offset, buf, to_read);
				if (bytes_read == 0) break;
				self_session->SendDataChunk(offset, buf, static_cast<uint32_t>(bytes_read));
				offset += bytes_read;
			}
			// Send DONE
			Json::Value done;
			done["file_id"] = self_session->file_id;
			done["status"] = "ok";
			self_session->SendJson(ID_FSVR_DONE, done.toStyledString());
			std::cout << "Download complete: file_id=" << self_session->file_id << std::endl;
		}).detach();
	}
}

// =====================================================================
// ID_FSVR_DATA handler (upload: client sends file chunks)
// =====================================================================
void FileLogicSystem::HandleData(std::shared_ptr<FileSession> session, short msg_id,
	int64_t offset, const char* data, uint32_t len) {

	if (!session->is_upload || session->file_path.empty()) {
		std::cerr << "HandleData: session not authorized for upload" << std::endl;
		return;
	}

	// Write chunk to disk
	bool ok = FileStorage::GetInstance()->WriteChunk(session->file_path, offset, data, len);
	if (!ok) {
		std::cerr << "HandleData: disk write failed at offset=" << offset << std::endl;
		return;
	}

	session->bytes_received = offset + len;

	// Update Redis progress periodically (every 256 KB = every 4 chunks)
	if (session->bytes_received % (FSVR_MAX_CHUNK_SIZE * 4) == 0 ||
		session->bytes_received >= session->file_size) {
		std::string progress_key = std::string(FILE_UPLOAD_PROGRESS_PREFIX) + session->file_id;
		RedisMgr::GetInstance()->Set(progress_key, std::to_string(session->bytes_received));
	}

	// Check if upload is complete
	if (session->bytes_received >= session->file_size) {
		std::cout << "Upload complete: file_id=" << session->file_id
			<< " bytes=" << session->bytes_received << std::endl;

		// Update DB
		MysqlMgr::GetInstance()->UpdateFileComplete(session->file_id, session->file_path, "");

		// Clean up Redis progress
		std::string progress_key = std::string(FILE_UPLOAD_PROGRESS_PREFIX) + session->file_id;
		RedisMgr::GetInstance()->Del(progress_key);

		// Send DONE to client
		Json::Value done;
		done["file_id"] = session->file_id;
		done["status"] = "ok";
		session->SendJson(ID_FSVR_DONE, done.toStyledString());

		// gRPC notify ChatServer that upload is done.
		// Pass fromuid so the client can route to the specific ChatServer
		// instance where the sender is connected (via Redis uip_<fromuid>),
		// otherwise the NOTIFY_COMPLETE back to the sender would be lost in
		// a multi-ChatServer deployment.
		ChatServerClient::GetInstance()->NotifyUploadDone(
			session->file_id, session->file_path, "", session->fromuid);
	}
}
