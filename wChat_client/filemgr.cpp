#include "filemgr.h"
#include "apppaths.h"
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QtEndian>

// FileServer frame: [msg_id 2B][body_len 4B][offset 8B][payload]
static const int FRAME_HEADER_SIZE = 14;
static const int CHUNK_SIZE = 64 * 1024; // 64 KB

FileMgr::FileMgr() {
    // STAGE-B: _cache_dir is set lazily by Init() after login; do not read
    // any pre-login global path. This ensures different accounts on the same
    // machine never share cached files.
}

FileMgr::~FileMgr() {}

void FileMgr::Init() {
    QString dir = AppPaths::FilesDir();
    if (dir.isEmpty()) {
        qWarning() << "FileMgr::Init called before AppPaths::SetCurrentUser";
        _cache_dir.clear();
        return;
    }
    _cache_dir = dir;
    QDir().mkpath(_cache_dir);
    qDebug() << "FileMgr cache dir:" << _cache_dir;
}

void FileMgr::Shutdown() {
    _cache_dir.clear();
}

QString FileMgr::GetCacheDir() {
    return _cache_dir;
}

QString FileMgr::GetCachedPath(const QString& file_id, const QString& file_name) {
    if (_cache_dir.isEmpty()) return "";
    QString ext;
    int dot = file_name.lastIndexOf('.');
    if (dot >= 0) ext = file_name.mid(dot);
    QString path = _cache_dir + "/" + file_id + ext;
    if (QFile::exists(path)) return path;
    return "";
}

QString FileMgr::BuildCachedPath(const QString& file_id, const QString& file_name) {
    if (_cache_dir.isEmpty()) return "";
    QString ext;
    int dot = file_name.lastIndexOf('.');
    if (dot >= 0) ext = file_name.mid(dot);
    return _cache_dir + "/" + file_id + ext;
}

// =====================================================================
// Upload
// =====================================================================

void FileMgr::StartUpload(const QString& file_id, const QString& file_token,
                           const QString& host, const QString& port,
                           const QString& local_path) {
    quint16 p = port.toUShort();
    QThread* thread = new QThread();
    auto* worker = new QObject();
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, [this, file_id, file_token, host, p, local_path, worker]() {
        DoUpload(file_id, file_token, host, p, local_path);
        worker->thread()->quit();
    });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void FileMgr::DoUpload(QString file_id, QString file_token,
                        QString host, quint16 port, QString local_path) {
    QTcpSocket socket;
    socket.connectToHost(host, port);
    if (!socket.waitForConnected(5000)) {
        qDebug() << "FileMgr upload: connect failed";
        emit sig_upload_done(file_id, 1);
        return;
    }

    // 1. Send AUTH_REQ
    QJsonObject auth;
    auth["file_token"] = file_token;
    auth["file_id"] = file_id;
    QByteArray authData = QJsonDocument(auth).toJson(QJsonDocument::Compact);
    if (!SendFrame(&socket, 2001, 0, authData.data(), authData.size())) {
        emit sig_upload_done(file_id, 2);
        return;
    }

    // 2. Receive AUTH_RSP
    quint16 rsp_id; quint32 rsp_len; qint64 rsp_offset;
    QByteArray rsp_payload;
    if (!RecvFrame(&socket, rsp_id, rsp_len, rsp_offset, rsp_payload)) {
        emit sig_upload_done(file_id, 3);
        return;
    }

    QJsonObject rsp = QJsonDocument::fromJson(rsp_payload).object();
    if (rsp["error"].toInt() != 0) {
        qDebug() << "FileMgr upload auth failed:" << rsp["msg"].toString();
        emit sig_upload_done(file_id, 4);
        return;
    }

    qint64 resume_offset = static_cast<qint64>(rsp["offset"].toDouble());

    // 3. Open local file and send chunks
    QFile file(local_path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "FileMgr: cannot open local file" << local_path;
        emit sig_upload_done(file_id, 5);
        return;
    }
    qint64 total_size = file.size();
    file.seek(resume_offset);

    qint64 sent = resume_offset;
    char buf[CHUNK_SIZE];
    while (sent < total_size) {
        qint64 to_read = qMin<qint64>(CHUNK_SIZE, total_size - sent);
        qint64 actually_read = file.read(buf, to_read);
        if (actually_read <= 0) break;

        if (!SendFrame(&socket, 2003, sent, buf, static_cast<quint32>(actually_read))) {
            emit sig_upload_done(file_id, 6);
            file.close();
            return;
        }
        sent += actually_read;

        int percent = static_cast<int>(sent * 100 / total_size);
        emit sig_upload_progress(file_id, percent);
    }
    file.close();

    // 4. Wait for DONE from FileServer
    quint16 done_id; quint32 done_len; qint64 done_offset;
    QByteArray done_payload;
    if (!RecvFrame(&socket, done_id, done_len, done_offset, done_payload)) {
        emit sig_upload_done(file_id, 7);
        return;
    }

    qDebug() << "FileMgr upload complete: file_id=" << file_id;
    emit sig_upload_done(file_id, 0);
    socket.disconnectFromHost();
}

// =====================================================================
// Download
// =====================================================================

