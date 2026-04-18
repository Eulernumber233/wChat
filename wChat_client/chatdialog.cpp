#include "chatdialog.h"
#include "ui_chatdialog.h"
#include "conuseritem.h"
#include "filemgr.h"
#include "picturebubble.h"
#include "chatitembase.h"
#include "localdb.h"
#include "fluenticon.h"
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QFontDatabase>

// Render a Segoe Fluent Icons glyph to a QPixmap at the given pixel size
// and color. Used when a control only accepts QIcon (QLineEdit::addAction,
// QAbstractButton::setIcon) and we still want font-based icons.
static QPixmap glyphToPixmap(const QString &glyph, int pixelSize, const QColor &color) {
    QPixmap pm(pixelSize, pixelSize);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont f;
    const auto fams = QFontDatabase::families();
    if (fams.contains("Segoe Fluent Icons"))       f.setFamily("Segoe Fluent Icons");
    else if (fams.contains("Segoe MDL2 Assets"))   f.setFamily("Segoe MDL2 Assets");
    else                                            f.setFamily("Segoe UI Symbol");
    f.setPixelSize(pixelSize - 2);
    p.setFont(f);
    p.setPen(color);
    p.drawText(QRect(0, 0, pixelSize, pixelSize), Qt::AlignCenter, glyph);
    return pm;
}

// Clip a square pixmap to a circle of the same size (keeps storage
// rectangular as per user requirement). Used for avatar rendering on
// QLabel targets that can't easily be swapped to CircleAvatarLabel.
static QPixmap clipToCircle(const QPixmap &src, int side) {
    if (src.isNull() || side <= 0) return src;
    const QPixmap scaled = src.scaled(QSize(side, side),
                                      Qt::KeepAspectRatioByExpanding,
                                      Qt::SmoothTransformation);
    QPixmap out(side, side);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath clip;
    clip.addEllipse(0, 0, side, side);
    p.setClipPath(clip);
    const int sx = (scaled.width()  - side) / 2;
    const int sy = (scaled.height() - side) / 2;
    p.drawPixmap(0, 0, scaled, sx, sy, side, side);
    return out;
}
ChatDialog::ChatDialog(QWidget *parent):
    QDialog(parent), ui(new Ui::ChatDialog),_mode(ChatUIMode::ChatMode),
    _state(ChatUIMode::ChatMode),_b_loading(false),_cur_chat_uid(0),_last_widget(nullptr)
{
    ui->setupUi(this);
    setObjectName("ChatDialog");

    // add-friend button: Fluent plus glyph (replaces old png + border-image)
    FIC::applyIconFont(ui->add_btn, 16);
    ui->add_btn->setText(QString(FIC::Add));
    ui->add_btn->SetState("normal","hover","press");
    ui->add_btn->setProperty("state","normal");
    ui->add_btn->setCursor(Qt::PointingHandCursor);

    // Sidebar nav: Fluent glyphs inside StateWidget
    ui->side_chat_lb->SetGlyph(QString(FIC::Chat), 20);
    ui->side_contact_lb->SetGlyph(QString(FIC::People), 20);
    ui->side_chat_lb->setCursor(Qt::PointingHandCursor);
    ui->side_contact_lb->setCursor(Qt::PointingHandCursor);

    ui->search_edit->SetMaxLength(15);
    // Leading magnifier glyph (Fluent Icons rendered to pixmap).
    QAction *searchAction = new QAction(ui->search_edit);
    searchAction->setIcon(QIcon(glyphToPixmap(QString(FIC::Search), 16, QColor("#8a838f"))));
    ui->search_edit->addAction(searchAction, QLineEdit::LeadingPosition);
    ui->search_edit->setPlaceholderText(QStringLiteral("搜索用户 UID"));

    // Trailing clear glyph: shown only when there's text.
    const QIcon clearIconOn  = QIcon(glyphToPixmap(QString(FIC::Close), 14, QColor("#8a838f")));
    const QIcon clearIconOff = QIcon(QPixmap(1, 1));  // invisible placeholder
    QAction *clearAction = new QAction(ui->search_edit);
    clearAction->setIcon(clearIconOff);
    ui->search_edit->addAction(clearAction, QLineEdit::TrailingPosition);

    connect(ui->search_edit, &QLineEdit::textChanged, [clearAction, clearIconOn, clearIconOff](const QString &text) {
        clearAction->setIcon(text.isEmpty() ? clearIconOff : clearIconOn);
    });
    connect(clearAction, &QAction::triggered, [this, clearAction, clearIconOff]() {
        ui->search_edit->clear();
        clearAction->setIcon(clearIconOff);
        ui->search_edit->clearFocus();
        ShowSearch(false);
    });
    // 当搜索框输入时
    connect(ui->search_edit,&QLineEdit::textChanged,this,&ChatDialog::slot_text_changed);
    ui->search_edit->SetMaxLength(15);
    ShowSearch(false);

    ui->search_list->SetSearchEdit(ui->search_edit);
    //连接searchlist跳转聊天信号
    connect(ui->search_list, &SearchList::sig_jump_chat_item, this, &ChatDialog::slot_jump_chat_item);

    //连接清除搜索框操作
    connect(ui->friend_apply_page, &ApplyFriendPage::sig_show_search, this, &ChatDialog::slot_show_search);


    //会话列表栏与联系人列表栏增加信号
    connect(ui->chat_user_list,&ChatUserList::sig_loading_chat_user,
            this,&ChatDialog::slot_loading_chat_user);
    connect(ui->con_user_list,&ContactUserList::sig_loading_contact_user,
            this,&ChatDialog::slot_loading_contact_user);

    addChatUserList();
    QString icon = UserMgr::GetInstance()->GetUserInfo()->_icon;
    // CircleAvatarLabel handles cropping + centring internally so the
    // source image can stay rectangular and display as a centred circle.
    ui->side_head_lb->setImagePath(icon);

    ui->side_chat_lb->setProperty("state","normal");
    ui->side_chat_lb->SetState("normal","hover","pressed","selected_normal","selected_hover","selected_pressed");
    ui->side_contact_lb->SetState("normal","hover","pressed","selected_normal","selected_hover","selected_pressed");

    AddLBGroup(ui->side_chat_lb);
    AddLBGroup(ui->side_contact_lb);

    //连接侧边导航栏按钮，切换会话列表栏，联系人列表栏两功能区的信号与切换的槽函数
    connect(ui->side_chat_lb, &StateWidget::clicked, this, &ChatDialog::slot_side_chat);
    connect(ui->side_contact_lb, &StateWidget::clicked, this, &ChatDialog::slot_side_contact);


    this->installEventFilter(this);

    ui->side_chat_lb->SetSelected(true);


    //连接申请添加好友信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_friend_apply, this, &ChatDialog::slot_apply_friend);

    //连接认证添加好友信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_add_auth_friend, this, &ChatDialog::slot_add_auth_friend);


    //链接自己认证回复信号
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_auth_rsp, this,
            &ChatDialog::slot_auth_rsp);
    //连接点击联系人item发出的信号和用户信息展示槽函数
    connect(ui->con_user_list, &ContactUserList::sig_switch_friend_info_page,
            this,&ChatDialog::slot_friend_info_page);
    //连接联系人页面点击好友申请条目的信号
    connect(ui->con_user_list, &ContactUserList::sig_switch_apply_friend_page,
            this,&ChatDialog::slot_switch_apply_friend_page);

    //连接好友信息界面发送的点击事件
    connect(ui->friend_info_page, &FriendInfoPage::sig_jump_chat_item, this,
            &ChatDialog::slot_jump_chat_item_from_infopage);

    //连接聊天列表点击信号
    connect(ui->chat_user_list, &QListWidget::itemClicked, this, &ChatDialog::slot_item_clicked);


    connect(ui->chat_page, &ChatPage::sig_append_send_chat_msg, this, &ChatDialog::slot_append_send_chat_msg);

    //连接对端消息通知
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_text_chat_msg, this, &ChatDialog::slot_text_chat_msg);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_text_chat_msg_rsp, this, &ChatDialog::slot_text_chat_msg_rsp);

    // File transfer connections
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_file_upload_rsp,
            this, &ChatDialog::slot_file_upload_rsp);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_file_msg_notify,
            this, &ChatDialog::slot_file_msg_notify);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_file_upload_persisted,
            this, &ChatDialog::slot_file_upload_persisted);

    // STAGE-C: conversation summary (fires once after login)
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_conv_summary,
            this, &ChatDialog::slot_conv_summary);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_pull_messages_rsp,
            this, &ChatDialog::slot_pull_messages_rsp);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_download_token_rsp,
            this, &ChatDialog::slot_download_token_rsp);

    // 心跳定时器：每 30 秒发送一次心跳包
    _heartbeat_timer = new QTimer(this);
    connect(_heartbeat_timer, &QTimer::timeout, this, &ChatDialog::sendHeartbeat);
    _heartbeat_timer->start(30000); // 30秒
}

