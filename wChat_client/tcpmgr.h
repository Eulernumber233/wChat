#ifndef TCPMGR_H
#define TCPMGR_H
#include"global.h"
#include "singleton.h"
#include "userdata.h"
#include "usermgr.h"
#include <QDateTime>
// 继承QObejct可发送信号接收槽
class TcpMgr:public QObject,public Singleton<TcpMgr>,
               public std::enable_shared_from_this<TcpMgr>
{
    Q_OBJECT
public:
    ~TcpMgr();
    void CloseConnection();
    // 心跳响应相关
    qint64 GetLastHeartbeatRspMs() const { return _last_heartbeat_rsp_ms; }
    void ResetHeartbeatRsp() { _last_heartbeat_rsp_ms = QDateTime::currentMSecsSinceEpoch(); }
private:
    friend class Singleton<TcpMgr>;
    TcpMgr();
    void initHandlers();
    void handleMsg(ReqId id,int len, QByteArray data);
    QTcpSocket _socket;
    QString _host;
    uint16_t _port;
    QByteArray _buffer;
    bool _b_recv_pending;// 是否收全
    bool _kick_pending;  // 已收到踢人通知，断线时不再重复弹窗
    qint64 _last_heartbeat_rsp_ms; // 最后收到 1024 心跳回复的时间戳（毫秒）
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
    // STAGE-C: own-text send echo; lets ChatDialog persist the sent msg to LocalDb
    void sig_text_chat_msg_rsp(std::shared_ptr<TextChatMsg> msg);
    // File transfer signals
    void sig_file_upload_rsp(QString file_id, QString file_token,
                             QString host, QString port, QString local_path, int error);
    void sig_file_notify_complete(QString file_id, int error);
    // STAGE-C: enriched upload-done echo for own-side LocalDb persistence
    void sig_file_upload_persisted(std::shared_ptr<FileChatData> file_data);
    void sig_file_msg_notify(std::shared_ptr<FileChatData> file_data);

    // STAGE-C: lazy-loading history signals
    // summaries: array of {peer_uid, last_msg_db_id, last_msg_type,
    //                      last_msg_preview, last_msg_time, unread_count}
    void sig_conv_summary(QJsonArray summaries);
    // messages are returned id-DESC by the server; receiver is responsible
    // for ordering and dedupe. `peer_uid` identifies the conversation.
    void sig_pull_messages_rsp(int peer_uid, QJsonArray messages, bool has_more);
    // on-demand download token response (for opening history file messages)
    void sig_download_token_rsp(QString file_id, QString host, QString port,
                                QString token, int error);
    // 被踢下线通知
    void sig_kick_user(QString reason);
    // TCP 连接断开（非主动踢人导致）
    void sig_connection_lost();
};

#endif // TCPMGR_H
