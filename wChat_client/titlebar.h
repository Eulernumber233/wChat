#ifndef TITLEBAR_H
#define TITLEBAR_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QPoint>

// Custom title bar used by the frameless MainWindow.
// Provides: window title, minimize, close. (Maximize is intentionally
// omitted — all pages have fixed size per design spec.) Dragging the
// titlebar moves the parent top-level window.
class TitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit TitleBar(QWidget *parent = nullptr);
    void setTitle(const QString &text);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QLabel      *_title_lb = nullptr;
    QPushButton *_min_btn  = nullptr;
    QPushButton *_close_btn = nullptr;
    bool   _dragging = false;
    QPoint _drag_offset;
};

#endif // TITLEBAR_H
