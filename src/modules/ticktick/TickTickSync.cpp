#include "TickTickSync.h"

#include "TickTickAuth.h"
#include "TickTickModule.h"
#include "TickTickNotes.h"
#include "TickTickTaskList.h"

#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QVariantMap>

namespace {
// Longer than the local save debounce (~300ms): that one guards a microsecond
// file write, this one guards a network round trip, and there is no value in
// uploading every pause in someone's typing.
//
// Also — and this is why 3500 rather than something closer to that 300ms —
// deliberately kept LONGER than tickTickCompletionFade's three-second total in
// NoteWindow.qml. That fade is the advertised undo window for a tick: the
// line stays on screen, still just locally checked, for three seconds before
// it disappears. A completion sent before the line actually goes would make
// the undo window fake — Ctrl+Z could untick the line, but the request is
// already in flight and nothing in TickTickClient can reopen a completed
// task (see TickTickClient.h). Keeping this debounce longer means the flush
// that finally fires sees the tick AND the fade's removal together, as a
// single edit, so an undo pressed anywhere inside the hold has something
// real to cancel. See flushPendingUploads below for what that coalesced
// flush actually does with a line seen both ticked and gone.
constexpr int kUploadDebounceMs = 3500;

constexpr int kMsPerMinute = 60 * 1000;
}

