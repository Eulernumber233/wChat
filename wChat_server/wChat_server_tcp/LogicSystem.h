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
	void LoginHandler(std::shared_ptr<CSession> session, const short &msg_id, const std::string &msg_data);
	void SearchInfo(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
	// 发送添加好友申请
	void AddFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
	// 同意对方申请
	void AuthFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
	// 接收消息
	void DealChatTextMsg(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
	void HeartBeatHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data);
	bool isPureDigit(const std::string& str);
	void GetUserByUid(std::string uid_str, Json::Value& rtvalue);
	void GetUserByName(std::string name, Json::Value& rtvalue);
	// 获取用户基础信息
	bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo> &userinfo);
	// 获取好友申请列表
	bool GetFriendApplyInfo(int to_uid, std::vector<std::shared_ptr<ApplyInfo>>& list);
	// 获取好友列表
	bool GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>> & user_list);
	std::thread _worker_thread;
	std::queue<std::shared_ptr<LogicNode>> _msg_que;
	std::mutex _mutex;
	std::condition_variable _consume;
	bool _b_stop;
	std::map<short, FunCallBack> _fun_callbacks;
	std::shared_ptr<CServer> _p_server;
};

