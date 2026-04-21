#pragma once
#include "core.h"
#include "data.h"
#include "message.grpc.pb.h"
#include "message.pb.h"

// Server-side impl of AgentDataService (defined in proto/message.proto).
// Called by the Python AgentServer over gRPC to fetch chat context it
// needs to build LLM prompts.
//
// Design notes:
//  - Hosted on the same gRPC port as ChatServiceImpl / FileServiceImpl —
//    wChat_server_tcp.cpp registers all three against one ServerBuilder.
//  - Token is validated against Redis USERTOKENPREFIX key, same way as
//    LoginHandler does on first login. If Redis is unreachable we fail
//    closed here (unlike RateLimit-style fail-open on AgentServer),
//    because the alternative is leaking chat history without proof the
//    caller is authenticated.
//  - Profile fetch mirrors ChatServiceImpl::GetBaseInfo: Redis
//    ubaseinfo_<uid> first, MySQL on miss, writeback to Redis.

using grpc::ServerContext;
using grpc::Status;
using message::AgentDataService;
using message::GetChatHistoryReq;
using message::GetChatHistoryRsp;
using message::GetFriendProfileReq;
using message::GetFriendProfileRsp;

class AgentDataServiceImpl final : public AgentDataService::Service {
public:
    AgentDataServiceImpl() = default;

    Status GetChatHistory(ServerContext* context,
        const GetChatHistoryReq* request,
        GetChatHistoryRsp* response) override;

    Status GetFriendProfile(ServerContext* context,
        const GetFriendProfileReq* request,
        GetFriendProfileRsp* response) override;

private:
    // Shared auth check: returns true iff Redis utoken_<uid> equals token.
    bool VerifyToken(int self_uid, const std::string& token);

    // Same Redis→MySQL→writeback pattern as ChatServiceImpl::GetBaseInfo.
    // Kept local here rather than calling across service impls so each
    // service is self-contained (the project convention per CLAUDE.md §11.1).
    bool LoadBaseInfo(int uid, std::shared_ptr<UserInfo>& userinfo);
};