TickTickSync::TickTickSync(TickTickAuth &auth,
                           TickTickNotes &notes,
                           const QString &configFilePath,
                           QObject *parent)
    : QObject(parent)
    , m_auth(auth)
    , m_notes(notes)
    , m_client(this)
    , m_prefs(configFilePath)
{
    connect(&m_client, &TickTickClient::projectsFetched,
            this, &TickTickSync::onProjectsFetched);
    connect(&m_client, &TickTickClient::tasksFetched,
            this, &TickTickSync::onTasksFetched);

    connect(&m_client, &TickTickClient::projectCreated, this,
            [this](const TickTickClient::Project &project) {
                m_projectId = project.id;
                m_prefs.setProject(project.id, project.name);
                emit selectedProjectChanged();
                m_client.fetchTasksInProject(m_projectId);
            });

    connect(&m_client, &TickTickClient::requestFailed, this, &TickTickSync::fail);

    connect(&m_client, &TickTickClient::resultsPossiblyTruncated, this, [this](int count) {
        // Said out loud rather than swallowed: with no paging in the API, a
        // capped response would otherwise look exactly like a complete one and
        // notes past the cap would silently never appear.
        fail(tr("TickTick returned %1 tasks, which is its maximum for one request — "
                "some notes may be missing.")
                 .arg(count));
    });

    // Covers both automatic pulls with one connection: a login completing, and
    // a stored token finishing loading at startup (which is asynchronous, since
    // the secret store is). Guarded because this also fires on sign-out — which
    // is also where the scheduled pull has to stop, so signing out really does
    // leave the app doing nothing at all.
    connect(&m_auth, &TickTickAuth::authorizedChanged, this, [this]() {
        updateSchedule();
        if (m_auth.isAuthorized())
            pullNow();
    });

    connect(&m_client, &TickTickClient::taskUpdated, this, [this](const QString &taskId) {
        clearPendingUploadUnlessEditQueued();
        stampSyncedNow();
        emit pushFinished(taskId);
    });

    connect(&m_client, &TickTickClient::taskCompleted, this, [this](const QString &taskId) {
        // The note's own text was already updated by the diff that led here
        // (see completeTask/flushPendingUploads) — there is nothing further
        // to do to it now that TickTick has confirmed. This just clears the
        // guard that was holding pulls back, and reports the id in case a
        // future "task completed" toast wants it.
        clearPendingUploadUnlessEditQueued();
        stampSyncedNow();
        emit taskCompleted(taskId);
    });

    connect(&m_client, &TickTickClient::taskCreated, this,
            [this](const QString &clientRef, const TickTickClient::Task &task) {
                m_creatingNow.remove(clientRef);
                m_pendingCreateClientRefs.remove(clientRef);
                // Before the m_completeAfterCreate branch below returns early,
                // so both ways out of this handler stamp: the task exists on
                // TickTick either way, which is what the footer is reporting.
                stampSyncedNow();
                const QString latestTitle = m_pendingCreateTitles.take(clientRef);

                const int index = m_lastRenderedTaskIds.indexOf(clientRef);
                if (index >= 0)
                    m_lastRenderedTaskIds[index] = task.id;
                m_taskContentById.insert(task.id, task.content);

                if (m_completeAfterCreate.remove(clientRef)) {
                    // The user already ticked or deleted this line before its
                    // own create even confirmed. There is no task to close
                    // out until right now — honoring it here is what stops an
                    // orphaned task sitting open on TickTick that the user
                    // already said they were done with.
                    if (index >= 0) {
                        m_lastRenderedTaskIds.removeAt(index);
                        if (index < m_lastRenderedLines.size())
                            m_lastRenderedLines.removeAt(index);
                    }
                    m_client.setAccessToken(m_auth.accessToken());
                    m_client.completeTask(m_projectId, task.id);
                    return;
                }

                clearPendingUploadUnlessEditQueued();

                // Anything typed into this line while the create was still in
                // flight is queued but unsendable (it had no task id). Now it
                // has one.
                if (!latestTitle.isEmpty() && latestTitle != task.title) {
                    m_client.setAccessToken(m_auth.accessToken());
                    m_client.updateTask(task.id, m_projectId, latestTitle, QString());
                }
            });

    connect(&m_client, &TickTickClient::taskCreateFailed, this, [this](const QString &clientRef) {
        // Dropped from "in flight" but kept in "awaiting an id", so the next
        // natural trigger retries it. requestFailed has already reported why.
        m_creatingNow.remove(clientRef);
    });

    connect(&m_client, &TickTickClient::taskRejectedPermanently, this,
            [this](const QString &, const QString &reason) {
                // Dropped rather than retried: this request can never succeed,
                // and leaving it queued would re-send it on every later trigger
                // forever. The edit is still safe in the cache. (A newer edit
                // still waiting on the debounce is a different edit — it keeps
                // its place and gets its own verdict, same as any reply.)
                clearPendingUploadUnlessEditQueued();
                fail(reason);
            });

    m_uploadDebounce = new QTimer(this);
    m_uploadDebounce->setSingleShot(true);
    connect(m_uploadDebounce, &QTimer::timeout, this, &TickTickSync::flushPendingUploads);

    // The one repeating timer in this app — see the class header for why it is
    // allowed here and nowhere else. Created stopped; updateSchedule() below is
    // the only thing that ever arms it.
    m_scheduleTimer = new QTimer(this);
    m_scheduleTimer->setSingleShot(false);
    connect(m_scheduleTimer, &QTimer::timeout, this, &TickTickSync::pullNow);

    connect(&m_notes, &TickTickNotes::noteEditedLocally, this,
            [this](const QString &localUuid, const QString &) { onNoteEditedLocally(localUuid); });

    m_projectId = m_prefs.projectId();

    // One-time migration off the old one-note-per-task cache (feature
    // request: one aggregated checkbox-list note instead of one note per
    // task). Guarded so it runs exactly once per install — see
    // TickTickPrefs::migratedToTaskList and TickTickNotes::
    // removeAllCachedNotesExcept for why leaving the old files in place would
    // be a real (if silent) problem, not just clutter.
    if (!m_prefs.migratedToTaskList()) {
        m_notes.removeAllCachedNotesExcept(TickTickNotes::taskListUuid());
        m_prefs.setMigratedToTaskList(true);
    }

    updateSchedule();
}

QString TickTickSync::defaultProjectName()
{
    return QStringLiteral("suopNotes");
}

bool TickTickSync::isBusy() const
{
    return m_busy;
}

QString TickTickSync::lastError() const
{
    return m_lastError;
}

QVariantList TickTickSync::availableProjects() const
{
    QVariantList list;
    list.reserve(m_availableProjects.size());
    for (const TickTickClient::Project &project : m_availableProjects) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), project.id);
        entry.insert(QStringLiteral("name"), project.name);
        list.append(entry);
    }
    return list;
}

