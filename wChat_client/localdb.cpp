#include "localdb.h"
#include "apppaths.h"

#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

LocalDb& LocalDb::Inst() {
    static LocalDb inst;
    return inst;
}

bool LocalDb::IsOpen() const {
    return _db.isValid() && _db.isOpen();
}

bool LocalDb::Open() {
    // Tear down any previous connection (e.g. re-login as another account).
    Close();

    const QString path = AppPaths::DbPath();
    if (path.isEmpty()) {
        qWarning() << "LocalDb::Open: AppPaths not initialized (no current user)";
        return false;
    }

    // Make sure parent dir exists. AppPaths::SetCurrentUser already does this
    // for the /db subdir but be defensive.
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    // Unique connection name per-uid so we never collide with a stale handle.
    _conn_name = QStringLiteral("wchat_local_%1").arg(AppPaths::CurrentUid());

    // If a QSqlDatabase with this name already exists (e.g. from a previous
    // Open cycle that didn't Close cleanly), remove it first. addDatabase
    // would otherwise emit a warning and reuse stale settings.
    if (QSqlDatabase::contains(_conn_name)) {
        QSqlDatabase::removeDatabase(_conn_name);
    }

    _db = QSqlDatabase::addDatabase("QSQLITE", _conn_name);
    _db.setDatabaseName(path);

    if (!_db.open()) {
        qWarning() << "LocalDb::Open failed:" << _db.lastError().text()
                   << " path=" << path;
        return false;
    }

    // Good-defaults pragmas. WAL gives much better concurrent read behavior
    // which we'll appreciate once the UI thread queries while background
    // syncs write.
    QSqlQuery pragma(_db);
    pragma.exec("PRAGMA journal_mode = WAL");
    pragma.exec("PRAGMA synchronous  = NORMAL");
    pragma.exec("PRAGMA foreign_keys = ON");

    if (!EnsureSchema()) {
        qWarning() << "LocalDb::Open: EnsureSchema failed";
        _db.close();
        return false;
    }

    qDebug() << "LocalDb opened:" << path;
    return true;
}

void LocalDb::Close() {
    if (_db.isOpen()) {
        _db.close();
    }
    _db = QSqlDatabase();
    if (!_conn_name.isEmpty() && QSqlDatabase::contains(_conn_name)) {
        QSqlDatabase::removeDatabase(_conn_name);
    }
    _conn_name.clear();
}

bool LocalDb::EnsureSchema() {
    QSqlQuery q(_db);

    const char* kMessages =
        "CREATE TABLE IF NOT EXISTS local_messages ("
        "  msg_db_id  INTEGER PRIMARY KEY,"
        "  peer_uid   INTEGER NOT NULL,"
        "  direction  INTEGER NOT NULL,"
        "  msg_type   INTEGER NOT NULL,"
        "  content    TEXT    NOT NULL,"
        "  send_time  INTEGER NOT NULL,"
        "  status     INTEGER NOT NULL DEFAULT 0"
        ")";
    if (!q.exec(kMessages)) {
        qWarning() << "create local_messages failed:" << q.lastError().text();
        return false;
    }

    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_msg_peer_id "
                "ON local_messages(peer_uid, msg_db_id DESC)")) {
        qWarning() << "create idx_msg_peer_id failed:" << q.lastError().text();
        return false;
    }

    const char* kFiles =
        "CREATE TABLE IF NOT EXISTS local_files ("
        "  file_id         TEXT PRIMARY KEY,"
        "  file_name       TEXT NOT NULL,"
        "  file_size       INTEGER NOT NULL,"
        "  file_type       INTEGER NOT NULL,"
        "  local_path      TEXT,"
        "  download_status INTEGER NOT NULL DEFAULT 0,"
        "  md5             TEXT,"
        "  last_access     INTEGER NOT NULL DEFAULT 0"
        ")";
    if (!q.exec(kFiles)) {
        qWarning() << "create local_files failed:" << q.lastError().text();
        return false;
    }

    const char* kSync =
        "CREATE TABLE IF NOT EXISTS sync_state ("
        "  peer_uid           INTEGER PRIMARY KEY,"
        "  last_synced_msg_id INTEGER NOT NULL DEFAULT 0"
        ")";
    if (!q.exec(kSync)) {
        qWarning() << "create sync_state failed:" << q.lastError().text();
        return false;
    }

    return true;
}

// ==========================================================================
// Messages
// ==========================================================================

bool LocalDb::UpsertMessages(const QVector<MsgRow>& rows) {
    if (!IsOpen()) return false;
    if (rows.isEmpty()) return true;

    if (!_db.transaction()) {
        qWarning() << "LocalDb::UpsertMessages: begin txn failed:"
                   << _db.lastError().text();
        return false;
    }

    QSqlQuery q(_db);
    q.prepare(
        "INSERT OR IGNORE INTO local_messages "
        "(msg_db_id, peer_uid, direction, msg_type, content, send_time, status) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");

    for (const auto& r : rows) {
        q.bindValue(0, r.msg_db_id);
        q.bindValue(1, r.peer_uid);
        q.bindValue(2, r.direction);
        q.bindValue(3, r.msg_type);
        q.bindValue(4, r.content);
        q.bindValue(5, r.send_time);
        q.bindValue(6, r.status);
        if (!q.exec()) {
            qWarning() << "UpsertMessages insert failed:" << q.lastError().text()
                       << " msg_db_id=" << r.msg_db_id;
            _db.rollback();
            return false;
        }
    }

    if (!_db.commit()) {
        qWarning() << "UpsertMessages commit failed:" << _db.lastError().text();
        _db.rollback();
        return false;
    }
    return true;
}

