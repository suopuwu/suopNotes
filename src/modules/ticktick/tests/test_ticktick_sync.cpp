#include <catch2/catch_test_macros.hpp>

#include "TickTickAuth.h"
#include "TickTickClient.h"
#include "TickTickNotes.h"
#include "TickTickSync.h"
#include "TickTickTaskList.h"
#include "TickTickTokenStore.h"

#include "TickTickModule.h"

#include "FakeNetwork.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

// What "synced N ago" means, and when it moves.
//
// The note's footer (NoteWindow.qml) reads TickTickSync::lastSyncTime and
// nothing else, so these specs are the definition of that label: a sync is
// EITHER direction agreeing with TickTick — a pull answering, or a local edit
// being confirmed — not the pull alone. Before this, typing in the task note
// really did upload, but the footer went on saying "synced 40m ago", which
// read as "your edit never left".
//
// These drive TickTickSync's client directly (findChild — the client is a
// child object of the sync) so a confirmation can be delivered without a
// network, an account, or a real project. Outbound traffic goes to a
// FakeNetworkAccessManager (see FakeNetwork.h), so no spec ever touches the
// live API, and a spec can assert exactly which request was dispatched.

namespace {

// Everything TickTickSync needs to exist, pointed entirely at temporary files.
// Deliberately left UNAUTHORIZED: an authorized sync pulls on construction,
// and these specs are about what happens when a reply arrives, not about
// making requests.
struct SyncFixture {
    QTemporaryDir tempDir;
    TickTickTokenStore tokenStore;
    TickTickAuth auth;
    TickTickNotes notes;
    TickTickSync sync;
    FakeNetworkAccessManager *network = nullptr;

    SyncFixture()
        : tokenStore(tempDir.filePath("ticktick.ini"),
                     TickTickTokenStore::SecretBackend::ConfigFileOnly)
        , auth(tokenStore)
        , notes(tempDir.filePath("cache"))
        , sync(auth, notes, tempDir.filePath("ticktick.ini"))
    {
        network = new FakeNetworkAccessManager;
        client()->setNetworkAccessManager(network);
    }

    // The TickTickClient the sync owns — the seam a real reply comes in
    // through.
    TickTickClient *client() { return sync.findChild<TickTickClient *>(); }
};

// A sync that believes it is signed in and has already resolved a list, built
// by seeding its config file before it is constructed — which is exactly where
// TickTickSync reads both from at startup. Needed for the commit path, whose
// whole job is to refuse when either is missing.
//
// The token goes in through the ConfigFileOnly backend's plaintext key, the
// same one a machine with no secret store uses.
struct AuthorizedSyncFixture {
    QTemporaryDir tempDir;
    QString configPath;
    std::unique_ptr<TickTickTokenStore> tokenStore;
    std::unique_ptr<TickTickAuth> auth;
    std::unique_ptr<TickTickNotes> notes;
    std::unique_ptr<TickTickSync> sync;
    FakeNetworkAccessManager *network = nullptr;

    // withChosenProject=false builds the first-run shape: signed in, but no
    // list ever chosen or resolved — what the default-list-creation and
    // commit-refusal specs need.
    explicit AuthorizedSyncFixture(bool withChosenProject = true)
        : configPath(tempDir.filePath("ticktick.ini"))
    {
        {
            QSettings seed(configPath, QSettings::IniFormat);
            seed.setValue("insecure/accessToken", "test-token");
            if (withChosenProject) {
                seed.setValue("sync/projectId", "project-1");
                seed.setValue("sync/projectName", "suopNotes");
            }
            seed.sync();
        }
        tokenStore = std::make_unique<TickTickTokenStore>(
            configPath, TickTickTokenStore::SecretBackend::ConfigFileOnly);
        auth = std::make_unique<TickTickAuth>(*tokenStore);
        notes = std::make_unique<TickTickNotes>(tempDir.filePath("cache"));
        sync = std::make_unique<TickTickSync>(*auth, *notes, configPath);

        // Installed BEFORE the event loop spins: the token finishing its load
        // below fires authorizedChanged, whose handler starts a pull — which
        // must land in the fake, not on the real API.
        network = new FakeNetworkAccessManager;
        client()->setNetworkAccessManager(network);

        // The token store reports its result through the event loop even on the
        // plaintext path (a deliberate QTimer::singleShot, so both backends look
        // the same to callers — see test_main.cpp). Without this spin the fixture
        // would still be signed out by the time a spec ran.
        QCoreApplication::processEvents();
    }

    QString noteId() const { return TickTickModule::makeNoteId(TickTickNotes::taskListUuid()); }
    QString noteText() const { return notes->loadNoteContent(noteId()); }
    void typeIntoNote(const QString &text) { notes->saveNoteContent(noteId(), text); }

    // The TickTickClient the sync owns — same reflection SyncFixture above
    // uses, the seam a real reply comes in through.
    TickTickClient *client() { return sync->findChild<TickTickClient *>(); }
};

} // namespace

SCENARIO("The last-sync stamp starts empty and moves when TickTick confirms an edit",
         "[ticktick][sync]")
{
    GIVEN("a sync that has never talked to TickTick")
    {
        SyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());
        REQUIRE(fixture.client() != nullptr);

        THEN("there is no last-sync time, so the footer can say 'not synced yet'")
        {
            CHECK_FALSE(fixture.sync.lastSyncTime().isValid());
        }

        WHEN("TickTick confirms a task rename")
        {
            QSignalSpy stampSpy(&fixture.sync, &TickTickSync::lastSyncTimeChanged);
            emit fixture.client()->taskUpdated("task-1");

            THEN("the stamp moves, because the user's text is now on the server")
            {
                CHECK(fixture.sync.lastSyncTime().isValid());
            }

            THEN("the change is announced, so the footer re-renders")
            {
                // The footer's text is a binding on lastSyncTime; without this
                // signal it would keep showing the old value until something
                // unrelated happened to repaint it.
                CHECK(stampSpy.count() == 1);
            }
        }

        WHEN("TickTick confirms a completion")
        {
            QSignalSpy stampSpy(&fixture.sync, &TickTickSync::lastSyncTimeChanged);
            emit fixture.client()->taskCompleted("task-1");

            THEN("the stamp moves — ticking a box is an edit like any other")
            {
                CHECK(fixture.sync.lastSyncTime().isValid());
                CHECK(stampSpy.count() == 1);
            }
        }

        WHEN("TickTick confirms a newly created task")
        {
            QSignalSpy stampSpy(&fixture.sync, &TickTickSync::lastSyncTimeChanged);

            TickTickClient::Task created;
            created.id = "task-1";
            created.title = "Buy milk";
            emit fixture.client()->taskCreated("client-ref-1", created);

            THEN("the stamp moves — the line the user typed is now a real task")
            {
                CHECK(fixture.sync.lastSyncTime().isValid());
                CHECK(stampSpy.count() == 1);
            }
        }
    }
}