QString TickTickSync::selectedProjectId() const
{
    return m_prefs.projectId();
}

QString TickTickSync::selectedProjectName() const
{
    return m_prefs.projectName();
}

int TickTickSync::syncIntervalMinutes() const
{
    return m_prefs.syncIntervalMinutes();
}

void TickTickSync::setSyncIntervalMinutes(int minutes)
{
    const int clamped = TickTickPrefs::clampSyncIntervalMinutes(minutes);
    if (clamped == m_prefs.syncIntervalMinutes())
        return;

    m_prefs.setSyncIntervalMinutes(clamped);
    updateSchedule();
    emit syncIntervalMinutesChanged();
}

QDateTime TickTickSync::lastSyncTime() const
{
    return m_prefs.lastSyncTime();
}

void TickTickSync::updateSchedule()
{
    const int minutes = m_prefs.syncIntervalMinutes();

    // Not authorized, or the user asked for no schedule at all: the timer is
    // stopped outright rather than left armed and short-circuiting in its
    // handler, so an idle or signed-out app has genuinely nothing scheduled.
    if (!m_auth.isAuthorized() || minutes <= 0) {
        m_scheduleTimer->stop();
        return;
    }

    m_scheduleTimer->setInterval(minutes * kMsPerMinute);
    // start() on an already-running timer restarts it, which is what makes a
    // manual pull (see beginPull) push the next scheduled one a full interval
    // out instead of firing moments later.
    m_scheduleTimer->start();
}

void TickTickSync::refreshProjects()
{
    // Declined while a pull is running, and not merely to save a request: that
    // pull is waiting on a projectsFetched of its own, and clearing
    // m_pullAfterProjects below would make it read that response as this
    // refresh's — never fetching any tasks, and leaving busy stuck on forever.
    // A pull publishes availableProjects anyway, so the dropdown loses nothing.
    if (!m_auth.isAuthorized() || m_busy)
        return;

    // Deliberately does not set busy: this is a background refresh for the
    // settings dropdown, and showing "Syncing…" for it would suggest notes were
    // being fetched when none are.
    m_pullAfterProjects = false;
    m_client.setAccessToken(m_auth.accessToken());
    m_client.fetchProjects();
}

void TickTickSync::selectProject(const QString &projectId, const QString &projectName)
{
    if (projectId == m_prefs.projectId() && projectName == m_prefs.projectName())
        return;

    m_prefs.setProject(projectId, projectName);
    m_projectId = projectId;
    emit selectedProjectChanged();

    // The cached note belongs to the OLD list — left in place it would keep
    // showing that list's tasks until a pull happened to overwrite it, which
    // would read as "my tasks vanished" rather than "the list changed".
    //
    // Emptied rather than REMOVED, which is what this used to do. Removing it
    // dropped the note out of NoteStore::noteIds(), so Main.qml's reconcile
    // closed its window — and closing a note discards its session state, i.e.
    // its saved position and size, plus its per-note appearance overrides. The
    // pull moments later then reopened it as a brand-new note at the default
    // cascade position on the primary monitor. Switching TickTick lists is not
    // a reason to lose where the user keeps their task note.
    const QString localUuid = TickTickNotes::taskListUuid();
    const QString emptied = TickTickTaskList::render({}, QString());
    m_notes.writeCachedNote(localUuid, emptied);
    m_lastRenderedLines = TickTickTaskList::linesOf(emptied);
    m_lastRenderedTaskIds = TickTickTaskList::taskIdsFor({});
    m_taskContentById.clear();

    // No-ops if a pull for the OLD list is still in flight — that pull's
    // response then arrives carrying the old project id, and onTasksFetched's
    // stale-response guard is what starts the fresh pull in its place.
    pullNow();
}

void TickTickSync::pullNow()
{
    if (m_busy || !m_auth.isAuthorized())
        return;
    beginPull();
}

