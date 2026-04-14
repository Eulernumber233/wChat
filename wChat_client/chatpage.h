#ifndef CHATPAGE_H
#define CHATPAGE_H

#include <QWidget>
#include <QStyleOption>
#include <QPainter>
#include <QFileInfo>
#include <QQueue>
#include "userdata.h"
namespace Ui {
class ChatPage;
}

class ChatPage : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPage(QWidget *parent = nullptr);
    ~ChatPage();
    void SetUserInfo(std::shared_ptr<UserInfo> user_info);
    // STAGE-C: re-render the current conversation from LocalDb WITHOUT
    // triggering a new ID_PULL_MESSAGES_REQ. Used by ChatDialog after
    // a pull response is written to the DB, to avoid a pull → render →
    // pull → render infinite loop.
    void RefreshFromLocalDb();
    void AppendChatMsg(std::shared_ptr<TextChatData> msg);
    void AppendImageBubble(const QString& image_path, ChatRole role,
                           const QString& name, const QString& icon);
    QString PopPendingFilePath();

    // STAGE-C: accessor used by ChatDialog when routing pulled messages
    // to the currently-open conversation.
    int CurrentPeerUid() const {
        return _user_info ? _user_info->_uid : 0;
    }
protected:
    void paintEvent(QPaintEvent *event);

private slots:
    void on_send_btn_clicked();
signals:
    void sig_append_send_chat_msg(std::shared_ptr<TextChatData>);
private:
    Ui::ChatPage *ui;
    std::shared_ptr<UserInfo> _user_info;
    QMap<QString, QWidget*>  _bubble_map;
    QQueue<QString>          _pending_file_paths; // FIFO queue of local_paths
};

#endif // CHATPAGE_H