void ChatDialog::sendHeartbeat()
{
    int uid = UserMgr::GetInstance()->GetUid();
    if (uid <= 0) return; // Reset 后不再发送

    // 检查上次心跳回复是否超时（90 秒，对应 3 次心跳周期未响应）
    // 用于检测"半死 TCP 连接"——socket 还开着但链路已断，send 不会立即报错
    const qint64 HEARTBEAT_TIMEOUT_MS = 90000;
    auto tcp = TcpMgr::GetInstance();
    qint64 last_rsp = tcp->GetLastHeartbeatRspMs();
    qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    if (last_rsp > 0 && (now_ms - last_rsp) > HEARTBEAT_TIMEOUT_MS) {
        qWarning() << "Heartbeat response timeout:" << (now_ms - last_rsp) << "ms";
        // 主动关闭连接，触发 sig_connection_lost → slotBackToLogin
        tcp->CloseConnection();
        return;
    }

    QJsonObject obj;
    obj["fromuid"] = uid;
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    emit tcp->sig_send_data(ReqId::ID_HEART_BEAT_REQ, data);
}

ChatDialog::~ChatDialog()
{
    if (_heartbeat_timer) {
        _heartbeat_timer->stop();
        _heartbeat_timer->disconnect(); // 切断所有信号连接，防止已入队的 timeout 继续触发
    }
    delete ui;
}

void ChatDialog::ShowSearch(bool bsearch)
{
    if(bsearch){
        ui->chat_user_list->hide();
        ui->con_user_list->hide();
        ui->search_list->show();
        _mode = ChatUIMode::SearchMode;
    }else if(_state == ChatUIMode::ChatMode){
        ui->chat_user_list->show();
        ui->con_user_list->hide();
        ui->search_list->hide();
        _mode = ChatUIMode::ChatMode;
    }else if(_state == ChatUIMode::ContactMode){
        ui->chat_user_list->hide();
        ui->search_list->hide();
        ui->con_user_list->show();
        _mode = ChatUIMode::ContactMode;
    }
}

void ChatDialog::AddLBGroup(StateWidget *lb)
{
    _lb_list.push_back(lb);
}


