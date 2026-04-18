#include "statewidget.h"
#include "fluenticon.h"

StateWidget::StateWidget(QWidget *parent): QWidget(parent),_curstate(ClickLbState::Normal)
{
    setCursor(Qt::PointingHandCursor);
    //添加红点
    AddRedPoint();
}
void StateWidget::SetState(QString normal, QString hover, QString press, QString select, QString select_hover, QString select_press)
{
    _normal = normal;
    _normal_hover = hover;
    _normal_press = press;
    _selected = select;
    _selected_hover = select_hover;
    _selected_press = select_press;
    setProperty("state",normal);
    repolish(this);
}
ClickLbState StateWidget::GetCurState()
{
    return _curstate;
}
void StateWidget::ClearState()
{
    _curstate = ClickLbState::Normal;
    setProperty("state",_normal);
    repolish(this);
    update();
}
void StateWidget::SetSelected(bool bselected)
{
    if(bselected){
        _curstate = ClickLbState::Selected;
        setProperty("state",_selected);
        repolish(this);
        update();
        return;
    }
    _curstate = ClickLbState::Normal;
    setProperty("state",_normal);
    repolish(this);
    update();
    return;
}
void StateWidget::AddRedPoint()
{
    // Create a centred glyph label (empty by default) + a small red dot
    // badge in the top-right corner. The glyph label holds a Fluent Icon
    // codepoint when SetGlyph() is called.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    _glyph_lb = new QLabel(this);
    _glyph_lb->setAlignment(Qt::AlignCenter);
    _glyph_lb->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    _glyph_lb->setObjectName("nav_glyph");
    layout->addWidget(_glyph_lb);

    // Red-dot badge floats on top; it's positioned via the child widget's
    // default top-right corner (8x8 circle rendered by QSS).
    _red_point = new QLabel(this);
    _red_point->setObjectName("red_point");
    _red_point->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    _red_point->setFixedSize(10, 10);
    _red_point->setVisible(false);
    _red_point->raise();
}

void StateWidget::SetGlyph(const QString &glyph, int pixelSize)
{
    if (!_glyph_lb) return;
    FIC::applyIconFont(_glyph_lb, pixelSize);
    _glyph_lb->setText(glyph);
}
void StateWidget::ShowRedPoint(bool show)
{
    _red_point->setVisible(true);
}
void StateWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (_red_point) {
        const int sz = 10;
        _red_point->setGeometry(width() - sz - 2, 2, sz, sz);
    }
}

void StateWidget::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    return;
}
void StateWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if(_curstate == ClickLbState::Selected){
            qDebug()<<"PressEvent , already to selected press: "<< _selected_press;
            //emit clicked();
            // 调用基类的mousePressEvent以保证正常的事件处理
            QWidget::mousePressEvent(event);
            return;
        }
        if(_curstate == ClickLbState::Normal){
            qDebug()<<"PressEvent , change to selected press: "<< _selected_press;
            _curstate = ClickLbState::Selected;
            setProperty("state",_selected_press);
            repolish(this);
            update();
        }
        return;
    }
    // 调用基类的mousePressEvent以保证正常的事件处理
    QWidget::mousePressEvent(event);
}
void StateWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if(_curstate == ClickLbState::Normal){
            //qDebug()<<"ReleaseEvent , change to normal hover: "<< _normal_hover;
            setProperty("state",_normal_hover);
            repolish(this);
            update();
        }else{
            //qDebug()<<"ReleaseEvent , change to select hover: "<< _selected_hover;
            setProperty("state",_selected_hover);
            repolish(this);
            update();
        }
        emit clicked();
        return;
    }
    // 调用基类的mousePressEvent以保证正常的事件处理
    QWidget::mousePressEvent(event);
}

void StateWidget::enterEvent(QEnterEvent *event)
{
    // 在这里处理鼠标悬停进入的逻辑
    if(_curstate == ClickLbState::Normal){
        //qDebug()<<"enter , change to normal hover: "<< _normal_hover;
        setProperty("state",_normal_hover);
        repolish(this);
        update();

    }else{
        //qDebug()<<"enter , change to selected hover: "<< _selected_hover;
        setProperty("state",_selected_hover);
        repolish(this);
        update();
    }

    QWidget::enterEvent(event);
}

void StateWidget::leaveEvent(QEvent *event)
{
    // 在这里处理鼠标悬停离开的逻辑
    if(_curstate == ClickLbState::Normal){
        // qDebug()<<"leave , change to normal : "<< _normal;
        setProperty("state",_normal);
        repolish(this);
        update();
    }else{
        // qDebug()<<"leave , change to select normal : "<< _selected;
        setProperty("state",_selected);
        repolish(this);
        update();
    }
    QWidget::leaveEvent(event);
}
