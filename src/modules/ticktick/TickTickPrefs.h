#pragma once

#include <QDateTime>
#include <QString>

// The module's own user-facing preferences: which TickTick list to pull from,
// and how often to pull it.
//
// Stored in the module's own INI, deliberately NOT in the app's Settings
// class — same reasoning as TickTickTokenStore's: the module then needs no
// core changes at all, and removing it leaves one orphan file rather than
// keys threaded through shared config.
//
// A plain value type over QSettings: no QObject, no signals, nothing to keep
// in sync. Both the id and the display name of the
// chosen list are kept — see projectName() for why storing only one is not
// enough.
class TickTickPrefs
{
public:
    explicit TickTickPrefs(const QString &configFilePath);

    // Empty until the user has picked a list. The pull resolves this id against
    // the live project list every time rather than trusting it blindly, so a
    // list deleted in TickTick is noticed instead of failing every later pull
    // against an id that no longer exists.
    QString projectId() const;

    // The chosen list's name as it was when it was chosen. Kept alongside the
    // id for two reasons: the settings dropdown can show what is selected
    // before any network call has answered, and a list the user deleted and
    // recreated under the same name comes back with a NEW id — matching on the
    // remembered name recovers that case instead of silently syncing nothing.
    QString projectName() const;

    void setProject(const QString &projectId, const QString &projectName);

    // How often to pull, in minutes. 0 means never — the integration then only
    // syncs on the events it always has (connecting, starting up already
    // connected, the tray, the settings button).
    int syncIntervalMinutes() const;
    void setSyncIntervalMinutes(int minutes);

    // The default when nothing has been stored. Once an hour: frequent enough
    // that a task added on a phone shows up the same working session, rare
    // enough that the app is still doing nothing at all for 99.9% of its life.
    static constexpr int kDefaultSyncIntervalMinutes = 60;

    // A floor on what the user can ask for, enforced here rather than only in
    // the spinbox: this app's whole idle-cost budget rests on pulls being rare,
    // and a hand-edited INI must not be able to turn it into a busy loop.
    static constexpr int kMinSyncIntervalMinutes = 5;
    static constexpr int kMaxSyncIntervalMinutes = 24 * 60;

    // Clamps to [kMin, kMax], passing 0 (never) through untouched. Public and
    // static so the same rule is testable on its own and reusable by the UI.
    static int clampSyncIntervalMinutes(int minutes);

    // When the aggregated task note was last successfully pulled. Persisted
    // (rather than kept only in memory) so the note's "synced N ago" footer
    // reads correctly right after a restart instead of claiming "not synced
    // yet" until the next pull happens to run. Invalid (the default
    // QDateTime()) until the first pull of this app's lifetime completes.
    QDateTime lastSyncTime() const;
    void setLastSyncTime(const QDateTime &time);

    // One-time migration guard (see TickTickNotes' constructor): true once
    // this install has swept away the old one-note-per-task cache files in
    // favor of the single aggregated task note. Checked and set exactly
    // once, so a user who has already migrated never has their cache
    // directory re-scanned/re-deleted on every later launch.
    bool migratedToTaskList() const;
    void setMigratedToTaskList(bool migrated);

private:
    QString m_configFilePath;
};
