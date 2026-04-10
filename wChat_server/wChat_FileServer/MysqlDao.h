#pragma once
#include "core.h"

class MySqlPool {
public:
	MySqlPool(const std::string& url, const std::string& user,
		const std::string& pass, const std::string& schema, int poolSize);
	~MySqlPool();
	std::unique_ptr<sql::Connection> getConnection();
	void returnConnection(std::unique_ptr<sql::Connection> con);
private:
	std::string url_;
	std::string user_;
	std::string pass_;
	std::string schema_;
	int poolSize_;
	std::queue<std::unique_ptr<sql::Connection>> pool_;
	std::mutex mutex_;
	std::condition_variable cond_;
};

struct SqlConnection {
	SqlConnection(std::unique_ptr<sql::Connection> con, MySqlPool* pool)
		: _con(std::move(con)), _pool(pool) {}
	~SqlConnection() {
		if (_con) _pool->returnConnection(std::move(_con));
	}
	std::unique_ptr<sql::Connection> _con;
	MySqlPool* _pool;
};

class MysqlDao {
public:
	MysqlDao();
	~MysqlDao();

	// Update file status (registered -> uploading -> uploaded)
	bool UpdateFileStatus(const std::string& file_id, int status);

	// Update file as complete (status=2, fill file_path + md5)
	bool UpdateFileComplete(const std::string& file_id,
		const std::string& file_path, const std::string& md5);

	// Get file info for download
	bool GetFileInfo(const std::string& file_id,
		std::string& file_path, std::string& file_name, int64_t& file_size);

private:
	std::unique_ptr<MySqlPool> _pool;
};