SCENARIO("A failed upload leaves the last-sync stamp alone", "[ticktick][sync]")
{
    GIVEN("a sync that has never talked to TickTick")
    {
        SyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());
        REQUIRE(fixture.client() != nullptr);

        WHEN("a request fails")
        {
            emit fixture.client()->requestFailed("network is down");

            THEN("nothing is stamped, so the footer does not claim a sync that never happened")
            {
                // The whole point of the label is to say how stale the note
                // is. A failure that moved it would hide exactly the situation
                // the user needs to notice.
                CHECK_FALSE(fixture.sync.lastSyncTime().isValid());
            }

            THEN("the reason is reported instead")
            {
                CHECK(fixture.sync.lastError() == "network is down");
            }
        }

        WHEN("an upload is rejected as permanently unacceptable")
        {
            emit fixture.client()->taskRejectedPermanently("task-1", "content too large");

            THEN("nothing is stamped")
            {
                CHECK_FALSE(fixture.sync.lastSyncTime().isValid());
            }
        }
    }
}

SCENARIO("The last-sync stamp survives a restart", "[ticktick][sync]")
{
    GIVEN("a sync whose edit TickTick has confirmed")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        const QString configPath = tempDir.filePath("ticktick.ini");

        QDateTime stamped;
        {
            TickTickTokenStore tokenStore(configPath,
                                          TickTickTokenStore::SecretBackend::ConfigFileOnly);
            TickTickAuth auth(tokenStore);
            TickTickNotes notes(tempDir.filePath("cache"));
            TickTickSync sync(auth, notes, configPath);

            TickTickClient *client = sync.findChild<TickTickClient *>();
            REQUIRE(client != nullptr);
            emit client->taskUpdated("task-1");

            stamped = sync.lastSyncTime();
            REQUIRE(stamped.isValid());
        }

        WHEN("the app is started again against the same config")
        {
            TickTickTokenStore tokenStore(configPath,
                                          TickTickTokenStore::SecretBackend::ConfigFileOnly);
            TickTickAuth auth(tokenStore);
            TickTickNotes notes(tempDir.filePath("cache"));
            TickTickSync sync(auth, notes, configPath);

            THEN("the footer still knows when the note was last in step with TickTick")
            {
                // Persisted rather than in-memory: otherwise every launch reads
                // "not synced yet" until the first pull answers, which makes a
                // perfectly fresh note look untrustworthy.
                CHECK(sync.lastSyncTime().isValid());
                CHECK(qAbs(sync.lastSyncTime().secsTo(stamped)) <= 1);
            }
        }
    }
}

// The compose slot: how a task actually gets created now.
//
// A task used to be minted by any edit that added a line, which meant the
// ribbon's "＋ task" button put a blank task on the user's phone for every
// stray click, and any stray extra line did the same. Creating is now one
// explicit gesture — commitComposedTask — and these specs are what "explicit"
// buys: nothing reaches TickTick until the user says so, and nothing they typed
// is thrown away when it can't.

SCENARIO("The note is created with an empty compose slot, and nothing else",
         "[ticktick][sync]")
{
    GIVEN("a signed-in sync with no cached note yet")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());
        REQUIRE(fixture.auth->isAuthorized());
        REQUIRE_FALSE(fixture.notes->hasCachedNote(TickTickNotes::taskListUuid()));

        WHEN("the ribbon's new-task button asks for a compose slot")
        {
            QSignalSpy idsSpy(fixture.notes.get(), &TickTickNotes::noteIdsChanged);
            const QString noteId = fixture.sync->ensureComposeSlot();

            THEN("the note is created holding a single blank checkbox line")
            {
                CHECK(noteId == fixture.noteId());
                CHECK(fixture.noteText() == "- [ ] ");
            }

            THEN("the new note id is announced, which is what opens its window")
            {
                CHECK(idsSpy.count() == 1);
            }
        }

        WHEN("it is asked a second time, with text already half-typed into the slot")
        {
            fixture.sync->ensureComposeSlot();
            fixture.typeIntoNote("- [ ] Call den");

            const QString noteId = fixture.sync->ensureComposeSlot();

            THEN("the in-progress text is left exactly as it was")
            {
                // The button means "take me to the slot", not "clear it" — a
                // second click while typing must not wipe the line.
                CHECK(noteId == fixture.noteId());
                CHECK(fixture.noteText() == "- [ ] Call den");
            }
        }
    }
}

SCENARIO("Committing the compose slot creates the task and leaves a fresh slot",
         "[ticktick][sync]")
{
    GIVEN("a signed-in sync whose slot has been typed into")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());
        fixture.sync->ensureComposeSlot();
        fixture.typeIntoNote("- [ ] Call dentist");

        WHEN("the commit gesture fires (Enter, or the slot's check button)")
        {
            const bool committed = fixture.sync->commitComposedTask();

            THEN("it reports success")
            {
                CHECK(committed);
            }

            THEN("the typed text becomes a task line under a fresh empty slot")
            {
                // The slot is replenished in the same edit, so a second task can
                // be typed immediately without pressing anything else.
                CHECK(fixture.noteText() == "- [ ] \n- [ ] Call dentist");
            }
        }
    }
}

SCENARIO("An empty compose slot commits nothing", "[ticktick][sync]")
{
    GIVEN("a signed-in sync whose slot is blank")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());
        fixture.sync->ensureComposeSlot();

        WHEN("Enter is pressed on it anyway")
        {
            const bool committed = fixture.sync->commitComposedTask();

            THEN("nothing happens, and no error is reported")
            {
                // Enter on an empty slot is inert, not a failure worth showing
                // the user a banner for.
                CHECK_FALSE(committed);
                CHECK(fixture.sync->lastError().isEmpty());
                CHECK(fixture.noteText() == "- [ ] ");
            }
        }
    }
}

SCENARIO("A commit that cannot reach TickTick leaves the typed text where it is",
         "[ticktick][sync]")
{
    // The text in the slot exists nowhere else — there is no server copy to get
    // it back from — so a refused commit must never be the thing that consumes
    // it.
    GIVEN("a sync that is not signed in, with text in the slot")
    {
        SyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());
        REQUIRE_FALSE(fixture.auth.isAuthorized());

        const QString noteId = TickTickModule::makeNoteId(TickTickNotes::taskListUuid());
        fixture.notes.writeCachedNote(TickTickNotes::taskListUuid(), "- [ ] Call dentist");

        WHEN("the commit gesture fires")
        {
            const bool committed = fixture.sync.commitComposedTask();

            THEN("it refuses, says why, and the note is untouched")
            {
                CHECK_FALSE(committed);
                CHECK_FALSE(fixture.sync.lastError().isEmpty());
                CHECK(fixture.notes.loadNoteContent(noteId) == "- [ ] Call dentist");
            }
        }
    }
}