void ChatDialog::addChatUserList()
{
    //先按照好友列表加载聊天记录，等以后客户端实现聊天记录数据库之后再按照最后信息排序
    auto friend_list = UserMgr::GetInstance()->GetChatListPerPage();
    if (friend_list.empty() == false) {
        const int self_uid = UserMgr::GetInstance()->GetUid();
        for(auto & friend_ele : friend_list){
            auto find_iter = _chat_items_added.find(friend_ele->_uid);
            if(find_iter != _chat_items_added.end()){
                continue;
            }
            auto user_info = std::make_shared<UserInfo>(friend_ele);

            // Fallback: if the server summary didn't fill _last_msg yet,
            // ask LocalDb for the most recent message with this peer and
            // show it as the preview. This makes the conv list actually
            // reflect local history on first render (req #7).
            if (user_info->_last_msg.isEmpty()) {
                auto rows = LocalDb::Inst().LoadRecent(friend_ele->_uid, 1);
                if (!rows.isEmpty()) {
                    auto tds = LocalDb::RowToTextChatData(rows.back(), self_uid);
                    if (!tds.isEmpty()) {
                        user_info->_last_msg = tds.back()->_msg_content;
                    }
                }
            }

            auto *chat_user_wid = new ChatUserWid();
            chat_user_wid->SetInfo(user_info);
            QListWidgetItem *item = new QListWidgetItem;
            item->setSizeHint(chat_user_wid->sizeHint());
            ui->chat_user_list->addItem(item);
            ui->chat_user_list->setItemWidget(item, chat_user_wid);
            _chat_items_added.insert(friend_ele->_uid, item);
        }

        //更新已加载条目
        UserMgr::GetInstance()->UpdateChatLoadedCount();
    }

    // // 创建QListWidgetItem，并设置自定义的widget
    // for(int i = 0; i < 13; i++){
    //     int randomValue = QRandomGenerator::global()->bounded(100); // 生成0到99之间的随机整数
    //     int str_i = randomValue%strs.size();
    //     int head_i = randomValue%heads.size();
    //     int name_i = randomValue%names.size();

    //     auto *chat_user_wid = new ChatUserWid();
    //     auto user_info=std::make_shared<UserInfo>(i,names[name_i],names[name_i],heads[head_i],1, strs[str_i],"");
    //     chat_user_wid->SetInfo(user_info);
    //     QListWidgetItem *item = new QListWidgetItem;
    //     //qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
    //     item->setSizeHint(chat_user_wid->sizeHint());
    //     ui->chat_user_list->addItem(item);
    //     ui->chat_user_list->setItemWidget(item, chat_user_wid);
    // }
}

void ChatDialog::slot_loading_chat_user()
{
    if(_b_loading){
        return;
    }

    LoadingDlg *loadingDialog = new LoadingDlg(this);
    loadingDialog->setModal(true);
    loadingDialog->show();
    qDebug() << "add new data to list.....";
    loadMoreChatUser();
    // 加载完成后关闭对话框
    loadingDialog->deleteLater();

    _b_loading = false;
}

void ChatDialog::slot_loading_contact_user()
{
    qDebug() << "slot loading contact user";
    if(_b_loading){
        return;
    }

    _b_loading = true;
    LoadingDlg *loadingDialog = new LoadingDlg(this);
    loadingDialog->setModal(true);
    loadingDialog->show();
    qDebug() << "add new data to list.....";
    loadMoreConUser();
    // 加载完成后关闭对话框
    loadingDialog->deleteLater();

    _b_loading = false;
}

void ChatDialog::slot_side_chat()
{
    qDebug()<< "receive side chat clicked";
    ClearLabelState(ui->side_chat_lb);
    ui->stackedWidget->setCurrentWidget(ui->chat_page);
    _state = ChatUIMode::ChatMode;
    ShowSearch(false);
}

void ChatDialog::slot_side_contact()
{
    qDebug()<< "receive side contact clicked";
    ClearLabelState(ui->side_contact_lb);
    //设置
    ui->stackedWidget->setCurrentWidget(ui->friend_apply_page);
    _state = ChatUIMode::ContactMode;
    ShowSearch(false);
}

void ChatDialog::ClearLabelState(StateWidget *lb)
{
    for(auto & ele: _lb_list){
        if(ele == lb){
            continue;
        }
        ele->ClearState();
    }
}


void ChatDialog::slot_text_changed(const QString &str)
{
    if(!str.isEmpty()){
        ShowSearch(true);
    }
}



bool ChatDialog::eventFilter(QObject *watched, QEvent *event)
{
    if(event->type()==QEvent::MouseButtonPress){
        QMouseEvent*mouseEvent = static_cast<QMouseEvent*>(event);
        handleGlobalMousePress(mouseEvent);
    }
    return QDialog::eventFilter(watched,event);
}



void ChatDialog::handleGlobalMousePress(QMouseEvent *event)
{
    // 实现点击位置的判断和处理逻辑
    // 先判断是否处于搜索模式，如果不处于搜索模式则直接返回
    if( _mode != ChatUIMode::SearchMode){
        return;
    }
    // 将鼠标点击位置转换为搜索列表坐标系中的位置
    QPoint posInSearchList = ui->search_list->mapFromGlobal(event->globalPos());
    // 判断点击位置是否在聊天列表的范围内
    if (!ui->search_list->rect().contains(posInSearchList)) {
        // 如果不在聊天列表内，清空输入框
        ui->search_edit->clear();
        ShowSearch(false);
    }
}



