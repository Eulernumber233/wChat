#pragma once
#include "Singleton.h"
#include "CSession.h"
#include "core.h"
#include "data.h"

class CServer;
typedef  std::function<void(std::shared_ptr<CSession>, const short &msg_id, const std::string &msg_data)> FunCallBack;
class LogicSystem:public Singleton<LogicSystem>
{
	friend class Singleton<LogicSystem>;
public:
	~LogicSystem();
	void PostMsgToQue(std::shared_ptr < LogicNode> msg);
	void SetServer(std::shared_ptr<CServer> pserver);
private:
	LogicSystem();
	void DealMsg();
	void RegisterCallBacks();
	// 校验 session 是否仍然合法（被动踢除兜底）
	bool ValidateSession(std::shared_ptr<CSession> session);
	void LoginHandler(std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data);
	void SearchInfo(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
	void AddFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
	void AuthFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
	void DealChatTextMsg(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
	void HeartBeatHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
	void FileUploadReqHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
	// STAGE-C: lazy-loading history
	void PullConvSummaryHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
	void PullMessagesHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
	void GetDownloadTokenHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
public:
	// Called by FileServiceImpl when FileServer reports upload done.
	// rpc_fromuid is the sender uid carried by the gRPC request (added so
	// FileServer can route this callback to the exact ChatServer instance
	// where the sender is connected); we cross-check it against the Redis
	// file_meta:<file_id> entry. Pass 0 if unavailable / legacy callers.
	void HandleFileUploadDone(const std::string& file_id,
		const std::string& file_path, const std::string& md5,
		int rpc_fromuid = 0);
	bool isPureDigit(const std::string& str);
	void GetUserByUid(std::string uid_str, Json::Value& rtvalue);
	void GetUserByName(std::string name, Json::Value& rtvalue);
	bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo> &userinfo);
	bool GetFriendApplyInfo(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& list);
	bool GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>> & user_list);
	std::thread _worker_thread;
	std::queue<std::shared_ptr<LogicNode>> _msg_que;
	std::mutex _mutex;
	std::condition_variable _consume;
	bool _b_stop;
	std::map<short, FunCallBack> _fun_callbacks;
	std::shared_ptr<CServer> _p_server;
};

