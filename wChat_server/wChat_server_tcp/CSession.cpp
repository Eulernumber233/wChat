#include "CSession.h"
#include "CServer.h"
#include "LogicSystem.h"

CSession::CSession(boost::asio::io_context& io_context, CServer* server):
	_socket(io_context), _server(server), _b_close(false),_b_head_parse(false)
{
	boost::uuids::uuid  a_uuid = boost::uuids::random_generator()();
	_session_id = boost::uuids::to_string(a_uuid);
	_recv_head_node = std::make_shared<MsgNode>(HEAD_TOTAL_LEN);
}

CSession::~CSession() {
	std::cout << "~CSession destruct" << std::endl;
}

tcp::socket& CSession::GetSocket() {
	return _socket;
}


std::string& CSession::GetSessionId()
{
	return _session_id;
}
void CSession::SetUserId(int uid)
{
	_user_id = uid;
}

int CSession::GetUserId()
{
	return _user_id;
}

void CSession::RefreshHeartbeat() {
	_last_heartbeat.store(
		std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count(),
		std::memory_order_relaxed
	);
}

bool CSession::IsHeartbeatExpired(int timeout_sec) const {
	auto last = _last_heartbeat.load(std::memory_order_relaxed);
	if (last == 0) return false; // 还未登录，不判定超时
	auto now = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::steady_clock::now().time_since_epoch()
	).count();
	return (now - last) > timeout_sec;
}

std::shared_ptr<CSession>CSession::SharedSelf() {
	return shared_from_this();
}

void CSession::Start() {
	AsyncReadHead(HEAD_TOTAL_LEN);
}

void CSession::Close() {
	// 原子 exchange 保证只执行一次
	if (_b_close.exchange(true)) return;
	boost::system::error_code ec;
	_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
	_socket.close(ec);
}


void CSession::asyncReadFull(std::size_t maxLength, std::function<void(const boost::system::error_code&, std::size_t)> handler)
{
	::memset(_data, 0, MAX_LENGTH);
	asyncReadLen(0, maxLength, handler);
}

void CSession::asyncReadLen(std::size_t read_len, std::size_t total_len,
	std::function<void(const boost::system::error_code&, std::size_t)> handler)
{
	auto self = shared_from_this();
	_socket.async_read_some(boost::asio::buffer(_data + read_len, total_len - read_len),
		[read_len, total_len, handler, self](const boost::system::error_code& ec, std::size_t  bytesTransfered) {
			if (ec) {
				handler(ec, read_len + bytesTransfered);
				return;
			}

			if (read_len + bytesTransfered >= total_len) {
				handler(ec, read_len + bytesTransfered);
				return;
			}

			self->asyncReadLen(read_len + bytesTransfered, total_len, handler);
		});
}

void CSession::AsyncReadHead(int total_len)
{
	auto self = shared_from_this();
	asyncReadFull(HEAD_TOTAL_LEN, [self, this](const boost::system::error_code& ec, std::size_t bytes_transfered) {
		try {
			if (ec) {
				std::cout << "handle read failed, error is " << ec.what() << std::endl;
				if (!_b_close.load()) {
					Close();
					_server->ClearSession(_session_id);
				}
				return;
			}

			if (bytes_transfered < HEAD_TOTAL_LEN) {
				std::cout << "read length not match, read [" << bytes_transfered << "] , total ["
					<< HEAD_TOTAL_LEN << "]" << std::endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			_recv_head_node->Clear();
			memcpy(_recv_head_node->_data, _data, bytes_transfered);

			short msg_id = 0;
			memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
			msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
			std::cout << "msg_id is " << msg_id << std::endl;
			if (msg_id > MAX_LENGTH) {
				std::cout << "invalid msg_id is " << msg_id << std::endl;
				_server->ClearSession(_session_id);
				return;
			}
			short msg_len = 0;
			memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
			msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);
			std::cout << "msg_len is " << msg_len << std::endl;

			if (msg_len > MAX_LENGTH) {
				std::cout << "invalid data length is " << msg_len << std::endl;
				_server->ClearSession(_session_id);
				return;
			}

			_recv_msg_node = std::make_shared<RecvNode>(msg_len, msg_id);
			AsyncReadBody(msg_len);
		}
		catch (std::exception& e) {
			std::cout << "Exception code is " << e.what() << std::endl;
		}
		});
}

