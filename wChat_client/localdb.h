#ifndef LOCALDB_H
#define LOCALDB_H

#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>
#include <QtSql/QSqlDatabase>

struct TextChatData;

// Per-user SQLite persistence for chat messages and file metadata.
//
// Lifecycle:
//   1. AppPaths::SetCurrentUser(uid)  — sets up the per-user directory
//   2. LocalDb::Inst().Open()         — opens / creates <UserRoot>/db/chat.sqlite
//   3. use UpsertMessages / LoadRecent / ...
//   4. LocalDb::Inst().Close()        — on logout (not wired in STAGE-C)
//
// Open() is idempotent. Calling it after a user switch closes the previous
// connection and opens the one under the new AppPaths::DbPath().
//
// Schema is created on first open via CREATE TABLE IF NOT EXISTS, so older
// databases can add new tables in future stages without a migration step.
class LocalDb {
public:
    static LocalDb& Inst();

    bool Open();
    void Close();
    bool IsOpen() const;

    // ---- Messages ----
    struct MsgRow {
        qint64  msg_db_id = 0;  // PK: mirrors server chat_messages.id
        int     peer_uid  = 0;  // the friend's uid (NOT self)
        int     direction = 0;  // 0 = recv, 1 = send
        int     msg_type  = 1;  // 1 text, 2 image, 3 file, 4 audio
        QString content;        // server content JSON, stored verbatim
        qint64  send_time = 0;  // unix seconds
        int     status    = 0;
    };

    // Batch insert with INSERT OR IGNORE (dedupes on msg_db_id). Runs in a
    // single transaction. Returns false only on a hard DB error.
    bool UpsertMessages(const QVector<MsgRow>& rows);

    // Most recent N messages for the conversation with peer_uid, returned
    // in id-ascending order (oldest first) so callers can append directly
    // to the chat view.
    QVector<MsgRow> LoadRecent(int peer_uid, int limit);

    // Page of messages strictly older than `before_msg_db_id`, ascending.
    // Used for scroll-to-top history loading.
    QVector<MsgRow> LoadBefore(int peer_uid, qint64 before_msg_db_id, int limit);

    // ---- Files ----
    struct FileRow {
        QString file_id;             // PK
        QString file_name;
        qint64  file_size = 0;
        int     file_type = 0;       // mirrors msg_type: 2/3/4
        QString local_path;          // absolute path once downloaded
        int     download_status = 0; // 0 none, 1 in-flight, 2 done, 3 failed
        QString md5;
        qint64  last_access = 0;     // unix seconds; for future LRU eviction
    };

    // INSERT OR REPLACE on file_id. Simpler than a manual UPSERT and fine
    // because file metadata is small and identity is the server-minted UUID.
    bool UpsertFile(const FileRow& row);

    // Fills `out` and returns true on hit; false (out unchanged) on miss.
    bool GetFile(const QString& file_id, FileRow& out);

    // ---- Sync state (per-peer high-water mark of synced msg_db_id) ----
    qint64 GetLastSyncedMsgId(int peer_uid);   // 0 when no row
    bool   SetLastSyncedMsgId(int peer_uid, qint64 msg_id);

    // ---- Helpers: MsgRow <-> TextChatData / server JSON ----
    //
    // Build a MsgRow from the per-message JSON returned by the server (either
    // via ID_PULL_MESSAGES_RSP or from realtime text/file notifications). The
    // caller supplies self_uid so we can set `direction` correctly.
    //
    // For text messages the server stores `content` as a JSON array of
    // {msgid, content} bubbles. `out_rows` therefore may get MORE entries
    // than `in_msgs.size()`: we fan text-array rows into separate MsgRows
    // keyed by msg_db_id * 1000 + bubble_index to stay unique... no, we
    // actually keep each DB row as ONE MsgRow and let the UI layer decide
    // how to render multi-bubble text content. Stage C.4b takes the "one
    // DB row == one bubble" shortcut: text arrays of length > 1 still
    // become a single MsgRow with the whole array as content, and the UI
    // renders only the first bubble. This is a known stage-C limitation
    // documented in C.6.
    //
    // Returns the number of rows appended to `out_rows`.
    static int RowsFromServerMessages(const QJsonArray& in_msgs,
                                      int self_uid,
                                      QVector<MsgRow>& out_rows);

    // Convert a single MsgRow into zero-or-more TextChatData records suitable
    // for ChatPage::AppendChatMsg. Text rows with multi-bubble content are
    // fanned out into multiple TextChatData entries; file rows produce one.
    static QVector<std::shared_ptr<TextChatData>> RowToTextChatData(
        const MsgRow& row, int self_uid);

private:
    LocalDb() = default;
    ~LocalDb() = default;
    LocalDb(const LocalDb&) = delete;
    LocalDb& operator=(const LocalDb&) = delete;

    bool EnsureSchema();

    QSqlDatabase _db;
    // Unique connection name so Qt's default connection is untouched and
    // we can re-open after a Close() without name collisions.
    QString _conn_name;
};

#endif // LOCALDB_H