void ChatDialog::slot_apply_friend(std::shared_ptr<AddFriendApply> apply)
{
    qDebug() << "receive apply friend slot, applyuid is " << apply->_from_uid << " name is "
             << apply->_name << " desc is " << apply->_desc;

    bool b_already = UserMgr::GetInstance()->AlreadyApply(apply->_from_uid);
    if(b_already){
        return;
    }

    UserMgr::GetInstance()->AddApplyList(std::make_shared<ApplyInfo>(apply));
    ui->side_contact_lb->ShowRedPoint(true);
    ui->con_user_list->ShowRedPoint(true);
    ui->friend_apply_page->AddNewApply(apply);
}



void ChatDialog::slot_add_auth_friend(std::shared_ptr<AuthInfo> auth_info) {
    qDebug() << "receive slot_add_auth__friend uid is " << auth_info->_uid
             << " name is " << auth_info->_name << " nick is " << auth_info->_nick;

    //判断如果已经是好友则跳过
    auto bfriend = UserMgr::GetInstance()->CheckFriendById(auth_info->_uid);
    if(bfriend){
        return;
    }

    UserMgr::GetInstance()->AddFriend(auth_info);

    auto* chat_user_wid = new ChatUserWid();
    auto user_info = std::make_shared<UserInfo>(auth_info);
    chat_user_wid->SetInfo(user_info);
    QListWidgetItem* item = new QListWidgetItem;
    //qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
    item->setSizeHint(chat_user_wid->sizeHint());
    ui->chat_user_list->insertItem(0, item);
    ui->chat_user_list->setItemWidget(item, chat_user_wid);
    _chat_items_added.insert(auth_info->_uid, item);
}

void ChatDialog::slot_auth_rsp(std::shared_ptr<AuthRsp> auth_rsp)
{
    qDebug() << "receive slot_auth_rsp uid is " << auth_rsp->_uid
             << " name is " << auth_rsp->_name << " nick is " << auth_rsp->_nick;

    //判断如果已经是好友则跳过
    auto bfriend = UserMgr::GetInstance()->CheckFriendById(auth_rsp->_uid);
    if(bfriend){
        return;
    }

    UserMgr::GetInstance()->AddFriend(auth_rsp);

    auto* chat_user_wid = new ChatUserWid();
    auto user_info = std::make_shared<UserInfo>(auth_rsp);
    chat_user_wid->SetInfo(user_info);
    QListWidgetItem* item = new QListWidgetItem;
    //qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
    item->setSizeHint(chat_user_wid->sizeHint());
    ui->chat_user_list->insertItem(0, item);
    ui->chat_user_list->setItemWidget(item, chat_user_wid);
    _chat_items_added.insert(auth_rsp->_uid, item);
}


void ChatDialog::slot_jump_chat_item(std::shared_ptr<SearchInfo> si)
{
    qDebug() << "slot jump chat item " << Qt::endl;
    auto find_iter = _chat_items_added.find(si->_uid);
    if(find_iter != _chat_items_added.end()){
        qDebug() << "jump to chat item , uid is " << si->_uid;
        ui->chat_user_list->scrollToItem(find_iter.value());
        ui->side_chat_lb->SetSelected(true);
        SetSelectChatItem(si->_uid);
        //更新聊天界面信息
        SetSelectChatPage(si->_uid);
        slot_side_chat();
        return;
    }

    //如果没找到，则创建新的插入listwidget

    auto* chat_user_wid = new ChatUserWid();
    auto user_info = std::make_shared<UserInfo>(si);
    chat_user_wid->SetInfo(user_info);
    QListWidgetItem* item = new QListWidgetItem;
    //qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
    item->setSizeHint(chat_user_wid->sizeHint());
    ui->chat_user_list->insertItem(0, item);
    ui->chat_user_list->setItemWidget(item, chat_user_wid);

    _chat_items_added.insert(si->_uid, item);

    ui->side_chat_lb->SetSelected(true);
    SetSelectChatItem(si->_uid);
    //更新聊天界面信息
    SetSelectChatPage(si->_uid);
    slot_side_chat();
}

void ChatDialog::slot_friend_info_page(std::shared_ptr<UserInfo> user_info)
{
    qDebug()<<"receive switch friend info page sig";
    _last_widget = ui->friend_info_page;
    ui->stackedWidget->setCurrentWidget(ui->friend_info_page);
    ui->friend_info_page->SetInfo(user_info);
}



void ChatDialog::SetSelectChatItem(int uid)
{
    if(ui->chat_user_list->count() <= 0){
        return;
    }

    if(uid == 0){
        ui->chat_user_list->setCurrentRow(0);
        QListWidgetItem *firstItem = ui->chat_user_list->item(0);
        if(!firstItem){
            return;
        }

        //转为widget
        QWidget *widget = ui->chat_user_list->itemWidget(firstItem);
        if(!widget){
            return;
        }

        auto con_item = qobject_cast<ChatUserWid*>(widget);
        if(!con_item){
            return;
        }

        _cur_chat_uid = con_item->GetUserInfo()->_uid;

        return;
    }

    auto find_iter = _chat_items_added.find(uid);
    if(find_iter == _chat_items_added.end()){
        qDebug() << "uid " <<uid<< " not found, set curent row 0";
        ui->chat_user_list->setCurrentRow(0);
        return;
    }

    ui->chat_user_list->setCurrentItem(find_iter.value());

    _cur_chat_uid = uid;
}

