#pragma once
#include "core.h"
struct UserInfo {
	UserInfo() :name(""), pwd(""), uid(0), email(""), nick(""), desc(""), sex(0), icon(""), back("") {}
	std::string name;
	std::string pwd;
	int uid;
	std::string email;
	int sex;
	std::string icon;

	std::string nick;
	std::string desc;
	std::string back;
};

struct FriendInfo
{
	int id;
	int self_id;
	int friend_id;
	std::string back;

	FriendInfo(): id(0), self_id(0), friend_id(0), back(""){}
};


struct ApplyInfo {
	ApplyInfo(int uid, std::string name, std::string certification,
		std::string icon, std::string nick, int sex, int status)
		:_uid(uid), _name(name), _certification(certification),
		_icon(icon), _nick(nick), _sex(sex), _status(status) {
	}

	int _uid;
	std::string _name;
	std::string _certification;
	std::string _icon;
	std::string _nick;
	int _sex;
	int _status;
};