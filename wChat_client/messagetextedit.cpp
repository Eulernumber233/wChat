#include "MessageTextEdit.h"
#include "pinkmessagebox.h"
#include <QDebug>

// MIME（Multipurpose Internet Mail Extensions，多用途互联网邮件扩展）
// 最初是为解决邮件中无法传输非文本数据（如图片、文件）而设计的标准，
// 后来被广泛应用于 所有需要 “跨组件 / 跨应用传输异构数据” 的场景（如拖拽文件、复制粘贴图片、网络传输文件等）

MessageTextEdit::MessageTextEdit(QWidget *parent)
    : QTextEdit(parent)
{
    this->setMaximumHeight(60);

    // Placeholder shown when the compose box is empty. QSS's
    // `placeholder-text-color` doesn't apply to QTextEdit on all Qt
    // versions — set PlaceholderText role on the palette as a reliable
    // fallback.
    this->setPlaceholderText(
        QStringLiteral("输入消息 (Enter 发送 / Shift+Enter 换行)"));
    QPalette pal = this->palette();
    pal.setColor(QPalette::PlaceholderText, QColor(0xc3, 0xbe, 0xc8));
    this->setPalette(pal);
}

MessageTextEdit::~MessageTextEdit()
{

}

QVector<MsgInfo> MessageTextEdit::getMsgList()
{
    mGetMsgList.clear();

    QString doc = this->document()->toPlainText();
    QString text="";//存储文本信息
    int indexUrl = 0;
    int count = mMsgList.size();

    for(int index=0; index<doc.size(); index++)  // 遍历纯文本的每个字符
    {
        if(doc[index]==QChar::ObjectReplacementCharacter)  // // 遇到“对象占位符”→ 嵌入对象（图片/文件）
        {
            // 处理：这是一个嵌入对象（如图片、文件等，非文本）
            if(!text.isEmpty())  // 如果临时文本不为空，先把积累的文本存为一条消息
            {
                QPixmap pix;  // 图片为空（因为是文本消息）
                insertMsgList(mGetMsgList,"text",text,pix);  // 插入文本消息到结果列表
                text.clear();  // 清空临时文本
            }

            // 在原始消息列表mMsgList中查找对应的嵌入对象消息
            while(indexUrl<count)
            {
                MsgInfo msg =  mMsgList[indexUrl];  // 取一条原始消息
                // 通过HTML内容匹配（因为嵌入对象的信息可能保存在HTML中）
                if(this->document()->toHtml().contains(msg.content,Qt::CaseSensitive))
                {
                    indexUrl++;  // 匹配成功，移动索引
                    mGetMsgList.append(msg);  // 将该对象消息加入结果列表
                    break;  // 跳出循环，继续处理下一个字符
                }
                indexUrl++;  // 不匹配，继续查找下一条原始消息
            }
        }
        else  // 不是特殊字符，说明是普通文本
        {
            text.append(doc[index]);  // 积累到临时文本中
        }
    }
    if(!text.isEmpty())
    {
        QPixmap pix;
        insertMsgList(mGetMsgList,"text",text,pix);
        text.clear();
    }
    mMsgList.clear();
    this->clear();
    return mGetMsgList;
}

void MessageTextEdit::dragEnterEvent(QDragEnterEvent *event)
{
    if(event->source()==this)
        event->ignore();
    else
        event->accept();
}

void MessageTextEdit::dropEvent(QDropEvent *event)
{
    insertFromMimeData(event->mimeData());
    event->accept();
}

void MessageTextEdit::keyPressEvent(QKeyEvent *e)
{
    if((e->key()==Qt::Key_Enter||e->key()==Qt::Key_Return)&& !(e->modifiers() & Qt::ShiftModifier))
    {
        emit send();
        return;
    }
    QTextEdit::keyPressEvent(e);
}

void MessageTextEdit::insertFileFromUrl(const QStringList &urls)
{
    if(urls.isEmpty())
        return;

    foreach (QString url, urls){
        if(isImage(url))
            insertImages(url);
        else
            insertTextFile(url);
    }
}

void MessageTextEdit::insertImages(const QString &url)
{
    QImage image(url);
    //按比例缩放图片
    if(image.width()>120||image.height()>80)
    {
        if(image.width()>image.height())
        {
            image =  image.scaledToWidth(120,Qt::SmoothTransformation);
        }
        else
            image = image.scaledToHeight(80,Qt::SmoothTransformation);
    }
    QTextCursor cursor = this->textCursor();// 记录当前的编辑位置
    // QTextDocument *document = this->document();
    // document->addResource(QTextDocument::ImageResource, QUrl(url), QVariant(image));
    cursor.insertImage(image,url);//往当前位置插入对应类型的内容

    insertMsgList(mMsgList,"image",url,QPixmap::fromImage(image));
}

