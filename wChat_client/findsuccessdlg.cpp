#include "findsuccessdlg.h"
#include "ui_findsuccessdlg.h"

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
    // 图片缩放到head_lb大小
    head_pix = head_pix.scaled(ui->head_lb->size(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui->head_lb->setPixmap(head_pix);
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
