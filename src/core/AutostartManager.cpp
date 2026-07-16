#include "AutostartManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

QString buildAutostartDesktopEntry(const QString &executablePath)
{
    // Quote the Exec value only when the path contains whitespace: the
    // freedesktop Desktop Entry spec requires reserved characters (spaces
    // among them) to be wrapped in double quotes, but an unconditional quote
    // would be noise for the overwhelmingly common space-free install path.
    const QString exec =
        executablePath.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\"").arg(executablePath) : executablePath;

    // Mirrors data/suopnotes.desktop.in (the app-menu launcher) so the
    // login-time entry advertises the same name/icon, plus the two keys that
    // are specifically about autostart: X-GNOME-Autostart-enabled is what
    // GNOME reads to honor the entry (KDE just uses the file's presence, so
    // it's harmless-but-explicit there), and Terminal=false keeps a login
    // launch from flashing a console.
    return QStringLiteral(
               "[Desktop Entry]\n"
               "Type=Application\n"
               "Name=suopNotes\n"
               "Comment=Lightweight sticky notes for the desktop\n"
               "Exec=%1\n"
               "Icon=accessories-text-editor\n"
               "Terminal=false\n"
               "Categories=Utility;\n"
               "StartupNotify=false\n"
               "X-GNOME-Autostart-enabled=true\n")
        .arg(exec);
}

namespace {

QString defaultAutostartDir()
{
    // The XDG autostart directory: $XDG_CONFIG_HOME/autostart, which
    // GenericConfigLocation resolves to (~/.config on a default Linux setup).
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart");
}

}

AutostartManager::AutostartManager(QObject *parent)
    : AutostartManager(defaultAutostartDir(), QCoreApplication::applicationFilePath(), parent)
{
}

AutostartManager::AutostartManager(const QString &autostartDir, const QString &executablePath, QObject *parent)
    : QObject(parent)
    , m_autostartDir(autostartDir)
    , m_executablePath(executablePath)
{
}

bool AutostartManager::isAvailable() const
{
#if defined(Q_OS_LINUX)
    return true;
#else
    // No XDG autostart directory convention off Linux — writing a .desktop
    // file there would do nothing at login, so report the feature as absent.
    return false;
#endif
}

QString AutostartManager::desktopFilePath() const
{
    return m_autostartDir + QStringLiteral("/suopnotes.desktop");
}

bool AutostartManager::isEnabled() const
{
    return QFile::exists(desktopFilePath());
}

void AutostartManager::setEnabled(bool enabled)
{
    if (!isAvailable())
        return;

    if (enabled == isEnabled())
        return; // already in the desired state — no write, no signal

    if (enabled) {
        // The autostart dir may not exist yet on a fresh profile — create it
        // before writing rather than letting the QFile::open below fail.
        QDir().mkpath(m_autostartDir);

        QFile file(desktopFilePath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return; // couldn't write — leave state (and signal) untouched
        file.write(buildAutostartDesktopEntry(m_executablePath).toUtf8());
        file.close();
    } else {
        if (!QFile::remove(desktopFilePath()))
            return; // couldn't remove — leave state (and signal) untouched
    }

    emit enabledChanged();
}
