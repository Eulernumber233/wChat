#include "global.h"

QString gate_url_prefix="";
QString gate_url_prefix_domain="";

std::function<void(QWidget*)>repolish =[](QWidget*w){
    w->style()->unpolish(w);
    w->style()->polish(w);
};

std::function<QString(QString)>xorString = [](QString input){
    QString result = input;
    int length =input.length();
    length =length%255;
    for(int i=0;i<length;i++){
        result[i]=QChar(static_cast<ushort>(input[i].unicode()^static_cast<ushort>(length)));
    }
    return result;
};


std::vector<QString>  strs ={"hello world !",
                             "nice to meet u",
                             "New year，new life",
                             "You have to love yourself",
                             "My love is written in the wind ever since the whole world is you"};

std::vector<QString> heads = {
    ":/asserts/head_1.jpg",
    ":/asserts/head_2.jpg",
    ":/asserts/head_3.jpg",
    ":/asserts/head_4.jpg",
    ":/asserts/head_5.jpg"
};

std::vector<QString> names = {
    "llfc",
    "zack",
    "golang",
    "cpp",
    "java",
    "nodejs",
    "python",
    "rust"
};