SCENARIO("Switching TickTick lists empties the note rather than removing it",
         "[ticktick][sync]")
{
    // Removing it dropped the note out of NoteStore::noteIds(), which made
    // Main.qml's reconcile close its window — and closing a note discards its
    // saved position, size and per-note appearance overrides. The pull moments
    // later then reopened it as a brand-new note at the default cascade position
    // on the primary monitor. Changing a setting is not a reason to move where
    // the user keeps their task note.
    GIVEN("a signed-in sync with a note holding the old list's tasks")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());
        fixture.sync->ensureComposeSlot();
        fixture.typeIntoNote("- [ ] \n- [ ] Walk dog\n- [ ] Buy milk");

        WHEN("the user picks a different list in Settings")
        {
            QSignalSpy idsSpy(fixture.notes.get(), &TickTickNotes::noteIdsChanged);
            fixture.sync->selectProject("project-2", "Other list");

            THEN("the note still exists, now holding just an empty compose slot")
            {
                CHECK(fixture.notes->hasCachedNote(TickTickNotes::taskListUuid()));
                CHECK(fixture.noteText() == "- [ ] ");
            }

            THEN("the set of notes never changed, so no window was closed")
            {
                // This is the whole point: a closed window is a lost position.
                CHECK(idsSpy.count() == 0);
            }
        }
    }
}

// The completion fade in NoteWindow.qml holds a ticked line on screen for
// three seconds before removing it, advertised as an undo window. For that to
// be real rather than cosmetic, kUploadDebounceMs has to outlast the fade, so
// an edit made anywhere inside the hold — the tick, and an undo of it — is
// still sitting unflushed when the fade finally acts. See kUploadDebounceMs's
// own comment in TickTickSync.cpp, and the matching comment on
// tickTickCompletionFade in NoteWindow.qml.
//
// Driven entirely off TickTickNotes::saveNoteContent (fixture.typeIntoNote),
// the same call the note's own local-save debounce ends in.
//
// Asserted two ways, which corroborate each other: pendingUploadCount() reads
// TickTickSync's OWN bookkeeping — flushPendingUploads only leaves it non-zero
// when it genuinely dispatched something and is now waiting on a reply (see
// that function's comment on "nothing to send" versus "sent, waiting to hear
// back") — and the fixture's FakeNetworkAccessManager records exactly which
// requests actually left the client, so "its completion was dispatched" is
// checked against the /complete request itself, not inferred.
SCENARIO("A tick undone before the fade removes the line never reaches TickTick",
         "[ticktick][sync]")
{
    GIVEN("a signed-in sync with one already-open task rendered from a pull")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());

        TickTickClient::Task task;
        task.id = "task-1";
        task.projectId = "project-1";
        task.title = "Buy milk";
        emit fixture.client()->tasksFetched("project-1", {task});
        REQUIRE(fixture.noteText() == "- [ ] \n- [ ] Buy milk");
        REQUIRE(fixture.sync->pendingUploadCount() == 0);

        WHEN("the task is ticked, then unticked again before the upload debounce elapses")
        {
            fixture.typeIntoNote("- [ ] \n- [x] Buy milk");
            QTest::qWait(TickTickSync::uploadDebounceMs() / 2);
            fixture.typeIntoNote("- [ ] \n- [ ] Buy milk");

            // Long enough to be sure the (restarted) debounce has fired.
            QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

            THEN("no completion was dispatched, so nothing is left pending on a reply "
                 "that will never come")
            {
                // A real dispatch only clears once a reply arrives (see
                // flushPendingUploads); a flush that decided there was nothing
                // to send has to clear it itself, or every later pull would be
                // held back by an edit that was correctly never sent.
                CHECK(fixture.sync->pendingUploadCount() == 0);
                CHECK_FALSE(fixture.network->sawRequestTo("/complete"));
            }

            THEN("the task is still open in the note's own text — the undo held")
            {
                CHECK(fixture.noteText() == "- [ ] \n- [ ] Buy milk");
            }
        }

        WHEN("the task is ticked and left alone past the debounce, with no undo")
        {
            fixture.typeIntoNote("- [ ] \n- [x] Buy milk");
            QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

            THEN("its completion IS dispatched and left waiting on a reply — unlike the "
                 "undone case above, so that case is known to be testing something real")
            {
                CHECK(fixture.sync->pendingUploadCount() == 1);
                CHECK(fixture.network->sawRequestTo("/project/project-1/task/task-1/complete"));
            }
        }
    }
}

SCENARIO("The upload debounce is pinned to outlast the completion fade's hold",
         "[ticktick][sync]")
{
    GIVEN("kUploadDebounceMs")
    {
        THEN("it exceeds the three seconds tickTickCompletionFade holds a ticked "
             "line for in NoteWindow.qml (PauseAnimation 2680ms + NumberAnimation "
             "320ms) — the invariant the scenario above, and the two coupled "
             "comment blocks in TickTickSync.cpp/NoteWindow.qml, all depend on")
        {
            constexpr int kFadeTotalMs = 2680 + 320;
            CHECK(TickTickSync::uploadDebounceMs() > kFadeTotalMs);
        }
    }
}

// The bug this item fixes: flushPendingUploads' nothingToDo branch used to
// leave m_lastRenderedLines untouched no matter WHY the Intent came back
// empty, so a baseline that had drifted from the note (an undo restoring a
// line, or the compose slot being typed into — see TickTickTaskList::diff's
// own doc comment on Intent::understood) stayed drifted forever. From that
// point every later tick or rename diffed against stale text, found more than
// one line different, and was silently dropped — the line still faded away
// locally (the fade is driven purely off the document, not off whether
// anything was ever sent), but TickTick never heard about it.
//
// The fix splits "genuinely unchanged" (baseline already correct) from
// "understood but nothing to send" (baseline needs to move to catch up) from
// "too ambiguous to interpret" (baseline is wrong and self-repair requires an
// immediate pull) — see flushPendingUploads' nothingToDo branch.

