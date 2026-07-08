#pragma once

#include <QObject>

// QML-facing wrapper around KWinRuleApplier.
class PositionManager : public QObject
{
    Q_OBJECT
    // Set right before quitting because a later-launched instance is taking
    // over (req. 2), never on a normal close/quit. Without this, every
    // NoteWindow's Component.onDestruction firing during this process's own
    // shutdown would call removePosition() for notes the INCOMING process is
    // concurrently re-applying rules for (same note ids, since both
    // processes read the same persisted session) — a real race confirmed
    // live: the dying instance's cleanup can win that race and leave a note
    // with no KWin rule at all, right after the new instance just wrote one.
    // A replace isn't the user closing these notes, so their rules
    // shouldn't be torn down at all here.
    Q_PROPERTY(bool suppressCleanupOnQuit READ suppressCleanupOnQuit WRITE setSuppressCleanupOnQuit)
public:
    explicit PositionManager(QObject *parent = nullptr);

    // x, y are global (virtual-desktop) coordinates — the same convention
    // frameGeometry and Settings' stored NoteSessionState use everywhere
    // else. Ensures this note has a spawn-position KWin rule (Linux/KWin
    // only; a no-op elsewhere) seeded at that exact point — see
    // KWinRuleSpec's comment for why that alone is what puts the window on
    // the right monitor, with no monitor-forcing rule involved.
    Q_INVOKABLE void applyPosition(const QString &noteId, const QString &windowTitle, int x, int y,
                                    bool alwaysOnTop);

    Q_INVOKABLE void removePosition(const QString &noteId);

    bool suppressCleanupOnQuit() const;
    void setSuppressCleanupOnQuit(bool suppress);

private:
    bool m_suppressCleanupOnQuit = false;
};
