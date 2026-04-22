#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include "global.h"
QT_BEGIN_NAMESPACE
class QMovie;
QT_END_NAMESPACE
namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();

private:
    int _uid;
    QString _token;
    Ui::LoginDialog *ui;
    QMap<TipErr, QString> _tip_errs;
    QMap<ReqId, std::function<void(const QJsonObject&)>> _handlers;
    QMovie *_loading_movie = nullptr; // lazily created, parented to `this`
    void initHttpHandlers();
    void initHead();
    bool checkPwdValid();
    bool checkUserValid();
    bool enableBtn(bool enabled);
    void AddTipErr(TipErr te,QString tips);
    void DelTipErr(TipErr te);
    void showTip(QString str, bool b_ok);
    void showLoadingTip(QString str);
private slots:
    void slot_forget_pwd();
    // NOTE: deliberately NOT named on_login_btn_clicked — Qt's
    // connectSlotsByName (called from ui->setupUi) would auto-connect it
    // to login_btn::clicked, duplicating the explicit connect() in the
    // ctor and causing every click to fire the login twice.
    void slot_login_btn_clicked();
    void slot_login_mod_finish(ReqId id,QString res,ErrorCodes err);

    void slot_tcp_con_finish(bool bsuccess);
    void slot_login_failed(int err);
signals:
    void switchRegist();
    void switchReset();
    void sig_connect_tcp(ServerInfo);
};

#endif // LOGINDIALOG_H