static LocalDb::MsgRow rowFromQuery(QSqlQuery& q) {
    LocalDb::MsgRow r;
    r.msg_db_id = q.value(0).toLongLong();
    r.peer_uid  = q.value(1).toInt();
    r.direction = q.value(2).toInt();
    r.msg_type  = q.value(3).toInt();
    r.content   = q.value(4).toString();
    r.send_time = q.value(5).toLongLong();
    r.status    = q.value(6).toInt();
    return r;
}

QVector<LocalDb::MsgRow> LocalDb::LoadRecent(int peer_uid, int limit) {
    QVector<MsgRow> out;
    if (!IsOpen()) return out;

    QSqlQuery q(_db);
    q.prepare(
        "SELECT msg_db_id, peer_uid, direction, msg_type, content, send_time, status "
        "FROM local_messages "
        "WHERE peer_uid = ? "
        "ORDER BY msg_db_id DESC "
        "LIMIT ?");
    q.bindValue(0, peer_uid);
    q.bindValue(1, limit);
    if (!q.exec()) {
        qWarning() << "LoadRecent failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.append(rowFromQuery(q));

    // We queried DESC for LIMIT efficiency but callers want ASC.
    std::reverse(out.begin(), out.end());
    return out;
}

QVector<LocalDb::MsgRow> LocalDb::LoadBefore(int peer_uid,
                                              qint64 before_msg_db_id,
                                              int limit) {
    QVector<MsgRow> out;
    if (!IsOpen()) return out;

    QSqlQuery q(_db);
    q.prepare(
        "SELECT msg_db_id, peer_uid, direction, msg_type, content, send_time, status "
        "FROM local_messages "
        "WHERE peer_uid = ? AND msg_db_id < ? "
        "ORDER BY msg_db_id DESC "
        "LIMIT ?");
    q.bindValue(0, peer_uid);
    q.bindValue(1, before_msg_db_id);
    q.bindValue(2, limit);
    if (!q.exec()) {
        qWarning() << "LoadBefore failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.append(rowFromQuery(q));
    std::reverse(out.begin(), out.end());
    return out;
}

// ==========================================================================
// Files
// ==========================================================================

bool LocalDb::UpsertFile(const FileRow& row) {
    if (!IsOpen()) return false;

    QSqlQuery q(_db);
    q.prepare(
        "INSERT OR REPLACE INTO local_files "
        "(file_id, file_name, file_size, file_type, local_path, download_status, md5, last_access) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    q.bindValue(0, row.file_id);
    q.bindValue(1, row.file_name);
    q.bindValue(2, row.file_size);
    q.bindValue(3, row.file_type);
    q.bindValue(4, row.local_path);
    q.bindValue(5, row.download_status);
    q.bindValue(6, row.md5);
    q.bindValue(7, row.last_access);
    if (!q.exec()) {
        qWarning() << "UpsertFile failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool LocalDb::GetFile(const QString& file_id, FileRow& out) {
    if (!IsOpen()) return false;

    QSqlQuery q(_db);
    q.prepare(
        "SELECT file_id, file_name, file_size, file_type, local_path, "
        "       download_status, md5, last_access "
        "FROM local_files WHERE file_id = ?");
    q.bindValue(0, file_id);
    if (!q.exec()) {
        qWarning() << "GetFile exec failed:" << q.lastError().text();
        return false;
    }
    if (!q.next()) return false;

    out.file_id         = q.value(0).toString();
    out.file_name       = q.value(1).toString();
    out.file_size       = q.value(2).toLongLong();
    out.file_type       = q.value(3).toInt();
    out.local_path      = q.value(4).toString();
    out.download_status = q.value(5).toInt();
    out.md5             = q.value(6).toString();
    out.last_access     = q.value(7).toLongLong();
    return true;
}

// ==========================================================================
// Sync state
// ==========================================================================

qint64 LocalDb::GetLastSyncedMsgId(int peer_uid) {
    if (!IsOpen()) return 0;
    QSqlQuery q(_db);
    q.prepare("SELECT last_synced_msg_id FROM sync_state WHERE peer_uid = ?");
    q.bindValue(0, peer_uid);
    if (!q.exec()) {
        qWarning() << "GetLastSyncedMsgId failed:" << q.lastError().text();
        return 0;
    }
    if (!q.next()) return 0;
    return q.value(0).toLongLong();
}

bool LocalDb::SetLastSyncedMsgId(int peer_uid, qint64 msg_id) {
    if (!IsOpen()) return false;
    QSqlQuery q(_db);
    q.prepare(
        "INSERT INTO sync_state (peer_uid, last_synced_msg_id) VALUES (?, ?) "
        "ON CONFLICT(peer_uid) DO UPDATE SET last_synced_msg_id = excluded.last_synced_msg_id");
    q.bindValue(0, peer_uid);
    q.bindValue(1, msg_id);
    if (!q.exec()) {
        qWarning() << "SetLastSyncedMsgId failed:" << q.lastError().text();
        return false;
    }
    return true;
}
