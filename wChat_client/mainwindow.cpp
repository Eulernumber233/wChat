#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    _login_dlg = new LoginDialog(this);
    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint|Qt::FramelessWindowHint);
    setCentralWidget(_login_dlg);
    //_login_dlg->show();

    connect(_login_dlg,&::LoginDialog::switchRegist,this,&MainWindow::slotSwitchRegist);
    connect(_login_dlg, &LoginDialog::switchReset, this, &MainWindow::slotSwitchReset);
    connect(TcpMgr::GetInstance().get(),&TcpMgr::sig_switch_chatdlg,this,&MainWindow::slotSwitchchat);

    // 被踢下线 → 弹窗 + 回登录界面
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_kick_user, this, [this](QString reason) {
        slotBackToLogin(QString::fromUtf8("您的账号在其他设备登录，当前设备已被迫下线。"));
    });

    // TCP 断线（非踢人）→ 弹窗 + 回登录界面
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_connection_lost, this, [this]() {
        slotBackToLogin(QString::fromUtf8("与服务器断开连接，请重新登录。"));
    });

    // test
    //emit TcpMgr::GetInstance()->sig_switch_chatdlg();
}

MainWindow::~MainWindow()
{
    // All dialogs are created with 'this' as parent,
    // Qt's parent-child mechanism deletes them automatically.
    delete ui;
}

void MainWindow::slotSwitchRegist()
{
    _regist_dlg =new RegistDialog(this);
    _regist_dlg->setWindowFlags(Qt::CustomizeWindowHint|Qt::FramelessWindowHint);
//    _regist_dlg->hide();
    //注册界面返回登录界面
    connect(_regist_dlg,&RegistDialog::sigSwitchLogin,this,&MainWindow::slotSwitchLogin);

    setCentralWidget(_regist_dlg);
    _login_dlg->hide();
    _regist_dlg->show();
}

//从注册界面返回登录界面
void MainWindow::slotSwitchLogin()
{
    //创建一个CentralWidget, 并将其设置为MainWindow的中心部件
    _login_dlg = new LoginDialog(this);
    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint|Qt::FramelessWindowHint);
    setCentralWidget(_login_dlg);

    _regist_dlg->hide();
    _login_dlg->show();
    //连接登录界面忘记密码信号
    connect(_login_dlg, &LoginDialog::switchReset, this, &MainWindow::slotSwitchReset);
    //连接登录界面注册信号
    connect(_login_dlg, &LoginDialog::switchRegist, this, &MainWindow::slotSwitchRegist);


}

// 忘记密码
void MainWindow::slotSwitchReset()
{
    //创建一个CentralWidget, 并将其设置为MainWindow的中心部件
    _reset_dlg = new ResetDialog(this);
    _reset_dlg->setWindowFlags(Qt::CustomizeWindowHint|Qt::FramelessWindowHint);
    setCentralWidget(_reset_dlg);

    _login_dlg->hide();
    _reset_dlg->show();
    //注册返回登录信号和槽函数
    connect(_reset_dlg, &ResetDialog::switchLogin, this, &MainWindow::slotSwitchLogin2);
}

//从重置界面返回登录界面
void MainWindow::slotSwitchLogin2()
{
    //创建一个CentralWidget, 并将其设置为MainWindow的中心部件
    _login_dlg = new LoginDialog(this);
    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint|Qt::FramelessWindowHint);
    setCentralWidget(_login_dlg);

    _reset_dlg->hide();
    _login_dlg->show();
    //连接登录界面忘记密码信号
    connect(_login_dlg, &LoginDialog::switchReset, this, &MainWindow::slotSwitchReset);
    //连接登录界面注册信号
    connect(_login_dlg, &LoginDialog::switchRegist, this, &MainWindow::slotSwitchRegist);
}

void MainWindow::slotSwitchchat()
{
    _chat_dlg =new ChatDialog();
    _chat_dlg->setWindowFlags(Qt::CustomizeWindowHint|Qt::FramelessWindowHint);
    setCentralWidget(_chat_dlg);
    _chat_dlg->show();
    _login_dlg->hide();
    this->setMinimumSize(QSize(1050,900));
    this->setMaximumSize(QSize(1050,900));
}

void MainWindow::slotBackToLogin(QString reason)
{
    // 防止重入：sig_kick_user 和 sig_connection_lost 可能同时或先后触发，
    // 而且 CloseConnection() 本身会触发 disconnected 信号导致递归调用
    if (_switching_to_login || !_chat_dlg) {
        return;
    }
    _switching_to_login = true;

    // 关闭 TCP 连接（会触发 disconnected，但上面的 flag 挡住了重入）
    TcpMgr::GetInstance()->CloseConnection();

    // 恢复登录界面尺寸（先解除聊天界面的固定尺寸约束）
    this->setMinimumSize(QSize(0, 0));
    this->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    // 先切换界面：setCentralWidget 会销毁旧的 _chat_dlg（停掉心跳 timer），
    // 必须在 UserMgr::Reset() 之前完成，否则 timer 可能在 Reset 后触发访问已清空的数据
    _login_dlg = new LoginDialog(this);
    _login_dlg->setWindowFlags(Qt::CustomizeWindowHint|Qt::FramelessWindowHint);
    setCentralWidget(_login_dlg);  // 旧 _chat_dlg 在此被销毁
    _login_dlg->show();
    _chat_dlg = nullptr;

    // ChatDialog 已销毁，现在可以安全清理缓存
    UserMgr::GetInstance()->Reset();

    // 设置为登录窗口固定尺寸 400x500
    this->setFixedSize(QSize(400, 500));

    // 重新连接登录界面的信号
    connect(_login_dlg, &LoginDialog::switchReset, this, &MainWindow::slotSwitchReset);
    connect(_login_dlg, &LoginDialog::switchRegist, this, &MainWindow::slotSwitchRegist);

    _switching_to_login = false;

    // 弹窗放最后：QMessageBox::warning 是模态的，会重入事件循环，
    // 此时 ChatDialog 和心跳 timer 已销毁，不会再触发 UserMgr::GetUid()
    QMessageBox::warning(this, QString::fromUtf8("下线通知"), reason);
}