void ChatDialog::SetSelectChatPage(int uid)
{
    if( ui->chat_user_list->count() <= 0){
        return;
    }

    if (uid == 0) {
        auto item = ui->chat_user_list->item(0);
        //转为widget
        QWidget* widget = ui->chat_user_list->itemWidget(item);
        if (!widget) {
            return;
        }

        auto con_item = qobject_cast<ChatUserWid*>(widget);
        if (!con_item) {
            return;
        }

        //设置信息
        auto user_info = con_item->GetUserInfo();
        ui->chat_page->SetUserInfo(user_info);
        return;
    }

    auto find_iter = _chat_items_added.find(uid);
    if(find_iter == _chat_items_added.end()){
        return;
    }

    //转为widget
    QWidget *widget = ui->chat_user_list->itemWidget(find_iter.value());
    if(!widget){
        return;
    }

    //判断转化为自定义的widget
    // 对自定义widget进行操作， 将item 转化为基类ListItemBase
    ListItemBase *customItem = qobject_cast<ListItemBase*>(widget);
    if(!customItem){
        qDebug()<< "qobject_cast<ListItemBase*>(widget) is nullptr";
        return;
    }

    auto itemType = customItem->GetItemType();
    if(itemType == CHAT_USER_ITEM){
        auto con_item = qobject_cast<ChatUserWid*>(customItem);
        if(!con_item){
            return;
        }

        //设置信息
        auto user_info = con_item->GetUserInfo();
        ui->chat_page->SetUserInfo(user_info);

        return;
    }

}


void ChatDialog::loadMoreChatUser()
{
    auto friend_list = UserMgr::GetInstance()->GetChatListPerPage();
    if (friend_list.empty() == false) {
        for(auto & friend_ele : friend_list){
            auto find_iter = _chat_items_added.find(friend_ele->_uid);
            if(find_iter != _chat_items_added.end()){
                continue;
            }
            auto *chat_user_wid = new ChatUserWid();
            auto user_info = std::make_shared<UserInfo>(friend_ele);
            chat_user_wid->SetInfo(user_info);
            QListWidgetItem *item = new QListWidgetItem;
            //qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
            item->setSizeHint(chat_user_wid->sizeHint());
            ui->chat_user_list->addItem(item);
            ui->chat_user_list->setItemWidget(item, chat_user_wid);
            _chat_items_added.insert(friend_ele->_uid, item);
        }

        //更新已加载条目
        UserMgr::GetInstance()->UpdateChatLoadedCount();
    }
}



void ChatDialog::loadMoreConUser()
{
    auto friend_list = UserMgr::GetInstance()->GetConListPerPage();
    if (friend_list.empty() == false) {
        for(auto & friend_ele : friend_list){
            auto *chat_user_wid = new ConUserItem();
            chat_user_wid->SetInfo(friend_ele->_uid,friend_ele->_name,
                                   friend_ele->_icon);
            QListWidgetItem *item = new QListWidgetItem;
            //qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
            item->setSizeHint(chat_user_wid->sizeHint());
            ui->con_user_list->addItem(item);
            ui->con_user_list->setItemWidget(item, chat_user_wid);
        }

        //更新已加载条目
        UserMgr::GetInstance()->UpdateContactLoadedCount();
    }
}


void ChatDialog::slot_switch_apply_friend_page()
{
    qDebug()<<"receive switch apply friend page sig";
    _last_widget = ui->friend_apply_page;
    ui->stackedWidget->setCurrentWidget(ui->friend_apply_page);
}



void ChatDialog::slot_jump_chat_item_from_infopage(std::shared_ptr<UserInfo> user_info)
{
    qDebug() << "slot jump chat item " << Qt::endl;
    auto find_iter = _chat_items_added.find(user_info->_uid);
    if(find_iter != _chat_items_added.end()){
        qDebug() << "jump to chat item , uid is " << user_info->_uid;
        ui->chat_user_list->scrollToItem(find_iter.value());
        ui->side_chat_lb->SetSelected(true);
        SetSelectChatItem(user_info->_uid);
        //更新聊天界面信息
        SetSelectChatPage(user_info->_uid);
        slot_side_chat();
        return;
    }

    //如果没找到，则创建新的插入listwidget

    auto* chat_user_wid = new ChatUserWid();
    chat_user_wid->SetInfo(user_info);
    QListWidgetItem* item = new QListWidgetItem;
    //qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
    item->setSizeHint(chat_user_wid->sizeHint());
    ui->chat_user_list->insertItem(0, item);
    ui->chat_user_list->setItemWidget(item, chat_user_wid);

    _chat_items_added.insert(user_info->_uid, item);

    ui->side_chat_lb->SetSelected(true);
    SetSelectChatItem(user_info->_uid);
    //更新聊天界面信息
    SetSelectChatPage(user_info->_uid);
    slot_side_chat();
}



void ChatDialog::slot_item_clicked(QListWidgetItem *item)
{
    QWidget *widget = ui->chat_user_list->itemWidget(item); // 获取自定义widget对象
    if(!widget){
        qDebug()<< "slot item clicked widget is nullptr";
        return;
    }

    // 对自定义widget进行操作， 将item 转化为基类ListItemBase
    ListItemBase *customItem = qobject_cast<ListItemBase*>(widget);
    if(!customItem){
        qDebug()<< "slot item clicked widget is nullptr";
        return;
    }

    auto itemType = customItem->GetItemType();
    if(itemType == ListItemType::INVALID_ITEM
        || itemType == ListItemType::GROUP_TIP_ITEM){
        qDebug()<< "slot invalid item clicked ";
        return;
    }


    if(itemType == ListItemType::CHAT_USER_ITEM){
        // 创建对话框，提示用户
        qDebug()<< "contact user item clicked ";

        auto chat_wid = qobject_cast<ChatUserWid*>(customItem);
        auto user_info = chat_wid->GetUserInfo();
        //跳转到聊天界面
        ui->chat_page->SetUserInfo(user_info);
        _cur_chat_uid = user_info->_uid;
        return;
    }
}

