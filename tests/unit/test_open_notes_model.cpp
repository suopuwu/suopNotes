#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/NoteStore.h"
#include "core/OpenNotesModel.h"
#include "core/Settings.h"

namespace
{
QString tempConfigPath(const QTemporaryDir &dir)
{
    return dir.path() + QLatin1String("/settings.ini");
}
}

SCENARIO("OpenNotesModel reflects Settings' open-notes list on construction", "[open_notes_model]")
{
    GIVEN("a Settings instance with two notes already marked open")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        Settings::NoteSessionState a;
        a.noteId = "alpha";
        settings.upsertNoteState(a);
        Settings::NoteSessionState b;
        b.noteId = "bravo";
        settings.upsertNoteState(b);

        WHEN("an OpenNotesModel is constructed against it")
        {
            OpenNotesModel model(&settings);

            THEN("rowCount and data reflect both notes, in order")
            {
                REQUIRE(model.rowCount() == 2);
                CHECK(model.data(model.index(0), OpenNotesModel::NoteIdRole).toString() == "alpha");
                CHECK(model.data(model.index(1), OpenNotesModel::NoteIdRole).toString() == "bravo");
            }
        }
    }
}

SCENARIO("Opening a note via the model", "[open_notes_model]")
{
    GIVEN("an OpenNotesModel over an empty Settings session")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);
        REQUIRE(model.rowCount() == 0);

        WHEN("openNote is called for a new id")
        {
            QSignalSpy insertedSpy(&model, &OpenNotesModel::rowsInserted);
            model.openNote("charlie");

            THEN("the model gains one row for it")
            {
                REQUIRE(model.rowCount() == 1);
                CHECK(model.data(model.index(0), OpenNotesModel::NoteIdRole).toString() == "charlie");
                CHECK(insertedSpy.count() == 1);
            }

            THEN("isOpen reports true for it")
            {
                CHECK(model.isOpen("charlie"));
            }

            THEN("Settings itself now lists it as an open note (persisted, not just in-memory)")
            {
                const auto notes = settings.openNotes();
                REQUIRE(notes.size() == 1);
                CHECK(notes.first().noteId == "charlie");
            }
        }

        WHEN("openNote is called twice for the same id")
        {
            model.openNote("charlie");
            model.openNote("charlie");

            THEN("it appears only once, not duplicated")
            {
                CHECK(model.rowCount() == 1);
            }
        }
    }
}

SCENARIO("Closing a note via the model", "[open_notes_model]")
{
    GIVEN("an OpenNotesModel with three open notes")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);
        model.openNote("alpha");
        model.openNote("bravo");
        model.openNote("charlie");
        REQUIRE(model.rowCount() == 3);

        WHEN("the middle note is closed")
        {
            QSignalSpy removedSpy(&model, &OpenNotesModel::rowsRemoved);
            model.closeNote("bravo");

            THEN("only that note is removed; the other two remain")
            {
                REQUIRE(model.rowCount() == 2);
                CHECK_FALSE(model.isOpen("bravo"));
                CHECK(model.isOpen("alpha"));
                CHECK(model.isOpen("charlie"));
                CHECK(removedSpy.count() == 1);
            }

            THEN("Settings no longer lists it as open")
            {
                const auto notes = settings.openNotes();
                for (const auto &state : notes)
                    CHECK(state.noteId != "bravo");
            }
        }
    }
}