SCENARIO("A tick after an undo restored a line still completes the task", "[ticktick][sync]")
{
    // The literal repro: tick a task (dispatches its completion, same as the
    // routine single-tick path), have the line's removal reach the note (what
    // the completion fade does), undo that removal (the line comes back), then
    // tick a DIFFERENT task. Before this item, the undo left the baseline one
    // line short forever and the second tick vanished from the note without
    // ever reaching TickTick.
    GIVEN("a signed-in sync with two already-open tasks rendered from a pull")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());

        TickTickClient::Task buyMilk;
        buyMilk.id = "t-milk";
        buyMilk.projectId = "project-1";
        buyMilk.title = "Buy milk";
        TickTickClient::Task walkDog;
        walkDog.id = "t-dog";
        walkDog.projectId = "project-1";
        walkDog.title = "Walk dog";
        emit fixture.client()->tasksFetched("project-1", {buyMilk, walkDog});
        REQUIRE(fixture.noteText() == "- [ ] \n- [ ] Walk dog\n- [ ] Buy milk");
        REQUIRE_FALSE(fixture.sync->isBusy());

        WHEN("Walk dog's line is removed (as the completion fade does after a tick), and that "
             "settles")
        {
            fixture.typeIntoNote("- [ ] \n- [ ] Buy milk");
            QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

            THEN("its completion was dispatched, same as any routine single tick")
            {
                CHECK(fixture.sync->pendingUploadCount() == 1);
            }

            AND_WHEN("the removal is undone — the line comes back — and that settles too")
            {
                // Watched via busyChanged rather than a final isBusy() read, so
                // the assertion doesn't depend on whether the pull's request has
                // been answered by the time qWait returns — a spy saw it toggle
                // either way.
                QSignalSpy busySpy(fixture.sync.get(), &TickTickSync::busyChanged);
                fixture.typeIntoNote("- [ ] \n- [ ] Walk dog\n- [ ] Buy milk");
                QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

                THEN("nothing is left pending on the never-arriving reply for an edit that was "
                     "never sent, and a reconciling pull was requested instead of the baseline "
                     "quietly staying wrong")
                {
                    CHECK(fixture.sync->pendingUploadCount() == 0);
                    CHECK(busySpy.count() >= 1);
                }

                AND_WHEN("the reconciling pull answers, and a different task is then ticked")
                {
                    // The reconcile itself: the server's own answer re-establishes
                    // both the note text and the baseline, the same as any other
                    // pull.
                    emit fixture.client()->tasksFetched("project-1", {buyMilk, walkDog});
                    REQUIRE_FALSE(fixture.sync->isBusy());

                    fixture.typeIntoNote("- [ ] \n- [ ] Walk dog\n- [x] Buy milk");
                    QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

                    THEN("its completion IS dispatched — this is the failing case before this "
                         "item's fix, where the stale baseline made diff() see two changed "
                         "lines at once and refuse to guess")
                    {
                        CHECK(fixture.sync->pendingUploadCount() == 1);
                    }
                }
            }
        }
    }
}

SCENARIO("A tick after typing in the compose slot still completes the task", "[ticktick][sync]")
{
    // The second trigger for the same bug, found while scouting item 001, and
    // far more common than undo: every keystroke burst in the compose slot
    // reaches flushPendingUploads as an Intent that is empty but fully
    // understood (see TickTickTaskList::diff's compose-slot early return).
    // Before this item, the branch could not tell that apart from "too
    // ambiguous to interpret", so the baseline never moved and the very next
    // tick diffed against two changed lines and was dropped.
    GIVEN("a signed-in sync with one already-open task rendered from a pull")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());

        TickTickClient::Task task;
        task.id = "t-dog";
        task.projectId = "project-1";
        task.title = "Walk dog";
        emit fixture.client()->tasksFetched("project-1", {task});
        REQUIRE(fixture.noteText() == "- [ ] \n- [ ] Walk dog");
        REQUIRE(fixture.sync->pendingUploadCount() == 0);

        WHEN("a task is typed into the compose slot, without being committed, and that settles")
        {
            fixture.typeIntoNote("- [ ] Call dentist\n- [ ] Walk dog");
            QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

            THEN("nothing was uploaded — there is no task behind the slot to address an edit to")
            {
                CHECK(fixture.sync->pendingUploadCount() == 0);
            }

            AND_WHEN("a different task is then ticked, and that settles too")
            {
                fixture.typeIntoNote("- [ ] Call dentist\n- [x] Walk dog");
                QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

                THEN("its completion was dispatched — the compose-slot edit moved the baseline "
                     "forward, so the tick diffs against real text instead of the slot's "
                     "now-stale previous contents")
                {
                    CHECK(fixture.sync->pendingUploadCount() == 1);
                }
            }
        }
    }
}

SCENARIO("An edit diff() refuses to interpret triggers a reconciling pull", "[ticktick][sync]")
{
    GIVEN("a signed-in sync with one already-open task rendered from a pull")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());

        TickTickClient::Task task;
        task.id = "t-milk";
        task.projectId = "project-1";
        task.title = "Buy milk";
        emit fixture.client()->tasksFetched("project-1", {task});
        REQUIRE(fixture.noteText() == "- [ ] \n- [ ] Buy milk");
        REQUIRE_FALSE(fixture.sync->isBusy());

        WHEN("an extra line appears that diff() cannot read as anything — a paste, or an undo "
             "of a removal — and that settles")
        {
            // Watched via busyChanged rather than a final isBusy() read — see
            // the matching comment in the undo-restore scenario above.
            QSignalSpy busySpy(fixture.sync.get(), &TickTickSync::busyChanged);
            fixture.typeIntoNote("- [ ] \n- [ ] Buy milk\n- [ ] Walk dog");
            QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

            THEN("a pull was requested, so the wrong baseline gets fixed instead of staying "
                 "wrong until the next scheduled pull happens to land")
            {
                CHECK(busySpy.count() >= 1);
            }

            THEN("nothing was left pending on a reply for an edit that was never dispatched")
            {
                CHECK(fixture.sync->pendingUploadCount() == 0);
            }
        }
    }
}

SCENARIO("A flush with genuinely unchanged text does not pull", "[ticktick][sync]")
{
    // Pins that the fix above does not turn every quiet flush — an idle
    // debounce firing with nothing new typed — into a network request.
    GIVEN("a signed-in sync with one already-open task rendered from a pull")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());

        TickTickClient::Task task;
        task.id = "t-milk";
        task.projectId = "project-1";
        task.title = "Buy milk";
        emit fixture.client()->tasksFetched("project-1", {task});
        REQUIRE(fixture.noteText() == "- [ ] \n- [ ] Buy milk");
        REQUIRE_FALSE(fixture.sync->isBusy());

        WHEN("the note is saved again with exactly the text already on screen")
        {
            fixture.typeIntoNote("- [ ] \n- [ ] Buy milk");
            QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

            THEN("no pull was requested and nothing was uploaded")
            {
                CHECK_FALSE(fixture.sync->isBusy());
                CHECK(fixture.sync->pendingUploadCount() == 0);
            }
        }
    }
}

