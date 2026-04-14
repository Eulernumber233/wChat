#include "CServer.h"
#include "AsioIOContextPool.h"
#include "UserMgr.h"
#include "RedisMgr.h"
#include "ConfigMgr.h"
CServer::CServer(boost::asio::io_context& io_context, short port) :_io_context(io_context), _port(port),
_acceptor(io_context, tcp::endpoint(tcp::v4(), port))
{
	std::cout << "Server start success, listen on port : " << _port << std::endl;
	StartAccept();
	_heartbeat_checker = std::thread(&CServer::HeartbeatCheckLoop, this);
}

CServer::~CServer()
{
    std::cout << "Server destruct listen on port : " << _port << std::endl;
    _b_stop.store(true);
    if (_heartbeat_checker.joinable()) {
        _heartbeat_checker.join();
    }
}




void CServer::HandleAccept(std::shared_ptr<CSession> new_session, const boost::system::error_code& error)
{
    if (!error) {
        new_session->Start();
        std::lock_guard<std::mutex>lock(_mutex);
        _sessions.insert(std::make_pair(new_session->GetSessionId(), new_session));
    }
    else {
        std::cout << "session accept failed error is" << error.what() << std::endl;
    }
    StartAccept();
}

void CServer::StartAccept() {
    auto& io_context = AsioIOContextPool::GetInstance()->GetIOContext();
    std::shared_ptr<CSession> new_session = std::make_shared<CSession>(io_context, this);
    _acceptor.async_accept(new_session->GetSocket(), std::bind(&CServer::HandleAccept, this, new_session, std::placeholders::_1));
}

void CServer::ClearSession(std::string session_id)
{
    std::shared_ptr<CSession> session;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _sessions.find(session_id);
        if (it == _sessions.end()) {
            return;
        }
        session = it->second;
        _sessions.erase(it);
    }

    int uid = session->GetUserId();
    // 移除 UserMgr 中的映射（按 session_id 匹配，不会误删新 session）
    UserMgr::GetInstance()->RmvUserSession(uid, session_id);

    // 清理 Redis：只在 usession_ 仍然是自己时才删除，防止误删新登录的记录
    if (uid > 0) {
        std::string uid_str = std::to_string(uid);
        std::string stored_sid;
        bool has_sid = RedisMgr::GetInstance()->Get(
            USER_SESSION_PREFIX + uid_str, stored_sid);
        if (has_sid && stored_sid == session_id) {
            RedisMgr::GetInstance()->Del(USERIPPREFIX + uid_str);
            RedisMgr::GetInstance()->Del(USER_SESSION_PREFIX + uid_str);
            std::cout << "ClearSession: cleaned Redis for uid=" << uid << std::endl;
        }
    }
}

void CServer::HeartbeatCheckLoop()
{
    const int HEARTBEAT_TIMEOUT = 90;  // 秒（客户端30秒发一次，3次未收到判定超时）
    const int CHECK_INTERVAL = 15;     // 每15秒扫描一次

    while (!_b_stop.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(CHECK_INTERVAL));
        if (_b_stop.load()) break;

        // 收集超时 session（不在持锁期间执行清理，避免长时间持锁）
        std::vector<std::string> expired_sids;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (auto& kv : _sessions) {
                if (kv.second->GetUserId() > 0 &&
                    kv.second->IsHeartbeatExpired(HEARTBEAT_TIMEOUT))
                {
                    expired_sids.push_back(kv.first);
                }
            }
        }

        for (auto& sid : expired_sids) {
            std::shared_ptr<CSession> session;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                auto it = _sessions.find(sid);
                if (it == _sessions.end()) continue;
                session = it->second;
            }

            int uid = session->GetUserId();
            std::cout << "HeartbeatCheck: timeout for uid=" << uid
                << " session=" << sid << std::endl;

            // ClearSession 内部已包含 RmvUserSession + Redis 清理
            ClearSession(sid);
        }
    }
}
