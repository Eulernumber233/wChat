#include "circleavatarlabel.h"
#include <QPainter>
#include <QPainterPath>

CircleAvatarLabel::CircleAvatarLabel(QWidget *parent) : QLabel(parent)
{
    setScaledContents(false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAlignment(Qt::AlignCenter);   // safety net for any base-class painting
    setMinimumSize(1, 1);
}

void CircleAvatarLabel::setPixmap(const QPixmap &pixmap)
{
    _src = pixmap;
    rebuildCache();
    QLabel::setPixmap(_cached);
}

void CircleAvatarLabel::setImagePath(const QString &path)
{
    QPixmap pm(path);
    if (pm.isNull()) {
        _src = QPixmap();
        _cached = QPixmap();
        QLabel::setPixmap(QPixmap());
        return;
    }
    setPixmap(pm);
}

void CircleAvatarLabel::setRing(bool enabled)
{
    if (_ring == enabled) return;
    _ring = enabled;
    rebuildCache();
    QLabel::setPixmap(_cached);
}

void CircleAvatarLabel::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    if (!_src.isNull()) {
        rebuildCache();
        QLabel::setPixmap(_cached);
    }
}

void CircleAvatarLabel::paintEvent(QPaintEvent *event)
{
    if (_cached.isNull()) { QLabel::paintEvent(event); return; }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // Draw the cached round pixmap centered inside our QLabel rect.
    const qreal dpr = _cached.devicePixelRatioF();
    const int   pw  = static_cast<int>(_cached.width()  / dpr);
    const int   ph  = static_cast<int>(_cached.height() / dpr);
    const int   x   = (width()  - pw) / 2;
    const int   y   = (height() - ph) / 2;
    p.drawPixmap(x, y, _cached);
}

void CircleAvatarLabel::rebuildCache()
{
    if (_src.isNull() || width() <= 0 || height() <= 0) {
        _cached = QPixmap();
        return;
    }

    // Step 1: take the LARGEST CENTERED SQUARE from the source image
    // (= the square that fully fits inside the source rect, centered).
    // This makes the next "inscribed circle" the largest circle you can
    // get from the original image without any padding or distortion.
    const int srcW = _src.width();
    const int srcH = _src.height();
    const int sqSide = std::min(srcW, srcH);
    const int sx = (srcW - sqSide) / 2;
    const int sy = (srcH - sqSide) / 2;
    const QPixmap srcSquare = _src.copy(sx, sy, sqSide, sqSide);

    // Step 2: scale that square down (or up) to the target side, where
    // target = the inscribed-circle diameter inside our QLabel rect.
    const qreal dpr  = devicePixelRatioF();
    const int   side = std::min(width(), height());
    const int   px   = static_cast<int>(side * dpr);

    QPixmap result(px, px);
    result.fill(Qt::transparent);
    result.setDevicePixelRatio(dpr);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Step 3: clip to a circle inscribed in [0, 0, side, side].
    QPainterPath clip;
    clip.addEllipse(0, 0, side, side);
    painter.setClipPath(clip);

    // Step 4: draw the centered square, scaled to fill the circle.
    const QPixmap scaled = srcSquare.scaled(QSize(side, side),
                                            Qt::IgnoreAspectRatio,
                                            Qt::SmoothTransformation);
    painter.drawPixmap(0, 0, scaled);

    if (_ring) {
        painter.setClipping(false);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, 220), 2));
        painter.drawEllipse(QRectF(1, 1, side - 2, side - 2));
    }

    _cached = result;
}