void TickTickSync::beginPull()
{
    setBusy(true);
    m_triedCreatingProject = false;
    m_pullAfterProjects = true;

    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit lastErrorChanged();
    }

    m_client.setAccessToken(m_auth.accessToken());

    // Any pull, scheduled or manual, resets the clock — so pressing Sync now
    // does not leave a scheduled pull about to fire seconds later.
    updateSchedule();

    // The project id is re-resolved against the live list every pull rather
    // than trusted from config: one cheap request, and it means a list deleted
    // or renamed in TickTick is noticed instead of causing every later pull to
    // fail against an id that no longer exists.
    m_client.fetchProjects();
}

void TickTickSync::onProjectsFetched(const QList<TickTickClient::Project> &projects)
{
    m_availableProjects = projects;
    emit availableProjectsChanged();

    // A dropdown refresh, not a pull — nothing further to do, and in particular
    // no project must be created off the back of it.
    if (!m_pullAfterProjects)
        return;
    m_pullAfterProjects = false;

    const QString wantedId = m_prefs.projectId();
    const QString wantedName = m_prefs.projectName();
    const bool userHasChosen = !wantedId.isEmpty() || !wantedName.isEmpty();

    // Id first, because it survives a rename. Name second, because it survives
    // a delete-and-recreate — which mints a new id for what the user still
    // thinks of as the same list.
    for (const TickTickClient::Project &project : projects) {
        if (!wantedId.isEmpty() && project.id == wantedId) {
            m_projectId = project.id;
            if (project.name != wantedName) {
                m_prefs.setProject(project.id, project.name);
                emit selectedProjectChanged();
            }
            m_client.fetchTasksInProject(m_projectId);
            return;
        }
    }

    const QString byName = userHasChosen ? wantedName : defaultProjectName();
    for (const TickTickClient::Project &project : projects) {
        if (!byName.isEmpty() && project.name == byName) {
            m_projectId = project.id;
            m_prefs.setProject(project.id, project.name);
            emit selectedProjectChanged();
            m_client.fetchTasksInProject(m_projectId);
            return;
        }
    }

    // A list the user explicitly chose has gone. Reported, never recreated:
    // quietly making a fresh empty list of the same name would present as "all
    // my notes disappeared" with no hint that the list itself was the problem.
    if (userHasChosen) {
        fail(tr("The TickTick list \"%1\" no longer exists. Pick another in Settings.")
                 .arg(wantedName.isEmpty() ? wantedId : wantedName));
        return;
    }

    if (m_triedCreatingProject) {
        fail(tr("Could not find or create the %1 list in TickTick.").arg(defaultProjectName()));
        return;
    }

    // First run and no list chosen: the default does not exist yet.
    m_triedCreatingProject = true;
    m_client.createProject(defaultProjectName());
}

void TickTickSync::onTasksFetched(const QString &projectId, const QList<TickTickClient::Task> &tasks)
{
    // A response for a list the user has since switched away from: while this
    // request was in flight, selectProject changed m_projectId and emptied the
    // note (its pullNow was a no-op against this pull's busy flag). Rendering
    // the response would fill the just-emptied note with the OLD list's tasks
    // and stamp them as synced — so it is dropped, and the pull that could not
    // start then runs now instead of waiting for the next scheduled one.
    if (projectId != m_projectId) {
        setBusy(false);
        pullNow();
        return;
    }

    // Remembered regardless of whether the note gets rewritten below (see the
    // pending-upload guard just after), so a rename dispatched later still
    // has each task's latest description available to send back unchanged.
    m_taskContentById.clear();
    for (const TickTickClient::Task &task : tasks)
        m_taskContentById.insert(task.id, task.content);

    const QString localUuid = TickTickNotes::taskListUuid();

    // A note with an edit still queued is NOT overwritten. TickTick owns the
    // content, but a pull carrying a version older than what the user just
    // typed would revert their text on screen mid-session — which the
    // scheduled pull (and now the note's own click-to-refresh gesture) makes
    // a routine occurrence rather than a rare race. This matters MORE now
    // than it used to: there is only one note, so any single unconfirmed
    // edit anywhere in the task list holds back the WHOLE render, not just
    // one task's.
    if (!m_pendingUploads.contains(localUuid)) {
        // Whatever the user had already typed into the compose slot is carried
        // into the new render rather than replaced with a blank one. Without
        // this, a scheduled pull landing mid-sentence would silently wipe a
        // task they were halfway through writing — and unlike a task line,
        // there is no server copy to get it back from.
        const QString noteId = TickTickModule::makeNoteId(localUuid);
        const QString composeTitle =
            TickTickTaskList::composeTitleOf(m_notes.loadNoteContent(noteId));

        const QString rendered = TickTickTaskList::render(tasks, composeTitle);
        m_lastRenderedTaskIds = TickTickTaskList::taskIdsFor(tasks);
        m_lastRenderedLines = TickTickTaskList::linesOf(rendered);
        m_notes.writeCachedNote(localUuid, rendered);
    }

    // Stamped unconditionally: a pull happened and answered, whether or not
    // it happened to skip rewriting the note this time.
    stampSyncedNow();

    setBusy(false);

    // Anything that was waiting on a project id, or a create/update that
    // needs retrying, gets its chance now — see flushPendingUploads.
    flushPendingUploads();

    emit pullFinished(tasks.size());
}

