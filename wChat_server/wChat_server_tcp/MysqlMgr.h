#pragma once
#include "core.h"
#include "MysqlDao.h"
#include "Singleton.h"
#include <vector>

class MysqlMgr: public Singleton<MysqlMgr>
{
	friend class Singleton<MysqlMgr>;
public:
	~MysqlMgr();
	int RegUser(const std::string& name, const std::string& email,  const std::string& pwd);
	bool CheckEmail(const std::string& name, const std::string & email);
	bool UpdatePwd(const std::string& name, const std::string& email);
	bool CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo);
	bool AddFriendApply(const int& from, const int& to, const std::string& back, const std::string& certification);
	// 受邀方同意后修改数据库
	bool AuthFriendApply(const int& from, const int& to);
	// 添加好友
	bool AddFriend(const int& from, const int& to, std::string back_name);
	// 添加发送的消息
	bool AddMessage(const int& from, const int& to, std::string message);
	// 查询消息表
	bool GetMessages(const int& from, const int& to, Json::Value& messages);
	std::shared_ptr<UserInfo> GetUser(int uid);
	std::shared_ptr<UserInfo> GetUser(std::string name);
	std::shared_ptr<FriendInfo> GetFriendBaseInfo(int self_uid, int friend_uid);
	bool GetApplyList(int touid, std::vector<std::shared_ptr<ApplyInfo>>& applyList, int begin, int limit=10);
	bool GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo> >& user_info);
private:
	MysqlMgr();
	MysqlDao  _dao;
};

