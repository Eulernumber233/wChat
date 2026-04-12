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
	// ������ͬ����޸����ݿ�
	bool AuthFriendApply(const int& from, const int& to);
	// ���Ӻ���
	bool AddFriend(const int& from, const int& to, std::string back_name);
	// ���ӷ��͵���Ϣ; returns chat_messages.id on success, 0 on failure.
	int AddMessage(const int& from, const int& to, std::string message);
	// ��ѯ��Ϣ��
	bool GetMessages(const int& from, const int& to, Json::Value& messages);

	// STAGE-C: lazy-loading history support
	bool GetConvSummaries(int self_uid, Json::Value& out);
	bool GetMessagesPage(int self_uid, int peer_uid, int64_t before_msg_db_id,
		int limit, Json::Value& messages_out, bool& has_more_out);
	bool UserCanAccessFile(int uid, const std::string& file_id);
	std::shared_ptr<UserInfo> GetUser(int uid);
	std::shared_ptr<UserInfo> GetUser(std::string name);
	std::shared_ptr<FriendInfo> GetFriendBaseInfo(int self_uid, int friend_uid);
	bool GetApplyList(int touid, std::vector<std::shared_ptr<ApplyInfo>>& applyList, int begin, int limit=10);
	bool GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo> >& user_info);
	// File operations
	bool RegisterFile(const std::string& file_id, int uploader_uid,
		const std::string& file_name, int64_t file_size, int file_type, const std::string& mime_type);
	bool UpdateFileStatus(const std::string& file_id, int status,
		const std::string& file_path = "", const std::string& md5 = "");
	int AddFileMessage(int from, int to, int msg_type, const std::string& content);
	bool GetFileInfo(const std::string& file_id,
		std::string& file_path, std::string& file_name, int64_t& file_size);
private:
	MysqlMgr();
	MysqlDao  _dao;
};

