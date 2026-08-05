#pragma once

#include "TickTickClient.h"
#include "TickTickPrefs.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>

class QTimer;

class TickTickAuth;
class TickTickNotes;

// Pulls TickTick tasks into the note cache, and pushes note edits back.
//
// ON THE REPEATING TIMER BELOW. CLAUDE.md forbids recurring polling, and this
// is the one sanctioned exception in the app — granted explicitly, for a
// reason worth writing down. TickTick has no webhooks, no server-sent events
// and no changed-since query, so there is NO event that can tell this app a
// task changed elsewhere. Asking is the only mechanism the API offers. Without
// a schedule the only way to see a task added on a phone is to remember to
// press Sync, which makes the integration feel broken rather than idle.
//
// What keeps it within the spirit of the rule is the cost: one HTTPS request
// per interval, defaulting to once an hour (TickTickPrefs::kDefault…), floored
// at five minutes so a hand-edited config cannot turn it into a busy loop, and
// stopped outright whenever the module is not authorized or the user sets the
// interval to 0. Between ticks the timer is a single armed QTimer and nothing
// else — no work, no wakeups of our own beyond that one. This is emphatically
// NOT licence to add a second such timer anywhere: every other trigger here is
// still an event (a login completing, a stored token finishing its load at
// startup, the tray, the settings button, an edit's one-shot debounce). The
// note's own "synced N ago" footer (NoteWindow.qml) is no exception — it is
// re-stamped on pullFinished/hover/window-activation, never on a clock.
//
// Between ticks a note changed in TickTick still shows its previous content.
// That is not a shortcut taken here — it is the best the API allows, and it is
// why the local copy is defined as a cache rather than a second authoritative
// copy.
//
// ON THE SINGLE AGGREGATED NOTE (feature request: one checkbox-list note for
// every open task, instead of one note per task). The old per-task design
// needed a persistent task-id-to-note-uuid map to give each task's note a
// durable identity across the task being deleted/recreated remotely — real
// note-per-task window geometry, KWin rules and appearance overrides all
// needed that. None of that exists once there is exactly one note:
// m_lastRenderedLines/m_lastRenderedTaskIds (below) are the only bookkeeping
// a single aggregated note needs, and every pull simply replaces them
// outright from the server's own answer.
class TickTickSync : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    // The lists available to choose from, as [{id, name}, …] for a QML model.
    // Empty until refreshProjects() (or a pull) has answered — the settings
    // dropdown asks for them when it opens.
    Q_PROPERTY(QVariantList availableProjects READ availableProjects NOTIFY availableProjectsChanged)
    // The chosen list, readable before any network call so the dropdown has
    // something to show immediately. Empty means "not chosen yet", which the
    // first pull resolves to the default list name.
    Q_PROPERTY(QString selectedProjectId READ selectedProjectId NOTIFY selectedProjectChanged)
    Q_PROPERTY(QString selectedProjectName READ selectedProjectName NOTIFY selectedProjectChanged)
    Q_PROPERTY(int syncIntervalMinutes READ syncIntervalMinutes WRITE setSyncIntervalMinutes
                   NOTIFY syncIntervalMinutesChanged)
    // When this app and TickTick were last known to agree — the raw data
    // behind the note's own "synced N ago" footer (NoteWindow.qml turns this
    // into relative text itself; see its own no-polling comment for why that
    // text is recomputed on events rather than a timer). Invalid (QDateTime())
    // until the first sync of this app's lifetime completes.
    //
    // Stamped by BOTH halves of a sync — a pull answering, and a local edit
    // being confirmed by TickTick (see stampSyncedNow) — not by the pull
    // alone. A push IS a sync: the user's own text has reached the server, and
    // a footer that still read "synced 40m ago" a second after their typing
    // had been delivered looked like the upload had silently not happened.
    Q_PROPERTY(QDateTime lastSyncTime READ lastSyncTime NOTIFY lastSyncTimeChanged)

