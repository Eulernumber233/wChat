#include "pinkmessagebox.h"
#include "fluenticon.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>

namespace {
constexpr int kPad     = 10;   // outer translucent ring padding
constexpr int kRadius  = 22;   // outer pill corner radius
constexpr int kInnerR  = 16;   // inner panel corner radius
} // namespace

PinkMessageBox::PinkMessageBox(QWidget *parent, Kind kind,
                               const QString &title, const QString &text)
    : QDialog(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setModal(true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(kPad, kPad, kPad, kPad);
    outer->setSpacing(0);

    auto *inner = new QWidget(this);
    inner->setObjectName("PinkMsgInner");
    inner->setAttribute(Qt::WA_StyledBackground, true);
    outer->addWidget(inner);

    auto *v = new QVBoxLayout(inner);
    v->setContentsMargins(20, 16, 20, 16);
    v->setSpacing(12);

    // Title row: title text on left, close (×) on right
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(6);
    _title_lb = new QLabel(title, inner);
    _title_lb->setObjectName("PinkMsgTitle");
    titleRow->addWidget(_title_lb, 1);

    _close_btn = new QPushButton(QString(FIC::Close), inner);
    _close_btn->setObjectName("PinkMsgClose");
    _close_btn->setCursor(Qt::PointingHandCursor);
    _close_btn->setFixedSize(22, 22);
    _close_btn->setFocusPolicy(Qt::NoFocus);
    titleRow->addWidget(_close_btn);
    v->addLayout(titleRow);

    // Body row: large status glyph + multi-line text
    auto *body = new QHBoxLayout;
    body->setSpacing(14);
    _icon_lb = new QLabel(inner);
    _icon_lb->setObjectName("PinkMsgIcon");
    _icon_lb->setFixedSize(40, 40);
    _icon_lb->setAlignment(Qt::AlignCenter);
    // Use a Fluent glyph in the icon font: Warning vs Info
    QFont iconFont;
    iconFont.setFamily("Segoe MDL2 Assets");
    iconFont.setPixelSize(28);
    _icon_lb->setFont(iconFont);
    // Warning chevron / Info circle codepoints (both available in MDL2).
    _icon_lb->setText(kind == Kind::Warn ? QString(QChar(0xE7BA))
                                         : QString(QChar(0xE946)));
    body->addWidget(_icon_lb, 0, Qt::AlignTop);

    _text_lb = new QLabel(text, inner);
    _text_lb->setObjectName("PinkMsgText");
    _text_lb->setWordWrap(true);
    body->addWidget(_text_lb, 1);
    v->addLayout(body);

    // Footer: OK button (right-aligned)
    auto *foot = new QHBoxLayout;
    foot->addStretch(1);
    _ok_btn = new QPushButton(QStringLiteral("好的"), inner);
    _ok_btn->setObjectName("PinkMsgOk");
    _ok_btn->setCursor(Qt::PointingHandCursor);
    _ok_btn->setFixedHeight(32);
    _ok_btn->setMinimumWidth(80);
    foot->addWidget(_ok_btn);
    v->addLayout(foot);

    connect(_ok_btn,    &QPushButton::clicked, this, &QDialog::accept);
    connect(_close_btn, &QPushButton::clicked, this, &QDialog::accept);

    // Sensible default size; QSS controls colors (see stylesheet.qss
    // section "PinkMessageBox").
    resize(360, 180);
}

void PinkMessageBox::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRect full = rect();

    // Outer translucent ring (matches MainWindow glass treatment).
    QPainterPath outer;
    outer.addRoundedRect(full, kRadius, kRadius);
    p.fillPath(outer, QColor(255, 255, 255, 110));
    p.setPen(QPen(QColor(255, 255, 255, 165), 1));
    p.drawPath(outer);

    // Inner solid pink-white panel.
    QRect inner = full.adjusted(kPad, kPad, -kPad, -kPad);
    QPainterPath innerPath;
    innerPath.addRoundedRect(inner, kInnerR, kInnerR);
    p.fillPath(innerPath, QColor(0xfb, 0xf7, 0xfa));
    p.setPen(QPen(QColor(0xec, 0xe7, 0xee), 1));
    p.drawPath(innerPath);
}

void PinkMessageBox::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        _dragging = true;
        _drag_offset = e->globalPos() - frameGeometry().topLeft();
        e->accept();
        return;
    }
    QDialog::mousePressEvent(e);
}

void PinkMessageBox::mouseMoveEvent(QMouseEvent *e)
{
    if (_dragging && (e->buttons() & Qt::LeftButton)) {
        move(e->globalPos() - _drag_offset);
        e->accept();
        return;
    }
    QDialog::mouseMoveEvent(e);
}

void PinkMessageBox::mouseReleaseEvent(QMouseEvent *e)
{
    _dragging = false;
    QDialog::mouseReleaseEvent(e);
}

void PinkMessageBox::info(QWidget *parent, const QString &title, const QString &text)
{
    PinkMessageBox box(parent, Kind::Info, title, text);
    box.exec();
}
void PinkMessageBox::warn(QWidget *parent, const QString &title, const QString &text)
{
    PinkMessageBox box(parent, Kind::Warn, title, text);
    box.exec();
}