SCENARIO("The model tracks external changes to Settings, not just its own mutators", "[open_notes_model]")
{
    GIVEN("an OpenNotesModel over an empty Settings session")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);
        REQUIRE(model.rowCount() == 0);

        WHEN("Settings::upsertNoteState is called directly, bypassing the model's own openNote()")
        {
            Settings::NoteSessionState state;
            state.noteId = "direct";
            settings.upsertNoteState(state);

            THEN("the model picks up the change via Settings::openNotesChanged")
            {
                REQUIRE(model.rowCount() == 1);
                CHECK(model.data(model.index(0), OpenNotesModel::NoteIdRole).toString() == "direct");
            }
        }

        WHEN("Settings::setOpenNotes wholesale-replaces the list")
        {
            model.openNote("stale-1");
            model.openNote("stale-2");
            REQUIRE(model.rowCount() == 2);

            Settings::NoteSessionState fresh;
            fresh.noteId = "fresh";
            settings.setOpenNotes({ fresh });

            THEN("the model ends up with exactly the new list")
            {
                REQUIRE(model.rowCount() == 1);
                CHECK(model.data(model.index(0), OpenNotesModel::NoteIdRole).toString() == "fresh");
            }
        }
    }
}

SCENARIO("roleNames exposes noteId for QML delegate binding", "[open_notes_model]")
{
    GIVEN("any OpenNotesModel")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);

        THEN("the noteId role is registered under that exact name")
        {
            const auto roles = model.roleNames();
            REQUIRE(roles.contains(OpenNotesModel::NoteIdRole));
            CHECK(roles.value(OpenNotesModel::NoteIdRole) == "noteId");
        }

        THEN("geometry roles are registered for QML delegate binding")
        {
            const auto roles = model.roleNames();
            CHECK(roles.value(OpenNotesModel::XRole) == "noteX");
            CHECK(roles.value(OpenNotesModel::YRole) == "noteY");
            CHECK(roles.value(OpenNotesModel::WidthRole) == "noteWidth");
            CHECK(roles.value(OpenNotesModel::HeightRole) == "noteHeight");
            CHECK(roles.value(OpenNotesModel::ScreenNameRole) == "screenName");
        }
    }
}

SCENARIO("A newly-opened note exposes its persisted geometry via model roles", "[open_notes_model]")
{
    GIVEN("Settings with a note that already has specific geometry")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        Settings::NoteSessionState state;
        state.noteId = "alpha";
        state.x = 42;
        state.y = 84;
        state.width = 300;
        state.height = 400;
        state.screenName = "DP-4";
        settings.upsertNoteState(state);

        WHEN("an OpenNotesModel is constructed against it")
        {
            OpenNotesModel model(&settings);
            const QModelIndex idx = model.index(0);

            THEN("every geometry role matches what was persisted")
            {
                CHECK(model.data(idx, OpenNotesModel::XRole).toInt() == 42);
                CHECK(model.data(idx, OpenNotesModel::YRole).toInt() == 84);
                CHECK(model.data(idx, OpenNotesModel::WidthRole).toInt() == 300);
                CHECK(model.data(idx, OpenNotesModel::HeightRole).toInt() == 400);
                CHECK(model.data(idx, OpenNotesModel::ScreenNameRole).toString() == "DP-4");
            }
        }
    }
}

SCENARIO("updateSize persists a note's live size without disturbing others", "[open_notes_model]")
{
    GIVEN("an OpenNotesModel with two open notes")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);
        model.openNote("alpha");
        model.openNote("bravo");

        WHEN("updateSize is called for one of them")
        {
            model.updateSize("alpha", 333, 444);

            THEN("that note's size roles reflect the new values")
            {
                int alphaRow = -1;
                for (int i = 0; i < model.rowCount(); ++i) {
                    if (model.data(model.index(i), OpenNotesModel::NoteIdRole).toString() == "alpha")
                        alphaRow = i;
                }
                REQUIRE(alphaRow >= 0);
                const QModelIndex idx = model.index(alphaRow);
                CHECK(model.data(idx, OpenNotesModel::WidthRole).toInt() == 333);
                CHECK(model.data(idx, OpenNotesModel::HeightRole).toInt() == 444);
            }

            THEN("the other note's size is untouched, and no row was added/removed")
            {
                REQUIRE(model.rowCount() == 2);
            }

            THEN("it is persisted in Settings, not just the model's in-memory cache")
            {
                const auto notes = settings.openNotes();
                bool found = false;
                for (const auto &state : notes) {
                    if (state.noteId == "alpha") {
                        found = true;
                        CHECK(state.width == 333);
                        CHECK(state.height == 444);
                    }
                }
                CHECK(found);
            }
        }
    }
}

