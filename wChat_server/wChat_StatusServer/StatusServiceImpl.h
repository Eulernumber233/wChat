#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::StatusService;
using message::LoginRsp;
using message::LoginReq;

struct ChatServer {
    std::string host;
    std::string port;
    std::string name;
    int con_count;
};

// M2: parallel pool for AgentServer (AI smart reply). Dispatched at the
// same time as the ChatServer pick so the client gets a single login
// round-trip. Empty pool = AI features disabled.
struct AgentServer {
    std::string host;
    std::string port;  // stored as string from INI; converted to int32 at send time
    std::string name;
};

class StatusServiceImpl final : public StatusService::Service
{
public:
    StatusServiceImpl();
    Status GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply) override;
    Status Login(ServerContext* context, const LoginReq* request, LoginRsp* reply) override;

private:
    void insertToken(int uid, std::string token);
    ChatServer getChatServer();
    // Round-robin pick. Returns false when no AgentServer is configured —
    // the caller then leaves agent_host empty in the response so the
    // client can hide AI UI.
    bool getAgentServer(AgentServer& out);

    std::unordered_map<std::string, ChatServer> _servers;
    std::mutex _server_mtx;
    int i = 0;
    int NUM_SERVER = 0;

    std::vector<AgentServer> _agent_servers;  // vector: ordered round-robin
    std::mutex _agent_mtx;
    size_t _agent_rr = 0;
};