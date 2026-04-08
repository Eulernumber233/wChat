#ifndef CHATUSERLIST_H
#define CHATUSERLIST_H
#include "global.h"

class ChatUserList: public QListWidget
{
    Q_OBJECT
public:
    ChatUserList(QWidget *parent = nullptr);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool _load_pending;
signals:
    void sig_loading_chat_user();
};
#endif // CHATUSERLIST_H
