#pragma once
#include "core.h"
#include "FileSession.h"

class CServer {
public:
	CServer(boost::asio::io_context& io_context, short port);
	~CServer();
	void ClearSession(std::string session_id);
private:
	void HandleAccept(std::shared_ptr<FileSession> new_session, const boost::system::error_code& error);
	void StartAccept();
	boost::asio::io_context& _io_context;
	short _port;
	tcp::acceptor _acceptor;
	std::map<std::string, std::shared_ptr<FileSession>> _sessions;
	std::mutex _mutex;
};
