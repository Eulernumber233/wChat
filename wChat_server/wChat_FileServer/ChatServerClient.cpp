#include "ChatServerClient.h"
#include "ConfigMgr.h"

ChatServerClient::ChatServerClient() {}

ChatServerClient::~ChatServerClient() {}

void ChatServerClient::Init() {
	auto& cfg = ConfigMgr::Inst();

	// Read ChatServer list from config [ChatServer] section
	// Format:
	//   [ChatServer]
	//   Servers = chatserver1,chatserver2
	//   [chatserver1]
	//   Host = 127.0.0.1
	//   RPCPort = 50055
	std::string servers_str = cfg["ChatServer"]["Servers"];
	if (servers_str.empty()) {
		std::cerr << "ChatServerClient::Init: no [ChatServer] Servers configured" << std::endl;
		return;
	}

	// Parse comma-separated server names
	std::vector<std::string> server_names;
	std::stringstream ss(servers_str);
	std::string name;
	while (std::getline(ss, name, ',')) {
		// Trim whitespace
		size_t start = name.find_first_not_of(" \t");
		size_t end = name.find_last_not_of(" \t");
		if (start != std::string::npos) {
			server_names.push_back(name.substr(start, end - start + 1));
		}
	}

	for (auto& sname : server_names) {
		std::string host = cfg[sname]["Host"];
		std::string rpc_port = cfg[sname]["RPCPort"];
		if (host.empty() || rpc_port.empty()) {
			std::cerr << "ChatServerClient: missing Host/RPCPort for [" << sname << "]" << std::endl;
			continue;
		}

		std::string addr = host + ":" + rpc_port;
		StubInfo info;
		info.channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
		info.stub = FileService::NewStub(info.channel);
		_stubs[sname] = std::move(info);
		std::cout << "ChatServerClient: connected to " << sname << " at " << addr << std::endl;
	}
}

bool ChatServerClient::NotifyUploadDone(const std::string& file_id,
	const std::string& file_path, const std::string& md5) {

	FileUploadDoneReq request;
	request.set_file_id(file_id);
	request.set_file_path(file_path);
	request.set_md5(md5);

	// Try each ChatServer until one succeeds.
	// In practice, any instance can handle the notification since they share
	// the same Redis/MySQL. We just need ONE to process it.
	for (auto& [name, info] : _stubs) {
		ClientContext context;
		FileUploadDoneRsp reply;

		Status status = info.stub->NotifyUploadDone(&context, request, &reply);
		if (status.ok() && reply.error() == 0) {
			std::cout << "NotifyUploadDone OK via " << name
				<< " for file_id=" << file_id << std::endl;
			return true;
		}
		else {
			std::cerr << "NotifyUploadDone failed via " << name
				<< ": " << status.error_message() << std::endl;
		}
	}

	std::cerr << "NotifyUploadDone: all ChatServer instances failed for file_id="
		<< file_id << std::endl;
	return false;
}
