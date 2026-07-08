#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QTemporaryDir>

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

SCENARIO("updateGeometry persists a note's live geometry without disturbing others", "[open_notes_model]")
{
    GIVEN("an OpenNotesModel with two open notes")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        OpenNotesModel model(&settings);
        model.openNote("alpha");
        model.openNote("bravo");

        WHEN("updateGeometry is called for one of them")
        {
            model.updateGeometry("alpha", 111, 222, 333, 444, "HDMI-A-1");

            THEN("that note's geometry roles reflect the new values")
            {
                int alphaRow = -1;
                for (int i = 0; i < model.rowCount(); ++i) {
                    if (model.data(model.index(i), OpenNotesModel::NoteIdRole).toString() == "alpha")
                        alphaRow = i;
                }
                REQUIRE(alphaRow >= 0);
                const QModelIndex idx = model.index(alphaRow);
                CHECK(model.data(idx, OpenNotesModel::XRole).toInt() == 111);
                CHECK(model.data(idx, OpenNotesModel::YRole).toInt() == 222);
                CHECK(model.data(idx, OpenNotesModel::WidthRole).toInt() == 333);
                CHECK(model.data(idx, OpenNotesModel::HeightRole).toInt() == 444);
                CHECK(model.data(idx, OpenNotesModel::ScreenNameRole).toString() == "HDMI-A-1");
            }

            THEN("the other note's geometry is untouched, and no row was added/removed")
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
                        CHECK(state.x == 111);
                        CHECK(state.screenName == "HDMI-A-1");
                    }
                }
                CHECK(found);
            }
        }
    }
}

SCENARIO("updateGeometry preserves a note's pin state instead of resetting it", "[open_notes_model]")
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

        WHEN("updateGeometry is called for it, as a drag/resize would trigger")
        {
            model.updateGeometry("alpha", 10, 20, 30, 40, "DP-4");

            THEN("it is still pinned afterward")
            {
                CHECK(model.data(model.index(0), OpenNotesModel::AlwaysOnTopRole).toBool());
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
