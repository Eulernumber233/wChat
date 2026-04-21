#include "conuseritem.h"
#include "ui_conuseritem.h"
#include "fluenticon.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontDatabase>

// Paints a pink gradient circle with a white Fluent glyph centred in it.
// Used by the "新的朋友" virtual contact and anywhere else an avatar
// position needs a styled icon (not a user photo).
static QPixmap glyphAvatarPixmap(const QString &glyph, int side)
{
    QPixmap pm(side, side);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient g(0, 0, side, side);
    g.setColorAt(0.0, QColor(0xef, 0xb7, 0xc9));
    g.setColorAt(1.0, QColor(0xd9, 0x7a, 0x95));
    p.setBrush(g);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, side, side);

    QFont f;
    const auto fams = QFontDatabase::families();
    if (fams.contains("Segoe Fluent Icons"))     f.setFamily("Segoe Fluent Icons");
    else if (fams.contains("Segoe MDL2 Assets")) f.setFamily("Segoe MDL2 Assets");
    else                                          f.setFamily("Segoe UI Symbol");
    f.setPixelSize(static_cast<int>(side * 0.5));
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(0, 0, side, side), Qt::AlignCenter, glyph);
    return pm;
}

ConUserItem::ConUserItem(QWidget *parent) :
    ListItemBase(parent),
    ui(new Ui::ConUserItem)
{
    ui->setupUi(this);
    SetItemType(ListItemType::CONTACT_USER_ITEM);
    ui->red_point->raise();
    ShowRedPoint(false);
    // Hide the secondary line by default; SetInfo populates if we have data.
    ui->user_chat_lb->hide();
}
ConUserItem::~ConUserItem() { delete ui; }

QSize ConUserItem::sizeHint() const { return QSize(300, 66); }

void ConUserItem::SetInfo(std::shared_ptr<AuthInfo> auth_info)
{
    _info = std::make_shared<UserInfo>(auth_info);
    ui->icon_lb->setImagePath(_info->_icon);
    ui->user_name_lb->setText(_info->_name);
    QString sub = !_info->_last_msg.isEmpty() ? _info->_last_msg : _info->_desc;
    if (!sub.isEmpty()) { ui->user_chat_lb->setText(sub); ui->user_chat_lb->show(); }
}

void ConUserItem::SetInfo(int uid, QString name, QString icon)
{
    _info = std::make_shared<UserInfo>(uid, name, icon);

    // Special case: the "new friend" virtual entry uses an icon resource
    // that doesn't look like a user photo (green plus). Render a pink
    // gradient + Fluent AddFriend glyph instead to fit the palette.
    if (uid == 0) {
        ui->icon_lb->setPixmap(glyphAvatarPixmap(QString(FIC::AddFriend), 44));
        ui->user_name_lb->setText(name);
        ui->user_chat_lb->setText(QStringLiteral("查看好友申请"));
        ui->user_chat_lb->show();
        return;
    }

    ui->icon_lb->setImagePath(_info->_icon);
    ui->user_name_lb->setText(_info->_name);
    QString sub = !_info->_last_msg.isEmpty() ? _info->_last_msg : _info->_desc;
    if (!sub.isEmpty()) { ui->user_chat_lb->setText(sub); ui->user_chat_lb->show(); }
}

void ConUserItem::SetInfo(std::shared_ptr<AuthRsp> auth_rsp)
{
    _info = std::make_shared<UserInfo>(auth_rsp);
    ui->icon_lb->setImagePath(_info->_icon);
    ui->user_name_lb->setText(_info->_name);
    QString sub = !_info->_last_msg.isEmpty() ? _info->_last_msg : _info->_desc;
    if (!sub.isEmpty()) { ui->user_chat_lb->setText(sub); ui->user_chat_lb->show(); }
}

void ConUserItem::ShowRedPoint(bool show)
{
    if (show) ui->red_point->show(); else ui->red_point->hide();
}

std::shared_ptr<UserInfo> ConUserItem::GetInfo() { return _info; }
