#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "core.h"
#include "RedisMgr.h"

std::string generate_unique_string() {
    // ����UUID����
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    // ��UUIDת��Ϊ�ַ���
    std::string unique_string = to_string(uuid);
    return unique_string;
}
ChatServer StatusServiceImpl::getChatServer() {
    std::lock_guard<std::mutex> guard(_server_mtx);
    i = (i + 1) % NUM_SERVER;
    auto it = _servers.begin();
    std::advance(it, i);
    auto& minServer = it->second;
    //auto minServer = _servers.begin()->second;
    //auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, minServer.name);
    //if (count_str.empty()) {
    //    //��������Ĭ������Ϊ���
    //    minServer.con_count = INT_MAX;
    //}
    //else {
    //    minServer.con_count = std::stoi(count_str);
    //}
    //// ʹ�÷�Χ����forѭ��
    //for (auto& server : _servers) {
    //    if (server.second.name == minServer.name) {
    //        continue;
    //    }
    //    auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server.second.name);
    //    if (count_str.empty()) {
    //        server.second.con_count = INT_MAX;
    //        std::cout << "----find server_count false----\n";
    //    }
    //    else {
    //        server.second.con_count = std::stoi(count_str);
    //    }
    //    if (server.second.con_count < minServer.con_count) {
    //        minServer = server.second;
    //    }
    //}
    return minServer;
}
Status StatusServiceImpl::GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply)
{
    std::string prefix("llfc status server has received : ");
    const auto& server = getChatServer();
    reply->set_host(server.host);
    reply->set_port(server.port);
    reply->set_error(ErrorCodes::Success);
    reply->set_token(generate_unique_string());

    // M2: dispatch an AgentServer in the same response so the client
    // never needs a second round-trip to find the AI endpoint.
    // Empty pool → send empty host / port 0; client hides AI UI.
    AgentServer agent;
    if (getAgentServer(agent)) {
        reply->set_agent_host(agent.host);
        try {
            reply->set_agent_port(std::stoi(agent.port));
        } catch (...) {
            // malformed config — treat as no agent rather than crash
            reply->set_agent_host("");
            reply->set_agent_port(0);
        }
    } else {
        reply->set_agent_host("");
        reply->set_agent_port(0);
    }

    insertToken(request->uid(), reply->token());
    return Status::OK;
}
Status StatusServiceImpl::Login(ServerContext* context, const LoginReq* request, LoginRsp* reply)
{
    auto uid = request->uid();
    auto token = request->token();
    std::cout << "uid = " << uid << "  token = " << token << std::endl;
    std::string uid_str = std::to_string(uid);
    std::string token_key = USERTOKENPREFIX + uid_str;
    std::string token_value = "";
    bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
    if (!success) {
        reply->set_error(ErrorCodes::UidInvalid);
        return Status::OK;
    }

    if (token_value != token) {
        reply->set_error(ErrorCodes::TokenInvalid);
        return Status::OK;
    }
    reply->set_error(ErrorCodes::Success);
    reply->set_uid(uid);
    reply->set_token(token);
    return Status::OK;
}

void StatusServiceImpl::insertToken(int uid, std::string token)
{
    std::string uid_str = std::to_string(uid);
    std::string token_key = USERTOKENPREFIX + uid_str;
    RedisMgr::GetInstance()->Set(token_key, token);
}

StatusServiceImpl::StatusServiceImpl() {
    auto& cfg = ConfigMgr::Inst();
    auto server_list = cfg["ChatServers"]["Name"];

    std::vector<std::string> words;

    std::stringstream ss(server_list);
    std::string word;

    while (std::getline(ss, word, ',')) {
        words.push_back(word);
    }

    for (auto& word : words) {
        if (cfg[word]["Name"].empty()) {
            continue;
        }

        ChatServer server;
        server.port = cfg[word]["Port"];
        server.host = cfg[word]["Host"];
        server.name = cfg[word]["Name"];
        _servers[server.name] = server;
        std::cout << "server.name :" << server.name << "server.host :"
            << server.host << "server.port :" << server.port << std::endl;
        NUM_SERVER++;
    }

    // M2: AgentServer pool. Entirely optional — missing section is fine.
    // Section layout mirrors [ChatServers]:
    //   [AgentServers] Name = AgentServer1,AgentServer2,...
    //   [AgentServer1] Name=agent1 Host=... Port=...
    auto agent_list = cfg["AgentServers"]["Name"];
    if (!agent_list.empty()) {
        std::stringstream ass(agent_list);
        std::string token;
        while (std::getline(ass, token, ',')) {
            // allow whitespace around commas
            auto trim_l = token.find_first_not_of(" \t");
            auto trim_r = token.find_last_not_of(" \t");
            if (trim_l == std::string::npos) continue;
            token = token.substr(trim_l, trim_r - trim_l + 1);

            if (cfg[token]["Name"].empty()) continue;
            AgentServer a;
            a.name = cfg[token]["Name"];
            a.host = cfg[token]["Host"];
            a.port = cfg[token]["Port"];
            _agent_servers.push_back(a);
            std::cout << "agent.name :" << a.name << " agent.host :"
                << a.host << " agent.port :" << a.port << std::endl;
        }
    }
    if (_agent_servers.empty()) {
        std::cout << "[StatusServer] no AgentServer configured — AI features disabled for clients" << std::endl;
    }
}

bool StatusServiceImpl::getAgentServer(AgentServer& out) {
    std::lock_guard<std::mutex> guard(_agent_mtx);
    if (_agent_servers.empty()) return false;
    out = _agent_servers[_agent_rr % _agent_servers.size()];
    _agent_rr = (_agent_rr + 1) % _agent_servers.size();
    return true;
}