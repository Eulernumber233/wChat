#ifndef USERMGR_H
#define USERMGR_H
#include <QObject>
#include <memory>
#include <singleton.h>
#include "userdata.h"
#include <vector>
#include <QJsonArray>

class UserMgr:public QObject,public Singleton<UserMgr>,
                public std::enable_shared_from_this<UserMgr>
{
    Q_OBJECT
public:
    friend class Singleton<UserMgr>;
    ~ UserMgr();
    void SetToken(QString token);
    int GetUid();
    QString GetName();
    std::vector<std::shared_ptr<ApplyInfo>>GetApplyList();
    std::shared_ptr<UserInfo> GetUserInfo();
    bool AlreadyApply(int uid);
    void AddApplyList(std::shared_ptr<ApplyInfo>app);
    void SetUserInfo(std::shared_ptr<UserInfo>user_info);
    void AppendApplyList(QJsonArray array);
    bool CheckFriendById(int uid);
    void AddFriend(std::shared_ptr<AuthRsp> auth_rsp);
    void AddFriend(std::shared_ptr<AuthInfo> auth_info);
    std::shared_ptr<FriendInfo> GetFriendById(int uid);
    void AppendFriendList(QJsonArray array);// 获取第一界面好友列表
    // STAGE-C: merge conv summary array (from ID_PULL_CONV_SUMMARY_RSP) into
    // _friend_map entries. Creates entries for peers that aren't yet in the
    // friend list? No — conversations always exist for a friend already in
    // the list, so unknown peer_uids are just dropped with a warning.
    void ApplyConvSummaries(const QJsonArray& summaries);
    void AppendFriendChatMsg(int friend_id,std::vector<std::shared_ptr<TextChatData>>msgs);
    std::vector<std::shared_ptr<FriendInfo>> GetChatListPerPage();
    bool IsLoadChatFin();
    void UpdateChatLoadedCount();
    std::vector<std::shared_ptr<FriendInfo>> GetConListPerPage();
    void UpdateContactLoadedCount();
    bool IsLoadConFin();
    void Reset(); // 登出/被踢时清空所有缓存状态
private:
    UserMgr();
    QString _token;
    std::vector<std::shared_ptr<ApplyInfo>>_apply_list;//申请列表
    std::shared_ptr<UserInfo> _user_info;
    QMap<int,std::shared_ptr<FriendInfo>>_friend_map;
    std::vector<std::shared_ptr<FriendInfo>>_friend_list;
    int _chat_loaded;
    int _contact_loaded;
};
#endif // USERMGR_H
