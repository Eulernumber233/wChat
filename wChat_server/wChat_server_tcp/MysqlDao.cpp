#include "MysqlDao.h"
#include "ConfigMgr.h"

MysqlDao::MysqlDao()
{
	auto & cfg = ConfigMgr::Inst();
	const auto& host = cfg["Mysql"]["Host"];
	const auto& port = cfg["Mysql"]["Port"];
	const auto& pwd = cfg["Mysql"]["Passwd"];
	const auto& schema = cfg["Mysql"]["Schema"];
	const auto& user = cfg["Mysql"]["User"];
	pool_.reset(new MySqlPool(host+":"+port, user, pwd,schema, 5));
}

MysqlDao::~MysqlDao(){
	pool_->Close();
}

int MysqlDao::RegUser(const std::string& name, const std::string& email, const std::string& pwd)
{
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
			return false;
		}
		// ׼�����ô洢����
		std::unique_ptr < sql::PreparedStatement > stmt(con->_con->prepareStatement("CALL reg_user(?,?,?,@result)"));
		// �����������
		stmt->setString(1, name);
		stmt->setString(2, email);
		stmt->setString(3, pwd);

		// ����PreparedStatement��ֱ��֧��ע�����������������Ҫʹ�ûỰ������������������ȡ���������ֵ

		  // ִ�д洢����
		stmt->execute();
		// ����洢���������˻Ự��������������ʽ��ȡ���������ֵ�������������ִ��SELECT��ѯ����ȡ����
	   // ���磬����洢����������һ���Ự����@result���洢������������������ȡ��
	   std::unique_ptr<sql::Statement> stmtResult(con->_con->createStatement());
	  std::unique_ptr<sql::ResultSet> res(stmtResult->executeQuery("SELECT @result AS result"));
	  if (res->next()) {
	       int result = res->getInt("result");
	      std::cout << "Result: " << result << std::endl;
		  pool_->returnConnection(std::move(con));
		  return result;
	  }
	  pool_->returnConnection(std::move(con));
		return -1;
	}
	catch (sql::SQLException& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return -1;
	}
}

bool MysqlDao::CheckEmail(const std::string& name, const std::string& email) {
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
			return false;
		}

		// ׼����ѯ���
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT email FROM user WHERE name = ?"));

		// �󶨲���
		pstmt->setString(1, name);

		// ִ�в�ѯ
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

		// ���������
		while (res->next()) {
			std::cout << "Check Email: " << res->getString("email") << std::endl;
			if (email != res->getString("email")) {
				pool_->returnConnection(std::move(con));
				return false;
			}
			pool_->returnConnection(std::move(con));
			return true;
		}
		return true;
	}
	catch (sql::SQLException& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}
}

bool MysqlDao::UpdatePwd(const std::string& name, const std::string& newpwd) {
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
			return false;
		}

		// ׼����ѯ���
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("UPDATE user SET pwd = ? WHERE name = ?"));

		// �󶨲���
		pstmt->setString(2, name);
		pstmt->setString(1, newpwd);

		// ִ�и���
		int updateCount = pstmt->executeUpdate();

		std::cout << "Updated rows: " << updateCount << std::endl;
		pool_->returnConnection(std::move(con));
		return true;
	}
	catch (sql::SQLException& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}
}

bool MysqlDao::CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// ׼��SQL���
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT * FROM user WHERE name = ?"));
		pstmt->setString(1, name); // ��username�滻Ϊ��Ҫ��ѯ���û���

		// ִ�в�ѯ
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		std::string origin_pwd = "";
		// ���������
		while (res->next()) {
			origin_pwd = res->getString("pwd");
			// �����ѯ��������
			std::cout << "Password: " << origin_pwd << std::endl;
			break;
		}

		if (pwd != origin_pwd) {
			return false;
		}
		userInfo.name = name;
		userInfo.email = res->getString("email");
		userInfo.uid = res->getInt("uid");
		userInfo.pwd = origin_pwd;
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}
}

