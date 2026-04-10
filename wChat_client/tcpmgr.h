#ifndef TCPMGR_H
#define TCPMGR_H
#include"global.h"
#include "singleton.h"
#include "userdata.h"
#include "usermgr.h"
// 继承QObejct可发送信号接收槽
class TcpMgr:public QObject,public Singleton<TcpMgr>,
               public std::enable_shared_from_this<TcpMgr>
{
    Q_OBJECT
public:
    ~TcpMgr();
private:
    friend class Singleton<TcpMgr>;
    TcpMgr();
    void initHandlers();
    void CloseConnection();
    void handleMsg(ReqId id,int len, QByteArray data);
    QTcpSocket _socket;
    QString _host;
    uint16_t _port;
    QByteArray _buffer;
    bool _b_recv_pending;// 是否收全
    quint16 _message_id;
    quint16 _message_len;
    QMap<ReqId,std::function<void(ReqId id,int len, QByteArray data)>>_handlers;
public slots:
    void slot_tcp_connect(ServerInfo);
    void slot_send_data(ReqId reqId, QByteArray data);
    void slot_read_data();
signals:
    void sig_con_success(bool bsuccess);
    void sig_send_data(ReqId reqId, QByteArray data);
//    void sig_swich_chatdlg();
    void sig_login_failed(int);
    void sig_switch_chatdlg();
    void sig_user_search(std::shared_ptr<SearchInfo> si);//通知SearchList弹出搜索结果窗口

    void sig_friend_apply(std::shared_ptr<AddFriendApply>);
    void sig_add_auth_friend(std::shared_ptr<AuthInfo>);
    void sig_auth_rsp(std::shared_ptr<AuthRsp>);
    void sig_text_chat_msg(std::shared_ptr<TextChatMsg>msg);
    // File transfer signals
    void sig_file_upload_rsp(QString file_id, QString file_token,
                             QString host, QString port, QString local_path, int error);
    void sig_file_notify_complete(QString file_id, int error);
    void sig_file_msg_notify(std::shared_ptr<FileChatData> file_data);
};

#endif // TCPMGR_H
