#include "ChatServerClient.h"
#include "ConfigMgr.h"
#include "RedisMgr.h"

// Keep in sync with ChatServer core.h USERIPPREFIX. We don't share headers
// across services (see CLAUDE.md §11.1), so the prefix is duplicated here.
static constexpr const char* kUserIpPrefix = "uip_";

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
	const std::string& file_path, const std::string& md5,
	int fromuid) {

	FileUploadDoneReq request;
	request.set_file_id(file_id);
	request.set_file_path(file_path);
	request.set_md5(md5);
	request.set_fromuid(fromuid);

	// 1. Precise route: look up uip_<fromuid> in Redis to find the exact
	//    ChatServer instance the sender is currently connected to.
	if (fromuid > 0) {
		std::string uip_key = std::string(kUserIpPrefix) + std::to_string(fromuid);
		std::string target_server;
		if (RedisMgr::GetInstance()->Get(uip_key, target_server) && !target_server.empty()) {
			auto it = _stubs.find(target_server);
			if (it != _stubs.end()) {
				ClientContext context;
				FileUploadDoneRsp reply;
				Status status = it->second.stub->NotifyUploadDone(&context, request, &reply);
				if (status.ok() && reply.error() == 0) {
					std::cout << "NotifyUploadDone OK via " << target_server
						<< " for file_id=" << file_id
						<< " fromuid=" << fromuid << " (routed)" << std::endl;
					return true;
				}
				std::cerr << "NotifyUploadDone routed to " << target_server
					<< " failed: " << status.error_message()
					<< " — falling back to broadcast" << std::endl;
			}
			else {
				std::cerr << "NotifyUploadDone: uip_" << fromuid << " = "
					<< target_server << " but no stub configured; falling back"
					<< std::endl;
			}
		}
		else {
			// Sender may have gone offline between upload finish and this
			// callback. Broadcast so the ChatServer that owns the receiver
			// still gets the notification and can persist + push to recv.
			std::cout << "NotifyUploadDone: no uip_" << fromuid
				<< " in Redis, broadcasting" << std::endl;
		}
	}

	// 2. Fallback: try every configured ChatServer. Any instance can still
	//    update MySQL, notify the receiver, etc. (shared Redis/MySQL).
	//    Only the "notify sender" branch silently no-ops when the picked
	//    instance isn't the one the sender is on — which is the exact bug
	//    this method's routing logic above exists to avoid.
	for (auto& [name, info] : _stubs) {
		ClientContext context;
		FileUploadDoneRsp reply;

		Status status = info.stub->NotifyUploadDone(&context, request, &reply);
		if (status.ok() && reply.error() == 0) {
			std::cout << "NotifyUploadDone OK via " << name
				<< " for file_id=" << file_id << " (fallback)" << std::endl;
			return true;
		}
		std::cerr << "NotifyUploadDone failed via " << name
			<< ": " << status.error_message() << std::endl;
	}

	std::cerr << "NotifyUploadDone: all ChatServer instances failed for file_id="
		<< file_id << std::endl;
	return false;
}
