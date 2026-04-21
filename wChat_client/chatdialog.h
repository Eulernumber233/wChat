#ifndef CHATDIALOG_H
#define CHATDIALOG_H

#include "global.h"
#include <QDialog>
#include <QMouseEvent>
#include <QTimer>
#include "statewidget.h"
#include "userdata.h"
#include "chatuserwid.h"
#include "loadingdlg.h"
namespace Ui {
class ChatDialog;
}

class ChatDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChatDialog(QWidget *parent = nullptr);
    ~ChatDialog();
    void addChatUserList();
protected:
    bool eventFilter(QObject*watched,QEvent* event)override;
    void handleGlobalMousePress(QMouseEvent*event);
private:
    Ui::ChatDialog *ui;
    ChatUIMode _mode;
    ChatUIMode _state;
    bool _b_loading;
    QList<StateWidget*> _lb_list;//导航栏
    QMap<int,QListWidgetItem*>_chat_items_added;
    int _cur_chat_uid;
    QWidget* _last_widget;
    // STAGE-C: file_id → source local path for in-flight uploads. Populated
    // in slot_file_upload_rsp, consumed in slot_file_upload_persisted to
    // copy the source into the per-user cache and mirror to LocalDb.
    QMap<QString, QString> _pending_file_copies;
    QTimer* _heartbeat_timer;
    void ShowSearch(bool bsearch=false);
    void AddLBGroup(StateWidget*lb);
    void ClearLabelState(StateWidget *lb);// 切换导航栏时清理其他导航栏的资源
    void SetSelectChatItem(int uid = 0);
    void SetSelectChatPage(int uid = 0);
    void loadMoreChatUser();
    void loadMoreConUser();
    void UpdateChatMsg(std::vector<std::shared_ptr<TextChatData>>msgdata);
    void sendHeartbeat();
private slots:
    void slot_show_search(bool show);
    void slot_loading_chat_user();//会话列表栏增加信号
    void slot_loading_contact_user();//联系人列表栏增加信号
    void slot_side_chat();
    void slot_side_contact();
    void slot_text_changed(const QString &str);
    void slot_append_send_chat_msg(std::shared_ptr<TextChatData>msgdata);
public slots:
    void slot_apply_friend(std::shared_ptr<AddFriendApply>apply);
    void slot_add_auth_friend(std::shared_ptr<AuthInfo>auth_info);
    void slot_auth_rsp(std::shared_ptr<AuthRsp>auth_rsp);

    void slot_friend_info_page(std::shared_ptr<UserInfo> user_info);
    void slot_switch_apply_friend_page();
    void slot_jump_chat_item(std::shared_ptr<SearchInfo>si);//从会话列表栏跳转到聊天窗口
    void slot_jump_chat_item_from_infopage(std::shared_ptr<UserInfo> user_info);//从联系人列表跳转到聊天窗口
    void slot_item_clicked(QListWidgetItem*item);
    void slot_text_chat_msg(std::shared_ptr<TextChatMsg>msg);
    // STAGE-C: own-text send echo (persists outgoing to LocalDb)
    void slot_text_chat_msg_rsp(std::shared_ptr<TextChatMsg> msg);
    // File transfer slots
    void slot_file_upload_rsp(QString file_id, QString file_token,
                              QString host, QString port, QString local_path, int error);
    void slot_file_msg_notify(std::shared_ptr<FileChatData> file_data);
    // STAGE-C: sender-side mirror after ChatServer confirmed upload done.
    void slot_file_upload_persisted(std::shared_ptr<FileChatData> file_data);

    // STAGE-C: conversation summary received after login
    void slot_conv_summary(QJsonArray summaries);
    // STAGE-C: paged history for one peer (response to ID_PULL_MESSAGES_REQ)
    void slot_pull_messages_rsp(int peer_uid, QJsonArray messages, bool has_more);
    // STAGE-C: download token received (for on-demand history file loads)
    void slot_download_token_rsp(QString file_id, QString host, QString port,
                                  QString token, int error);
};

static void PersistTextMsgToLocalDb(const std::shared_ptr<TextChatMsg>& msg);
#endif // CHATDIALOG_H