void CSession::AsyncReadBody(int total_len)
{
	auto self = shared_from_this();
	asyncReadFull(total_len, [self, this, total_len](const boost::system::error_code& ec, std::size_t bytes_transfered) {
		try {
			if (ec) {
				std::cout << "handle read failed, error is " << ec.what() << std::endl;
				if (!_b_close.load()) {
					Close();
					_server->ClearSession(_session_id);
				}
				return;
			}

			if (bytes_transfered < total_len) {
				std::cout << "read length not match, read [" << bytes_transfered << "] , total ["
					<< total_len << "]" << std::endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			memcpy(_recv_msg_node->_data, _data, bytes_transfered);
			_recv_msg_node->_cur_len += bytes_transfered;
			_recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
			std::cout << "receive data is " << _recv_msg_node->_data << std::endl;
			LogicSystem::GetInstance()->PostMsgToQue(std::make_shared<LogicNode>(shared_from_this(), _recv_msg_node));
			AsyncReadHead(HEAD_TOTAL_LEN);
		}
		catch (std::exception& e) {
			std::cout << "Exception code is " << e.what() << std::endl;
		}
		});
}


void CSession::Send(std::string msg, short msgid) {
	std::lock_guard<std::mutex> lock(_send_lock);
	if (_b_close) return; // socket 已关闭，不再发送
	int send_que_size = _send_que.size();
	if (send_que_size > MAX_SENDQUE) {
		std::cout << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << std::endl;
		return;
	}

	_send_que.push(std::make_shared<SendNode>(msg.c_str(), msg.length(), msgid));
	if (send_que_size > 0) {
		return;
	}
	auto& msgnode = _send_que.front();
	boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
		std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
}

void CSession::SendAndClose(std::string msg, short msgid) {
	// 已经在关闭流程中，不再重复发送/关闭
	if (_b_close.load()) return;
	Send(msg, msgid);
	auto self = shared_from_this();
	boost::asio::post(_socket.get_executor(), [self, this]() {
		Close();
	});
}

void CSession::Send(char* msg, short max_length, short msgid) {
	std::lock_guard<std::mutex> lock(_send_lock);
	if (_b_close) return; // socket 已关闭，不再发送
	int send_que_size = _send_que.size();
	if (send_que_size > MAX_SENDQUE) {
		std::cout << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << std::endl;
		return;
	}

	_send_que.push(std::make_shared<SendNode>(msg, max_length, msgid));
	if (send_que_size > 0) {
		return;
	}
	auto& msgnode = _send_que.front();
	boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
		std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
}

void CSession::HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self) {
	try {
		if (!error) {
			std::lock_guard<std::mutex> lock(_send_lock);
			//cout << "send data " << _send_que.front()->_data+HEAD_LENGTH << endl;
			_send_que.pop();
			if (!_send_que.empty()) {
				auto& msgnode = _send_que.front();
				boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
					std::bind(&CSession::HandleWrite, this, std::placeholders::_1, shared_self));
			}
		}
		else {
			std::cout << "handle write failed, error is " << error.what() << std::endl;
			if (!_b_close.load()) {
				Close();
				_server->ClearSession(_session_id);
			}
		}
	}
	catch (std::exception& e) {
		std::cerr << "Exception code : " << e.what() << std::endl;
	}

}