SCENARIO("updateSize preserves a note's pin state instead of resetting it", "[open_notes_model]")
{
    GIVEN("a pinned, open note")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);
        model.openNote("alpha");
        model.setAlwaysOnTop("alpha", true);
        REQUIRE(model.data(model.index(0), OpenNotesModel::AlwaysOnTopRole).toBool());

        WHEN("updateSize is called for it, as a resize would trigger")
        {
            model.updateSize("alpha", 30, 40);

            THEN("it is still pinned afterward")
            {
                CHECK(model.data(model.index(0), OpenNotesModel::AlwaysOnTopRole).toBool());
            }
        }
    }
}

SCENARIO("updateSize does not alter a note's existing position or screen", "[open_notes_model]")
{
    GIVEN("an open note whose position was saved by a live compositor-driven drag "
          "(PositionEventService's domain, not this model's)")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);
        model.openNote("alpha");

        Settings::NoteSessionState dragged;
        dragged.noteId = "alpha";
        dragged.x = 900;
        dragged.y = 700;
        dragged.width = 260;
        dragged.height = 260;
        dragged.screenName = "DP-2";
        settings.upsertNoteState(dragged);

        WHEN("updateSize is called afterward, as a resize on that same note would trigger")
        {
            // Regression test: this used to be updateGeometry(noteId, x, y,
            // width, height, screenName), called with NoteWindow's own x/y —
            // which on Wayland are never live-synced back from a real
            // compositor-driven drag. Every resize after a drag silently
            // reverted x/y/screenName to whatever those stale values were,
            // even though the note visually stayed at the dragged position.
            model.updateSize("alpha", 500, 600);

            THEN("the note's dragged position and screen are untouched, only size changed")
            {
                const QModelIndex idx = model.index(0);
                CHECK(model.data(idx, OpenNotesModel::XRole).toInt() == 900);
                CHECK(model.data(idx, OpenNotesModel::YRole).toInt() == 700);
                CHECK(model.data(idx, OpenNotesModel::ScreenNameRole).toString() == "DP-2");
                CHECK(model.data(idx, OpenNotesModel::WidthRole).toInt() == 500);
                CHECK(model.data(idx, OpenNotesModel::HeightRole).toInt() == 600);
            }
        }
    }
}

SCENARIO("openNoteIds reports every currently-open note id", "[open_notes_model]")
{
    GIVEN("an OpenNotesModel with no open notes")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);

        THEN("it starts empty")
        {
            REQUIRE(model.openNoteIds().isEmpty());
        }

        WHEN("three notes are opened")
        {
            model.openNote("alpha");
            model.openNote("bravo");
            model.openNote("charlie");

            THEN("openNoteIds lists exactly those three")
            {
                const auto ids = model.openNoteIds();
                REQUIRE(ids.size() == 3);
                CHECK(ids.contains("alpha"));
                CHECK(ids.contains("bravo"));
                CHECK(ids.contains("charlie"));
            }

            AND_WHEN("one is closed")
            {
                model.closeNote("bravo");

                THEN("openNoteIds no longer lists it")
                {
                    const auto ids = model.openNoteIds();
                    REQUIRE(ids.size() == 2);
                    CHECK_FALSE(ids.contains("bravo"));
                }
            }
        }
    }
}