int TickTickSync::pendingUploadCount() const
{
    return m_pendingUploads.size();
}

int TickTickSync::uploadDebounceMs()
{
    return kUploadDebounceMs;
}

QString TickTickSync::taskNoteId() const
{
    return TickTickModule::makeNoteId(TickTickNotes::taskListUuid());
}

bool TickTickSync::completeTask(const QString &taskId)
{
    if (taskId.isEmpty())
        return false;

    if (!m_auth.isAuthorized()) {
        fail(tr("Connect TickTick before completing a task."));
        return false;
    }

    // This line's own create has not confirmed yet — there is no real task
    // to address. Remembered so the moment taskCreated supplies the real id
    // (see that handler), it is completed immediately instead of being left
    // open on TickTick after the user already said they were done with it.
    if (m_pendingCreateClientRefs.contains(taskId)) {
        m_completeAfterCreate.insert(taskId);
        return true;
    }

    if (m_projectId.isEmpty()) {
        fail(tr("That task has not reached TickTick yet — try again in a moment."));
        return false;
    }

    m_client.setAccessToken(m_auth.accessToken());
    m_client.completeTask(m_projectId, taskId);
    return true;
}

QString TickTickSync::ensureComposeSlot()
{
    const QString localUuid = TickTickNotes::taskListUuid();
    const QString noteId = TickTickModule::makeNoteId(localUuid);

    // Already there: every render puts a compose slot at the top, so an
    // existing note has one and there is nothing to do but say where it is.
    // Deliberately not "and it is empty" — a slot the user has half-typed into
    // is exactly what they want the caret back in.
    if (m_notes.hasCachedNote(localUuid))
        return noteId;

    // The very first time, before any pull has answered: write the note so it
    // has a slot to type into offline. writeCachedNote both writes the cache
    // AND announces the new note id, and NoteStore's reconcile turns that into
    // a window synchronously, on this same call stack (see NoteStore::
    // createNote).
    const QString rendered = TickTickTaskList::render({}, QString());
    if (m_notes.writeCachedNote(localUuid, rendered).isEmpty()) {
        fail(tr("Could not create the local task note."));
        return QString();
    }

    // The baseline is this empty render, not nothing: a diff against an empty
    // baseline would read the slot itself as an added line.
    m_lastRenderedLines = TickTickTaskList::linesOf(rendered);
    m_lastRenderedTaskIds = TickTickTaskList::taskIdsFor({});

    // Nothing was uploaded and nothing needs to be — an empty slot is not a
    // task. A pull is still worth kicking off, since this is very likely the
    // user's first look at the note.
    pullNow();
    return noteId;
}

