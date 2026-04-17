#ifndef CHATPAGE_H
#define CHATPAGE_H

#include <QWidget>
#include <QStyleOption>
#include <QPainter>
#include <QFileInfo>
#include <QQueue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include "userdata.h"
namespace Ui {
class ChatPage;
}

class ChatPage : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPage(QWidget *parent = nullptr);
    ~ChatPage();
    void SetUserInfo(std::shared_ptr<UserInfo> user_info);
    void RefreshFromLocalDb();
    void AppendChatMsg(std::shared_ptr<TextChatData> msg);
    void AppendImageBubble(const QString& image_path, ChatRole role,
                           const QString& name, const QString& icon);
    QString PopPendingFilePath();

    int CurrentPeerUid() const {
        return _user_info ? _user_info->_uid : 0;
    }
protected:
    void paintEvent(QPaintEvent *event);

private slots:
    void on_send_btn_clicked();
    void onAiToggleClicked();
    void onAiRequestClicked();
    void onAiReplyFinished(QNetworkReply* reply);
    void onCandidateUseClicked(int index);
    void onCandidateRefineClicked(int index);
    void onRefineReplyFinished(QNetworkReply* reply, int index);
signals:
    void sig_append_send_chat_msg(std::shared_ptr<TextChatData>);
private:
    void setupAiPanel();
    QJsonArray buildRecentMessagesJson(int limit = 10);
    void clearCandidatesUi();
    void renderCandidates(const QJsonArray& candidates);
    QNetworkRequest makeAgentRequest(const QString& path);

    Ui::ChatPage *ui;
    std::shared_ptr<UserInfo> _user_info;
    QMap<QString, QWidget*>  _bubble_map;
    QQueue<QString>          _pending_file_paths;

    // M2: AI suggestion panel
    QPushButton*  _ai_toggle_btn = nullptr;
    QWidget*      _ai_panel = nullptr;
    QComboBox*    _ai_preset_combo = nullptr;
    QLineEdit*    _ai_custom_prompt = nullptr;
    QPushButton*  _ai_request_btn = nullptr;
    QVBoxLayout*  _ai_candidates_layout = nullptr;
    QWidget*      _ai_candidates_container = nullptr;
    QLabel*       _ai_status_label = nullptr;
    QNetworkAccessManager _ai_nam;
    QStringList   _ai_candidate_texts;
    QString       _ai_session_id;
};

#endif // CHATPAGE_H
