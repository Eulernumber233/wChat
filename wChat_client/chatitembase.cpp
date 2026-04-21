#include "chatitembase.h"
#include <QPainter>
#include <QPainterPath>

// Crop the largest centred square from `src`, scale to `side`x`side`,
// then mask to a circle. Mirrors CircleAvatarLabel's algorithm so that
// every avatar in the app uses identical "inscribed circle" geometry.
static QPixmap roundAvatar(const QPixmap &src, int side) {
    if (src.isNull() || side <= 0) return src;
    const int sqSide = std::min(src.width(), src.height());
    const int sx = (src.width()  - sqSide) / 2;
    const int sy = (src.height() - sqSide) / 2;
    const QPixmap sq = src.copy(sx, sy, sqSide, sqSide);
    const QPixmap scaled = sq.scaled(QSize(side, side),
                                     Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
    QPixmap out(side, side);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath clip;
    clip.addEllipse(0, 0, side, side);
    p.setClipPath(clip);
    p.drawPixmap(0, 0, scaled);
    return out;
}

ChatItemBase::ChatItemBase(ChatRole role, QWidget *parent)
    : QWidget(parent), m_role(role)
{
    m_pNameLabel = new QLabel();
    m_pNameLabel->setObjectName("chat_user_name");
    QFont font("Microsoft YaHei");
    font.setPointSize(9);
    m_pNameLabel->setFont(font);
    m_pNameLabel->setFixedHeight(20);

    m_pIconLabel = new QLabel();
    m_pIconLabel->setScaledContents(false);
    m_pIconLabel->setAlignment(Qt::AlignCenter);
    m_pIconLabel->setFixedSize(42, 42);

    // 消息气泡容器（用于显示消息内容，后续可能添加文本/图片等）
    m_pBubble = new QWidget();

    //创建QGridLayout网格布局（按 “行 × 列” 排列控件）
    QGridLayout *pGLayout = new QGridLayout();
    pGLayout->setVerticalSpacing(3); // 行与行之间的垂直间距3px
    pGLayout->setHorizontalSpacing(3); // 列与列之间的水平间距3px
    pGLayout->setContentsMargins(3, 3, 3, 3); // 布局内边距（左、上、右、下均3px）

    //创建一个水平方向可扩展的间隔项（空白区域），用于 “推挤” 控件到指定位置
    QSpacerItem*pSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
    if(m_role == ChatRole::Self)
    {
        m_pNameLabel->setContentsMargins(0,0,8,0); // 用户名右侧留8px空白
        m_pNameLabel->setAlignment(Qt::AlignRight); // 用户名右对齐

        // 网格布局位置：行索引, 列索引, 占用行数, 占用列数, 对齐方式
        pGLayout->addWidget(m_pNameLabel, 0, 1, 1, 1); // 用户名放在第0行第1列，占1行1列
        pGLayout->addWidget(m_pIconLabel, 0, 2, 2, 1, Qt::AlignTop); // 头像放在第0行第2列，占2行1列（和用户名、气泡对齐），顶部对齐
        pGLayout->addItem(pSpacer, 1, 0, 1, 1); // 间隔项放在第1行第0列，用于左侧填充
        pGLayout->addWidget(m_pBubble, 1, 1, 1, 1); // 消息气泡放在第1行第1列

        // 列拉伸系数（控制列的空间分配比例）
        pGLayout->setColumnStretch(0, 2); // 第0列拉伸权重2（左侧空白区域）
        pGLayout->setColumnStretch(1, 3); // 第1列拉伸权重3（消息气泡区域）
    }else{
        m_pNameLabel->setContentsMargins(8,0,0,0);
        m_pNameLabel->setAlignment(Qt::AlignLeft);
        pGLayout->addWidget(m_pIconLabel, 0, 0, 2,1, Qt::AlignTop);
        pGLayout->addWidget(m_pNameLabel, 0,1, 1,1);
        pGLayout->addWidget(m_pBubble, 1,1, 1,1);
        pGLayout->addItem(pSpacer, 1, 2, 1, 1);
        pGLayout->setColumnStretch(1, 3);
        pGLayout->setColumnStretch(2, 2);
    }
    this->setLayout(pGLayout);
}
void ChatItemBase::setUserName(const QString &name)
{
    m_pNameLabel->setText(name);
}
void ChatItemBase::setUserIcon(const QPixmap &icon)
{
    // Render the bubble avatar as a centered round portrait so it
    // matches the rest of the app's avatar style.
    m_pIconLabel->setPixmap(roundAvatar(icon, m_pIconLabel->width()));
}

void ChatItemBase::setWidget(QWidget *w)
{
    QGridLayout *pGLayout = (qobject_cast<QGridLayout *>)(this->layout());
    pGLayout->replaceWidget(m_pBubble, w);
    delete m_pBubble;
    m_pBubble = w;
}
