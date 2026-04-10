#include "MysqlDao.h"
#include "ConfigMgr.h"

// =====================================================================
// MySqlPool
// =====================================================================

MySqlPool::MySqlPool(const std::string& url, const std::string& user,
	const std::string& pass, const std::string& schema, int poolSize)
	: url_(url), user_(user), pass_(pass), schema_(schema), poolSize_(poolSize) {
	for (int i = 0; i < poolSize_; ++i) {
		sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
		auto con = std::unique_ptr<sql::Connection>(driver->connect(url_, user_, pass_));
		con->setSchema(schema_);
		pool_.push(std::move(con));
	}
}

MySqlPool::~MySqlPool() {
	std::lock_guard<std::mutex> lock(mutex_);
	while (!pool_.empty()) {
		pool_.pop();
	}
}

std::unique_ptr<sql::Connection> MySqlPool::getConnection() {
	std::unique_lock<std::mutex> lock(mutex_);
	cond_.wait(lock, [this]() { return !pool_.empty(); });
	auto con = std::move(pool_.front());
	pool_.pop();
	// Ensure clean state: reset autoCommit in case a previous user left it off
	try { con->setAutoCommit(true); } catch (...) {}
	return con;
}

void MySqlPool::returnConnection(std::unique_ptr<sql::Connection> con) {
	std::lock_guard<std::mutex> lock(mutex_);
	pool_.push(std::move(con));
	cond_.notify_one();
}

// =====================================================================
// MysqlDao
// =====================================================================

MysqlDao::MysqlDao() {
	auto& cfg = ConfigMgr::Inst();
	std::string host = cfg["Mysql"]["Host"];
	std::string port = cfg["Mysql"]["Port"];
	std::string user = cfg["Mysql"]["User"];
	std::string pwd = cfg["Mysql"]["Passwd"];
	std::string schema = cfg["Mysql"]["Schema"];
	_pool = std::make_unique<MySqlPool>(
		"tcp://" + host + ":" + port, user, pwd, schema, 3);
}

MysqlDao::~MysqlDao() {}

bool MysqlDao::UpdateFileStatus(const std::string& file_id, int status) {
	auto conn = std::make_shared<SqlConnection>(_pool->getConnection(), _pool.get());
	try {
		std::unique_ptr<sql::PreparedStatement> pstmt(
			conn->_con->prepareStatement(
				"UPDATE chat_files SET status = ? WHERE file_id = ?"));
		pstmt->setInt(1, status);
		pstmt->setString(2, file_id);
		pstmt->executeUpdate();
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "MysqlDao::UpdateFileStatus error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::UpdateFileComplete(const std::string& file_id,
	const std::string& file_path, const std::string& md5) {
	auto conn = std::make_shared<SqlConnection>(_pool->getConnection(), _pool.get());
	try {
		std::unique_ptr<sql::PreparedStatement> pstmt(
			conn->_con->prepareStatement(
				"UPDATE chat_files SET status = 2, file_path = ?, md5 = ? WHERE file_id = ?"));
		pstmt->setString(1, file_path);
		pstmt->setString(2, md5);
		pstmt->setString(3, file_id);
		pstmt->executeUpdate();
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "MysqlDao::UpdateFileComplete error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::GetFileInfo(const std::string& file_id,
	std::string& file_path, std::string& file_name, int64_t& file_size) {
	auto conn = std::make_shared<SqlConnection>(_pool->getConnection(), _pool.get());
	try {
		std::unique_ptr<sql::PreparedStatement> pstmt(
			conn->_con->prepareStatement(
				"SELECT file_path, file_name, file_size FROM chat_files "
				"WHERE file_id = ?"));
		pstmt->setString(1, file_id);
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		if (res->next()) {
			file_path = res->getString("file_path");
			file_name = res->getString("file_name");
			file_size = res->getInt64("file_size");
			return true;
		}
		return false;
	}
	catch (sql::SQLException& e) {
		std::cerr << "MysqlDao::GetFileInfo error: " << e.what() << std::endl;
		return false;
	}
}
