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
	// Routing: we look up Redis uip_<fromuid> to find the ChatServer
	// instance name the sender is currently connected to, then call that
	// instance's stub directly. This is required so the sender's client
	// reliably receives ID_FILE_NOTIFY_COMPLETE (which carries msg_db_id
	// needed for LocalDb). Falls back to trying all instances when the
	// Redis lookup fails, so we stay robust in the face of transient
	// Redis issues — but this should be a rare path.
	bool NotifyUploadDone(const std::string& file_id,
		const std::string& file_path, const std::string& md5,
		int fromuid);

private:
	ChatServerClient();

	// Map of server_name -> stub
	struct StubInfo {
		std::shared_ptr<grpc::Channel> channel;
		std::unique_ptr<FileService::Stub> stub;
	};
	std::unordered_map<std::string, StubInfo> _stubs;
};
