#pragma once
#include "core.h"
#include "Singleton.h"
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "message.pb.h"

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::FileService;
using message::FileUploadDoneReq;
using message::FileUploadDoneRsp;

// gRPC client for calling ChatServer's FileService::NotifyUploadDone
class ChatServerClient : public Singleton<ChatServerClient> {
	friend class Singleton<ChatServerClient>;
public:
	~ChatServerClient();

	// Initialize stub pools for all ChatServer instances listed in config
	void Init();

	// Notify a ChatServer that a file upload is complete.
	// server_name: which ChatServer to notify (e.g. "chatserver1")
	//   — in practice, any ChatServer instance can handle it since they
	//   share the same Redis/MySQL, so we notify all or pick one.
	bool NotifyUploadDone(const std::string& file_id,
		const std::string& file_path, const std::string& md5);

private:
	ChatServerClient();

	// Map of server_name -> stub
	struct StubInfo {
		std::shared_ptr<grpc::Channel> channel;
		std::unique_ptr<FileService::Stub> stub;
	};
	std::unordered_map<std::string, StubInfo> _stubs;
};