void CSession::HandleRead(const boost::system::error_code& error, size_t  bytes_transferred, std::shared_ptr<CSession> shared_self){
	try {
		if (!error) {
			int copy_len = 0;
			while (bytes_transferred > 0) {
				if (!_b_head_parse) {
					if (bytes_transferred + _recv_head_node->_cur_len < HEAD_TOTAL_LEN) {
						memcpy(_recv_head_node->_data + _recv_head_node->_cur_len, _data + copy_len, bytes_transferred);
						_recv_head_node->_cur_len += bytes_transferred;
						::memset(_data, 0, MAX_LENGTH);
						_socket.async_read_some(boost::asio::buffer(_data, MAX_LENGTH),
							std::bind(&CSession::HandleRead, this, std::placeholders::_1, std::placeholders::_2, shared_self));
						return;
					}
					int head_remain = HEAD_TOTAL_LEN - _recv_head_node->_cur_len;
					memcpy(_recv_head_node->_data + _recv_head_node->_cur_len, _data + copy_len, head_remain);
					copy_len += head_remain;
					bytes_transferred -= head_remain;
					short msg_id = 0;
					memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
					msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
					std::cout << "msg_id is " << msg_id << std::endl;
					if (msg_id > MAX_LENGTH) {
						std::cout << "invalid msg_id is " << msg_id << std::endl;
						_server->ClearSession(_session_id);
						return;
					}
					short msg_len = 0;
					memcpy(&msg_len, _recv_head_node->_data+HEAD_ID_LEN, HEAD_DATA_LEN);
					msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);
					std::cout << "msg_len is " << msg_len << std::endl;
					if (msg_len > MAX_LENGTH) {
						std::cout << "invalid data length is " << msg_len << std::endl;
						_server->ClearSession(_session_id);
						return;
					}

					_recv_msg_node = std::make_shared<RecvNode>(msg_len, msg_id);

					if (bytes_transferred < msg_len) {
						memcpy(_recv_msg_node->_data + _recv_msg_node->_cur_len, _data + copy_len, bytes_transferred);
						_recv_msg_node->_cur_len += bytes_transferred;
						::memset(_data, 0, MAX_LENGTH);
						_socket.async_read_some(boost::asio::buffer(_data, MAX_LENGTH),
							std::bind(&CSession::HandleRead, this, std::placeholders::_1, std::placeholders::_2, shared_self));
						_b_head_parse = true;
						return;
					}

					memcpy(_recv_msg_node->_data + _recv_msg_node->_cur_len, _data + copy_len, msg_len);
					_recv_msg_node->_cur_len += msg_len;
					copy_len += msg_len;
					bytes_transferred -= msg_len;
					_recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
					//cout << "receive data is " << _recv_msg_node->_data << endl;
					LogicSystem::GetInstance()->PostMsgToQue(std::make_shared<LogicNode>(shared_from_this(), _recv_msg_node));
				
					_b_head_parse = false;
					_recv_head_node->Clear();
					if (bytes_transferred <= 0) {
						::memset(_data, 0, MAX_LENGTH);
						_socket.async_read_some(boost::asio::buffer(_data, MAX_LENGTH),
							std::bind(&CSession::HandleRead, this, std::placeholders::_1, std::placeholders::_2, shared_self));
						return;
					}
					continue;
				}

				int remain_msg = _recv_msg_node->_total_len - _recv_msg_node->_cur_len;
				if (bytes_transferred < remain_msg) {
					memcpy(_recv_msg_node->_data + _recv_msg_node->_cur_len, _data + copy_len, bytes_transferred);
					_recv_msg_node->_cur_len += bytes_transferred;
					::memset(_data, 0, MAX_LENGTH);
					_socket.async_read_some(boost::asio::buffer(_data, MAX_LENGTH),
						std::bind(&CSession::HandleRead, this, std::placeholders::_1, std::placeholders::_2, shared_self));
					return;
				}
				memcpy(_recv_msg_node->_data + _recv_msg_node->_cur_len, _data + copy_len, remain_msg);
				_recv_msg_node->_cur_len += remain_msg;
				bytes_transferred -= remain_msg;
				copy_len += remain_msg;
				_recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
				//cout << "receive data is " << _recv_msg_node->_data << endl;
				LogicSystem::GetInstance()->PostMsgToQue(std::make_shared<LogicNode>(shared_from_this(), _recv_msg_node));
				
				_b_head_parse = false;
				_recv_head_node->Clear();
				if (bytes_transferred <= 0) {
					::memset(_data, 0, MAX_LENGTH);
					_socket.async_read_some(boost::asio::buffer(_data, MAX_LENGTH),
						std::bind(&CSession::HandleRead, this, std::placeholders::_1, std::placeholders::_2, shared_self));
					return;
				}
				continue;
			}
		}
		else {
			std::cout << "handle read failed, error is " << error.what() << std::endl;
			if (!_b_close.load()) {
				Close();
				_server->ClearSession(_session_id);
			}
		}
	}
	catch (std::exception& e) {
		std::cout << "Exception code is " << e.what() << std::endl;
	}
}

LogicNode::LogicNode(std::shared_ptr<CSession>  session, 
	std::shared_ptr<RecvNode> recvnode):_session(session),_recvnode(recvnode) {
	
}
