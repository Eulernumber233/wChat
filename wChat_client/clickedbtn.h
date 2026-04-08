#ifndef CLICKEDBTN_H
#define CLICKEDBTN_H
#include "global.h"
#include <QVariant>
#include <QPushButton>
class ClickedBtn :public QPushButton
{
    Q_OBJECT
public:
    ClickedBtn(QWidget *parent=nullptr);
    ~ClickedBtn();
    void SetState(QString nomal, QString hover,QString press);
protected:
    virtual void enterEvent(QEnterEvent *event)override;
    virtual void leaveEvent(QEvent *event)override;
    virtual void mousePressEvent(QMouseEvent *event)override;
    virtual void mouseReleaseEvent(QMouseEvent *event)override;
private:
    QString _normal;
    QString _hover;
    QString _press;
};

#endif // CLICKEDBTN_H