void MessageTextEdit::insertTextFile(const QString &url)
{
    QFileInfo fileInfo(url);
    if(fileInfo.isDir())// 若拖拽的是文件夹
    {
        PinkMessageBox::info(this,"提示","只允许拖拽单个文件!");
        return;
    }

    if(fileInfo.size()>100*1024*1024)
    {
        PinkMessageBox::info(this,"提示","发送的文件大小不能大于100M");
        return;
    }

    QPixmap pix = getFileIconPixmap(url);
    QTextCursor cursor = this->textCursor();
    cursor.insertImage(pix.toImage(),url);
    insertMsgList(mMsgList,"file",url,pix);
}

bool MessageTextEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return QTextEdit::canInsertFromMimeData(source);
}

void MessageTextEdit::insertFromMimeData(const QMimeData *source)
{
    QStringList urls = getUrl(source->text());

    if(urls.isEmpty())
        return;

    foreach (QString url, urls)
    {
        if(isImage(url))
            insertImages(url);
        else
            insertTextFile(url);
    }
}

bool MessageTextEdit::isImage(QString url)
{
    QString imageFormat = "bmp,jpg,png,tif,gif,pcx,tga,exif,fpx,svg,psd,cdr,pcd,dxf,ufo,eps,ai,raw,wmf,webp";
    QStringList imageFormatList = imageFormat.split(",");
    QFileInfo fileInfo(url);
    QString suffix = fileInfo.suffix();
    if(imageFormatList.contains(suffix,Qt::CaseInsensitive)){
        return true;
    }
    return false;
}

void MessageTextEdit::insertMsgList(QVector<MsgInfo> &list, QString flag, QString text, QPixmap pix)
{
    MsgInfo msg;
    msg.msgFlag=flag;
    msg.content = text;
    msg.pixmap = pix;
    list.append(msg);
}

QStringList MessageTextEdit::getUrl(QString text)
{
    QStringList urls;
    if(text.isEmpty()) return urls;

    // 当同时拖拽多个文件时，系统返回的text中，每个文件的路径会用换行符分隔
    QStringList list = text.split("\n");
    foreach (QString url, list) {
        if(!url.isEmpty()){
            // 单个带前缀路径（如 file:///C:/test.jpg）做处理
            QStringList str = url.split("///");
            if(str.size()>=2)
                // str.at(0)：file:（前缀部分，无用）
                // str.at(1)：C:/test.jpg（纯净的文件路径，有用）
                urls.append(str.at(1));
        }
    }
    return urls;
}

QPixmap MessageTextEdit::getFileIconPixmap(const QString &url)
{
    QFileIconProvider provder;
    QFileInfo fileinfo(url);
    QIcon icon = provder.icon(fileinfo);

    QString strFileSize = getFileSize(fileinfo.size());
    //qDebug() << "FileSize=" << fileinfo.size();

    QFont font(QString("宋体"),10,QFont::Normal,false);
    QFontMetrics fontMetrics(font);
    QSize textSize = fontMetrics.size(Qt::TextSingleLine, fileinfo.fileName());

    QSize FileSize = fontMetrics.size(Qt::TextSingleLine, strFileSize);
    int maxWidth = textSize.width() > FileSize.width() ? textSize.width() :FileSize.width();
    QPixmap pix(50 + maxWidth + 10, 50);
    pix.fill();

    QPainter painter;
    // painter.setRenderHint(QPainter::Antialiasing, true);
    //painter.setFont(font);
    painter.begin(&pix);
    // 文件图标
    QRect rect(0, 0, 50, 50);
    painter.drawPixmap(rect, icon.pixmap(40,40));
    painter.setPen(Qt::black);
    // 文件名称
    QRect rectText(50+10, 3, textSize.width(), textSize.height());
    painter.drawText(rectText, fileinfo.fileName());
    // 文件大小
    QRect rectFile(50+10, textSize.height()+5, FileSize.width(), FileSize.height());
    painter.drawText(rectFile, strFileSize);
    painter.end();
    return pix;
}

QString MessageTextEdit::getFileSize(qint64 size)
{
    QString Unit;
    double num;
    if(size < 1024){
        num = size;
        Unit = "B";
    }
    else if(size < 1024 * 1224){
        num = size / 1024.0;
        Unit = "KB";
    }
    else if(size <  1024 * 1024 * 1024){
        num = size / 1024.0 / 1024.0;
        Unit = "MB";
    }
    else{
        num = size / 1024.0 / 1024.0/ 1024.0;
        Unit = "GB";
    }
    return QString::number(num,'f',2) + " " + Unit;
}

void MessageTextEdit::textEditChanged()
{
    //qDebug() << "text changed!" << endl;
}