bool MysqlDao::AddFriendApply(const int& from, const int& to, const std::string& back, const std::string& certification)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// ׼��SQL���
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("INSERT INTO friend_apply (from_uid, to_uid, back ,certification) values (?,?,?,?) "
			"ON DUPLICATE KEY UPDATE from_uid = from_uid, to_uid = to_uid, back = back, certification = certification"));
		pstmt->setInt(1, from); // from id
		pstmt->setInt(2, to);
		pstmt->setString(3, back);
		pstmt->setString(4, certification);
		// ִ�и���
		int rowAffected = pstmt->executeUpdate();
		if (rowAffected < 0) {
			return false;
		}
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}


	return true;
}

bool MysqlDao::AuthFriendApply(const int& from, const int& to) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// ׼��SQL���
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("UPDATE friend_apply SET status = 1 "
			"WHERE from_uid = ? AND to_uid = ?"));
		//������������ʱfrom����֤ʱto
		pstmt->setInt(1, to); // from id
		pstmt->setInt(2, from);
		// ִ�и���
		int rowAffected = pstmt->executeUpdate();
		if (rowAffected < 0) {
			return false;
		}
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}


	return true;
}

bool MysqlDao::AddFriend(const int& from, const int& to, std::string back_to) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		if (con) {
			try { con->_con->setAutoCommit(true); } catch (...) {}
		}
		pool_->returnConnection(std::move(con));
		});

	try {
		con->_con->setAutoCommit(false);

		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("INSERT IGNORE INTO friend(self_id, friend_id, back) "
			"VALUES (?, ?, ?) "
			));
		//������������ʱfrom����֤ʱto
		pstmt->setInt(1, from); // from id
		pstmt->setInt(2, to);
		pstmt->setString(3, back_to);
		// ִ�и���
		int rowAffected = pstmt->executeUpdate();
		if (rowAffected < 0) {
			con->_con->rollback();
			return false;
		}


		// ׼���ڶ���SQL��䣬��ѯ���뷽�����������ǳ�
		std::string back_from;
		std::unique_ptr<sql::PreparedStatement> queryStmt(
			con->_con->prepareStatement("SELECT back FROM friend_apply "
				"WHERE from_uid = ? AND to_uid = ?")
		);
		queryStmt->setInt(1, to);   // from_uid
		queryStmt->setInt(2, from); // to_uid
		std::unique_ptr<sql::ResultSet> res(queryStmt->executeQuery());
		if (!res->next()) {
			return false;
		}
		back_from = res->getString("back");
		std::cout <<"---- back_to :" << back_to << "  back_from    " << back_from << "-----" << std::endl;


		//׼��������SQL��䣬�������뷽��������
		std::unique_ptr<sql::PreparedStatement> pstmt2(con->_con->prepareStatement("INSERT IGNORE INTO friend(self_id, friend_id, back) "
			"VALUES (?, ?, ?) "
		));
		//������������ʱfrom����֤ʱto
		pstmt2->setInt(1, to); // from id
		pstmt2->setInt(2, from);
		pstmt2->setString(3, back_from);
		// ִ�и���
		int rowAffected2 = pstmt2->executeUpdate();
		if (rowAffected2 < 0) {
			con->_con->rollback();
			return false;
		}
		// �ύ����
		con->_con->commit();


		//׼�����ĸ�SQL��䣬�����µĺ��ѻỰ
		int user_a = std::min(to,from);
		int user_b = std::max(to, from);
		std::unique_ptr<sql::PreparedStatement> pstmt4(con->_con->prepareStatement(
			"INSERT IGNORE INTO chat_conversations ("
			"user_a_id, user_b_id, last_msg_id, update_time"
			") VALUES (?, ?, 0, NOW())"
		));
		// �󶨲���
		pstmt4->setInt(1, user_a);   // �����Ľ�СID
		pstmt4->setInt(2, user_b);   // �����Ľϴ�ID
		// ִ�в���
		int rowAffected_4 = pstmt4->executeUpdate();
		if (rowAffected_4 < 0) {
			con->_con->rollback();
			return false;
		}
		// �ύ����
		con->_con->commit();
		

		std::cout << "------- addfriend insert friends success --------" << std::endl;
		return true;
	}
	catch (sql::SQLException& e) {
		// ����������󣬻ع�����
		if (con) {
			con->_con->rollback();
		}
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}


	return true;
}

