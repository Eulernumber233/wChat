#ifndef FILEMGR_H
#define FILEMGR_H

#include "global.h"
#include "singleton.h"
#include "userdata.h"
#include <QTcpSocket>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

class FileMgr : public QObject, public Singleton<FileMgr>
{
    Q_OBJECT
    friend class Singleton<FileMgr>;
public:
    ~FileMgr();

    // Upload a file to FileServer. Called after receiving ID_FILE_UPLOAD_RSP from ChatServer.
    void StartUpload(const QString& file_id, const QString& file_token,
                     const QString& host, const QString& port,
                     const QString& local_path);

    // Download a file from FileServer. Called after receiving ID_FILE_MSG_NOTIFY.
    void StartDownload(std::shared_ptr<FileChatData> file_data);

    // Check if a file is already cached locally. Returns local path or empty string.
    QString GetCachedPath(const QString& file_id, const QString& file_name);

    // Get cache directory path
    QString GetCacheDir();

signals:
    void sig_upload_progress(QString file_id, int percent);
    void sig_upload_done(QString file_id, int error);
    void sig_download_progress(QString file_id, int percent);
    void sig_download_done(QString file_id, QString local_path, int error);

private:
    FileMgr();

    // Internal upload/download on a worker thread
    void DoUpload(QString file_id, QString file_token,
                  QString host, quint16 port, QString local_path);
    void DoDownload(QString file_id, QString file_token,
                    QString host, quint16 port, QString file_name);

    // Frame I/O helpers (blocking, used on worker threads)
    // Send: header(14 bytes) + payload
    static bool SendFrame(QTcpSocket* sock, quint16 msg_id,
                          qint64 offset, const char* data, quint32 len);
    // Recv: read header, then payload. Returns false on error.
    static bool RecvFrame(QTcpSocket* sock, quint16& msg_id,
                          quint32& body_len, qint64& offset,
                          QByteArray& payload);

    QString _cache_dir;
};

#endif // FILEMGR_H
