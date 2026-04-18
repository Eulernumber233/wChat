#include "applyfrienditem.h"
#include "ui_applyfrienditem.h"
#include <QPainter>
#include <QPainterPath>

static QPixmap afi_round(const QPixmap &src, int side) {
    if (src.isNull() || side <= 0) return src;
    const int sq = std::min(src.width(), src.height());
    const int sx = (src.width()  - sq) / 2;
    const int sy = (src.height() - sq) / 2;
    const QPixmap cropped = src.copy(sx, sy, sq, sq);
    const QPixmap scaled  = cropped.scaled(QSize(side, side),
                                           Qt::IgnoreAspectRatio,
                                           Qt::SmoothTransformation);
    QPixmap out(side, side);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath clip; clip.addEllipse(0, 0, side, side);
    p.setClipPath(clip);
    p.drawPixmap(0, 0, scaled);
    return out;
}

ApplyFriendItem::ApplyFriendItem(QWidget *parent) :
    ListItemBase(parent), _added(false),
    ui(new Ui::ApplyFriendItem)
{
    ui->setupUi(this);
    SetItemType(ListItemType::APPLY_FRIEND_ITEM);
    ui->addBtn->SetState("normal","hover", "press");
    ui->addBtn->hide();
    connect(ui->addBtn, &ClickedBtn::clicked,  [this](){
        emit this->sig_auth_friend(_apply_info);
    });
}
ApplyFriendItem::~ApplyFriendItem()
{
    delete ui;
}
void ApplyFriendItem::SetInfo(std::shared_ptr<ApplyInfo> apply_info)
{
    _apply_info = apply_info;
    QPixmap pixmap(_apply_info->_icon);
    const int side = std::min(ui->icon_lb->width(), ui->icon_lb->height());
    ui->icon_lb->setScaledContents(false);
    ui->icon_lb->setAlignment(Qt::AlignCenter);
    ui->icon_lb->setPixmap(afi_round(pixmap, side > 0 ? side : 44));
    ui->user_name_lb->setText(_apply_info->_name);
    ui->user_chat_lb->setText(_apply_info->_desc);
}
void ApplyFriendItem::ShowAddBtn(bool bshow)
{
    if (bshow) {
        ui->addBtn->show();
        ui->already_add_lb->hide();
        _added = false;
    }
    else {
        ui->addBtn->hide();
        ui->already_add_lb->show();
        _added = true;
    }
}
int ApplyFriendItem::GetUid() {
    return _apply_info->_uid;
}
