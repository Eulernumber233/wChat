#ifndef APPPATHS_H
#define APPPATHS_H

#include <QString>

// Per-user on-disk layout helper.
// All client data that depends on "which user is logged in" should go through
// here instead of hard-coding paths. Layout:
//
//   <AppDataLocation>/wChat/users/<uid>/
//       files/   downloaded image / file / audio cache
//       db/      STAGE-C: local SQLite lives here
//       logs/    per-user logs
//
// Call SetCurrentUser(uid) once login succeeds (before any module reads its
// per-user dir). Call ClearCurrentUser() on logout.
class AppPaths {
public:
    static void SetCurrentUser(int uid);
    static void ClearCurrentUser();

    // -1 if not logged in.
    static int CurrentUid();

    // <AppData>/wChat/users/<uid>/   — empty string if no user set.
    static QString UserRoot();
    static QString FilesDir();
    static QString DbPath();    // STAGE-C placeholder: <UserRoot>/db/chat.sqlite
    static QString LogsDir();

private:
    AppPaths() = delete;

    static int _uid;
    static QString _user_root;
};

#endif // APPPATHS_H
