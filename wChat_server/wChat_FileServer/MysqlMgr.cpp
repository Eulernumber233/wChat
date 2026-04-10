#include "MysqlMgr.h"

MysqlMgr::MysqlMgr() {}
MysqlMgr::~MysqlMgr() {}

bool MysqlMgr::UpdateFileStatus(const std::string& file_id, int status) {
	return _dao.UpdateFileStatus(file_id, status);
}

bool MysqlMgr::UpdateFileComplete(const std::string& file_id,
	const std::string& file_path, const std::string& md5) {
	return _dao.UpdateFileComplete(file_id, file_path, md5);
}

bool MysqlMgr::GetFileInfo(const std::string& file_id,
	std::string& file_path, std::string& file_name, int64_t& file_size) {
	return _dao.GetFileInfo(file_id, file_path, file_name, file_size);
}