SCENARIO("setAlwaysOnTop toggles a single note's pin state", "[open_notes_model]")
{
    GIVEN("two open notes, neither pinned")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);
        model.openNote("alpha");
        model.openNote("bravo");

        WHEN("alpha is pinned")
        {
            QSignalSpy dataChangedSpy(&model, &OpenNotesModel::dataChanged);
            model.setAlwaysOnTop("alpha", true);

            THEN("only alpha reports pinned")
            {
                int alphaRow = model.data(model.index(0), OpenNotesModel::NoteIdRole).toString() == "alpha" ? 0 : 1;
                int bravoRow = 1 - alphaRow;
                CHECK(model.data(model.index(alphaRow), OpenNotesModel::AlwaysOnTopRole).toBool());
                CHECK_FALSE(model.data(model.index(bravoRow), OpenNotesModel::AlwaysOnTopRole).toBool());
            }

            THEN("dataChanged fires so QML bindings refresh")
            {
                CHECK(dataChangedSpy.count() == 1);
            }

            THEN("it is persisted, not just in-memory")
            {
                const auto notes = settings.openNotes();
                for (const auto &state : notes) {
                    if (state.noteId == "alpha")
                        CHECK(state.alwaysOnTop);
                }
            }
        }
    }
}

// A note created at a chosen spot — duplicating one — has to be positioned
// before its window exists, because a mapped Wayland toplevel cannot be placed
// by assigning x/y afterwards. openNoteAt() covers the case where the caller
// gets to open the note itself. It cannot cover the real app, where
// NoteStore::createNote() emits noteIdsChanged synchronously and Main.qml's
// reconciliation opens the note — via plain openNote() — before createNote()
// has even returned. The seed is what reaches that opener.

SCENARIO("A seeded spawn position reaches a note opened by someone else",
         "[open_notes_model]")
{
    GIVEN("a model with a spawn position seeded")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);
        model.seedNextSpawnPosition(640, 480);

        WHEN("a note is opened by a plain openNote(), as the reconciliation does")
        {
            model.openNote("alpha");

            THEN("it lands at the seeded position, not the unpositioned default")
            {
                // (0, 0) is the "never positioned" sentinel that makes
                // NoteWindow fall back to its index-based cascade. Landing
                // there is precisely the bug: every duplicate appeared on the
                // cascade instead of beside the note it was copied from.
                CHECK(model.data(model.index(0), OpenNotesModel::XRole).toInt() == 640);
                CHECK(model.data(model.index(0), OpenNotesModel::YRole).toInt() == 480);
            }
        }

        WHEN("a second note is opened after the first consumed the seed")
        {
            model.openNote("alpha");
            model.openNote("bravo");

            THEN("only the first is positioned")
            {
                // One-shot, not a mode: leaving it armed would drag every
                // later note — a TickTick pull's, say — to the same spot.
                const int bravoRow =
                    model.data(model.index(0), OpenNotesModel::NoteIdRole).toString() == "bravo" ? 0 : 1;
                CHECK(model.data(model.index(bravoRow), OpenNotesModel::XRole).toInt() == 0);
                CHECK(model.data(model.index(bravoRow), OpenNotesModel::YRole).toInt() == 0);
            }
        }
    }

    GIVEN("a model with notes already open and a seed set afterwards")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);
        model.openNote("existing-one");
        model.openNote("existing-two");
        model.seedNextSpawnPosition(300, 200);

        WHEN("the reconciliation sweeps every known note before reaching the new one")
        {
            // This is exactly what Main.qml does: it walks the full list of
            // ids on disk and calls openNote() for each. The already-open ones
            // must not eat the seed on their way past.
            model.openNote("existing-one");
            model.openNote("existing-two");
            model.openNote("brand-new");

            THEN("the seed survives the already-open notes and reaches the new one")
            {
                int newRow = -1;
                for (int row = 0; row < model.rowCount(); ++row) {
                    if (model.data(model.index(row), OpenNotesModel::NoteIdRole).toString()
                        == "brand-new")
                        newRow = row;
                }
                REQUIRE(newRow >= 0);
                CHECK(model.data(model.index(newRow), OpenNotesModel::XRole).toInt() == 300);
                CHECK(model.data(model.index(newRow), OpenNotesModel::YRole).toInt() == 200);
            }

            THEN("the already-open notes are left exactly where they were")
            {
                for (const auto &state : settings.openNotes()) {
                    if (state.noteId != "brand-new") {
                        CHECK(state.x == 0);
                        CHECK(state.y == 0);
                    }
                }
            }
        }
    }
}

