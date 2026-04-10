#include "CServer.h"
#include "AsioIOContextPool.h"

CServer::CServer(boost::asio::io_context& io_context, short port)
	: _io_context(io_context), _port(port),
	_acceptor(io_context, tcp::endpoint(tcp::v4(), port)) {
	std::cout << "FileServer start success, listen on port : " << _port << std::endl;
	StartAccept();
}

CServer::~CServer() {
	std::cout << "FileServer destruct listen on port : " << _port << std::endl;
}

void CServer::HandleAccept(std::shared_ptr<FileSession> new_session, const boost::system::error_code& error) {
	if (!error) {
		new_session->Start();
		std::lock_guard<std::mutex> lock(_mutex);
		_sessions.insert(std::make_pair(new_session->GetSessionId(), new_session));
	}
	else {
		std::cout << "FileServer accept failed: " << error.what() << std::endl;
	}
	StartAccept();
}

void CServer::StartAccept() {
	auto& io_context = AsioIOContextPool::GetInstance()->GetIOContext();
	auto new_session = std::make_shared<FileSession>(io_context, this);
	_acceptor.async_accept(new_session->GetSocket(),
		std::bind(&CServer::HandleAccept, this, new_session, std::placeholders::_1));
}

void CServer::ClearSession(std::string session_id) {
	std::lock_guard<std::mutex> lock(_mutex);
	_sessions.erase(session_id);
}
