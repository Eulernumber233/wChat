#include "textbubble.h"

TextBubble::TextBubble(ChatRole role, const QString &text, QWidget *parent)
    :BubbleFrame(role, parent), m_role(role)
{
    m_pTextEdit = new QTextEdit();
    m_pTextEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    m_pTextEdit->setReadOnly(true);
    m_pTextEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pTextEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pTextEdit->installEventFilter(this);

    QFont font("Microsoft YaHei");
    font.setPointSize(12);
    m_pTextEdit->setFont(font);

    // Apply the (transparent + correct text color) stylesheet BEFORE any
    // text is set so the very first paint already shows the final look.
    // Without this we briefly render the default Qt QTextEdit chrome
    // (white box, black text) on top of the bubble during batch loads.
    initStyleSheet();
    setPlainText(text);
    setWidget(m_pTextEdit);
    initStyleSheet();   // re-apply in case setWidget reset palette inheritance

    // Force initial height computation now instead of waiting for the
    // first paintEvent. Eliminates the "tall blank bubble for one frame"
    // flicker visible during batch loads from LocalDb.
    adjustTextHeight();
}


bool TextBubble::eventFilter(QObject *o, QEvent *e)
{
    if(m_pTextEdit == o && e->type() == QEvent::Paint)
    {
        adjustTextHeight(); //PaintEvent中设置 调整高度
    }
    return BubbleFrame::eventFilter(o, e);
}

void TextBubble::adjustTextHeight()
{
    qreal doc_margin = m_pTextEdit->document()->documentMargin();    //字体到边框的距离默认为4
    QTextDocument *doc = m_pTextEdit->document();
    qreal text_height = 0;
    //把每一段的高度相加=文本高
    for (QTextBlock it = doc->begin(); it != doc->end(); it = it.next())
    {
        QTextLayout *pLayout = it.layout();
        QRectF text_rect = pLayout->boundingRect();                             //这段的rect
        text_height += text_rect.height();
    }
    int vMargin = this->layout()->contentsMargins().top();
    //设置这个气泡需要的高度 文本高+文本边距+TextEdit边框到气泡边框的距离
    setFixedHeight(text_height + doc_margin *2 + vMargin*2 );
}

void TextBubble::initStyleSheet()
{
    // Other-side bubbles are white → near-black text.
    // Self-side bubbles are pink gradient → deep-pink ink for contrast.
    const QString color = (m_role == ChatRole::Self) ? "#6b3a4a" : "#26222b";
    m_pTextEdit->setStyleSheet(
        QString("QTextEdit { background: transparent; border: none; color: %1; }")
            .arg(color));
}

void TextBubble::setPlainText(const QString &text)
{
    m_pTextEdit->setPlainText(text);
    //m_pTextEdit->setHtml(text);
    //找到段落中最大宽度
    qreal doc_margin = m_pTextEdit->document()->documentMargin();
    int margin_left = this->layout()->contentsMargins().left();
    int margin_right = this->layout()->contentsMargins().right();
    QFontMetricsF fm(m_pTextEdit->font());
    QTextDocument *doc = m_pTextEdit->document();
    int max_width = 0;
    //遍历每一段找到 最宽的那一段
    for (QTextBlock it = doc->begin(); it != doc->end(); it = it.next())    //字体总长
    {
        int txtW = int(fm.horizontalAdvance(it.text())) +2;
        max_width = max_width < txtW ? txtW : max_width;                 //找到最长的那段
    }
    //设置这个气泡的最大宽度 只需要设置一次
    setMaximumWidth(max_width + doc_margin * 2 + (margin_left + margin_right));        //设置最大宽度
}




