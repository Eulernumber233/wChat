#ifndef TEXTBUBBLE_H
#define TEXTBUBBLE_H
#include <QTextLayout>
#include <QTextEdit>

#include "BubbleFrame.h"
#include <QHBoxLayout>
#include <QTextDocument>
#include <QTextBlock>
class TextBubble : public BubbleFrame
{
    Q_OBJECT
public:
    TextBubble(ChatRole role, const QString &text, QWidget *parent = nullptr);
protected:
    bool eventFilter(QObject *o, QEvent *e);
private:
    void adjustTextHeight();
    void setPlainText(const QString &text);
    void initStyleSheet();
private:
    QTextEdit *m_pTextEdit;
    ChatRole   m_role;
};

#endif // TEXTBUBBLE_H