// The three regressions below all share a shape: an edit the user made was
// still sitting on the upload debounce (or a pull was still in flight) when
// something else finished first, and the finishing thing used to throw the
// waiting one away. In every case the on-screen text looked right at the time
// and the loss only became visible when the next pull reverted it.

SCENARIO("A reply for an older edit does not swallow one still on the debounce",
         "[ticktick][sync]")
{
    GIVEN("a signed-in sync with one task, renamed once with the rename dispatched")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());

        TickTickClient::Task task;
        task.id = "t-dog";
        task.projectId = "project-1";
        task.title = "Walk dog";
        emit fixture.client()->tasksFetched("project-1", {task});
        REQUIRE(fixture.noteText() == "- [ ] \n- [ ] Walk dog");

        fixture.typeIntoNote("- [ ] \n- [ ] Feed dog");
        QTest::qWait(TickTickSync::uploadDebounceMs() + 500);
        REQUIRE(fixture.sync->pendingUploadCount() == 1);
        REQUIRE(fixture.network->countRequestsTo("/task/t-dog") == 1);

        WHEN("a second rename is typed, and the FIRST one's confirmation arrives while "
             "the second still sits on the debounce")
        {
            fixture.typeIntoNote("- [ ] \n- [ ] Feed dogs");
            emit fixture.client()->taskUpdated("t-dog");

            THEN("the note stays pending — a reply confirms what was dispatched, not "
                 "what was typed since")
            {
                // Clearing here was the bug: the debounce's flush then found
                // nothing pending, returned early, and the second rename was
                // never sent — until the next pull reverted it on screen.
                CHECK(fixture.sync->pendingUploadCount() == 1);
            }

            AND_WHEN("the debounce elapses")
            {
                QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

                THEN("the second rename is dispatched as its own update")
                {
                    CHECK(fixture.network->countRequestsTo("/task/t-dog") == 2);
                    CHECK(fixture.network->sawBodyContaining("Feed dogs"));
                }

                AND_WHEN("its confirmation arrives with nothing further queued")
                {
                    emit fixture.client()->taskUpdated("t-dog");

                    THEN("the note finally settles to not-pending")
                    {
                        CHECK(fixture.sync->pendingUploadCount() == 0);
                    }
                }
            }
        }
    }
}

SCENARIO("Committing a new task does not swallow a rename still on the debounce",
         "[ticktick][sync]")
{
    GIVEN("a signed-in sync with one task renamed, and a new task typed into the "
          "slot, all inside one debounce window")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());

        TickTickClient::Task task;
        task.id = "t-dog";
        task.projectId = "project-1";
        task.title = "Walk dog";
        emit fixture.client()->tasksFetched("project-1", {task});
        REQUIRE(fixture.noteText() == "- [ ] \n- [ ] Walk dog");

        fixture.typeIntoNote("- [ ] \n- [ ] Feed dog");
        fixture.typeIntoNote("- [ ] Call dentist\n- [ ] Feed dog");

        WHEN("the compose slot is committed before the debounce has fired")
        {
            const bool committed = fixture.sync->commitComposedTask();

            THEN("the commit itself works as always")
            {
                CHECK(committed);
                CHECK(fixture.noteText() == "- [ ] \n- [ ] Call dentist\n- [ ] Feed dog");
                CHECK(fixture.network->sawBodyContaining("Call dentist"));
            }

            THEN("the rename was dispatched first rather than absorbed into the "
                 "commit's rewrite of the baseline")
            {
                // The commit moves the remembered baseline to the committed
                // text — which already contains the renamed line. Absorbed
                // there without being dispatched, the rename could never be
                // diffed again: it looked saved on screen, and the next pull
                // reverted it. This is also why diff() must not count the
                // compose slot's own text as a changed line — at commit time
                // the slot is ALWAYS mid-edit alongside the rename.
                CHECK(fixture.network->countRequestsTo("/task/t-dog") == 1);
                CHECK(fixture.network->sawBodyContaining("Feed dog"));
            }
        }
    }
}

SCENARIO("Switching lists while a pull is in flight never shows the old list's tasks",
         "[ticktick][sync]")
{
    GIVEN("a signed-in sync whose pull has already asked the OLD list for its tasks")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());

        TickTickClient::Task oldTask;
        oldTask.id = "t-old";
        oldTask.projectId = "project-1";
        oldTask.title = "Old list task";
        emit fixture.client()->tasksFetched("project-1", {oldTask});
        REQUIRE(fixture.noteText() == "- [ ] \n- [ ] Old list task");

        // A fresh pull, walked to the point where the tasks request for the
        // old list is in flight.
        fixture.sync->pullNow();
        REQUIRE(fixture.sync->isBusy());
        TickTickClient::Project oldProject;
        oldProject.id = "project-1";
        oldProject.name = "suopNotes";
        TickTickClient::Project newProject;
        newProject.id = "project-2";
        newProject.name = "Other list";
        emit fixture.client()->projectsFetched({oldProject, newProject});
        REQUIRE(fixture.network->sawRequestTo("/task/filter"));

        WHEN("the user switches to another list, and the old list's response only "
             "lands afterward")
        {
            fixture.sync->selectProject("project-2", "Other list");
            REQUIRE(fixture.noteText() == "- [ ] ");

            const int pullsBefore = fixture.network->countRequestsTo("/project");
            emit fixture.client()->tasksFetched("project-1", {oldTask});

            THEN("the stale response is dropped — the emptied note does not fill up "
                 "with the list the user just left")
            {
                CHECK(fixture.noteText() == "- [ ] ");
            }

            THEN("a fresh pull starts in its place, since selectProject's own pull "
                 "was blocked by the in-flight one")
            {
                CHECK(fixture.sync->isBusy());
                CHECK(fixture.network->countRequestsTo("/project") == pullsBefore + 1);
            }

            AND_WHEN("that fresh pull answers for the NEW list")
            {
                emit fixture.client()->projectsFetched({oldProject, newProject});

                TickTickClient::Task newTask;
                newTask.id = "t-new";
                newTask.projectId = "project-2";
                newTask.title = "New list task";
                emit fixture.client()->tasksFetched("project-2", {newTask});

                THEN("the note renders the new list")
                {
                    CHECK(fixture.noteText() == "- [ ] \n- [ ] New list task");
                    CHECK_FALSE(fixture.sync->isBusy());
                }
            }
        }
    }
}

// How a pull decides which list it is even talking about — the ~60 lines of
// onProjectsFetched. The rules, in order of trust: the stored id first
// (survives a rename), the stored name second (survives a delete-and-
// recreate, which mints a new id for what the user still thinks of as the
// same list), then an explicit failure for a chosen list that is simply gone
// — never a quiet recreation, which would present as "all my notes
// disappeared". Only a first run with no choice at all creates anything.