SCENARIO("An unconsumed spawn seed can be dropped", "[open_notes_model]")
{
    GIVEN("a seed set for a creation that then failed")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);
        model.seedNextSpawnPosition(900, 900);

        WHEN("the caller clears it and an unrelated note opens later")
        {
            // A create that fails never emits noteIdsChanged, so no
            // reconciliation runs and nothing consumes the seed. Without an
            // explicit clear it would sit armed and yank the next note opened
            // for any reason — a TickTick pull's, say — to 900,900.
            model.clearNextSpawnPosition();
            model.openNote("unrelated");

            THEN("that note is unpositioned, as a fresh note should be")
            {
                CHECK(model.data(model.index(0), OpenNotesModel::XRole).toInt() == 0);
                CHECK(model.data(model.index(0), OpenNotesModel::YRole).toInt() == 0);
            }
        }
    }
}

// The bug this seed exists for, reproduced against the real NoteStore rather
// than a stand-in: createNote() emits noteIdsChanged before it returns, and the
// app's reconciliation opens the note on that signal — so the caller's own
// "open it at this position" call arrives too late to have any effect.
//
// The lambda below IS Main.qml's reconcileNotesWithDisk(), reduced to the one
// line that matters here. Without it this file cannot see the bug at all, which
// is precisely why the bug survived: the QML test harness has no Main.qml, so
// the duplicate-position test there passed while every real duplicate landed on
// the default cascade.
SCENARIO("A note created while the app reconciles on disk changes still lands where asked",
         "[open_notes_model]")
{
    GIVEN("a NoteStore whose id changes are reconciled into the model synchronously")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);

        NoteStore store;
        store.setNotesFolder(dir.path() + QLatin1String("/notes"));

        QObject::connect(&store, &NoteStore::noteIdsChanged, &model, [&store, &model]() {
            const QStringList ids = store.noteIds();
            for (const QString &id : ids) {
                if (!model.isOpen(id))
                    model.openNote(id);
            }
        });

        WHEN("a note is created with its spawn position seeded first")
        {
            model.seedNextSpawnPosition(640, 480);
            const QString newId = store.createNote(QStringLiteral("a duplicate"));
            model.clearNextSpawnPosition();
            REQUIRE_FALSE(newId.isEmpty());

            THEN("the reconciliation has already opened it")
            {
                // This is the whole problem: by the time createNote() returns,
                // the note is open and — in the real app — its window is built
                // and its KWin spawn rule already evaluated. Anything the
                // caller does now is too late.
                CHECK(model.isOpen(newId));
            }

            THEN("it is nonetheless at the requested position")
            {
                int row = -1;
                for (int i = 0; i < model.rowCount(); ++i) {
                    if (model.data(model.index(i), OpenNotesModel::NoteIdRole).toString() == newId)
                        row = i;
                }
                REQUIRE(row >= 0);
                CHECK(model.data(model.index(row), OpenNotesModel::XRole).toInt() == 640);
                CHECK(model.data(model.index(row), OpenNotesModel::YRole).toInt() == 480);
            }
        }

        WHEN("a note is created with no seed, as the tray's New Note does")
        {
            const QString newId = store.createNote();
            REQUIRE_FALSE(newId.isEmpty());

            THEN("it is unpositioned, so NoteWindow's own cascade places it")
            {
                // A brand-new note has no source window to sit beside, so the
                // (0, 0) "never positioned" sentinel is the correct answer, not
                // a missed opportunity to place it.
                int row = -1;
                for (int i = 0; i < model.rowCount(); ++i) {
                    if (model.data(model.index(i), OpenNotesModel::NoteIdRole).toString() == newId)
                        row = i;
                }
                REQUIRE(row >= 0);
                CHECK(model.data(model.index(row), OpenNotesModel::XRole).toInt() == 0);
                CHECK(model.data(model.index(row), OpenNotesModel::YRole).toInt() == 0);
            }
        }
    }
}
