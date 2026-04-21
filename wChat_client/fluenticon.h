#ifndef FLUENTICON_H
#define FLUENTICON_H

// Segoe Fluent Icons (Windows 10+) — all single-codepoint glyphs in the
// Unicode private use area. Any icon-bearing widget should call
// applyIconFont(widget, pixel_size) and set its text to QString(FIC::Xxx).
//
// Reference: https://learn.microsoft.com/windows/apps/design/style/segoe-fluent-icons-font
//
// Keeping every codepoint used by wChat centralised here makes it easy to
// swap the whole icon set (e.g. for Material Icons) later.

#include <QChar>
#include <QFontDatabase>
#include <QWidget>

namespace FIC {

// Window chrome
constexpr QChar Minimize   = QChar(0xE921);
constexpr QChar Close      = QChar(0xE8BB);

// Auth page input adornments
constexpr QChar Mail       = QChar(0xE715);   // ✉ envelope
constexpr QChar Lock       = QChar(0xE72E);   // 🔒 padlock
constexpr QChar Contact    = QChar(0xE77B);   // 👤 person
constexpr QChar NumberSign = QChar(0xE943);   // # code / key

// Main shell
constexpr QChar Chat       = QChar(0xE8BD);   // 💬 chat bubbles
constexpr QChar People     = QChar(0xE716);   // 👥 contacts
constexpr QChar AddFriend  = QChar(0xE8FA);   // 👋 add friend / person+
constexpr QChar Search     = QChar(0xE721);   // 🔍 magnifier
constexpr QChar Add        = QChar(0xE710);   // + plus
constexpr QChar Picture    = QChar(0xEB9F);   // 🖼 picture
constexpr QChar AttachFile = QChar(0xE723);   // 📎 attach (unused but handy)
constexpr QChar Emoji      = QChar(0xE76E);   // 😊 emoji (unused)
constexpr QChar Sparkle    = QChar(0xE945);   // ✨ lightbulb/spark for AI

// Helper: apply Segoe Fluent Icons (or MDL2 fallback) to a widget.
inline void applyIconFont(QWidget *w, int pixelSize)
{
    static const QString familyCached = []{
        const auto fams = QFontDatabase::families();
        if (fams.contains("Segoe Fluent Icons")) return QStringLiteral("Segoe Fluent Icons");
        if (fams.contains("Segoe MDL2 Assets"))  return QStringLiteral("Segoe MDL2 Assets");
        return QStringLiteral("Segoe UI Symbol");   // broad geometric fallback
    }();
    QFont f(familyCached);
    f.setPixelSize(pixelSize);
    w->setFont(f);
}

} // namespace FIC

#endif // FLUENTICON_H