bool MysqlDao::AddMessage(const int& from, const int& to, std::string message)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// ׼����һ��SQL��䣬��ѯ�Ựid
		std::unique_ptr<sql::PreparedStatement> pstmt_1(con->_con->prepareStatement(
			"SELECT id FROM chat_conversations WHERE user_a_id = ? AND user_b_id = ?"));
		pstmt_1->setInt(1, std::min(from,to));
		pstmt_1->setInt(2, std::max(from, to));
		// ִ�в�ѯ
		std::unique_ptr<sql::ResultSet> res(pstmt_1->executeQuery());
		// �ж��Ƿ��н��
		if (!res->next()) {
			return false;
		}
		int conversationId = res->getInt("id");


		// ׼���ڶ���SQL��䣬������Ϣ
		std::unique_ptr<sql::PreparedStatement> pstmt_2(con->_con->prepareStatement(
			"INSERT INTO chat_messages "
			"(conversation_id, sender_id, receiver_id, content, msg_type) "
			"VALUES (?, ?, ?, ?, ?)"));
		pstmt_2->setInt(1, conversationId);
		pstmt_2->setInt(2, from);
		pstmt_2->setInt(3, to);
		pstmt_2->setString(4, message);
		pstmt_2->setInt(5, 1);// Ĭ��Ϊ�ı�
		int rowAffected = pstmt_2->executeUpdate();
		if (rowAffected < 0) {
			con->_con->rollback();
			return false;
		}
		con->_con->commit();

		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}


	return true;
}

bool MysqlDao::GetMessages(const int& from, const int& to, Json::Value& obj) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// ׼����һ��SQL��䣬��ѯ�Ựid
		std::unique_ptr<sql::PreparedStatement> pstmt_1(con->_con->prepareStatement(
			"SELECT id FROM chat_conversations WHERE user_a_id = ? AND user_b_id = ?"));
		pstmt_1->setInt(1, std::min(from, to));
		pstmt_1->setInt(2, std::max(from, to));
		// ִ�в�ѯ
		std::unique_ptr<sql::ResultSet> res_1(pstmt_1->executeQuery());
		// �ж��Ƿ��н��
		if (!res_1->next()) {
			return false;
		}
		int conversationId = res_1->getInt("id");


		// ׼���ڶ���SQL��䣬��ȡ��Ϣ�б�
		std::unique_ptr<sql::PreparedStatement> pstmt_2(con->_con->prepareStatement(
			"SELECT sender_id, receiver_id, content, msg_type, status, send_time, read_time "
			"FROM chat_messages "
			"WHERE conversation_id = ? "
			"ORDER BY send_time ASC")
		);
		pstmt_2->setInt(1, conversationId);
		std::unique_ptr<sql::ResultSet> res_2(pstmt_2->executeQuery());
		// �������������ÿ����Ϣ��ʽ�������vector
		while (res_2->next()) {
			if (res_2->getInt("msg_type") != 1)continue;
			if (res_2->getInt("status") != 0)continue;
			Json::Value messages;
			messages["sender_id"] = res_2->getInt("sender_id");
			messages["receiver_id"] = res_2->getInt("receiver_id");
			std::string content = res_2->getString("content");
			messages["content"] = content;
			obj["text_array"].append(messages);
			std::cout << "read one temp messages --------------\n";
		}


		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}
}


std::shared_ptr<UserInfo> MysqlDao::GetUser(int uid)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return nullptr;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// ׼��SQL���
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT * FROM user WHERE uid = ?"));
		pstmt->setInt(1, uid); // ��uid�滻Ϊ��Ҫ��ѯ��uid

		// ִ�в�ѯ
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		std::shared_ptr<UserInfo> user_ptr = nullptr;
		// ���������
		while (res->next()) {
			user_ptr.reset(new UserInfo);
			user_ptr->pwd = res->getString("pwd");
			user_ptr->email = res->getString("email");
			user_ptr->name = res->getString("name");
			user_ptr->nick = res->getString("nick");
			user_ptr->desc = res->getString("desc");
			user_ptr->sex = res->getInt("sex");
			user_ptr->uid = res->getInt("uid");
			user_ptr->icon = res->getString("icon");
			break;
		}
		return user_ptr;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return nullptr;
	}
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(std::string name)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return nullptr;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// ׼��SQL���
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT * FROM user WHERE name = ?"));
		pstmt->setString(1, name); // ��uid�滻Ϊ��Ҫ��ѯ��uid

		// ִ�в�ѯ
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		std::shared_ptr<UserInfo> user_ptr = nullptr;
		// ���������
		while (res->next()) {
			user_ptr.reset(new UserInfo);
			user_ptr->pwd = res->getString("pwd");
			user_ptr->email = res->getString("email");
			user_ptr->name = res->getString("name");
			user_ptr->nick = res->getString("nick");
			user_ptr->desc = res->getString("desc");
			user_ptr->sex = res->getInt("sex");
			user_ptr->uid = res->getInt("uid");
			user_ptr->icon = res->getString("icon");
			break;
		}
		return user_ptr;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return nullptr;
	}
}

