#include "findsuccessdlg.h"
#include "ui_findsuccessdlg.h"
#include <QPainter>
#include <QPainterPath>

static QPixmap fsd_round(const QPixmap &src, int side) {
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

FindSuccessDlg::FindSuccessDlg(QWidget *parent) :
    QDialog(parent),_parent(parent),
    ui(new Ui::FindSuccessDlg)
{
    ui->setupUi(this);
    // 设置对话框标题
    setWindowTitle("添加");
    // 隐藏对话框标题栏
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);


    ui->add_friend_btn->SetState("normal","hover","press");
    this->setModal(true);
}
FindSuccessDlg::~FindSuccessDlg()
{
    qDebug()<<"FindSuccessDlg destruct";
    delete ui;
}
void FindSuccessDlg::SetSearchInfo(std::shared_ptr<SearchInfo> si)
{
    ui->name_lb->setText(si->_name);
    _si = si;
    QPixmap head_pix(si->_icon);
    const int side = std::min(ui->head_lb->width(), ui->head_lb->height());
    ui->head_lb->setScaledContents(false);
    ui->head_lb->setAlignment(Qt::AlignCenter);
    ui->head_lb->setPixmap(fsd_round(head_pix, side > 0 ? side : 72));
}
void FindSuccessDlg::on_add_friend_btn_clicked()
{
    //todo... 添加好友界面弹出
    this->hide();
    auto applyFriend = new ApplyFriend(_parent);
    applyFriend->SetSearchInfo(_si);
    applyFriend->setModal(true);
    applyFriend->show();
}