public:
    TickTickSync(TickTickAuth &auth,
                 TickTickNotes &notes,
                 const QString &configFilePath,
                 QObject *parent = nullptr);

    bool isBusy() const;
    QString lastError() const;

    // The list this app pulls from when the user has not chosen one — created
    // on first run if it does not exist. Once a list HAS been chosen, a missing
    // one is reported rather than recreated: silently making a second list
    // would look like the user's notes had vanished.
    static QString defaultProjectName();

    QVariantList availableProjects() const;
    QString selectedProjectId() const;
    QString selectedProjectName() const;

    // Asks TickTick for the user's lists without pulling any tasks — what the
    // settings dropdown calls when it opens. No-op when not authorized.
    Q_INVOKABLE void refreshProjects();

    // Switches which list is pulled from, and immediately pulls it. Passing an
    // empty id reverts to resolving defaultProjectName() by name.
    Q_INVOKABLE void selectProject(const QString &projectId, const QString &projectName);

    int syncIntervalMinutes() const;
    void setSyncIntervalMinutes(int minutes);

    QDateTime lastSyncTime() const;

    // Safe to call at any time; does nothing if not authorized or already
    // running.
    Q_INVOKABLE void pullNow();

    // The one note every open TickTick task now lives in, as a note id QML
    // can compare against — so NoteWindow.qml can ask "is this note the task
    // note?" without ever learning the id scheme (TickTickModule::
    // makeNoteId/TickTickNotes::taskListUuid stay private to this module).
    // Empty only if the module has somehow been asked before construction
    // finished, which does not happen in practice.
    Q_INVOKABLE QString taskNoteId() const;

    // Makes sure the aggregated note exists with an empty compose slot ready to
    // type into, and returns its id (the same id every time — there is no
    // per-task note left to open a window for). The HUD's "＋ task" button and
    // the tray's "New TickTick Task" call this; the caller doesn't need to open
    // anything itself, since the note is already open (or reconcile opens it on
    // the same call stack the first time) — see NoteWindow.qml's createNewTask,
    // which additionally puts the caret in the slot.
    //
    // This no longer creates anything on TickTick, and that is the point: under
    // the compose slot (see TickTickTaskList::kComposeIndex) a task exists only
    // once it has a title the user has committed. The old behavior — dispatch a
    // create for an empty line the instant the button was pressed — put a blank
    // task on the user's phone for every stray click.
    //
    // Returns an empty string, having reported why, if the local note could not
    // be written at all.
    Q_INVOKABLE QString ensureComposeSlot();

    // Turns whatever is in the note's compose slot into a real TickTick task:
    // dispatches the create, moves the typed title down into a task line of its
    // own, and leaves a fresh empty slot on top ready for the next one. This is
    // the ONLY way a task is created — see TickTickTaskList::Intent.
    //
    // Called from the note itself, by the two gestures that mean "commit":
    // Enter, and the check button the slot's checkbox turns into once it has
    // text (NoteWindow.qml).
    //
    // Returns false, having reported why, when the slot is empty (nothing to
    // commit — not an error, just nothing to do), or when there is no session
    // or no resolved list yet. The typed text is left exactly where it is in
    // every one of those cases, so nothing the user wrote is lost to a failed
    // commit.
    Q_INVOKABLE bool commitComposedTask();

    // Marks `taskId` complete in TickTick. Called from this class's own diff-
    // driven dispatch (see flushPendingUploads) for both ways a line can mean
    // "I'm done with this" — ticking the box, and deleting the line outright
    // (Decision, drain 57a85c8d, modification 3) — never a remote delete.
    //
    // Returns false, having reported why, when there is no session, no
    // resolved project, or `taskId` is actually a not-yet-confirmed local
    // create (see m_pendingCreateClientRefs) — completing "nothing" silently
    // would look like the click was ignored. A pending create is instead
    // remembered (m_completeAfterCreate) and completed the moment its real
    // id arrives.
    //
    // The local copy is never dropped optimistically: the note's own text was
    // already updated by the diff that led here, and nothing further needs to
    // happen to it once TickTick confirms — see the taskCompleted client
    // signal handler.
    Q_INVOKABLE bool completeTask(const QString &taskId);

    // Notes edited locally whose upload has not yet succeeded. Non-zero simply
    // means "not delivered yet" — the edits themselves are already durable in
    // the cache. This is also the sound signal NoteWindow.qml's body-click
    // refresh gate uses (see its own comment) instead of NoteStore::
    // hasUnsavedChanges, which is always false for a provider-owned note (its
    // debounce lives here, not in NoteStore's pending-save map) and so would
    // never actually suppress anything.
    Q_INVOKABLE int pendingUploadCount() const;

    // The debounce this class waits out before an edit reaches TickTick.
    // Exposed only so a test can pin the invariant it exists to protect: this
    // must stay strictly longer than tickTickCompletionFade's ~3000ms hold in
    // NoteWindow.qml, or the undo window that fade advertises is fake (see
    // kUploadDebounceMs's own comment in TickTickSync.cpp). Not meant for any
    // other caller — production code never needs the raw number, only the
    // timer that already uses it.
    static int uploadDebounceMs();

signals:
    void busyChanged();
    void lastErrorChanged();
    void availableProjectsChanged();
    void selectedProjectChanged();
    void syncIntervalMinutesChanged();
    void lastSyncTimeChanged();
    void pullFinished(int noteCount);
    void pushFinished(const QString &taskId);
    // A completion TickTick confirmed, after the local diff already updated
    // the note's own text (see completeTask's doc comment) — nothing else
    // reacts to this yet, but it is kept, matching pushFinished, as a hook a
    // future "task completed" toast could use without touching this class.
    void taskCompleted(const QString &taskId);