SCENARIO("A pull resolves the chosen list by id, surviving a rename",
         "[ticktick][sync]")
{
    GIVEN("a sync whose chosen list has been renamed in TickTick")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());

        TickTickClient::Project renamed;
        renamed.id = "project-1";
        renamed.name = "Renamed list";
        TickTickClient::Project impostor;
        impostor.id = "project-9";
        impostor.name = "suopNotes";

        WHEN("the pull's project list answers")
        {
            QSignalSpy selectedSpy(fixture.sync.get(), &TickTickSync::selectedProjectChanged);
            emit fixture.client()->projectsFetched({renamed, impostor});

            THEN("tasks are fetched from the SAME list, found by id — not from the "
                 "unrelated list that now wears the old name")
            {
                CHECK(fixture.network->sawRequestTo("/task/filter"));
                CHECK(fixture.network->sawBodyContaining("project-1"));
                CHECK_FALSE(fixture.network->sawBodyContaining("project-9"));
            }

            THEN("the stored name follows the rename, so Settings shows the truth")
            {
                CHECK(fixture.sync->selectedProjectName() == "Renamed list");
                CHECK(selectedSpy.count() == 1);
            }
        }
    }
}

SCENARIO("A deleted-and-recreated list is recovered by name", "[ticktick][sync]")
{
    GIVEN("a sync whose chosen list was deleted and recreated, minting a new id")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());

        TickTickClient::Project recreated;
        recreated.id = "project-2";
        recreated.name = "suopNotes";

        WHEN("the pull's project list answers with only the new incarnation")
        {
            emit fixture.client()->projectsFetched({recreated});

            THEN("the name match adopts the new id and pulls from it")
            {
                // Without this, the stored id would match nothing forever and
                // the integration would silently sync an empty nothing.
                CHECK(fixture.sync->selectedProjectId() == "project-2");
                CHECK(fixture.network->sawBodyContaining("project-2"));
            }
        }
    }
}

SCENARIO("A chosen list that no longer exists is reported, never recreated",
         "[ticktick][sync]")
{
    GIVEN("a sync whose chosen list is gone entirely")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());

        TickTickClient::Project unrelated;
        unrelated.id = "project-3";
        unrelated.name = "Groceries";

        WHEN("the pull's project list answers without it")
        {
            emit fixture.client()->projectsFetched({unrelated});

            THEN("the pull fails with the list's name in the message")
            {
                CHECK(fixture.sync->lastError().contains("suopNotes"));
                CHECK_FALSE(fixture.sync->isBusy());
            }

            THEN("no list was created — a quiet recreation would look like every "
                 "note vanished, with no hint the list itself was the problem")
            {
                CHECK_FALSE(fixture.network->sawPostTo("/project"));
            }

            THEN("no tasks were fetched from anywhere")
            {
                CHECK_FALSE(fixture.network->sawRequestTo("/task/filter"));
            }
        }
    }
}

SCENARIO("A first run with no chosen list creates the default list", "[ticktick][sync]")
{
    GIVEN("a signed-in sync that has never had a list chosen")
    {
        AuthorizedSyncFixture fixture(false);
        REQUIRE(fixture.tempDir.isValid());

        WHEN("the pull finds the default list does not exist yet")
        {
            emit fixture.client()->projectsFetched({});

            THEN("it is created, by name")
            {
                CHECK(fixture.network->sawPostTo("/project"));
                CHECK(fixture.network->sawBodyContaining("suopNotes"));
            }

            AND_WHEN("TickTick answers with the created list")
            {
                TickTickClient::Project created;
                created.id = "project-7";
                created.name = "suopNotes";
                emit fixture.client()->projectCreated(created);

                THEN("it becomes the chosen list and its tasks are fetched")
                {
                    CHECK(fixture.sync->selectedProjectId() == "project-7");
                    CHECK(fixture.network->sawBodyContaining("project-7"));
                }
            }
        }
    }
}

SCENARIO("A dropdown refresh is not a pull", "[ticktick][sync]")
{
    GIVEN("a signed-in sync mid-pull")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());
        REQUIRE(fixture.sync->isBusy());

        WHEN("the settings dropdown asks for the project list while the pull runs")
        {
            const int requestsBefore = fixture.network->countRequestsTo("/project");
            fixture.sync->refreshProjects();

            THEN("it is declined — its response would be mistaken for the pull's own, "
                 "leaving the pull waiting forever")
            {
                CHECK(fixture.network->countRequestsTo("/project") == requestsBefore);
            }
        }

        WHEN("the pull has finished and the dropdown asks again")
        {
            TickTickClient::Project project;
            project.id = "project-1";
            project.name = "suopNotes";
            emit fixture.client()->projectsFetched({project});
            emit fixture.client()->tasksFetched("project-1", {});
            REQUIRE_FALSE(fixture.sync->isBusy());

            const int taskFetchesBefore = fixture.network->countRequestsTo("/task/filter");
            fixture.sync->refreshProjects();

            THEN("the request goes out without claiming to be a sync")
            {
                // Busy drives the "Syncing…" label; a background dropdown
                // refresh showing it would suggest notes were being fetched
                // when none are.
                CHECK_FALSE(fixture.sync->isBusy());
            }

            AND_WHEN("the list answers")
            {
                TickTickClient::Project other;
                other.id = "project-2";
                other.name = "Other list";
                emit fixture.client()->projectsFetched({project, other});

                THEN("the dropdown gets its entries, and no tasks are fetched off the "
                     "back of it")
                {
                    CHECK(fixture.sync->availableProjects().size() == 2);
                    CHECK(fixture.network->countRequestsTo("/task/filter") == taskFetchesBefore);
                    CHECK_FALSE(fixture.sync->isBusy());
                }
            }
        }
    }
}

// The pending-upload guard in onTasksFetched: the single most load-bearing
// rule of the one-note design. One unconfirmed edit anywhere in the task list
// holds back the WHOLE render — because there is only one note, a pull
// carrying a version older than what the user just typed would revert their
// text on screen mid-session.

SCENARIO("A pull cannot overwrite the note while an edit awaits confirmation",
         "[ticktick][sync]")
{
    GIVEN("a signed-in sync with one task, freshly ticked, its completion dispatched")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());

        TickTickClient::Task task;
        task.id = "t-milk";
        task.projectId = "project-1";
        task.title = "Buy milk";
        emit fixture.client()->tasksFetched("project-1", {task});
        REQUIRE(fixture.noteText() == "- [ ] \n- [ ] Buy milk");

        fixture.typeIntoNote("- [ ] \n- [x] Buy milk");
        QTest::qWait(TickTickSync::uploadDebounceMs() + 500);
        REQUIRE(fixture.sync->pendingUploadCount() == 1);

        WHEN("a pull races in, still listing the task as open")
        {
            emit fixture.client()->tasksFetched("project-1", {task});

            THEN("the note keeps the user's tick instead of reverting to the server's "
                 "older truth")
            {
                CHECK(fixture.noteText() == "- [ ] \n- [x] Buy milk");
            }

            AND_WHEN("the completion is confirmed and the next pull answers without "
                     "the task")
            {
                emit fixture.client()->taskCompleted("t-milk");
                REQUIRE(fixture.sync->pendingUploadCount() == 0);

                emit fixture.client()->tasksFetched("project-1", {});

                THEN("the note now rewrites freely — just the compose slot left")
                {
                    CHECK(fixture.noteText() == "- [ ] ");
                }
            }
        }
    }
}

