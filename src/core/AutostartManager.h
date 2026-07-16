#pragma once

#include <QObject>
#include <QString>

// Builds the freedesktop.org autostart entry ("[Desktop Entry]" .desktop
// file body) that makes suopNotes launch on login, pointing Exec= at the
// given executable path. Pure — no filesystem access — so it can be unit
// tested without touching the real ~/.config/autostart. Mirrors the
// buildKWinRuleKeys / KWinRuleApplier split in KWinRuleWriter.h: the string
// the file should contain is decided here, the actual writing lives in
// AutostartManager below.
QString buildAutostartDesktopEntry(const QString &executablePath);

// Controls whether suopNotes launches on login by writing/removing a single
// .desktop file in the XDG autostart directory (~/.config/autostart on
// Linux). KDE Plasma, GNOME and every other freedesktop desktop start each
// application whose entry sits in that directory when the session begins.
//
// The autostart FILE's on-disk existence is the single source of truth,
// deliberately NOT mirrored into the app's own Settings/QSettings: the user
// can toggle the exact same thing from KDE's System Settings > Autostart (or
// simply delete the file), so a cached bool in our settings would silently
// drift out of sync with what the session will actually do at login. Reading
// the filesystem on demand keeps the checkbox honest.
class AutostartManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    // CONSTANT: whether an XDG autostart directory convention exists at all is
    // a platform fact that never changes over a run, so the Settings dialog
    // can bind a control's visibility to it once without a change signal.
    Q_PROPERTY(bool available READ isAvailable CONSTANT)

public:
    // Writes/reads the real XDG autostart dir and launches whichever
    // executable is currently running (QCoreApplication::applicationFilePath),
    // so an autostart entry created from a build-dir binary points back at
    // that same build-dir binary, and one created from an installed binary
    // points at the installed path — no configure-time path baked in.
    explicit AutostartManager(QObject *parent = nullptr);

    // Test/advanced constructor: overrides both the autostart directory and
    // the executable path written into Exec=, so a test can point at a
    // QTemporaryDir and assert on the file without touching the real config
    // dir or depending on the test runner's own binary path. Intended for
    // tests, the same way Settings' explicit-path constructor is.
    AutostartManager(const QString &autostartDir, const QString &executablePath, QObject *parent = nullptr);

    // Whether launch-on-login can be configured on this platform. False off
    // Linux (no XDG autostart directory convention there) — the Settings
    // dialog hides the toggle entirely rather than showing a dead control.
    bool isAvailable() const;

    // True iff the autostart .desktop file currently exists on disk. Queried
    // live every time, never cached — see the class comment on why.
    bool isEnabled() const;

    // Writes the .desktop file (enable) or removes it (disable). A no-op when
    // already in the requested state, and emits enabledChanged only on an
    // actual transition. A no-op entirely when !isAvailable().
    void setEnabled(bool enabled);

signals:
    void enabledChanged();

private:
    QString desktopFilePath() const;

    QString m_autostartDir;
    QString m_executablePath;
};
