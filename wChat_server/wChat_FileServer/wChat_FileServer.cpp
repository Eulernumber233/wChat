#include "core.h"
#include "FileLogicSystem.h"
#include "AsioIOContextPool.h"
#include "CServer.h"
#include "ConfigMgr.h"
#include "RedisMgr.h"
#include "FileStorage.h"
#include "ChatServerClient.h"

int main(int argc, char* argv[])
{
	try {
		if (argc > 1) {
			ConfigMgr::SetConfigPath(argv[1]);
		}

		auto& cfg = ConfigMgr::Inst();
		auto server_name = cfg["SelfServer"]["Name"];
		auto storage_path = cfg["SelfServer"]["StoragePath"];

		// Initialize file storage
		FileStorage::GetInstance()->Init(storage_path);

		// Initialize gRPC client to ChatServer
		ChatServerClient::GetInstance()->Init();

		// Initialize IO thread pool
		auto pool = AsioIOContextPool::GetInstance();

		// Start TCP listener
		boost::asio::io_context io_context;
		boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
		signals.async_wait([&io_context, pool](auto, auto) {
			io_context.stop();
			pool->Stop();
		});

		auto port_str = cfg["SelfServer"]["Port"];
		CServer s(io_context, atoi(port_str.c_str()));
		std::cout << "FileServer [" << server_name << "] running on port " << port_str << std::endl;

		io_context.run();

		RedisMgr::GetInstance()->Close();
	}
	catch (std::exception& e) {
		std::cerr << "FileServer Exception: " << e.what() << std::endl;
	}

	return 0;
}