SCENARIO("A pull carries half-typed compose-slot text into the new render",
         "[ticktick][sync]")
{
    GIVEN("a signed-in sync with text sitting uncommitted in the compose slot")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());

        TickTickClient::Task task;
        task.id = "t-milk";
        task.projectId = "project-1";
        task.title = "Buy milk";
        emit fixture.client()->tasksFetched("project-1", {task});

        fixture.typeIntoNote("- [ ] Call den\n- [ ] Buy milk");
        // Let the flush read the slot edit and move the baseline past it, so
        // the pull below is not blocked by a pending upload.
        QTest::qWait(TickTickSync::uploadDebounceMs() + 500);
        REQUIRE(fixture.sync->pendingUploadCount() == 0);

        WHEN("a pull answers mid-sentence")
        {
            emit fixture.client()->tasksFetched("project-1", {task});

            THEN("the re-render keeps what was typed — unlike a task line, there is "
                 "no server copy to get it back from")
            {
                CHECK(fixture.noteText() == "- [ ] Call den\n- [ ] Buy milk");
            }
        }
    }
}

// The life of a line whose create has not confirmed yet. Its clientRef stands
// in for the task id in the remembered baseline; everything below is about
// what happens to edits addressed to that stand-in. The failed-create canned
// response is what captures the actual clientRef — the real client emits
// taskCreateFailed with it, which is the only place a spec can learn it.

namespace {

// Queues a canned rejection for the NEXT create, commits `title`, waits for
// the failure to come back, and returns the clientRef it carried.
QString commitAndCaptureClientRef(AuthorizedSyncFixture &fixture, const QString &title)
{
    QSignalSpy failedSpy(fixture.client(), &TickTickClient::taskCreateFailed);
    fixture.network->canned.append(CannedResponse{
        QByteArrayLiteral("/task"), 400, QByteArrayLiteral(R"({"errorMessage":"boom"})"),
        QNetworkReply::ProtocolInvalidOperationError, QStringLiteral("Bad Request")});

    QStringList lines = TickTickTaskList::linesOf(fixture.noteText());
    if (lines.isEmpty())
        lines.append(QString());
    lines[0] = QStringLiteral("- [ ] ") + title;
    fixture.typeIntoNote(lines.join(QLatin1Char('\n')));
    if (!fixture.sync->commitComposedTask())
        return QString();

    // The canned reply finishes through the event loop.
    if (failedSpy.isEmpty() && !failedSpy.wait(1000))
        return QString();
    return failedSpy.at(0).at(0).toString();
}

}

SCENARIO("A create that failed is retried on the next natural trigger",
         "[ticktick][sync]")
{
    GIVEN("a signed-in sync whose committed task's create was rejected")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());
        fixture.sync->ensureComposeSlot();

        const QString clientRef = commitAndCaptureClientRef(fixture, "Call dentist");
        REQUIRE_FALSE(clientRef.isEmpty());
        REQUIRE(fixture.network->countRequestsTo("/task") == 1);

        WHEN("the next flush runs, for any reason at all")
        {
            fixture.typeIntoNote(fixture.noteText());
            QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

            THEN("the create is sent again — the line still has no task behind it, "
                 "and without a retry it never would")
            {
                CHECK(fixture.network->countRequestsTo("/task") == 2);
            }
        }
    }
}

SCENARIO("The real task id is stitched into the committed line when the create confirms",
         "[ticktick][sync]")
{
    GIVEN("a signed-in sync with a committed line still waiting on its create")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());
        fixture.sync->ensureComposeSlot();

        const QString clientRef = commitAndCaptureClientRef(fixture, "Call dentist");
        REQUIRE_FALSE(clientRef.isEmpty());
        REQUIRE(fixture.noteText() == "- [ ] \n- [ ] Call dentist");

        WHEN("the create confirms with the task TickTick minted")
        {
            TickTickClient::Task realTask;
            realTask.id = "tt-real";
            realTask.projectId = "project-1";
            realTask.title = "Call dentist";
            emit fixture.client()->taskCreated(clientRef, realTask);

            AND_WHEN("the line is then renamed")
            {
                fixture.typeIntoNote("- [ ] \n- [ ] Call dentist NOW");
                QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

                THEN("the rename is addressed to the real task id — the stitch is what "
                     "makes the line editable at all after creation")
                {
                    CHECK(fixture.network->sawRequestTo("/task/tt-real"));
                    CHECK(fixture.network->sawBodyContaining("Call dentist NOW"));
                }
            }
        }
    }
}

SCENARIO("A rename typed while the create is still in flight reaches TickTick "
         "once the real id arrives",
         "[ticktick][sync]")
{
    GIVEN("a signed-in sync with a committed line renamed before its create confirmed")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());
        fixture.sync->ensureComposeSlot();

        const QString clientRef = commitAndCaptureClientRef(fixture, "Call dentist");
        REQUIRE_FALSE(clientRef.isEmpty());

        fixture.typeIntoNote("- [ ] \n- [ ] Call dentist URGENT");
        QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

        WHEN("the create finally confirms, carrying the ORIGINAL title")
        {
            TickTickClient::Task realTask;
            realTask.id = "tt-real";
            realTask.projectId = "project-1";
            realTask.title = "Call dentist";
            emit fixture.client()->taskCreated(clientRef, realTask);

            THEN("a follow-up update delivers the newer title — the rename had no "
                 "task id to be addressed to until this moment")
            {
                CHECK(fixture.network->sawRequestTo("/task/tt-real"));
                CHECK(fixture.network->sawBodyContaining("Call dentist URGENT"));
            }
        }
    }
}

