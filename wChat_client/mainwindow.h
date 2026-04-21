#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QPoint>
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

class TitleBar;

// Top-level translucent frameless window. Owns exactly one "page" at a
// time (Login / Regist / Reset / Chat). The page is embedded as a plain
// child Qt::Widget so that it participates in the same top-level window
// (no floating QDialogs with OS chrome).
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void slotSwitchRegist();
    void slotSwitchLogin();
    void slotSwitchReset();
    void slotSwitchLogin2();
    void slotSwitchchat();
    void slotBackToLogin(QString reason);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    // Install `page` into the content host, resize the window, set titlebar
    // text. `inner_w`/`inner_h` are the inner (white panel) sizes — outer
    // window adds padding*2 + titlebar on top of them.
    void installPage(QWidget *page, int inner_w, int inner_h, const QString &title);

    Ui::MainWindow *ui = nullptr;

    LoginDialog*  _login_dlg  = nullptr;
    RegistDialog* _regist_dlg = nullptr;
    ResetDialog*  _reset_dlg  = nullptr;
    ChatDialog*   _chat_dlg   = nullptr;

    // Frameless window widgets.
    QWidget*  _shell_root  = nullptr;  // central widget (padding only — paints glass)
    TitleBar* _titlebar    = nullptr;
    QWidget*  _page_host   = nullptr;  // fills inner area below titlebar

    bool _switching_to_login = false;

    // True when the current page uses the "auth card" style (Login/Regist/
    // Reset). In that mode paintEvent draws a filled white rounded panel
    // underneath the titlebar+page. For the chat page we skip the white
    // fill so the translucent gutters between columns can see the desktop
    // through the window.
    bool _auth_mode = true;
};
#endif // MAINWINDOW_H
