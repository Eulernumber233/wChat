#include "logindialog.h"
#include "ui_logindialog.h"
#include "httpmgr.h"
#include "tcpmgr.h"
#include "fluenticon.h"
#include <QMovie>

LoginDialog::LoginDialog(QWidget *parent) : QDialog(parent)
    , ui(new Ui::LoginDialog)
{
    ui->setupUi(this);
    setObjectName("LoginDialog");

    // Logo: pink gradient circle with a white "w" — QSS renders it.
    ui->auth_logo->setObjectName("auth_logo");

    // Leading icons inside the email / password "field" wraps. Using
    // Segoe Fluent Icons so they match the overall icon font throughout
    // the app (instead of Emoji glyphs that may not render).
    FIC::applyIconFont(ui->email_icon, 16);
    ui->email_icon->setText(QString(FIC::Mail));
    FIC::applyIconFont(ui->pass_icon, 16);
    ui->pass_icon->setText(QString(FIC::Lock));

    ui->pass_edit->setEchoMode(QLineEdit::Password);

    // Err tip icon slot (used when showing loading spinner via QMovie, or
    // left empty for plain text errors). QSS colours the label pink.
    ui->err_tip_icon->hide();

    connect(ui->reg_btn, &QPushButton::clicked, this, &LoginDialog::switchRegist);
    ui->reg_btn->setCursor(Qt::PointingHandCursor);
    ui->login_btn->setCursor(Qt::PointingHandCursor);
    connect(ui->login_btn, &QPushButton::clicked, this, &LoginDialog::on_login_btn_clicked);

    ui->forget_label->SetState("normal", "hover", "", "selected", "selected_hover", "");
    ui->forget_label->setCursor(Qt::PointingHandCursor);
    connect(ui->forget_label, &ClickedLabel::clicked, this, &LoginDialog::slot_forget_pwd);

    initHttpHandlers();
    connect(HttpMgr::GetInstance().get(), &HttpMgr::sig_login_mod_finish,
            this, &LoginDialog::slot_login_mod_finish);
    connect(this, &LoginDialog::sig_connect_tcp, TcpMgr::GetInstance().get(), &TcpMgr::slot_tcp_connect);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_con_success, this, &LoginDialog::slot_tcp_con_finish);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_login_failed, this, &LoginDialog::slot_login_failed);
}

LoginDialog::~LoginDialog()
{
    delete ui;
}

void LoginDialog::initHead() { /* no-op: new UI has no head avatar */ }

void LoginDialog::slot_forget_pwd() { emit switchReset(); }

bool LoginDialog::checkUserValid()
{
    auto email = ui->email_edit->text();
    if (email.isEmpty()) {
        AddTipErr(TipErr::TIP_EMAIL_ERR, tr("邮箱不能为空"));
        return false;
    }
    DelTipErr(TipErr::TIP_EMAIL_ERR);
    return true;
}

bool LoginDialog::checkPwdValid()
{
    auto pwd = ui->pass_edit->text();
    if (pwd.length() < 6 || pwd.length() > 15) {
        AddTipErr(TipErr::TIP_PWD_ERR, tr("密码长度应为 6~15"));
        return false;
    }
    QRegularExpression regExp("^[a-zA-Z0-9!@#$%^&*.]{6,15}$");
    if (!regExp.match(pwd).hasMatch()) {
        AddTipErr(TipErr::TIP_PWD_ERR, tr("不能包含非法字符且长度为 6~15"));
        return false;
    }
    DelTipErr(TipErr::TIP_PWD_ERR);
    return true;
}

bool LoginDialog::enableBtn(bool enabled)
{
    ui->login_btn->setEnabled(enabled);
    ui->reg_btn->setEnabled(enabled);
    return true;
}

void LoginDialog::on_login_btn_clicked()
{
    if (!checkUserValid() || !checkPwdValid()) return;

    enableBtn(false);
    auto email = ui->email_edit->text();
    auto pwd = ui->pass_edit->text();
    QJsonObject json_obj;
    json_obj["email"]  = email;
    json_obj["passwd"] = xorString(pwd);
    HttpMgr::GetInstance()->PostHttpReq("/user_login", json_obj,
                                        ReqId::ID_LOGIN_USER, Modules::LOGINMOD);
}