std::shared_ptr<FriendInfo> MysqlDao::GetFriendBaseInfo(int self_uid, int friend_uid) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return nullptr;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

	try {
		// 1. ��ѯ��ȡ������¼�������ֶ�
		std::unique_ptr<sql::PreparedStatement> queryStmt(
			con->_con->prepareStatement("SELECT * FROM friend "  // ʹ��SELECT *��ȡ�����ֶ�
				"WHERE self_id = ? AND friend_id = ?")
		);
		queryStmt->setInt(1, self_uid);
		queryStmt->setInt(2, friend_uid);

		std::unique_ptr<sql::ResultSet> res(queryStmt->executeQuery());
		if (!res->next()) {
			// û���ҵ���Ӧ�ļ�¼
			return nullptr;
		}
		std::shared_ptr<FriendInfo> ret = std::make_shared<FriendInfo>();
		// ��ʱres����������¼�������ֶΣ����Ը�����Ҫ��ȡ
		ret->back = res->getString("back");               // ��ȡback�ֶ�
		ret->friend_id = res->getInt("friend_id");
		ret->self_id = res->getInt("self_id");
		return ret;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return nullptr;
	}
}


bool MysqlDao::GetApplyList(int touid, std::vector<std::shared_ptr<ApplyInfo>>& applyList, int begin, int limit) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});

		//select apply.from_uid, apply.status, user.name, user.nick, user.sex
		//from friend_apply as apply
		//join user on apply.from_uid = user.uid
		//where apply.to_uid = ? and apply.id > ?
		//order by apply.id ASC
		//LIMIT ?

		try {
		// ׼��SQL���, ������ʼid���������������б�
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("select apply.from_uid, apply.status, user.name, "
				"user.nick, user.sex from friend_apply as apply join user on apply.from_uid = user.uid where apply.to_uid = ? "
			"and apply.id > ? order by apply.id ASC LIMIT ? "));

		pstmt->setInt(1, touid); // ��uid�滻Ϊ��Ҫ��ѯ��uid
		pstmt->setInt(2, begin); // ��ʼid
		pstmt->setInt(3, limit); //ƫ����
		// ִ�в�ѯ
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		// ���������
		while (res->next()) {	
			auto name = res->getString("name");
			auto uid = res->getInt("from_uid");
			auto status = res->getInt("status");
			auto nick = res->getString("nick");
			auto sex = res->getInt("sex");
			auto apply_ptr = std::make_shared<ApplyInfo>(uid, name, "", "", nick, sex, status);
			applyList.push_back(apply_ptr);
		}
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}
}

bool MysqlDao::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo> >& user_info_list) {

	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
		});


	try {
		// ׼��SQL���, ������ʼid���������������б�
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("select * from friend where self_id = ? "));

		pstmt->setInt(1, self_id); // ��uid�滻Ϊ��Ҫ��ѯ��uid
	
		// ִ�в�ѯ
		std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
		// ���������
		while (res->next()) {		
			auto friend_id = res->getInt("friend_id");
			auto back = res->getString("back");
			//��һ�β�ѯfriend_id��Ӧ����Ϣ
			auto user_info = GetUser(friend_id);
			if (user_info == nullptr) {
				continue;
			}

			user_info->back = back;
			std::cout << "-----name : " << user_info->name 
				<< "uid :" << user_info->uid << "icon : " << user_info->icon << "--------\n";
			user_info_list.push_back(user_info);
		}
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
	}

	return true;
}

// =====================================================================
// File operations
// =====================================================================