bool TickTickSync::commitComposedTask()
{
    // Any edit still waiting on the debounce is dispatched against the CURRENT
    // baseline first. The commit below rewrites the note and moves the baseline
    // with it — and a rename typed moments before the commit gesture would be
    // absorbed into that move without ever being diffed: it would sit on screen
    // looking saved, and the next pull would quietly revert it. Guarded on the
    // timer so a commit with nothing queued does not touch the flush path at
    // all (in particular, a dispatched-and-awaiting-reply edit stays exactly
    // where it is).
    if (m_uploadDebounce->isActive()) {
        m_uploadDebounce->stop();
        flushPendingUploads();
    }

    const QString localUuid = TickTickNotes::taskListUuid();
    const QString noteId = TickTickModule::makeNoteId(localUuid);
    const QString currentText = m_notes.loadNoteContent(noteId);
    const QString title = TickTickTaskList::composeTitleOf(currentText).trimmed();

    // Nothing typed. Not an error and not worth an error banner — pressing
    // Enter on an empty slot should simply do nothing at all.
    if (title.isEmpty())
        return false;

    if (!m_auth.isAuthorized()) {
        fail(tr("Connect TickTick before creating a task."));
        return false;
    }

    if (m_projectId.isEmpty()) {
        // Nothing to address the create to yet. Pulling resolves the list and
        // the user can commit again a moment later — better than moving their
        // text into a task line that can never become a task. The text stays
        // in the slot meanwhile, so nothing is lost.
        //
        // The pull is kicked FIRST, and the message set directly rather than
        // through fail(): fail() flips busy off, which used to let this very
        // pullNow() start a pull whose beginPull immediately wiped the message
        // — the user never saw why their Enter did nothing. This order lets
        // the message survive the pull it asks for.
        pullNow();
        m_lastError = tr("Syncing TickTick first — try again in a moment.");
        emit lastErrorChanged();
        return false;
    }

    const QString committed = TickTickTaskList::withComposedTaskCommitted(currentText, title);
    if (m_notes.writeCachedNote(localUuid, committed).isEmpty()) {
        fail(tr("Could not write the new task to the local note."));
        return false;
    }

    // The baseline moves with the note. The clientRef stands in for the real
    // task id until taskCreated supplies it (see that handler, which stitches
    // it into exactly this slot) — index 1, directly under the compose slot,
    // because that is where the new line went.
    const QString clientRef = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QStringList taskIds = m_lastRenderedTaskIds;
    const QStringList currentLines = TickTickTaskList::linesOf(currentText);
    if (taskIds.size() != currentLines.size()) {
        // The remembered ids are not index-aligned with what is actually in the
        // note — the note came back from the cache this session and no pull has
        // answered yet, say. Every line whose task is genuinely unknown gets the
        // compose slot's empty id, which diff() reads as "derive nothing from
        // this line": the next pull re-establishes the real ids, and until then
        // a rename or a tick is dropped rather than addressed to a guess.
        taskIds = QStringList(currentLines.size(), TickTickTaskList::composeTaskId());
    }
    if (taskIds.isEmpty())
        taskIds.append(TickTickTaskList::composeTaskId());
    taskIds.insert(TickTickTaskList::kComposeIndex + 1, clientRef);

    m_lastRenderedLines = TickTickTaskList::linesOf(committed);
    m_lastRenderedTaskIds = taskIds;

    m_pendingCreateClientRefs.insert(clientRef);
    m_pendingCreateTitles.insert(clientRef, title);
    sendCreateFor(clientRef);

    // The typing that filled the slot left the note queued for upload. The note
    // now matches the baseline exactly and the create is in flight on its own
    // bookkeeping, so there is no note-level edit left to send.
    m_pendingUploads.remove(localUuid);
    return true;
}

void TickTickSync::sendCreateFor(const QString &clientRef)
{
    if (!m_auth.isAuthorized() || m_projectId.isEmpty())
        return;

    m_creatingNow.insert(clientRef);
    m_client.setAccessToken(m_auth.accessToken());
    m_client.createTask(clientRef, m_projectId, m_pendingCreateTitles.value(clientRef), QString());
}

void TickTickSync::onNoteEditedLocally(const QString &localUuid)
{
    if (localUuid.isEmpty())
        return;

    m_pendingUploads.insert(localUuid);
    // Restarting a running single-shot timer coalesces a burst of typing into
    // one upload, the same way the local save debounce coalesces writes.
    m_uploadDebounce->start(kUploadDebounceMs);
}

