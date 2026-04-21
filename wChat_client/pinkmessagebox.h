#ifndef PINKMESSAGEBOX_H
#define PINKMESSAGEBOX_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QPoint>

// Frameless, translucent rounded message dialog matching the cherry-pink
// theme. A drop-in replacement for QMessageBox::warning / information
// when we want the popup to look like the rest of the app.
//
// Usage:
//   PinkMessageBox::warn(this, "下线通知", "您的账号在其他设备登录…");
class PinkMessageBox : public QDialog
{
    Q_OBJECT
public:
    enum class Kind { Info, Warn };

    static void info(QWidget *parent, const QString &title, const QString &text);
    static void warn(QWidget *parent, const QString &title, const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    explicit PinkMessageBox(QWidget *parent, Kind kind,
                            const QString &title, const QString &text);

    QLabel      *_title_lb = nullptr;
    QLabel      *_text_lb  = nullptr;
    QLabel      *_icon_lb  = nullptr;
    QPushButton *_ok_btn   = nullptr;
    QPushButton *_close_btn = nullptr;
    bool   _dragging = false;
    QPoint _drag_offset;
};

#endif // PINKMESSAGEBOX_H
