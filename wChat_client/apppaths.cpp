#include "apppaths.h"
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

int     AppPaths::_uid = -1;
QString AppPaths::_user_root;

void AppPaths::SetCurrentUser(int uid) {
    if (uid <= 0) {
        qWarning() << "AppPaths::SetCurrentUser: invalid uid" << uid;
        return;
    }
    _uid = uid;
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    _user_root = root + "/wChat/users/" + QString::number(uid);

    // Eagerly create the per-user subtree so callers don't each have to mkpath.
    QDir d;
    d.mkpath(_user_root + "/files");
    d.mkpath(_user_root + "/db");
    d.mkpath(_user_root + "/logs");

    qDebug() << "AppPaths: current user =" << uid << " root =" << _user_root;
}

void AppPaths::ClearCurrentUser() {
    _uid = -1;
    _user_root.clear();
}

int AppPaths::CurrentUid() {
    return _uid;
}

QString AppPaths::UserRoot() {
    return _user_root;
}

QString AppPaths::FilesDir() {
    if (_user_root.isEmpty()) return QString();
    return _user_root + "/files";
}

QString AppPaths::DbPath() {
    if (_user_root.isEmpty()) return QString();
    return _user_root + "/db/chat.sqlite";
}

QString AppPaths::LogsDir() {
    if (_user_root.isEmpty()) return QString();
    return _user_root + "/logs";
}