void LoginDialog::AddTipErr(TipErr te, QString tips)
{
    _tip_errs[te] = tips;
    showTip(tips, false);
}
void LoginDialog::DelTipErr(TipErr te)
{
    _tip_errs.remove(te);
    if (_tip_errs.empty()) { ui->err_tip->clear(); ui->err_tip_icon->hide(); return; }
    showTip(_tip_errs.first(), false);
}

void LoginDialog::showTip(QString str, bool b_ok)
{
    // "Success" path in the new UI is pink-neutral, not green.
    ui->err_tip->setProperty("state", b_ok ? "info" : "err");
    ui->err_tip->setText(str);
    repolish(ui->err_tip);

    // Hide spinner unless caller explicitly set it.
    ui->err_tip_icon->hide();
}

// Show an animated loading spinner next to the tip text (used while we
// wait for the TCP handshake after the HTTP login succeeds).
void LoginDialog::showLoadingTip(QString str)
{
    ui->err_tip->setProperty("state", "info");
    ui->err_tip->setText(str);
    repolish(ui->err_tip);

    static QMovie *sMovie = nullptr;
    if (!sMovie) {
        sMovie = new QMovie(":/asserts/loading.gif", QByteArray(), this);
        sMovie->setScaledSize(QSize(16, 16));
    }
    if (!sMovie->isValid()) {
        // Asset missing / unreadable — fall back to pink text only.
        ui->err_tip_icon->hide();
        return;
    }
    ui->err_tip_icon->setMovie(sMovie);
    ui->err_tip_icon->show();
    sMovie->start();
}

void LoginDialog::initHttpHandlers()
{
    _handlers.insert(ReqId::ID_LOGIN_USER, [this](QJsonObject jsonObj) {
        int error = jsonObj["error"].toInt();
        if (error != ErrorCodes::SUCCESS) {
            showTip(tr("参数错误"), false);
            enableBtn(true);
            return;
        }
        auto email = jsonObj["email"].toString();
        ServerInfo si;
        si.Uid  = jsonObj["uid"].toInt();
        si.Host = jsonObj["host"].toString();
        si.Port = jsonObj["port"].toString();
        si.Token = jsonObj["token"].toString();
        si.AgentHost = jsonObj.contains("agent_host") ? jsonObj["agent_host"].toString() : QString();
        si.AgentPort = jsonObj.contains("agent_port") ? jsonObj["agent_port"].toInt() : 0;
        _uid = si.Uid;
        _token = si.Token;
        qDebug() << "login email=" << email << " uid=" << si.Uid
                 << " host=" << si.Host << " port=" << si.Port;
        emit sig_connect_tcp(si);
    });
}

void LoginDialog::slot_login_mod_finish(ReqId id, QString res, ErrorCodes err)
{
    if (err != ErrorCodes::SUCCESS) { showTip(tr("网络请求错误"), false); enableBtn(true); return; }
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        showTip(tr("json 解析错误"), false);
        enableBtn(true);
        return;
    }
    _handlers[id](jsonDoc.object());
}

void LoginDialog::slot_tcp_con_finish(bool bsuccess)
{
    if (bsuccess) {
        // Pink-tinted, not green. Shows a spinner while we send the chat
        // login handshake and wait for sig_switch_chatdlg.
        showLoadingTip(tr("正在登录，请稍候…"));
        QJsonObject jsonObj;
        jsonObj["uid"]   = _uid;
        jsonObj["token"] = _token;
        QJsonDocument doc(jsonObj);
        emit TcpMgr::GetInstance()->sig_send_data(
            ReqId::ID_CHAT_LOGIN, doc.toJson(QJsonDocument::Indented));
    } else {
        showTip(tr("网络异常"), false);
        enableBtn(true);
    }
}

void LoginDialog::slot_login_failed(int err)
{
    showTip(QString(tr("登录失败，err=%1")).arg(err), false);
    enableBtn(true);
}
