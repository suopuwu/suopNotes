#pragma once

#include <QObject>
#include <QPointF>

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

    // Live-moves an already-open window (matched by exact title) to a new
    // global position — see KWinRuleApplier::moveWindow's own comment for
    // why this exists separately from applyPosition (whose position rule
    // only ever takes effect at the window's first creation). Runs off
    // the main thread, same reasoning as removePosition: this shells out
    // to a handful of qdbus6 calls plus a short settle sleep, and nothing
    // here needs to finish before the caller proceeds.
    Q_INVOKABLE void moveWindow(const QString &windowTitle, int x, int y);

    // Coalesces the KWin reconfigure across a burst of applyPosition() calls.
    // main.cpp brackets the QML engine load (which synchronously constructs
    // every restored note window and the frameless chrome windows, each of
    // which calls applyPosition() from its Component.onCompleted) between
    // these two. While batching, applyPosition() writes its rule but defers
    // the reconfigure; endBatch() issues a single reconfigure covering the
    // whole burst instead of one per window (each with its own 200ms settle).
    //
    // This is safe to do before the reconfigure without windows spawning at
    // the wrong spot because it runs BEFORE app.exec(): the windows have set
    // visible=true during the load, but Wayland doesn't actually map their
    // surfaces (the moment KWin evaluates the Apply-Initially position rule)
    // until the event loop runs — which is after endBatch(). Outside a batch
    // (a note opened at runtime, a pin toggle), applyPosition() reconfigures
    // synchronously as before, since there the event loop is already live and
    // the window maps promptly.
    void beginBatch();
    void endBatch();

    // A point guaranteed to fall outside the combined virtual desktop of every
    // connected screen. The several frameless "chrome" windows (the selection
    // toolbar, its popups, the find/replace panel, the HUD and context-menu
    // windows) stay permanently mapped and hide by parking their content at
    // this point, where no monitor can show it. Exceeding the desktop's right
    // and bottom edges by a wide margin clears every monitor arrangement,
    // including layouts with monitors at negative coordinates where a fixed
    // negative offset could still land on a real screen. Centralized here — the
    // object every one of those windows already calls to move itself — so the
    // definition they all park against lives in exactly one place instead of a
    // hand-copied function per window (which had already reached four copies).
    Q_INVOKABLE QPointF offscreenPoint() const;

    bool suppressCleanupOnQuit() const;
    void setSuppressCleanupOnQuit(bool suppress);

signals:
    // Emitted once moveWindow()'s background thread has actually finished
    // running the KWin script — lets a QML caller wait for a specific move
    // to really land (e.g. before revealing a freshly-repositioned popup)
    // instead of guessing at a fixed delay. Fires exactly once per
    // moveWindow() call, in COMPLETION order, which is not necessarily the
    // same as call order (see moveWindow's own comment: each call is an
    // independent, unsynchronized background thread).
    void windowMoved(const QString &windowTitle);

private:
    bool m_suppressCleanupOnQuit = false;

    // See beginBatch()/endBatch(). m_batching gates whether applyPosition()
    // reconfigures inline; m_batchReconfigurePending records whether any
    // write during the batch actually changed something, so an all-unchanged
    // batch (the normal-restart case) ends without reconfiguring at all.
    bool m_batching = false;
    bool m_batchReconfigurePending = false;
};
