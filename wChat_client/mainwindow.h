#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "logindialog.h"
#include "registdialog.h"
#include "resetdialog.h"
#include "chatdialog.h"
#include "tcpmgr.h"
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
public slots:
    void slotSwitchRegist();// 登录窗口切换注册窗口
    void slotSwitchLogin(); // 注册窗口返回登录窗口
    void slotSwitchReset(); // 登录窗口切换找回密码窗口
    void slotSwitchLogin2();// 找回密码窗口返回登录窗口
    void slotSwitchchat();
    void slotBackToLogin(QString reason); // 从聊天界面回到登录（被踢/断线）
private:
    Ui::MainWindow *ui = nullptr;
    LoginDialog* _login_dlg = nullptr;
    RegistDialog* _regist_dlg = nullptr;
    ResetDialog* _reset_dlg = nullptr;
    ChatDialog* _chat_dlg = nullptr;
    bool _switching_to_login = false; // 防止 slotBackToLogin 重入
};
#endif // MAINWINDOW_H
