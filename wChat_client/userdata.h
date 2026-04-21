#ifndef USERDATA_H
#define USERDATA_H

#include <QString>
#include <memory>
#include <QJsonArray>
#include "global.h"
class SearchInfo {
public:
    SearchInfo(int uid, QString name, QString nick, QString desc, int sex,QString icon)
        :_uid(uid),_name(name),_nick(nick),_desc(desc),_sex(sex),_icon(icon)
    {

    }
    int _uid;
    QString _name;
    QString _nick;
    QString _desc;
    QString _icon;
    int _sex;
};


// 存储对方发来的好友申请的好友信息
class AddFriendApply {
public:
    AddFriendApply(int from_uid, QString name, QString desc,
                   QString icon, QString nick, int sex)
        :_from_uid(from_uid),_name(name),_desc(desc),_icon(icon),_nick(nick),_sex(sex)
    {

    }
    int _from_uid;
    QString _name;
    QString _desc;
    QString _icon;
    QString _nick;
    int     _sex;
};



struct ApplyInfo {
    ApplyInfo(int uid, QString name, QString desc,
        QString icon, QString nick, int sex, int status)
        :_uid(uid),_name(name),_desc(desc),
        _icon(icon),_nick(nick),_sex(sex),_status(status){}

    ApplyInfo(std::shared_ptr<AddFriendApply> addinfo)
        :_uid(addinfo->_from_uid),_name(addinfo->_name),
          _desc(addinfo->_desc),_icon(addinfo->_icon),
          _nick(addinfo->_nick),_sex(addinfo->_sex),
          _status(0)
    {}
    void SetIcon(QString head){
        _icon = head;
    }
    int _uid;
    QString _name;
    QString _desc;
    QString _icon;
    QString _nick;
    int _sex;
    int _status;
};

struct AuthInfo {
    AuthInfo(int uid, QString name,
             QString nick, QString icon, int sex):
        _uid(uid), _name(name), _nick(nick), _icon(icon),
        _sex(sex){}
    int _uid;
    QString _name;
    QString _nick;
    QString _icon;
    int _sex;
};

struct AuthRsp {
    AuthRsp(int peer_uid, QString peer_name,
            QString peer_nick, QString peer_icon, int peer_sex)
        :_uid(peer_uid),_name(peer_name),_nick(peer_nick),
          _icon(peer_icon),_sex(peer_sex)
    {}

    int _uid;
    QString _name;
    QString _nick;
    QString _icon;
    int _sex;
};

struct TextChatData;
// 第一界面列表显示的好友
struct FriendInfo {
    FriendInfo(int uid, QString name, QString nick, QString icon,
               int sex, QString desc, QString back, QString last_msg=""):_uid(uid),
        _name(name),_nick(nick),_icon(icon),_sex(sex),_desc(desc),
        _back(back),_last_msg(last_msg){}

    FriendInfo(std::shared_ptr<AuthInfo> auth_info):_uid(auth_info->_uid),
        _nick(auth_info->_nick),_icon(auth_info->_icon),_name(auth_info->_name),
        _sex(auth_info->_sex){}

    FriendInfo(std::shared_ptr<AuthRsp> auth_rsp):_uid(auth_rsp->_uid),
        _nick(auth_rsp->_nick),_icon(auth_rsp->_icon),_name(auth_rsp->_name),
        _sex(auth_rsp->_sex){}

    void AppendChatMsgs(const std::vector<std::shared_ptr<TextChatData>> text_vec);

    int _uid;
    QString _name;
    QString _nick;
    QString _icon;
    int _sex;
    QString _desc;
    QString _back;
    QString _last_msg;
    std::vector<std::shared_ptr<TextChatData>> _chat_msgs;

    // STAGE-C: conversation summary populated by ID_PULL_CONV_SUMMARY_RSP.
    // _last_msg_db_id == 0 means "no messages yet" (fresh conversation).
    qint64  _last_msg_db_id = 0;
    int     _last_msg_type  = 0;
    qint64  _last_msg_time  = 0;   // unix seconds
    int     _unread_count   = 0;
};

struct UserInfo {
    UserInfo(int uid, QString name, QString nick, QString icon, int sex, QString last_msg = "", QString desc=""):
        _uid(uid),_name(name),_nick(nick),_icon(icon),_sex(sex),_last_msg(last_msg),_desc(desc){}

    UserInfo(std::shared_ptr<AuthInfo> auth):
        _uid(auth->_uid),_name(auth->_name),_nick(auth->_nick),
        _icon(auth->_icon),_sex(auth->_sex),_last_msg(""),_desc(""){}

    UserInfo(int uid, QString name, QString icon):
    _uid(uid), _name(name), _icon(icon),_nick(_name),
    _sex(0),_last_msg(""),_desc(""){

    }