void FileMgr::StartDownload(std::shared_ptr<FileChatData> file_data) {
    quint16 port = file_data->_file_port.toUShort();
    QString file_id = file_data->_file_id;
    QString file_token = file_data->_file_token;
    QString host = file_data->_file_host;
    QString file_name = file_data->_file_name;

    QThread* thread = new QThread();
    // Move work to thread with its own event loop so waitFor* works reliably on Windows
    auto* worker = new QObject();
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, [this, file_id, file_token, host, port, file_name, worker]() {
        DoDownload(file_id, file_token, host, port, file_name);
        worker->thread()->quit();
    });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void FileMgr::DoDownload(QString file_id, QString file_token,
                          QString host, quint16 port, QString file_name) {
    if (_cache_dir.isEmpty()) {
        qWarning() << "FileMgr download: no cache dir bound (user not logged in?)";
        emit sig_download_done(file_id, "", 8);
        return;
    }

    QTcpSocket socket;
    socket.connectToHost(host, port);
    if (!socket.waitForConnected(5000)) {
        qDebug() << "FileMgr download: connect failed";
        emit sig_download_done(file_id, "", 1);
        return;
    }

    // 1. Send AUTH_REQ
    QJsonObject auth;
    auth["file_token"] = file_token;
    auth["file_id"] = file_id;
    QByteArray authData = QJsonDocument(auth).toJson(QJsonDocument::Compact);
    if (!SendFrame(&socket, 2001, 0, authData.data(), authData.size())) {
        emit sig_download_done(file_id, "", 2);
        return;
    }

    // 2. Receive AUTH_RSP
    quint16 rsp_id; quint32 rsp_len; qint64 rsp_offset;
    QByteArray rsp_payload;
    if (!RecvFrame(&socket, rsp_id, rsp_len, rsp_offset, rsp_payload)) {
        emit sig_download_done(file_id, "", 3);
        return;
    }

    QJsonObject rsp = QJsonDocument::fromJson(rsp_payload).object();
    if (rsp["error"].toInt() != 0) {
        qDebug() << "FileMgr download auth failed:" << rsp["msg"].toString();
        emit sig_download_done(file_id, "", 4);
        return;
    }

    qint64 file_size = static_cast<qint64>(rsp["file_size"].toDouble());

    // 3. Prepare local cache file
    QString ext;
    int dot = file_name.lastIndexOf('.');
    if (dot >= 0) ext = file_name.mid(dot);
    QString local_path = _cache_dir + "/" + file_id + ext;
    QFile file(local_path);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "FileMgr: cannot create cache file" << local_path;
        emit sig_download_done(file_id, "", 5);
        return;
    }

    // 4. Receive data chunks until DONE
    qint64 received = 0;
    while (true) {
        quint16 msg_id; quint32 body_len; qint64 offset;
        QByteArray payload;
        if (!RecvFrame(&socket, msg_id, body_len, offset, payload)) {
            file.flush();
            file.close();
            emit sig_download_done(file_id, "", 6);
            return;
        }

        if (msg_id == 2004) { // ID_FSVR_DONE
            break;
        }

        if (msg_id == 2003) { // ID_FSVR_DATA
            file.seek(offset);
            file.write(payload);
            received += payload.size();
            if (file_size > 0) {
                int percent = static_cast<int>(received * 100 / file_size);
                emit sig_download_progress(file_id, percent);
            }
        }
    }
    // Flush all buffered data to disk before closing
    file.flush();
    file.close();

    qDebug() << "FileMgr download complete: file_id=" << file_id << " path=" << local_path;
    emit sig_download_done(file_id, local_path, 0);
    socket.disconnectFromHost();
}

// =====================================================================
// Frame I/O helpers (blocking socket, used on worker threads)
// =====================================================================

bool FileMgr::SendFrame(QTcpSocket* sock, quint16 msg_id,
                         qint64 offset, const char* data, quint32 len) {
    QByteArray frame;
    frame.resize(FRAME_HEADER_SIZE + len);
    char* p = frame.data();

    // msg_id (big-endian)
    quint16 id_be = qToBigEndian(msg_id);
    memcpy(p, &id_be, 2); p += 2;

    // body_len (big-endian, 4 bytes)
    quint32 len_be = qToBigEndian(len);
    memcpy(p, &len_be, 4); p += 4;

    // offset (big-endian, 8 bytes)
    qint64 off_be = qToBigEndian(offset);
    memcpy(p, &off_be, 8); p += 8;

    // payload
    memcpy(p, data, len);

    qint64 written = sock->write(frame);
    if (written != frame.size()) return false;
    return sock->waitForBytesWritten(10000);
}

bool FileMgr::RecvFrame(QTcpSocket* sock, quint16& msg_id,
                         quint32& body_len, qint64& offset,
                         QByteArray& payload) {
    // Read header (14 bytes)
    QByteArray header;
    while (header.size() < FRAME_HEADER_SIZE) {
        if (sock->bytesAvailable() == 0) {
            if (!sock->waitForReadyRead(30000)) return false;
        }
        QByteArray chunk = sock->read(FRAME_HEADER_SIZE - header.size());
        if (chunk.isEmpty()) {
            if (!sock->waitForReadyRead(30000)) return false;
            continue;
        }
        header.append(chunk);
    }

    const char* p = header.constData();
    quint16 id_be; memcpy(&id_be, p, 2);
    msg_id = qFromBigEndian(id_be); p += 2;

    quint32 len_be; memcpy(&len_be, p, 4);
    body_len = qFromBigEndian(len_be); p += 4;

    qint64 off_be; memcpy(&off_be, p, 8);
    offset = qFromBigEndian(off_be);

    // Read payload
    payload.clear();
    quint32 remaining = body_len;
    while (remaining > 0) {
        if (sock->bytesAvailable() == 0) {
            if (!sock->waitForReadyRead(30000)) return false;
        }
        QByteArray chunk = sock->read(remaining);
        if (chunk.isEmpty()) {
            // No data read despite bytesAvailable — retry with wait
            if (!sock->waitForReadyRead(30000)) return false;
            continue;
        }
        payload.append(chunk);
        remaining -= chunk.size();
    }

    return true;
}
