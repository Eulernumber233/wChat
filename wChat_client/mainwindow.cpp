#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "titlebar.h"
#include "usermgr.h"
#include "pinkmessagebox.h"
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>

namespace {
// Fixed inner-panel sizes per new UI spec (ui_prototype). No resizing.
constexpr int kLoginW  = 430, kLoginH  = 560;
constexpr int kRegistW = 430, kRegistH = 640;
constexpr int kResetW  = 430, kResetH  = 640;
constexpr int kChatW   = 1180, kChatH  = 760;

// Glass-frame padding (translucent outer ring that lets wallpaper show through).
constexpr int kOuterPad   = 10;
constexpr int kTitleBarH  = 36;
constexpr int kOuterRadius = 24;   // outer translucent pill radius
constexpr int kInnerRadius = 18;   // inner white panel radius
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // A bare QMainWindow includes a menuBar/statusBar by default; remove
    // both so the central widget fills the entire client area.
    setMenuBar(nullptr);
    setStatusBar(nullptr);

    // True frameless + translucent background so the paintEvent can paint
    // a half-transparent pill visible over the desktop wallpaper.
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, true);

    // Central widget is just a transparent container — all visible glass
    // and inner panel are painted by this MainWindow's paintEvent.
    _shell_root = new QWidget(this);
    _shell_root->setAttribute(Qt::WA_TranslucentBackground, true);

    _titlebar = new TitleBar(_shell_root);
    _page_host = new QWidget(_shell_root);
    _page_host->setObjectName("PageHost");
    _page_host->setAttribute(Qt::WA_TranslucentBackground, true);

    auto *root_v = new QVBoxLayout(_shell_root);
    root_v->setContentsMargins(kOuterPad, kOuterPad, kOuterPad, kOuterPad);
    root_v->setSpacing(0);
    root_v->addWidget(_titlebar);
    root_v->addWidget(_page_host, 1);

    setCentralWidget(_shell_root);

    // First page: login.
    _login_dlg = new LoginDialog(_page_host);
    installPage(_login_dlg, kLoginW, kLoginH, QStringLiteral("wChat"));

    connect(_login_dlg, &LoginDialog::switchRegist, this, &MainWindow::slotSwitchRegist);
    connect(_login_dlg, &LoginDialog::switchReset,  this, &MainWindow::slotSwitchReset);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_switch_chatdlg,
            this, &MainWindow::slotSwitchchat);

    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_kick_user, this, [this](QString) {
        slotBackToLogin(QStringLiteral("您的账号在其他设备登录，当前设备已被迫下线。"));
    });
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_connection_lost, this, [this]() {
        slotBackToLogin(QStringLiteral("与服务器断开连接，请重新登录。"));
    });
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect full = rect();

    // Outer translucent pill: rgba(255,255,255,0.42) + hairline border.
    // Because WA_TranslucentBackground is on, regions we don't paint stay
    // fully transparent and show the desktop/next window behind.
    QPainterPath outer;
    outer.addRoundedRect(full, kOuterRadius, kOuterRadius);
    p.fillPath(outer, QColor(255, 255, 255, 107));
    p.setPen(QPen(QColor(255, 255, 255, 165), 1));
    p.drawPath(outer);

    if (_auth_mode) {
        // Draw a solid white inner panel for auth pages (it covers title
        // bar + page host so the glass pad only appears as a ring).
        QRect inner = full.adjusted(kOuterPad, kOuterPad, -kOuterPad, -kOuterPad);
        QPainterPath innerPath;
        innerPath.addRoundedRect(inner, kInnerRadius, kInnerRadius);
        p.fillPath(innerPath, QColor(255, 255, 255, 255));
    }
    // Chat mode: no inner fill. Child columns paint their own solid
    // backgrounds and the gaps between them stay translucent (glass ring
    // color bleeds through, showing the wallpaper).
}

