#include "bubbleframe.h"

const int WIDTH_SANJIAO  = 8;  //三角宽
BubbleFrame::BubbleFrame(ChatRole role, QWidget *parent)
    :QFrame(parent),m_role(role),m_margin(3)
{
    m_pHLayout = new QHBoxLayout();
    if(m_role == ChatRole::Self)
        m_pHLayout->setContentsMargins(m_margin, m_margin, WIDTH_SANJIAO + m_margin, m_margin);
    else
        m_pHLayout->setContentsMargins(WIDTH_SANJIAO + m_margin, m_margin, m_margin, m_margin);
    this->setLayout(m_pHLayout);
}



void BubbleFrame::setWidget(QWidget *w)
{
    if(m_pHLayout->count() > 0)
        return ;
    else{
        m_pHLayout->addWidget(w);
    }
}


void BubbleFrame::setMargin(int margin)
{
    m_margin=margin;
}

void BubbleFrame::paintEvent(QPaintEvent *e)
{
    // Cherry-pink theme:
    //  - Other side: solid white bubble (with subtle border / shadow feel)
    //  - Self side:  pink gradient (light to mid-pink) with deep pink text
    // Rounded corners match the design (14px for main body, 4px on the
    // "pointer" side to anchor visually to the avatar).
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    const int radius = 14;

    if (m_role == ChatRole::Other) {
        painter.setBrush(QBrush(QColor(0xff, 0xff, 0xff)));
        QRect bk_rect(WIDTH_SANJIAO, 0, this->width() - WIDTH_SANJIAO, this->height());
        painter.drawRoundedRect(bk_rect, radius, radius);
        QPointF points[3] = {
            QPointF(bk_rect.x(), 12),
            QPointF(bk_rect.x(), 10 + WIDTH_SANJIAO + 2),
            QPointF(bk_rect.x() - WIDTH_SANJIAO, 10 + WIDTH_SANJIAO - WIDTH_SANJIAO/2),
        };
        painter.drawPolygon(points, 3);
    } else {
        QRect bk_rect(0, 0, this->width() - WIDTH_SANJIAO, this->height());
        QLinearGradient grad(bk_rect.topLeft(), bk_rect.bottomRight());
        grad.setColorAt(0.0, QColor(0xf6, 0xcf, 0xdd));  // --pink-200
        grad.setColorAt(1.0, QColor(0xef, 0xb7, 0xc9));  // --pink-300
        painter.setBrush(QBrush(grad));
        painter.drawRoundedRect(bk_rect, radius, radius);
        QPointF points[3] = {
            QPointF(bk_rect.x() + bk_rect.width(), 12),
            QPointF(bk_rect.x() + bk_rect.width(), 12 + WIDTH_SANJIAO + 2),
            QPointF(bk_rect.x() + bk_rect.width() + WIDTH_SANJIAO,
                    10 + WIDTH_SANJIAO - WIDTH_SANJIAO/2),
        };
        painter.setBrush(QBrush(QColor(0xef, 0xb7, 0xc9)));
        painter.drawPolygon(points, 3);
    }
    return QFrame::paintEvent(e);
}