void TickTickSync::flushPendingUploads()
{
    // Retry anything still waiting on a create id: everything in
    // m_pendingCreateClientRefs wants a real task id, and the subset also in
    // m_creatingNow already has a request in flight, so only the rest are
    // (re)sent — the same shape the old per-note m_awaitingTaskId/
    // m_creatingNow pair used, just scoped to a line instead of a note.
    const QSet<QString> awaitingCreate = m_pendingCreateClientRefs;
    for (const QString &clientRef : awaitingCreate) {
        if (!m_creatingNow.contains(clientRef))
            sendCreateFor(clientRef);
    }

    const QString localUuid = TickTickNotes::taskListUuid();
    if (!m_pendingUploads.contains(localUuid))
        return;

    if (!m_auth.isAuthorized()) {
        // Left queued deliberately: signing back in fires authorizedChanged,
        // which pulls, and the next edit or pull flushes this. No retry timer.
        return;
    }

    if (m_projectId.isEmpty()) {
        // projectId comes from the last pull. Without one there is nothing to
        // address a change to, so hold the edit until a pull has run — and
        // that pull's own completion calls back here.
        pullNow();
        return;
    }

    const QString noteId = TickTickModule::makeNoteId(localUuid);
    const QString currentText = m_notes.loadNoteContent(noteId);
    const QStringList currentLines = TickTickTaskList::linesOf(currentText);
    const TickTickTaskList::Intent intent =
        TickTickTaskList::diff(m_lastRenderedLines, m_lastRenderedTaskIds, currentText);

    const bool nothingToDo = intent.completedTaskIds.isEmpty() && intent.renamedTasks.isEmpty()
        && intent.removedIndices.isEmpty();

    if (nothingToDo) {
        // An empty Intent means one of two very different states — see
        // TickTickTaskList::Intent::understood — and only one of them is safe
        // to treat as "the baseline is still correct".
        if (currentLines == m_lastRenderedLines) {
            // Genuinely unchanged: today's behavior, unchanged. Nothing was
            // typed since the last flush (or it was typed and then undone
            // back to exactly this text), so there is nothing to upload and
            // nothing to move.
            m_pendingUploads.remove(localUuid);
            return;
        }

        if (intent.understood) {
            // diff() read the edit correctly and there was deliberately
            // nothing to send — typing into the compose slot is the routine
            // case (see TickTickTaskList::diff). The text still moved, so the
            // baseline has to move with it, or the very next edit diffs
            // against stale text and falls into the branch below for no
            // reason. Ids stay as they are: every edit that reaches this
            // branch understood leaves the line count unchanged (a rename or
            // tick would have produced a non-empty Intent instead), so
            // m_lastRenderedTaskIds is still index-aligned with currentLines.
            m_lastRenderedLines = currentLines;
            m_pendingUploads.remove(localUuid);
            return;
        }

        // Not understood: diff() refused to guess (a reorder, several lines
        // touched at once, an ambiguous removal, or a paste/undo that added a
        // line — see TickTickTaskList::diff's own doc comment). The
        // remembered baseline no longer matches the note and will not fix
        // itself — reconcile now with a pull, which re-establishes both the
        // note text and the baseline from the server. pullNow() already
        // no-ops when busy or unauthorized, so a burst of uninterpretable
        // edits cannot fan out into a burst of requests.
        m_pendingUploads.remove(localUuid);
        pullNow();
        return;
    }

    // Lines that are gone drop out of the remembered ids too, or the two arrays
    // stop being index-aligned and every later rename addresses the wrong task.
    // Driven off removedIndices rather than off the completions: a removed line
    // that completed nothing (it was already ticked, or was the compose slot —
    // see TickTickTaskList::diff) still has to go. Descending, so each removal
    // leaves the earlier indices valid.
    for (int i = intent.removedIndices.size() - 1; i >= 0; --i) {
        const int index = intent.removedIndices.at(i);
        if (index < m_lastRenderedTaskIds.size())
            m_lastRenderedTaskIds.removeAt(index);
    }

    m_client.setAccessToken(m_auth.accessToken());

    // Whether anything actually left this class — a request sent, or one
    // deliberately deferred until a create confirms. What clears the pending
    // flag is a REPLY (see the taskUpdated/taskCompleted/taskCreated handlers),
    // so an intent that dispatches nothing would otherwise leave this note
    // pending forever, and onTasksFetched's pending guard would hold every
    // later pull back with it.
    //
    // A ticked line and its later removal now normally reach here as ONE
    // edit, not two: kUploadDebounceMs is kept longer than
    // tickTickCompletionFade's three-second hold in NoteWindow.qml (see that
    // constant's own comment), so by the time this flush finally runs, the
    // line the user ticked has already faded away and vanished from the
    // document. TickTickTaskList::diff sees fewer lines than its baseline —
    // the baseline itself never having moved, since no flush ran in between —
    // and its removal branch dispatches exactly one completeTask for it (the
    // already-ticked-line guard at TickTickTaskList.cpp only matters for a
    // SECOND flush that observes a removal after the completion was already
    // recorded, e.g. after an undo/redo sequence — not the routine single-tick
    // path anymore). That still leaves dispatched-but-nothing-to-send as a
    // real, reachable case below: a completion this class declines to repeat,
    // or an intent whose only content was id bookkeeping (removedIndices) with
    // no completion, rename, or create attached.
    bool dispatched = false;

    for (const QString &taskId : intent.completedTaskIds) {
        // Written so completeTask always runs: a false answer means it already
        // reported why and nothing is in flight for it.
        const bool sent = completeTask(taskId);
        dispatched = dispatched || sent;
    }

    for (const auto &rename : intent.renamedTasks) {
        const QString &taskId = rename.first;
        const QString &newTitle = rename.second;
        if (m_pendingCreateClientRefs.contains(taskId)) {
            // Not a real task yet — remember the latest title so the create
            // (already in flight, or about to be retried above) picks it up,
            // or so a follow-up update goes out the moment the real id
            // arrives (see the taskCreated handler).
            m_pendingCreateTitles[taskId] = newTitle;
            dispatched = true;
            continue;
        }
        m_client.updateTask(taskId, m_projectId, newTitle, m_taskContentById.value(taskId));
        dispatched = true;
    }

    // No create loop here, deliberately: a create is an explicit gesture now
    // (TickTickTaskList::kComposeIndex), dispatched by commitComposedTask
    // alone, so nothing an ordinary edit does to this note can mint a task.

    // Nothing is in flight to clear this later, so it is cleared here — same
    // reasoning as the nothingToDo branch above, just reached after the intent
    // turned out to need no network at all. A completion this class refused
    // (no session, no project — it has already said so) lands here too, and
    // clearing is right for it as well: the next pull then rewrites the note
    // from the server rather than the note sitting frozen against a request
    // that was never sent.
    if (!dispatched)
        m_pendingUploads.remove(localUuid);

    // The remembered baseline moves to whatever is on screen right now,
    // regardless of which of the above fired — a second edit must diff
    // against what THIS flush just saw, not what the last pull saw, or it
    // would re-detect (and re-dispatch) the exact same rename/create/
    // completion all over again.
    m_lastRenderedLines = currentLines;
}

void TickTickSync::clearPendingUploadUnlessEditQueued()
{
    // See the declaration's comment: an active debounce means an edit newer
    // than the reply that got us here is queued, and the flush it fires needs
    // the pending flag still set or it returns without ever diffing the edit.
    if (!m_uploadDebounce->isActive())
        m_pendingUploads.remove(TickTickNotes::taskListUuid());
}

void TickTickSync::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void TickTickSync::stampSyncedNow()
{
    m_prefs.setLastSyncTime(QDateTime::currentDateTime());
    emit lastSyncTimeChanged();
}

void TickTickSync::fail(const QString &reason)
{
    m_lastError = reason;
    emit lastErrorChanged();
    setBusy(false);
}
