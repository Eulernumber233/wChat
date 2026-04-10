#pragma once
#include "core.h"

class CServer;

class FileSession : public std::enable_shared_from_this<FileSession> {
public:
	FileSession(boost::asio::io_context& io_context, CServer* server);
	~FileSession();

	tcp::socket& GetSocket();
	std::string& GetSessionId();
	void Start();
	void Close();
	std::shared_ptr<FileSession> SharedSelf();

	// Send a control message (AUTH_RSP / DONE): msg_id + body_len + offset=0 + JSON payload
	void SendJson(short msg_id, const std::string& json_str);

	// Send a file data chunk: msg_id=ID_FSVR_DATA + body_len + offset + binary payload
	void SendDataChunk(int64_t offset, const char* data, uint32_t length);

	// --- Upload state (set by FileLogicSystem after auth) ---
	std::string file_id;
	std::string file_path;      // relative path on disk
	int64_t     file_size = 0;  // expected total size
	int64_t     bytes_received = 0;
	bool        is_upload = true; // true=upload session, false=download session

private:
	// Read the 14-byte header, then dispatch
	void AsyncReadHeader();
	void HandleHeader(const boost::system::error_code& error, size_t bytes_transferred);

	// Read payload after header is parsed
	void AsyncReadPayload(short msg_id, uint32_t body_len, int64_t offset);
	void HandlePayload(short msg_id, uint32_t body_len, int64_t offset,
		const boost::system::error_code& error, size_t bytes_transferred);

	// Internal send queue
	void DoSend(std::shared_ptr<std::vector<char>> data);
	void HandleWrite(const boost::system::error_code& error);

	tcp::socket  _socket;
	std::string  _session_id;
	CServer*     _server;
	bool         _b_close;

	// Header buffer (14 bytes)
	char _header_buf[FSVR_HEAD_TOTAL_LEN];

	// Payload buffer
	std::vector<char> _payload_buf;

	// Send queue
	std::queue<std::shared_ptr<std::vector<char>>> _send_que;
	std::mutex _send_lock;
};
