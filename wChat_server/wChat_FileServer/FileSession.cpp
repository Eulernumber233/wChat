#include "FileSession.h"
#include "CServer.h"
#include "FileLogicSystem.h"

FileSession::FileSession(boost::asio::io_context& io_context, CServer* server)
	: _socket(io_context), _server(server), _b_close(false) {
	auto uuid = boost::uuids::random_generator()();
	_session_id = boost::uuids::to_string(uuid);
}

FileSession::~FileSession() {
	std::cout << "~FileSession " << _session_id << std::endl;
}

tcp::socket& FileSession::GetSocket() { return _socket; }
std::string& FileSession::GetSessionId() { return _session_id; }

std::shared_ptr<FileSession> FileSession::SharedSelf() {
	return shared_from_this();
}

void FileSession::Start() {
	AsyncReadHeader();
}

void FileSession::Close() {
	_b_close = true;
	boost::system::error_code ec;
	_socket.close(ec);
}

// =====================================================================
// Read loop: header -> payload -> dispatch -> header -> ...
// =====================================================================

void FileSession::AsyncReadHeader() {
	auto self = SharedSelf();
	boost::asio::async_read(_socket,
		boost::asio::buffer(_header_buf, FSVR_HEAD_TOTAL_LEN),
		[this, self](const boost::system::error_code& ec, size_t bytes) {
			HandleHeader(ec, bytes);
		});
}

void FileSession::HandleHeader(const boost::system::error_code& error, size_t bytes_transferred) {
	if (error) {
		// EOF after transfer complete is normal — client disconnected after DONE
		if (error != boost::asio::error::eof && error != boost::asio::error::operation_aborted) {
			std::cerr << "FileSession header read error: " << error.message() << std::endl;
		}
		Close();
		_server->ClearSession(_session_id);
		return;
	}

	// Parse header: msg_id (2B network) + body_len (4B network) + offset (8B network)
	short msg_id_net;
	memcpy(&msg_id_net, _header_buf, FSVR_HEAD_ID_LEN);
	short msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id_net);

	uint32_t body_len_net;
	memcpy(&body_len_net, _header_buf + FSVR_HEAD_ID_LEN, FSVR_HEAD_BODYLEN_LEN);
	uint32_t body_len = boost::asio::detail::socket_ops::network_to_host_long(body_len_net);

	int64_t offset_net;
	memcpy(&offset_net, _header_buf + FSVR_HEAD_ID_LEN + FSVR_HEAD_BODYLEN_LEN, FSVR_HEAD_OFFSET_LEN);
	// For offset we use manual byte-swap (64-bit)
	int64_t offset = 0;
	{
		unsigned char* src = reinterpret_cast<unsigned char*>(&offset_net);
		unsigned char* dst = reinterpret_cast<unsigned char*>(&offset);
		for (int i = 0; i < 8; ++i) dst[i] = src[7 - i];
	}

	if (body_len > FSVR_MAX_CHUNK_SIZE) {
		std::cerr << "FileSession: payload too large: " << body_len << std::endl;
		Close();
		_server->ClearSession(_session_id);
		return;
	}

	if (body_len == 0) {
		// No payload, dispatch immediately (shouldn't happen normally)
		FileLogicSystem::GetInstance()->PostMsg(SharedSelf(), msg_id, offset, nullptr, 0);
		AsyncReadHeader();
		return;
	}

	AsyncReadPayload(msg_id, body_len, offset);
}

void FileSession::AsyncReadPayload(short msg_id, uint32_t body_len, int64_t offset) {
	_payload_buf.resize(body_len);
	auto self = SharedSelf();
	boost::asio::async_read(_socket,
		boost::asio::buffer(_payload_buf.data(), body_len),
		[this, self, msg_id, body_len, offset](const boost::system::error_code& ec, size_t bytes) {
			HandlePayload(msg_id, body_len, offset, ec, bytes);
		});
}

