#include "MysqlMgr.h"


MysqlMgr::~MysqlMgr() {

}

int MysqlMgr::RegUser(const std::string& name, const std::string& email, const std::string& pwd)
{
	return _dao.RegUser(name, email, pwd);
}

bool MysqlMgr::CheckEmail(const std::string& name, const std::string& email) {
	return _dao.CheckEmail(name, email);
}

bool MysqlMgr::UpdatePwd(const std::string& name, const std::string& pwd) {
	return _dao.UpdatePwd(name, pwd);
}

MysqlMgr::MysqlMgr() {
}

bool MysqlMgr::CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo) {
	return _dao.CheckPwd(name, pwd, userInfo);
}

bool MysqlMgr::AddFriendApply(const int& from, const int& to, const std::string& back, const std::string& certification)
{
	return _dao.AddFriendApply(from, to, back, certification);
}

bool MysqlMgr::AuthFriendApply(const int& from, const int& to) {
	return _dao.AuthFriendApply(from, to);
}

bool MysqlMgr::AddFriend(const int& from, const int& to, std::string back_name) {
	return _dao.AddFriend(from, to, back_name);
}

bool MysqlMgr::AddMessage(const int& from, const int& to, std::string message) {
	return _dao.AddMessage(from, to, message);
}

bool MysqlMgr::GetMessages(const int& from, const int& to, Json::Value& messages)
{
	return _dao.GetMessages(from, to, messages);
}

std::shared_ptr<UserInfo> MysqlMgr::GetUser(int uid)
{
	return _dao.GetUser(uid);
}

std::shared_ptr<UserInfo> MysqlMgr::GetUser(std::string name)
{
	return _dao.GetUser(name);
}

std::shared_ptr<FriendInfo> MysqlMgr::GetFriendBaseInfo(int self_uid, int friend_uid)
{
	return _dao.GetFriendBaseInfo(self_uid, friend_uid);
}

bool MysqlMgr::GetApplyList(int touid, 
	std::vector<std::shared_ptr<ApplyInfo>>& applyList, int begin, int limit) {

	return _dao.GetApplyList(touid, applyList, begin, limit);
}

bool MysqlMgr::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo> >& user_info) {
	return _dao.GetFriendList(self_id, user_info);
}

bool MysqlMgr::RegisterFile(const std::string& file_id, int uploader_uid,
	const std::string& file_name, int64_t file_size, int file_type, const std::string& mime_type) {
	return _dao.RegisterFile(file_id, uploader_uid, file_name, file_size, file_type, mime_type);
}

bool MysqlMgr::UpdateFileStatus(const std::string& file_id, int status,
	const std::string& file_path, const std::string& md5) {
	return _dao.UpdateFileStatus(file_id, status, file_path, md5);
}

bool MysqlMgr::AddFileMessage(int from, int to, int msg_type, const std::string& content) {
	return _dao.AddFileMessage(from, to, msg_type, content);
}

bool MysqlMgr::GetFileInfo(const std::string& file_id,
	std::string& file_path, std::string& file_name, int64_t& file_size) {
	return _dao.GetFileInfo(file_id, file_path, file_name, file_size);
}

