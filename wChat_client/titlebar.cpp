#include "titlebar.h"
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QWindow>
#include <QPainter>
#include <QPainterPath>
#include <QFontDatabase>

// Segoe Fluent Icons codepoints (Windows 10/11 system font, matches the
// design spec). Falls back to Segoe MDL2 Assets (same glyph set) if
// Fluent Icons isn't installed (Win10 base image).
//   E921 = Chrome Minimize
//   E8BB = Chrome Close (X glyph)
static constexpr QChar kIconMinimize(0xE921);
static constexpr QChar kIconClose   (0xE8BB);

// Returns true if at least one of the listed icon fonts is installed.
// If none are present we render a plain text fallback (×  and  −).
static bool hasIconFont() {
    static const bool ok = []{
        const auto families = QFontDatabase::families();
        return families.contains("Segoe Fluent Icons")
            || families.contains("Segoe MDL2 Assets");
    }();
    return ok;
}

// Small logo: a pink gradient circle with a white "w" inside.
class LogoDot : public QWidget {
public:
    explicit LogoDot(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(22, 22);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QLinearGradient g(0, 0, width(), height());
        g.setColorAt(0.0, QColor(0xef, 0xb7, 0xc9));
        g.setColorAt(1.0, QColor(0xd9, 0x7a, 0x95));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(rect());
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPixelSize(13);
        f.setBold(true);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, "w");
    }
};

TitleBar::TitleBar(QWidget *parent) : QWidget(parent)
{
    setObjectName("TitleBar");
    setFixedHeight(36);
    setAttribute(Qt::WA_StyledBackground, true);

    auto *logo = new LogoDot(this);

    _title_lb = new QLabel("wChat", this);
    _title_lb->setObjectName("TitleBarLabel");

    const bool useIcon = hasIconFont();
    QFont iconFont("Segoe Fluent Icons");
    if (!QFontDatabase::families().contains("Segoe Fluent Icons")) {
        iconFont.setFamily("Segoe MDL2 Assets");
    }
    iconFont.setPixelSize(12);

    _min_btn = new QPushButton(this);
    _min_btn->setObjectName("TitleBarBtn");
    _min_btn->setCursor(Qt::PointingHandCursor);
    _min_btn->setToolTip(QStringLiteral("最小化"));
    _min_btn->setFocusPolicy(Qt::NoFocus);
    _min_btn->setFixedSize(26, 26);
    if (useIcon) {
        _min_btn->setFont(iconFont);
        _min_btn->setText(QString(kIconMinimize));
    } else {
        _min_btn->setText(QStringLiteral("−"));
    }

    _close_btn = new QPushButton(this);
    _close_btn->setObjectName("TitleBarBtnClose");
    _close_btn->setCursor(Qt::PointingHandCursor);
    _close_btn->setToolTip(QStringLiteral("关闭"));
    _close_btn->setFocusPolicy(Qt::NoFocus);
    _close_btn->setFixedSize(26, 26);
    if (useIcon) {
        _close_btn->setFont(iconFont);
        _close_btn->setText(QString(kIconClose));
    } else {
        _close_btn->setText(QStringLiteral("×"));
    }

    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(12, 0, 10, 0);
    h->setSpacing(8);
    h->addWidget(logo);
    h->addWidget(_title_lb, 1);
    h->addWidget(_min_btn);
    h->addWidget(_close_btn);

    connect(_min_btn, &QPushButton::clicked, this, [this]() {
        if (auto *w = window()) w->showMinimized();
    });
    connect(_close_btn, &QPushButton::clicked, this, [this]() {
        if (auto *w = window()) w->close();
    });
}

void TitleBar::setTitle(const QString &text) { _title_lb->setText(text); }

void TitleBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        _dragging = true;
        _drag_offset = e->globalPos() - window()->frameGeometry().topLeft();
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void TitleBar::mouseMoveEvent(QMouseEvent *e)
{
    if (_dragging && (e->buttons() & Qt::LeftButton)) {
        window()->move(e->globalPos() - _drag_offset);
        e->accept();
        return;
    }
    QWidget::mouseMoveEvent(e);
}

void TitleBar::mouseReleaseEvent(QMouseEvent *e)
{
    _dragging = false;
    QWidget::mouseReleaseEvent(e);
}
