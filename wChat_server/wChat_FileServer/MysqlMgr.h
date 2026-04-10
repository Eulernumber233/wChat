#pragma once
#include "core.h"
#include "MysqlDao.h"
#include "Singleton.h"

class MysqlMgr : public Singleton<MysqlMgr> {
	friend class Singleton<MysqlMgr>;
public:
	~MysqlMgr();

	bool UpdateFileStatus(const std::string& file_id, int status);
	bool UpdateFileComplete(const std::string& file_id,
		const std::string& file_path, const std::string& md5);
	bool GetFileInfo(const std::string& file_id,
		std::string& file_path, std::string& file_name, int64_t& file_size);

private:
	MysqlMgr();
	MysqlDao _dao;
};
