#pragma once
#include "core.h"
#include "Singleton.h"
#include "data.h"
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "message.pb.h"

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::AddFriendReq;
using message::AddFriendRsp;

using message::AuthFriendReq;
using message::AuthFriendRsp;

using message::GetChatServerRsp;
using message::LoginRsp;
using message::LoginReq;
using message::ChatService;

using message::TextChatMsgReq;
using message::TextChatMsgRsp;
using message::TextChatData;

using message::KickUserReq;
using message::KickUserRsp;




class ChatConPool
{
public:
	ChatConPool(size_t poolSize, std::string host, std::string port);
	~ChatConPool();
	std::unique_ptr<ChatService::Stub>getConnection();
	void returnConnection(std::unique_ptr<ChatService::Stub> context);
	void Close();
private:
	std::atomic<bool>b_stop_;
	size_t poolSize_;
	std::string host_;
	std::string port_;
	std::queue<std::unique_ptr<ChatService::Stub>>connections_;
	std::mutex mutex_;
	std::condition_variable cond_;

};

class ChatGrpcClient :public Singleton<ChatGrpcClient>
{
	friend class Singleton<ChatGrpcClient>;
public:
	~ChatGrpcClient() {

	}

	AddFriendRsp NotifyAddFriend(std::string server_ip, const AddFriendReq& req);
	AuthFriendRsp NotifyAuthFriend(std::string server_ip, const AuthFriendReq& req);
	bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);
	TextChatMsgRsp NotifyTextChatMsg(std::string server_ip, const TextChatMsgReq& req, const Json::Value& rtvalue);
	KickUserRsp NotifyKickUser(std::string server_ip, const KickUserReq& req);
private:
	ChatGrpcClient();
	std::unordered_map<std::string, std::unique_ptr<ChatConPool>> _pools;

};

