#include "friendinfopage.h"
#include "ui_friendinfopage.h"
#include "fluenticon.h"
#include <QDebug>

FriendInfoPage::FriendInfoPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FriendInfoPage),_user_info(nullptr)
{
    ui->setupUi(this);
    setObjectName("friend_info_page");

    ui->msg_chat->SetState("normal","hover","press");
    ui->msg_chat->setCursor(Qt::PointingHandCursor);
    // msg_chat now uses Fluent chat glyph + text (pink gradient in QSS).
    // Use a mixed font so the glyph renders as icon and the rest as UI.
    {
        QFont f = ui->msg_chat->font();
        // StyleStrategy: prefer the icon font for unicode PUA range only.
        f.setFamilies({"Segoe Fluent Icons", "Segoe MDL2 Assets",
                       "Microsoft YaHei", "Segoe UI"});
        ui->msg_chat->setFont(f);
        ui->msg_chat->setText(QString(FIC::Chat) + QStringLiteral("  发送消息"));
    }

    ui->video_chat->SetState("normal","hover","press");
    ui->voice_chat->SetState("normal","hover","press");

    // voice/video not implemented end-to-end — hide per Client_UI_Requirements §11.
    ui->video_chat->hide();
    ui->voice_chat->hide();
}

FriendInfoPage::~FriendInfoPage()
{
    delete ui;
}

void FriendInfoPage::SetInfo(std::shared_ptr<UserInfo> user_info)
{
    _user_info = user_info;
    // CircleAvatarLabel does the round clip + centering internally.
    ui->icon_lb->setImagePath(user_info->_icon);
    ui->name_lb->setText(user_info->_name);
    ui->nick_lb->setText(user_info->_nick);
    ui->bak_lb->setText(user_info->_nick);
}

void FriendInfoPage::on_msg_chat_clicked()
{
    emit sig_jump_chat_item(_user_info);
}
