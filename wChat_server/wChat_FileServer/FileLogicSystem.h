#pragma once
#include "Singleton.h"
#include "core.h"

class FileSession;

// A task posted from network thread to logic thread
struct FileTask {
	std::shared_ptr<FileSession> session;
	short msg_id;
	int64_t offset;
	std::vector<char> data;   // payload (may be JSON or binary)
};

typedef std::function<void(std::shared_ptr<FileSession>, short msg_id,
	int64_t offset, const char* data, uint32_t len)> FileFunCallBack;

class FileLogicSystem : public Singleton<FileLogicSystem> {
	friend class Singleton<FileLogicSystem>;
public:
	~FileLogicSystem();

	// Called by FileSession (network thread) to post a received message
	void PostMsg(std::shared_ptr<FileSession> session, short msg_id,
		int64_t offset, const char* data, uint32_t len);

private:
	FileLogicSystem();
	void DealMsg();
	void RegisterCallBacks();

	// Handlers
	void HandleAuthReq(std::shared_ptr<FileSession> session, short msg_id,
		int64_t offset, const char* data, uint32_t len);
	void HandleData(std::shared_ptr<FileSession> session, short msg_id,
		int64_t offset, const char* data, uint32_t len);

	std::thread _worker_thread;
	std::queue<std::shared_ptr<FileTask>> _msg_que;
	std::mutex _mutex;
	std::condition_variable _consume;
	bool _b_stop;
	std::map<short, FileFunCallBack> _fun_callbacks;
};
