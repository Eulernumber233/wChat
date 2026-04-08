#pragma once
#include "core.h"
#include "MysqlDao.h"
class MysqlMgr : public Singleton<MysqlMgr>
{
    friend class Singleton<MysqlMgr>;
public:
    ~MysqlMgr();
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
    bool CheckEmail(const std::string& email) { return _dao.CheckEmail(email); }
    bool UpdatePwd(const std::string& name, const std::string& pwd) { return _dao.UpdatePwd(name, pwd); }
    bool CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo);
private:
    MysqlMgr();
    MysqlDao  _dao;
};

