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
    void AppendChatMsg(std::shared_ptr<TextChatData> msg);
    void AppendImageBubble(const QString& image_path, ChatRole role,
                           const QString& name, const QString& icon);
    QString PopPendingFilePath();
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