    UserInfo(std::shared_ptr<AuthRsp> auth):
        _uid(auth->_uid),_name(auth->_name),_nick(auth->_nick),
        _icon(auth->_icon),_sex(auth->_sex),_last_msg(""){}

    UserInfo(std::shared_ptr<SearchInfo> search_info):
        _uid(search_info->_uid),_name(search_info->_name),_nick(search_info->_nick),
    _icon(search_info->_icon),_sex(search_info->_sex),_last_msg(""){

    }

    UserInfo(std::shared_ptr<FriendInfo> friend_info):
        _uid(friend_info->_uid),_name(friend_info->_name),_nick(friend_info->_nick),
        _icon(friend_info->_icon),_sex(friend_info->_sex),_last_msg(""){
            _chat_msgs = friend_info->_chat_msgs;
        }

    int _uid;
    QString _name;
    QString _nick;
    QString _icon;
    int _sex;
    QString _desc;
    QString _last_msg;
    std::vector<std::shared_ptr<TextChatData>> _chat_msgs;
};

// Unified chat message record. Despite the "Text" name (kept to avoid a
// sweeping rename in stage A), it also carries file-message fields.
// STAGE-C: replace with a proper ChatMsgData + LocalDb schema.
struct TextChatData{
    // Text constructor (existing call sites keep working)
    TextChatData(QString msg_id, QString msg_content, int fromuid, int touid)
        :_msg_id(msg_id),_msg_content(msg_content),_from_uid(fromuid),_to_uid(touid),
          _msg_type(MSG_TYPE_TEXT){}

    // File constructor (image / file / audio)
    TextChatData(QString msg_id, int msg_type, int fromuid, int touid,
                 QString file_id, QString file_name, qint64 file_size)
        :_msg_id(msg_id),_from_uid(fromuid),_to_uid(touid),_msg_type(msg_type),
          _file_id(file_id),_file_name(file_name),_file_size(file_size){}

    QString _msg_id;
    QString _msg_content;   // text body; empty for file messages
    int _from_uid;
    int _to_uid;

    // STAGE-A additions
    int _msg_type = MSG_TYPE_TEXT;  // 1=text, 2=image, 3=file, 4=audio
    int _msg_db_id = 0;             // server chat_messages.id; 0 for realtime msgs
    qint64 _send_time = 0;          // unix seconds; 0 for outgoing not-yet-persisted

    // File-message fields (only valid when _msg_type != TEXT)
    QString _file_id;
    QString _file_name;
    qint64  _file_size = 0;

    // Download routing (from LoginHandler or ID_FILE_MSG_NOTIFY)
    QString _file_host;
    QString _file_port;
    QString _file_token;

    // Local state after download completes
    QString _local_path;
    // True while a download for this msg is in flight; prevents re-issuing.
    bool _download_pending = false;

    bool IsFile() const { return _msg_type != MSG_TYPE_TEXT; }
};

struct TextChatMsg{
    TextChatMsg(int fromuid, int touid, QJsonArray arrays, qint64 msg_db_id = 0):
        _to_uid(touid),_from_uid(fromuid),_msg_db_id(msg_db_id){
        for(auto  msg_data : arrays){
            auto msg_obj = msg_data.toObject();
            auto content = msg_obj["content"].toString();
            auto msgid = msg_obj["msgid"].toString();
            auto msg_ptr = std::make_shared<TextChatData>(msgid, content,fromuid, touid);
            msg_ptr->_msg_db_id = msg_db_id;
            _chat_msgs.push_back(msg_ptr);
        }
    }
    int _to_uid;
    int _from_uid;
    qint64 _msg_db_id = 0;  // STAGE-C: 0 means "cross-server forwarded, id lost"
    std::vector<std::shared_ptr<TextChatData>> _chat_msgs;
};

// File message data (image / file / audio)
struct FileChatData {
    FileChatData(QString msg_id, QString file_id, QString file_name,
                 qint64 file_size, int file_type, int from_uid, int to_uid)
        : _msg_id(msg_id), _file_id(file_id), _file_name(file_name),
          _file_size(file_size), _file_type(file_type),
          _from_uid(from_uid), _to_uid(to_uid) {}

    QString _msg_id;
    QString _file_id;
    QString _file_name;
    qint64  _file_size;
    int     _file_type;     // 0=image, 1=file, 2=audio
    int     _from_uid;
    int     _to_uid;

    // STAGE-C: server chat_messages.id (0 for cross-server forwarded)
    qint64  _msg_db_id = 0;

    // Filled by FileMgr after download, or set immediately for sender
    QString _local_path;    // cache/files/<file_id>.<ext>

    // For download: FileServer connection info
    QString _file_host;
    QString _file_port;
    QString _file_token;
};

#endif // USERDATA_H
