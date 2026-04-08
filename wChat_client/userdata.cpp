#include "userdata.h"


void FriendInfo::AppendChatMsgs(const std::vector<std::shared_ptr<TextChatData> > text_vec)
{
    for(const auto & text: text_vec){
        _chat_msgs.push_back(text);
    }
}