private:
    void beginPull();
    void onNoteEditedLocally(const QString &localUuid);
    void flushPendingUploads();
    // What a client reply calls instead of removing from m_pendingUploads
    // directly: a reply confirms delivery of what was DISPATCHED, not of
    // anything typed since. If the upload debounce is running, a newer edit is
    // already queued behind this reply, and clearing the flag here would make
    // the flush that debounce fires early-return and silently drop that edit —
    // the next pull would then revert it on screen.
    void clearPendingUploadUnlessEditQueued();
    // Sends (or retries) the create request for one pending line, using
    // whatever title is currently remembered for its clientRef.
    void sendCreateFor(const QString &clientRef);
    void onProjectsFetched(const QList<TickTickClient::Project> &projects);
    void onTasksFetched(const QString &projectId, const QList<TickTickClient::Task> &tasks);
    void fail(const QString &reason);
    void setBusy(bool busy);
    // Records "this app and TickTick agreed just now" and announces it, so
    // every place that means it says it the same way. See lastSyncTime above
    // for why the push confirmations call this and not just the pull.
    void stampSyncedNow();

    // Starts, re-intervals or stops the scheduled pull to match the current
    // authorization state and interval preference. Called from every place
    // either can change — there is no other way the timer's state moves.
    void updateSchedule();

    TickTickAuth &m_auth;
    TickTickNotes &m_notes;
    TickTickClient m_client;
    TickTickPrefs m_prefs;

    // One-shot, restarted by each edit — an event-driven debounce, not a
    // schedule. It costs nothing while nobody is typing.
    QTimer *m_uploadDebounce = nullptr;
    // The sanctioned repeating timer; see this class's header comment. Armed
    // only while authorized with a non-zero interval.
    QTimer *m_scheduleTimer = nullptr;

    // --- the aggregated task note's own state ---
    //
    // The last text this class rendered (from a pull) or accepted (from a
    // dispatched edit), index-aligned with m_lastRenderedTaskIds — see
    // TickTickTaskList::diff for what "index-aligned" buys. Every element of
    // m_lastRenderedTaskIds is a real TickTick task id, a clientRef for a line
    // whose create has not been confirmed yet (see m_pendingCreateClientRefs),
    // or — for the compose slot alone, always at index 0 — the empty string
    // (TickTickTaskList::composeTaskId).
    QStringList m_lastRenderedLines;
    QStringList m_lastRenderedTaskIds;

    // Each known task's own `content` field (the description/details a note
    // never shows or edits anymore — see the Issue: "we don't need to worry
    // about being able to see the details for each item"). Remembered purely
    // so a rename dispatched from the note can send updateTask's required
    // content parameter back UNCHANGED instead of blanking it: TickTick's
    // update body always overwrites content with whatever this app sends
    // (see TickTickClient::buildTaskUpdateBody), and the note itself only
    // ever carries a title.
    QHash<QString, QString> m_taskContentById;

    // clientRefs (freshly minted per created line, never real task ids) whose
    // create has been dispatched but not yet confirmed — the subset also in
    // m_creatingNow is currently in flight; the rest are queued for a retry on
    // the next natural trigger. Mirrors the old per-note m_awaitingTaskId/
    // m_creatingNow pair, just scoped to "a line" instead of "a note".
    QSet<QString> m_pendingCreateClientRefs;
    QSet<QString> m_creatingNow;
    // clientRef -> the title to send (or re-send) for its create — kept
    // separately from the note's own text because by the time a retry or a
    // stitch-in-the-real-id runs, the line's position in the note may already
    // have moved.
    QHash<QString, QString> m_pendingCreateTitles;
    // clientRefs whose line was ticked or deleted locally BEFORE their create
    // was confirmed. There is no task to complete yet when that happens — so
    // it is remembered here, and completeTask() fires the moment taskCreated
    // supplies the real id, rather than leaving an orphaned task open on
    // TickTick that the user already said they were done with.
    QSet<QString> m_completeAfterCreate;

    // Notes whose latest text has not reached TickTick yet. A failed upload
    // stays here and is retried on the next natural trigger rather than on a
    // retry timer. In practice this holds at most one entry — TickTickNotes::
    // taskListUuid() — since there is only one TickTick-backed note now; kept
    // as a set rather than a bool so onNoteEditedLocally/flushPendingUploads
    // don't need a special case of their own.
    QSet<QString> m_pendingUploads;

    QList<TickTickClient::Project> m_availableProjects;

    QString m_projectId;
    bool m_busy = false;
    // Distinguishes a project list fetched as the first step of a pull from one
    // fetched just to populate the settings dropdown.
    bool m_pullAfterProjects = false;
    // Guards the one-shot project creation, so a missing project cannot loop
    // create-then-fail-then-create.
    bool m_triedCreatingProject = false;
    QString m_lastError;
};
