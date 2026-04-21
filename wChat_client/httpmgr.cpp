#include "httpmgr.h"

HttpMgr::~HttpMgr()
{

}

HttpMgr::HttpMgr()
{
    connect(this,&HttpMgr::sig_http_finish,this,&HttpMgr::slot_http_finish);
}

void HttpMgr::PostHttpReq(QString url, QJsonObject json, ReqId req_id, Modules mod)
{
    QUrl _url(gate_url_prefix + url);
    qDebug()<<_url <<Qt::endl;
    QByteArray data =QJsonDocument(json).toJson();
    QNetworkRequest request(_url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    request.setHeader(QNetworkRequest::ContentLengthHeader,QByteArray::number(data.length()));
    auto self =shared_from_this();
    QNetworkReply *reply =_manager.post(request,data);
    QObject::connect(reply,&QNetworkReply::finished,[self,reply,req_id,mod](){
        if(reply->error()!=QNetworkReply::NoError){
            qDebug()<<reply->errorString();
            emit self->sig_http_finish(req_id,"",ErrorCodes::ERR_NETWORK,mod);
            reply->deleteLater();
            return;
        }
        qDebug()<<"req_id :"<<req_id<<Qt::endl;
        QString res =reply->readAll();
        emit self->sig_http_finish(req_id,res,ErrorCodes::SUCCESS,mod);
        reply->deleteLater();
        return;
    });

}

void HttpMgr::slot_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod)
{
    if(mod == Modules::REGISTERMOD){
        //发送信号通知指定模块http的响应结束了
        emit sig_reg_mod_finish(id,res,err);
    }
    if(mod == Modules::RESETMOD){
        emit sig_reset_mod_finish(id,res,err);
    }
    if(mod == Modules::LOGINMOD){
        emit sig_login_mod_finish(id, res, err);
    }
}