SCENARIO("A line ticked away before its create confirmed is completed the moment "
         "the real id arrives",
         "[ticktick][sync]")
{
    GIVEN("a signed-in sync whose committed line was deleted before its create confirmed")
    {
        AuthorizedSyncFixture fixture;
        REQUIRE(fixture.tempDir.isValid());
        fixture.sync->ensureComposeSlot();

        const QString clientRef = commitAndCaptureClientRef(fixture, "Call dentist");
        REQUIRE_FALSE(clientRef.isEmpty());

        // The user says "done with this" (the completion fade's removal) while
        // there is not yet any task to complete.
        fixture.typeIntoNote("- [ ] ");
        QTest::qWait(TickTickSync::uploadDebounceMs() + 500);
        REQUIRE_FALSE(fixture.network->sawRequestTo("/complete"));

        WHEN("the create confirms")
        {
            TickTickClient::Task realTask;
            realTask.id = "tt-real";
            realTask.projectId = "project-1";
            realTask.title = "Call dentist";
            emit fixture.client()->taskCreated(clientRef, realTask);

            THEN("the remembered completion fires against the real id, so no orphaned "
                 "open task is left sitting on TickTick")
            {
                CHECK(fixture.network->sawRequestTo("/project/project-1/task/tt-real/complete"));
            }
        }
    }
}

// The scheduled pull — the one sanctioned repeating timer in the app
// (CLAUDE.md). These pin the terms the exception is granted under: armed only
// while authorized with a non-zero interval, stopped OUTRIGHT otherwise, and
// floored so a hand-edited config cannot turn it into a busy loop.

namespace {

QTimer *scheduledPullTimer(TickTickSync *sync)
{
    // The sync owns exactly two timers: the single-shot upload debounce, and
    // the repeating schedule.
    const QList<QTimer *> timers = sync->findChildren<QTimer *>();
    for (QTimer *timer : timers) {
        if (!timer->isSingleShot())
            return timer;
    }
    return nullptr;
}

}

SCENARIO("The scheduled pull runs only while signed in, at the configured interval",
         "[ticktick][sync]")
{
    GIVEN("a signed-out sync")
    {
        SyncFixture fixture;
        QTimer *timer = scheduledPullTimer(&fixture.sync);
        REQUIRE(timer != nullptr);

        THEN("nothing is scheduled — a signed-out app has genuinely nothing armed")
        {
            CHECK_FALSE(timer->isActive());
        }
    }

    GIVEN("a signed-in sync")
    {
        AuthorizedSyncFixture fixture;
        QTimer *timer = scheduledPullTimer(fixture.sync.get());
        REQUIRE(timer != nullptr);

        THEN("the pull is scheduled at the default hourly interval")
        {
            CHECK(timer->isActive());
            CHECK(timer->interval() == 60 * 60 * 1000);
        }

        WHEN("the user sets the interval to 0 — never")
        {
            fixture.sync->setSyncIntervalMinutes(0);

            THEN("the timer is stopped outright, not left firing into a no-op")
            {
                CHECK_FALSE(timer->isActive());
                CHECK(fixture.sync->syncIntervalMinutes() == 0);
            }
        }

        WHEN("the user asks for an interval below the floor")
        {
            fixture.sync->setSyncIntervalMinutes(1);

            THEN("it is clamped to the 5-minute floor before it ever reaches the timer")
            {
                CHECK(fixture.sync->syncIntervalMinutes() == 5);
                CHECK(timer->interval() == 5 * 60 * 1000);
                CHECK(timer->isActive());
            }
        }

        WHEN("the user signs out")
        {
            fixture.auth->signOut();

            THEN("the schedule stops with the session")
            {
                CHECK_FALSE(timer->isActive());
            }
        }
    }
}

SCENARIO("A commit before any list has been resolved refuses without losing the text",
         "[ticktick][sync]")
{
    GIVEN("a signed-in sync that has never resolved a list, with text in the slot")
    {
        AuthorizedSyncFixture fixture(false);
        REQUIRE(fixture.tempDir.isValid());
        fixture.sync->ensureComposeSlot();
        fixture.typeIntoNote("- [ ] Call dentist");
        QTest::qWait(TickTickSync::uploadDebounceMs() + 500);

        WHEN("the commit gesture fires")
        {
            const bool committed = fixture.sync->commitComposedTask();

            THEN("it refuses and says why — there is nothing to address the create to")
            {
                CHECK_FALSE(committed);
                CHECK_FALSE(fixture.sync->lastError().isEmpty());
            }

            THEN("the typed text stays in the slot, ready for the retry")
            {
                CHECK(fixture.noteText() == "- [ ] Call dentist");
            }
        }
    }
}

SCENARIO("A response at TickTick's result cap is reported as possibly incomplete",
         "[ticktick][sync]")
{
    GIVEN("a sync")
    {
        SyncFixture fixture;

        WHEN("a task fetch comes back at exactly the cap")
        {
            emit fixture.client()->resultsPossiblyTruncated(200);

            THEN("the user is told, because a capped response looks exactly like a "
                 "complete one and notes past the cap would silently never appear")
            {
                CHECK(fixture.sync.lastError().contains("200"));
            }
        }
    }
}

SCENARIO("The one-time migration sweeps old per-task cache files exactly once",
         "[ticktick][sync]")
{
    GIVEN("a cache directory still holding notes from the old one-note-per-task design")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        const QString configPath = tempDir.filePath("ticktick.ini");
        const QString cacheDir = tempDir.filePath("cache");

        const auto writeRaw = [&cacheDir](const QString &name) {
            QDir().mkpath(cacheDir);
            QFile file(QDir(cacheDir).filePath(name));
            REQUIRE(file.open(QIODevice::WriteOnly));
            file.write("old note");
        };
        writeRaw("old-task-a.md");
        writeRaw("old-task-b.md");
        writeRaw("tasks.md");

        WHEN("the sync is constructed over it for the first time")
        {
            {
                TickTickTokenStore tokenStore(configPath,
                                              TickTickTokenStore::SecretBackend::ConfigFileOnly);
                TickTickAuth auth(tokenStore);
                TickTickNotes notes(cacheDir);
                TickTickSync sync(auth, notes, configPath);
            }

            THEN("only the aggregated task note survives — the old files would sit "
                 "beside it as orphans no pull can ever reconcile away")
            {
                const QStringList remaining =
                    QDir(cacheDir).entryList({"*.md"}, QDir::Files, QDir::Name);
                CHECK(remaining == QStringList{"tasks.md"});
            }

            AND_WHEN("a stray file appears and the app starts again against the same config")
            {
                writeRaw("stray.md");
                {
                    TickTickTokenStore tokenStore(
                        configPath, TickTickTokenStore::SecretBackend::ConfigFileOnly);
                    TickTickAuth auth(tokenStore);
                    TickTickNotes notes(cacheDir);
                    TickTickSync sync(auth, notes, configPath);
                }

                THEN("nothing is swept — the migration ran once for this install, ever")
                {
                    CHECK(QDir(cacheDir).entryList({"*.md"}, QDir::Files, QDir::Name)
                          == (QStringList{"stray.md", "tasks.md"}));
                }
            }
        }
    }
}