bool MysqlDao::RegisterFile(const std::string& file_id, int uploader_uid,
	const std::string& file_name, int64_t file_size, int file_type, const std::string& mime_type) {
	auto con = pool_->getConnection();
	if (con == nullptr) return false;
	Defer defer([this, &con]() { pool_->returnConnection(std::move(con)); });
	try {
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
			"INSERT INTO chat_files (file_id, uploader_uid, file_name, file_size, file_type, mime_type, status) "
			"VALUES (?, ?, ?, ?, ?, ?, 0)"));
		pstmt->setString(1, file_id);
		pstmt->setInt(2, uploader_uid);
		pstmt->setString(3, file_name);
		pstmt->setInt64(4, file_size);
		pstmt->setInt(5, file_type);
		pstmt->setString(6, mime_type);
		pstmt->executeUpdate();
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "MysqlDao::RegisterFile error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::UpdateFileStatus(const std::string& file_id, int status,
	const std::string& file_path, const std::string& md5) {
	auto con = pool_->getConnection();
	if (con == nullptr) return false;
	Defer defer([this, &con]() { pool_->returnConnection(std::move(con)); });
	try {
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
			"UPDATE chat_files SET status = ?, file_path = ?, md5 = ? WHERE file_id = ?"));
		pstmt->setInt(1, status);
		pstmt->setString(2, file_path);
		pstmt->setString(3, md5);
		pstmt->setString(4, file_id);
		pstmt->executeUpdate();
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "MysqlDao::UpdateFileStatus error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::AddFileMessage(int from, int to, int msg_type, const std::string& content) {
	auto con = pool_->getConnection();
	if (con == nullptr) return false;
	Defer defer([this, &con]() {
		if (con) {
			try { con->_con->setAutoCommit(true); } catch (...) {}
		}
		pool_->returnConnection(std::move(con));
	});
	try {
		con->_con->setAutoCommit(false);
		// Find or create conversation (reuse existing AddMessage pattern)
		int conversationId = -1;
		int a = std::min(from, to), b = std::max(from, to);
		std::unique_ptr<sql::PreparedStatement> pstmt_1(con->_con->prepareStatement(
			"SELECT id FROM chat_conversations WHERE user_a_id = ? AND user_b_id = ?"));
		pstmt_1->setInt(1, a);
		pstmt_1->setInt(2, b);
		std::unique_ptr<sql::ResultSet> res(pstmt_1->executeQuery());
		if (res->next()) {
			conversationId = res->getInt("id");
		}
		else {
			std::unique_ptr<sql::PreparedStatement> pstmt_c(con->_con->prepareStatement(
				"INSERT INTO chat_conversations (user_a_id, user_b_id, last_msg_id, update_time) "
				"VALUES (?, ?, 0, NOW())"));
			pstmt_c->setInt(1, a);
			pstmt_c->setInt(2, b);
			pstmt_c->executeUpdate();
			std::unique_ptr<sql::Statement> stmt(con->_con->createStatement());
			std::unique_ptr<sql::ResultSet> res2(stmt->executeQuery("SELECT LAST_INSERT_ID() AS id"));
			if (res2->next()) conversationId = res2->getInt("id");
		}
		if (conversationId < 0) {
			con->_con->rollback();
			return false;
		}
		// Insert message
		std::unique_ptr<sql::PreparedStatement> pstmt_2(con->_con->prepareStatement(
			"INSERT INTO chat_messages (conversation_id, sender_id, receiver_id, content, msg_type, send_time) "
			"VALUES (?, ?, ?, ?, ?, NOW())"));
		pstmt_2->setInt(1, conversationId);
		pstmt_2->setInt(2, from);
		pstmt_2->setInt(3, to);
		pstmt_2->setString(4, content);
		pstmt_2->setInt(5, msg_type);
		pstmt_2->executeUpdate();
		con->_con->commit();
		return true;
	}
	catch (sql::SQLException& e) {
		std::cerr << "MysqlDao::AddFileMessage error: " << e.what() << std::endl;
		try { con->_con->rollback(); } catch (...) {}
		return false;
	}
}

bool MysqlDao::GetFileInfo(const std::string& file_id,
	std::string& file_path, std::string& file_name, int64_t& file_size) {
	auto con = pool_->getConnection();
	if (con == nullptr) return false;
	Defer defer([this, &con]() { pool_->returnConnection(std::move(con)); });
	try {
		std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
			"SELECT file_path, file_name, file_size FROM chat_files WHERE file_id = ?"));
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