void FileSession::HandlePayload(short msg_id, uint32_t body_len, int64_t offset,
	const boost::system::error_code& error, size_t bytes_transferred) {
	if (error) {
		std::cout << "FileSession payload read error: " << error.message() << std::endl;
		Close();
		_server->ClearSession(_session_id);
		return;
	}

	FileLogicSystem::GetInstance()->PostMsg(SharedSelf(), msg_id, offset,
		_payload_buf.data(), static_cast<uint32_t>(body_len));

	// Continue reading next frame
	AsyncReadHeader();
}

// =====================================================================
// Send methods
// =====================================================================

void FileSession::SendJson(short msg_id, const std::string& json_str) {
	uint32_t body_len = static_cast<uint32_t>(json_str.size());
	int64_t offset = 0;

	auto buf = std::make_shared<std::vector<char>>(FSVR_HEAD_TOTAL_LEN + body_len);
	char* p = buf->data();

	// msg_id (network byte order)
	short msg_id_net = boost::asio::detail::socket_ops::host_to_network_short(msg_id);
	memcpy(p, &msg_id_net, FSVR_HEAD_ID_LEN);
	p += FSVR_HEAD_ID_LEN;

	// body_len (network byte order)
	uint32_t body_len_net = boost::asio::detail::socket_ops::host_to_network_long(body_len);
	memcpy(p, &body_len_net, FSVR_HEAD_BODYLEN_LEN);
	p += FSVR_HEAD_BODYLEN_LEN;

	// offset = 0 (network byte order, 8 bytes)
	int64_t offset_net = 0;
	memcpy(p, &offset_net, FSVR_HEAD_OFFSET_LEN);
	p += FSVR_HEAD_OFFSET_LEN;

	// payload
	memcpy(p, json_str.data(), body_len);

	DoSend(buf);
}

void FileSession::SendDataChunk(int64_t offset, const char* data, uint32_t length) {
	auto buf = std::make_shared<std::vector<char>>(FSVR_HEAD_TOTAL_LEN + length);
	char* p = buf->data();

	// msg_id
	short msg_id_net = boost::asio::detail::socket_ops::host_to_network_short(ID_FSVR_DATA);
	memcpy(p, &msg_id_net, FSVR_HEAD_ID_LEN);
	p += FSVR_HEAD_ID_LEN;

	// body_len
	uint32_t body_len_net = boost::asio::detail::socket_ops::host_to_network_long(length);
	memcpy(p, &body_len_net, FSVR_HEAD_BODYLEN_LEN);
	p += FSVR_HEAD_BODYLEN_LEN;

	// offset (manual big-endian for 64-bit)
	{
		unsigned char* src = reinterpret_cast<unsigned char*>(&offset);
		unsigned char* dst = reinterpret_cast<unsigned char*>(p);
		for (int i = 0; i < 8; ++i) dst[i] = src[7 - i];
	}
	p += FSVR_HEAD_OFFSET_LEN;

	// payload
	memcpy(p, data, length);

	DoSend(buf);
}

void FileSession::DoSend(std::shared_ptr<std::vector<char>> data) {
	std::lock_guard<std::mutex> lock(_send_lock);
	bool sending = !_send_que.empty();
	_send_que.push(data);
	if (!sending) {
		auto self = SharedSelf();
		boost::asio::async_write(_socket,
			boost::asio::buffer(_send_que.front()->data(), _send_que.front()->size()),
			[this, self](const boost::system::error_code& ec, size_t) {
				HandleWrite(ec);
			});
	}
}

void FileSession::HandleWrite(const boost::system::error_code& error) {
	if (error) {
		std::cout << "FileSession write error: " << error.message() << std::endl;
		Close();
		_server->ClearSession(_session_id);
		return;
	}

	std::lock_guard<std::mutex> lock(_send_lock);
	_send_que.pop();
	if (!_send_que.empty()) {
		auto self = SharedSelf();
		boost::asio::async_write(_socket,
			boost::asio::buffer(_send_que.front()->data(), _send_que.front()->size()),
			[this, self](const boost::system::error_code& ec, size_t) {
				HandleWrite(ec);
			});
	}
}
