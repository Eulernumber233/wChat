#pragma once
#include "core.h"
#include "data.h"
#include "message.grpc.pb.h"
#include "message.pb.h"
#include "CSession.h"
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using message::AddFriendReq;
using message::AddFriendRsp;

using message::AuthFriendReq;
using message::AuthFriendRsp;

using message::ChatService;
using message::TextChatMsgReq;
using message::TextChatMsgRsp;
using message::TextChatData;
using message::KickUserReq;
using message::KickUserRsp;

class ChatServiceImpl final : public ChatService::Service
{
public:
	ChatServiceImpl();
	Status NotifyAddFriend(ServerContext* context, const AddFriendReq* request,
		AddFriendRsp* reply) override;

	Status NotifyAuthFriend(ServerContext* context,
		const AuthFriendReq* request, AuthFriendRsp* response) override;

	Status NotifyTextChatMsg(::grpc::ServerContext* context,
		const TextChatMsgReq* request, TextChatMsgRsp* response) override;

	bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);

	// 跨服踢人 RPC 实现
	Status NotifyKickUser(::grpc::ServerContext* context,
		const KickUserReq* request, KickUserRsp* response) override;

	void RegisterServer(std::shared_ptr<CServer> pServer);
private:
	std::shared_ptr<CServer> _p_server;
};


