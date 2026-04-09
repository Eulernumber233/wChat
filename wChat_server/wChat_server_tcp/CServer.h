#include "core.h"
#include "CSession.h"

class CServer
{
public:
    CServer(boost::asio::io_context& io_context, short port);
    ~CServer();
    void ClearSession(std::string session_id);
private:
    void HandleAccept(std::shared_ptr<CSession> new_session, const boost::system::error_code& error);
    void StartAccept();
    boost::asio::io_context& _io_context;
    short _port;
    tcp::acceptor _acceptor;
    std::map<std::string, std::shared_ptr<CSession>> _sessions;
    std::mutex _mutex;
};