void ChatDialog::slot_append_send_chat_msg(std::shared_ptr<TextChatData> msgdata) {
    if (_cur_chat_uid == 0) {
        return;
    }

    auto find_iter = _chat_items_added.find(_cur_chat_uid);
    if (find_iter == _chat_items_added.end()) {
        return;
    }

    //转为widget
    QWidget* widget = ui->chat_user_list->itemWidget(find_iter.value());
    if (!widget) {
        return;
    }

    //判断转化为自定义的widget
    // 对自定义widget进行操作， 将item 转化为基类ListItemBase
    ListItemBase* customItem = qobject_cast<ListItemBase*>(widget);
    if (!customItem) {
        qDebug() << "qobject_cast<ListItemBase*>(widget) is nullptr";
        return;
    }

    auto itemType = customItem->GetItemType();
    if (itemType == CHAT_USER_ITEM) {
        auto con_item = qobject_cast<ChatUserWid*>(customItem);
        if (!con_item) {
            return;
        }

        //设置信息
        std::shared_ptr<UserInfo> user_info = con_item->GetUserInfo();
        // 将消息写入好友聊天窗口里保存好友信息的userinfo里
        user_info->_chat_msgs.push_back(msgdata);
        std::vector<std::shared_ptr<TextChatData>> msg_vec;
        msg_vec.push_back(msgdata);
        // 将消息放入用户的好友信息里
        UserMgr::GetInstance()->AppendFriendChatMsg(_cur_chat_uid,msg_vec);
        // Also refresh the conv-list row's last_msg preview (req #7: show
        // last message in the sidebar list and keep it in sync on send).
        con_item->updateLastMsg(msg_vec);
        return;
    }
}






void ChatDialog::slot_text_chat_msg(std::shared_ptr<TextChatMsg> msg)
{
    // STAGE-C: persist same-server text messages (msg_db_id > 0). Cross-server
    // forwards arrive with 0 and rely on the next SetUserInfo refresh.
    PersistTextMsgToLocalDb(msg);

    auto find_iter = _chat_items_added.find(msg->_from_uid);
    if(find_iter != _chat_items_added.end()){
        qDebug() << "set chat item msg, uid is " << msg->_from_uid;
        QWidget *widget = ui->chat_user_list->itemWidget(find_iter.value());
        auto chat_wid = qobject_cast<ChatUserWid*>(widget);
        if(!chat_wid){
            return;
        }
        chat_wid->updateLastMsg(msg->_chat_msgs);
        //更新当前聊天页面记录
        UpdateChatMsg(msg->_chat_msgs);
        UserMgr::GetInstance()->AppendFriendChatMsg(msg->_from_uid,msg->_chat_msgs);
        return;
    }

    //如果没找到，则创建新的插入listwidget

    auto* chat_user_wid = new ChatUserWid();
    //查询好友信息
    auto fi_ptr = UserMgr::GetInstance()->GetFriendById(msg->_from_uid);
    chat_user_wid->SetInfo(fi_ptr);
    QListWidgetItem* item = new QListWidgetItem;
    //qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
    item->setSizeHint(chat_user_wid->sizeHint());
    chat_user_wid->updateLastMsg(msg->_chat_msgs);
    UserMgr::GetInstance()->AppendFriendChatMsg(msg->_from_uid,msg->_chat_msgs);
    ui->chat_user_list->insertItem(0, item);
    ui->chat_user_list->setItemWidget(item, chat_user_wid);
    _chat_items_added.insert(msg->_from_uid, item);

}


void ChatDialog::UpdateChatMsg(std::vector<std::shared_ptr<TextChatData> > msgdata)
{
    for(auto & msg : msgdata){
        if(msg->_from_uid != _cur_chat_uid){
            break;
        }

        ui->chat_page->AppendChatMsg(msg);
    }
}


void ChatDialog::slot_show_search(bool show)
{
    ShowSearch(show);
}

// =====================================================================
// File transfer slots
// =====================================================================

void ChatDialog::slot_file_upload_rsp(QString file_id, QString file_token,
                                       QString host, QString port,
                                       QString local_path, int error) {
    if (error != 0) {
        qDebug() << "File upload request failed, error=" << error;
        return;
    }

    // Find local_path from ChatPage's pending map
    // The server doesn't echo local_path, so we need to look it up.
    // For now, use the ChatPage's _pending_file_paths by filename (simplified).
    // A more robust approach would use file_id as key.
    if (local_path.isEmpty()) {
        local_path = ui->chat_page->PopPendingFilePath();
    }

    if (local_path.isEmpty()) {
        qDebug() << "File upload RSP: cannot find local path for file_id=" << file_id;
        return;
    }

    qDebug() << "Starting upload: file_id=" << file_id << " path=" << local_path;

    // STAGE-C: copy the source file into the per-user cache under its
    // server-assigned file_id so the sender has a permanent local copy
    // that survives the source being moved/deleted. GetCachedPath in
    // future sessions will hit this file directly.
    QString target = FileMgr::GetInstance()->BuildCachedPath(
        file_id, QFileInfo(local_path).fileName());
    if (!target.isEmpty() && target != local_path) {
        if (QFile::exists(target)) QFile::remove(target);
        if (QFile::copy(local_path, target)) {
            qDebug() << "sender-side file cached at" << target;
        } else {
            qWarning() << "sender-side cache copy failed:" << local_path
                       << "->" << target;
            target.clear();
        }
    }
    // Remember {file_id -> cached copy path} for slot_file_upload_persisted.
    _pending_file_copies.insert(file_id, target.isEmpty() ? local_path : target);

    FileMgr::GetInstance()->StartUpload(file_id, file_token, host, port, local_path);
}

