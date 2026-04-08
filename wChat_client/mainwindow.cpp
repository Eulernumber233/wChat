#include "mainwindow.h"
#include "ui_mainwindow.h"

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

    // test
    //emit TcpMgr::GetInstance()->sig_switch_chatdlg();
}

MainWindow::~MainWindow()
{
    delete ui;
    if(_login_dlg){
        delete _login_dlg;
        _login_dlg=nullptr;
    }
    if(_regist_dlg){
        delete _regist_dlg;
        _regist_dlg=nullptr;
    }

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
    this->setMaximumSize(QSize(500,500));
}
