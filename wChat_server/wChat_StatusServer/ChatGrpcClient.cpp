#include "ChatGrpcClient.h"
#include "RedisMgr.h"
#include "ConfigMgr.h"

#include "MysqlMgr.h"


ChatConPool::ChatConPool(size_t poolSize, std::string host, std::string port)
    :poolSize_(poolSize), host_(host), port_(port), b_stop_(false)
{
    for (size_t i = 0;i < poolSize_;++i) {
        std::shared_ptr<Channel>channel = grpc::CreateChannel(host + ":" + port, grpc::InsecureChannelCredentials());
        connections_.push(ChatService::NewStub(channel));// newstub 返回右值
    }
}

ChatConPool::~ChatConPool()
{
    std::lock_guard<std::mutex>LockFile(mutex_);
    Close();
    while (!connections_.empty()) {
        connections_.pop();
    }
}

std::unique_ptr<ChatService::Stub>ChatConPool::getConnection() {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] {
        if (b_stop_) {
            return true;
        }
        return !connections_.empty();
        });
    //如果停止则直接返回空指针
    if (b_stop_) {
        return  nullptr;
    }
    auto context = std::move(connections_.front());
    connections_.pop();
    return context;
}

void ChatConPool::returnConnection(std::unique_ptr<ChatService::Stub> context) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (b_stop_) {
        return;
    }
    connections_.push(std::move(context));
    cond_.notify_one();
}

void ChatConPool::Close() {
    b_stop_ = true;
    cond_.notify_all();
}


AddFriendRsp ChatGrpcClient::NotifyAddFriend(std::string server_ip, const AddFriendReq& req)
{
    AddFriendRsp rsp;
    return rsp;
}

//AuthFriendRsp ChatGrpcClient::NotifyAuthFriend(std::string server_ip, const AuthFriendReq& req)
//{
//    AuthFriendRsp rsp;
//    return rsp;
//}
//
//bool ChatGrpcClient::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
//{
//    return false;
//}
//
//
//
//TextChatMsgRsp ChatGrpcClient::NotifyTextChatMsg(std::string server_ip, const TextChatMsgReq& req, const Json::Value& rtvalue)
//{
//    return TextChatMsgRsp();
//}

ChatGrpcClient::ChatGrpcClient()
{
}