void ChatDialog::slot_file_upload_persisted(std::shared_ptr<FileChatData> file_data) {
    // STAGE-C: the file message is now live in MySQL (chat_messages) with
    // the msg_db_id we got. Mirror it into LocalDb so the sender's UI
    // stays consistent after switching chats.
    QString cached_path = _pending_file_copies.take(file_data->_file_id);

    // Build the content JSON matching exactly what GetMessagesPage returns
    // (server stores a single object for file messages).
    QJsonObject content;
    content["msgid"] = file_data->_msg_id;
    content["file_id"] = file_data->_file_id;
    content["file_name"] = file_data->_file_name;
    content["file_size"] = static_cast<double>(file_data->_file_size);
    content["file_type"] = file_data->_file_type;

    LocalDb::MsgRow row;
    row.msg_db_id = file_data->_msg_db_id;
    row.peer_uid  = file_data->_to_uid; // sender path: peer is receiver
    row.direction = 1;                   // send
    row.msg_type  = MSG_TYPE_IMAGE + file_data->_file_type;
    row.content   = QString::fromUtf8(
        QJsonDocument(content).toJson(QJsonDocument::Compact));
    row.send_time = QDateTime::currentSecsSinceEpoch();
    row.status    = 0;
    QVector<LocalDb::MsgRow> rows; rows.append(row);
    LocalDb::Inst().UpsertMessages(rows);

    qint64 cur_hwm = LocalDb::Inst().GetLastSyncedMsgId(row.peer_uid);
    if (row.msg_db_id > cur_hwm) {
        LocalDb::Inst().SetLastSyncedMsgId(row.peer_uid, row.msg_db_id);
    }

    // Also record the file in local_files so future opens hit the cached path.
    if (!cached_path.isEmpty()) {
        LocalDb::FileRow frow;
        frow.file_id   = file_data->_file_id;
        frow.file_name = file_data->_file_name;
        frow.file_size = file_data->_file_size;
        frow.file_type = row.msg_type;
        frow.local_path = cached_path;
        frow.download_status = 2; // "done" — we just wrote it
        frow.last_access = QDateTime::currentSecsSinceEpoch();
        LocalDb::Inst().UpsertFile(frow);
    }
}

void ChatDialog::slot_file_msg_notify(std::shared_ptr<FileChatData> file_data) {
    qDebug() << "Received file message: file_id=" << file_data->_file_id
             << " from=" << file_data->_from_uid;

    // Keep the legacy in-memory mirror for consistency with old code paths
    // that still read _chat_msgs. UI rendering now reads from LocalDb.
    int msg_type = MSG_TYPE_IMAGE + file_data->_file_type; // 0->IMAGE, 1->FILE, 2->AUDIO
    auto chat_msg = std::make_shared<TextChatData>(
        file_data->_msg_id, msg_type,
        file_data->_from_uid, file_data->_to_uid,
        file_data->_file_id, file_data->_file_name, file_data->_file_size);
    chat_msg->_file_host = file_data->_file_host;
    chat_msg->_file_port = file_data->_file_port;
    chat_msg->_file_token = file_data->_file_token;
    std::vector<std::shared_ptr<TextChatData>> vec;
    vec.push_back(chat_msg);
    UserMgr::GetInstance()->AppendFriendChatMsg(file_data->_from_uid, vec);

    // STAGE-C: mirror to LocalDb immediately when we have a msg_db_id
    // (same-server path). Cross-server notifies still arrive with 0 and
    // fall back to the next SetUserInfo refresh.
    if (file_data->_msg_db_id > 0) {
        QJsonObject content;
        content["msgid"] = file_data->_msg_id;
        content["file_id"] = file_data->_file_id;
        content["file_name"] = file_data->_file_name;
        content["file_size"] = static_cast<double>(file_data->_file_size);
        content["file_type"] = file_data->_file_type;

        LocalDb::MsgRow row;
        row.msg_db_id = file_data->_msg_db_id;
        row.peer_uid  = file_data->_from_uid; // receiver path: peer is sender
        row.direction = 0;
        row.msg_type  = msg_type;
        row.content   = QString::fromUtf8(
            QJsonDocument(content).toJson(QJsonDocument::Compact));
        row.send_time = QDateTime::currentSecsSinceEpoch();
        row.status    = 0;
        QVector<LocalDb::MsgRow> rows; rows.append(row);
        LocalDb::Inst().UpsertMessages(rows);

        qint64 hwm = LocalDb::Inst().GetLastSyncedMsgId(row.peer_uid);
        if (row.msg_db_id > hwm) {
            LocalDb::Inst().SetLastSyncedMsgId(row.peer_uid, row.msg_db_id);
        }
    }

    // Always download from server (no local cache check)
    // This avoids stale/corrupt cache issues and ensures data consistency
    auto file_id = file_data->_file_id;
    auto from_uid = file_data->_from_uid;

    // Use a QMetaObject::Connection so we can disconnect after one match
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(FileMgr::GetInstance().get(), &FileMgr::sig_download_done,
            this, [this, file_id, from_uid, chat_msg, conn](QString dl_file_id, QString local_path, int error) {
        if (dl_file_id != file_id) return;
        // Disconnect immediately — this lambda should fire only once
        disconnect(*conn);
        if (error != 0) {
            qDebug() << "File download failed: file_id=" << file_id << " error=" << error;
            return;
        }
        chat_msg->_local_path = local_path;

        // STAGE-C: record the downloaded file in local_files so future
        // opens of this conversation hit Case 1 (_local_path prefilled by
        // RowToTextChatData) without re-downloading.
        LocalDb::FileRow frow;
        frow.file_id   = chat_msg->_file_id;
        frow.file_name = chat_msg->_file_name;
        frow.file_size = chat_msg->_file_size;
        frow.file_type = chat_msg->_msg_type;
        frow.local_path = local_path;
        frow.download_status = 2;
        frow.last_access = QDateTime::currentSecsSinceEpoch();
        LocalDb::Inst().UpsertFile(frow);

        // Display image bubble regardless of which chat is active
        auto fi = UserMgr::GetInstance()->GetFriendById(from_uid);
        QString name = fi ? fi->_name : "";
        QString icon = fi ? fi->_icon : "";
        if (from_uid == _cur_chat_uid) {
            ui->chat_page->AppendImageBubble(local_path, ChatRole::Other, name, icon);
        }
    });

    FileMgr::GetInstance()->StartDownload(file_data);
}