void MainWindow::installPage(QWidget *page, int inner_w, int inner_h, const QString &title)
{
    // Detect mode from the page type so paintEvent picks the right look.
    _auth_mode = (qobject_cast<ChatDialog *>(page) == nullptr);

    // Remove + deleteLater any previous page still living inside _page_host.
    QLayout *old = _page_host->layout();
    if (old) {
        QLayoutItem *item;
        while ((item = old->takeAt(0)) != nullptr) {
            if (QWidget *w = item->widget()) {
                if (w != page) {
                    w->hide();
                    w->setParent(nullptr);
                    w->deleteLater();
                }
            }
            delete item;
        }
        delete old;
    }

    auto *v = new QVBoxLayout(_page_host);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    // Page must render as a plain child widget, not a top-level dialog.
    page->setParent(_page_host);
    page->setWindowFlags(Qt::Widget);
    v->addWidget(page);
    page->show();
    page->raise();

    _titlebar->setTitle(title);

    // Window size = outer padding + titlebar + inner panel size.
    const int total_w = inner_w + kOuterPad * 2;
    const int total_h = inner_h + kOuterPad * 2 + kTitleBarH;
    setFixedSize(total_w, total_h);

    update();  // trigger repaint so the auth/chat mode choice is reflected.
}

void MainWindow::slotSwitchRegist()
{
    _regist_dlg = new RegistDialog(_page_host);
    connect(_regist_dlg, &RegistDialog::sigSwitchLogin,
            this, &MainWindow::slotSwitchLogin);
    installPage(_regist_dlg, kRegistW, kRegistH, QStringLiteral("wChat · 注册"));
    _login_dlg = nullptr;  // destroyed inside installPage
}

void MainWindow::slotSwitchLogin()
{
    _login_dlg = new LoginDialog(_page_host);
    connect(_login_dlg, &LoginDialog::switchReset,  this, &MainWindow::slotSwitchReset);
    connect(_login_dlg, &LoginDialog::switchRegist, this, &MainWindow::slotSwitchRegist);
    installPage(_login_dlg, kLoginW, kLoginH, QStringLiteral("wChat"));
    _regist_dlg = nullptr;
}

void MainWindow::slotSwitchReset()
{
    _reset_dlg = new ResetDialog(_page_host);
    connect(_reset_dlg, &ResetDialog::switchLogin,
            this, &MainWindow::slotSwitchLogin2);
    installPage(_reset_dlg, kResetW, kResetH, QStringLiteral("wChat · 找回密码"));
    _login_dlg = nullptr;
}

void MainWindow::slotSwitchLogin2()
{
    _login_dlg = new LoginDialog(_page_host);
    connect(_login_dlg, &LoginDialog::switchReset,  this, &MainWindow::slotSwitchReset);
    connect(_login_dlg, &LoginDialog::switchRegist, this, &MainWindow::slotSwitchRegist);
    installPage(_login_dlg, kLoginW, kLoginH, QStringLiteral("wChat"));
    _reset_dlg = nullptr;
}

void MainWindow::slotSwitchchat()
{
    _chat_dlg = new ChatDialog(_page_host);
    installPage(_chat_dlg, kChatW, kChatH, QStringLiteral("wChat"));
    _login_dlg = nullptr;
}

void MainWindow::slotBackToLogin(QString reason)
{
    if (_switching_to_login || !_chat_dlg) return;
    _switching_to_login = true;

    TcpMgr::GetInstance()->CloseConnection();

    _login_dlg = new LoginDialog(_page_host);
    installPage(_login_dlg, kLoginW, kLoginH, QStringLiteral("wChat"));
    _chat_dlg = nullptr;

    UserMgr::GetInstance()->Reset();

    connect(_login_dlg, &LoginDialog::switchReset,  this, &MainWindow::slotSwitchReset);
    connect(_login_dlg, &LoginDialog::switchRegist, this, &MainWindow::slotSwitchRegist);

    _switching_to_login = false;

    PinkMessageBox::warn(this, QStringLiteral("下线通知"), reason);
}
