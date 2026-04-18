#ifndef CIRCLEAVATARLABEL_H
#define CIRCLEAVATARLABEL_H

#include <QLabel>
#include <QPixmap>

// A QLabel that clips whatever pixmap is assigned to it into a circle.
// The source QPixmap is kept as-is (storage unchanged, see user req #10);
// only the rendered output is round. Optional subtle white ring for
// better visibility on tinted backgrounds.
class CircleAvatarLabel : public QLabel
{
    Q_OBJECT
public:
    explicit CircleAvatarLabel(QWidget *parent = nullptr);

    // Preserve the QLabel API — these just feed into our rounded render.
    void setPixmap(const QPixmap &pixmap);
    // Convenience for code paths that only have a path (like UserInfo::_icon).
    void setImagePath(const QString &path);

    // Draw a thin white ring around the avatar (on by default for profile
    // cover; list rows turn it off to stay flush).
    void setRing(bool enabled);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildCache();

    QPixmap _src;       // original, unmodified
    QPixmap _cached;    // cached circular rendering sized to current rect
    bool    _ring = false;
};

#endif // CIRCLEAVATARLABEL_H