// =====================================================================
// STAGE-C: conversation summary handler
// =====================================================================

void ChatDialog::slot_conv_summary(QJsonArray summaries) {
    qDebug() << "conv summary received, count=" << summaries.size();
    UserMgr::GetInstance()->ApplyConvSummaries(summaries);
    // STAGE-C.4b TODO: refresh ChatUserList to show last_msg_preview /
    // unread badges from the newly populated FriendInfo fields. For C.4a
    // we only confirm the data reached UserMgr.
}

// STAGE-C: shared helper to persist a realtime text message (same-server
// path only; msg_db_id > 0). Used by both incoming notify and own-send rsp.
static void PersistTextMsgToLocalDb(const std::shared_ptr<TextChatMsg>& msg) {
    if (msg->_msg_db_id <= 0) return;
    int self_uid = UserMgr::GetInstance()->GetUid();
    int peer_uid = (msg->_from_uid == self_uid) ? msg->_to_uid : msg->_from_uid;
    int direction = (msg->_from_uid == self_uid) ? 1 : 0;

    QJsonArray arr;
    for (const auto& d : msg->_chat_msgs) {
        QJsonObject o;
        o["msgid"] = d->_msg_id;
        o["content"] = d->_msg_content;
        arr.append(o);
    }
    QString content_str = QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact));

    LocalDb::MsgRow row;
    row.msg_db_id = msg->_msg_db_id;
    row.peer_uid  = peer_uid;
    row.direction = direction;
    row.msg_type  = MSG_TYPE_TEXT;
    row.content   = content_str;
    row.send_time = QDateTime::currentSecsSinceEpoch();
    row.status    = 0;
    QVector<LocalDb::MsgRow> rows; rows.append(row);
    LocalDb::Inst().UpsertMessages(rows);

    qint64 cur_hwm = LocalDb::Inst().GetLastSyncedMsgId(peer_uid);
    if (msg->_msg_db_id > cur_hwm) {
        LocalDb::Inst().SetLastSyncedMsgId(peer_uid, msg->_msg_db_id);
    }
}

void ChatDialog::slot_text_chat_msg_rsp(std::shared_ptr<TextChatMsg> msg) {
    // Own-text outgoing echo. Just persist; UI already rendered the bubble
    // optimistically via slot_append_send_chat_msg.
    PersistTextMsgToLocalDb(msg);
}

void ChatDialog::slot_pull_messages_rsp(int peer_uid, QJsonArray messages, bool has_more) {
    Q_UNUSED(has_more);
    qDebug() << "pull messages rsp peer=" << peer_uid
             << " count=" << messages.size();

    // 1. Persist to LocalDb so subsequent opens hit cache.
    int self_uid = UserMgr::GetInstance()->GetUid();
    QVector<LocalDb::MsgRow> rows;
    LocalDb::RowsFromServerMessages(messages, self_uid, rows);
    if (rows.isEmpty()) return;
    LocalDb::Inst().UpsertMessages(rows);

    // 2. Update sync high-water mark to the max msg_db_id we've seen.
    qint64 max_id = LocalDb::Inst().GetLastSyncedMsgId(peer_uid);
    for (const auto& r : rows) {
        if (r.msg_db_id > max_id) max_id = r.msg_db_id;
    }
    LocalDb::Inst().SetLastSyncedMsgId(peer_uid, max_id);

    // 3. If the user is currently viewing this peer, re-render from DB.
    //    CRUCIAL: use RefreshFromLocalDb, NOT SetUserInfo. SetUserInfo
    //    itself fires another ID_PULL_MESSAGES_REQ, which would loop
    //    forever (pull → render → pull → render → ...).
    if (peer_uid == _cur_chat_uid) {
        ui->chat_page->RefreshFromLocalDb();
    }
}

void ChatDialog::slot_download_token_rsp(QString file_id, QString host, QString port,
                                          QString token, int error) {
    Q_UNUSED(file_id);
    Q_UNUSED(host);
    Q_UNUSED(port);
    Q_UNUSED(token);
    Q_UNUSED(error);
    // STAGE-C.4b: wiring slot; the actual on-demand download flow is kicked
    // off from ChatPage and listened on the FileMgr side. This slot is kept
    // for future use (e.g. retry UI, permission errors surfaced to user).
}

