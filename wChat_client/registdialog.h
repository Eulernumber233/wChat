#ifndef REGISTDIALOG_H
#define REGISTDIALOG_H

#include <QDialog>
#include "global.h"
namespace Ui {
class RegistDialog;
}

class RegistDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RegistDialog(QWidget *parent = nullptr);
    ~RegistDialog();

private slots:
    void on_get_code_clicked();
    void slot_reg_mod_finish(ReqId id,QString res,ErrorCodes err);
    void on_sure_btn_clicked();
    void on_cancel_btn_clicked();
    void on_ret_btn_clicked();

signals:
    void sigSwitchLogin();
private:
    bool isAllDigit(const QString& str);
    void AddTipErr(TipErr te, QString tips);
    void DelTipErr(TipErr te);
    bool checkUserValid();
    bool checkEmailValid();
    bool checkPassValid();
    bool checkVarifyValid();
    void initHttpHandlers();
    void showTip(QString str,bool b_ok);
    void ChangerTipPage();
    QMap<TipErr,QString>_tip_errs;
    Ui::RegistDialog *ui;
    QMap<ReqId,std::function<void(const QJsonObject&)>>_handlers;
    QString email_const;
    QTimer* _countdown_timer;
    int _countdown;
};

#endif // REGISTDIALOG_H
