#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/NoteStore.h"

// These specs describe NoteStore's contract: a folder of plain .md files,
// debounced-but-atomic saves, and change notification (including changes
// made externally to the app). Every scenario gets its own QTemporaryDir so
// nothing ever touches a real user directory.

namespace
{
// Debounce window in NoteStore.cpp is 300ms; wait comfortably past it so
// timing jitter on a loaded CI box can't make these flaky.
constexpr int kPastDebounceMs = 600;
}

SCENARIO("Enumerating notes in an existing folder", "[note_store]")
{
    GIVEN("a folder that already contains several .md files")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        QFile a(tempDir.filePath("alpha.md"));
        REQUIRE(a.open(QIODevice::WriteOnly));
        a.write("Alpha");
        a.close();

        QFile b(tempDir.filePath("bravo.md"));
        REQUIRE(b.open(QIODevice::WriteOnly));
        b.write("Bravo");
        b.close();

        // A non-.md file should be ignored entirely.
        QFile other(tempDir.filePath("notes.txt"));
        REQUIRE(other.open(QIODevice::WriteOnly));
        other.write("ignore me");
        other.close();

        WHEN("NoteStore is pointed at that folder")
        {
            NoteStore store;
            store.setNotesFolder(tempDir.path());

            THEN("noteIds() reports one id per .md file, and nothing else")
            {
                const QStringList ids = store.noteIds();
                REQUIRE(ids.size() == 2);

                // Ids are freshly-minted UUIDs, unrelated to the on-disk
                // filename — content is what ties an id back to its file.
                QStringList contents;
                for (const QString &id : ids)
                    contents << store.loadNoteContent(id);
                CHECK(contents.contains("Alpha"));
                CHECK(contents.contains("Bravo"));
            }

            THEN("the pre-existing filenames are left untouched (not yet edited by the app)")
            {
                CHECK(QFile::exists(tempDir.filePath("alpha.md")));
                CHECK(QFile::exists(tempDir.filePath("bravo.md")));
            }
        }
    }
}

SCENARIO("Creating a new note", "[note_store]")
{
    GIVEN("a NoteStore pointed at an empty folder")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());
        QSignalSpy idsChangedSpy(&store, &NoteStore::noteIdsChanged);

        WHEN("createNote() is called")
        {
            const QString id = store.createNote();

            THEN("it returns a fresh, non-empty id")
            {
                CHECK_FALSE(id.isEmpty());
            }

            THEN("an empty note file exists on disk, named 'Untitled' for lack of any content")
            {
                QFile file(tempDir.filePath("Untitled.md"));
                REQUIRE(file.exists());
                REQUIRE(file.open(QIODevice::ReadOnly));
                CHECK(file.readAll().isEmpty());
            }

            THEN("noteIds() includes the new id and noteIdsChanged was emitted")
            {
                CHECK(store.noteIds().contains(id));
                CHECK(idsChangedSpy.count() >= 1);
            }
        }
    }
}

SCENARIO("Creating a note with initial content (duplicating another note)", "[note_store]")
{
    GIVEN("a NoteStore pointed at an empty folder")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());

        WHEN("createNote() is called with content")
        {
            const QString id = store.createNote("Grocery list\n\n- milk\n- eggs");

            THEN("the file exists immediately, with no debounce wait, already holding that content")
            {
                CHECK(QFile::exists(tempDir.filePath("Grocery list.md")));
                CHECK(store.loadNoteContent(id) == "Grocery list\n\n- milk\n- eggs");
            }

            THEN("the filename is derived from the content, not left as 'Untitled'")
            {
                CHECK_FALSE(QFile::exists(tempDir.filePath("Untitled.md")));
            }
        }

        WHEN("a second note is created with the same content as an existing one")
        {
            const QString firstId = store.createNote("Groceries");
            const QString secondId = store.createNote("Groceries");

            THEN("they get distinct ids and distinct, collision-suffixed filenames")
            {
                CHECK(firstId != secondId);
                CHECK(QFile::exists(tempDir.filePath("Groceries.md")));
                CHECK(QFile::exists(tempDir.filePath("Groceries (2).md")));
            }

            THEN("each id's content is reachable independently")
            {
                CHECK(store.loadNoteContent(firstId) == "Groceries");
                CHECK(store.loadNoteContent(secondId) == "Groceries");
            }
        }
    }
}

SCENARIO("Saving note content is debounced", "[note_store]")
{
    GIVEN("a note that already exists")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());
        const QString id = store.createNote();

        WHEN("saveNoteContent is called several times in rapid succession for the same id")
        {
            store.saveNoteContent(id, "first draft");
            store.saveNoteContent(id, "second draft");
            store.saveNoteContent(id, "final draft");

            THEN("nothing is written to disk before the debounce window elapses")
            {
                CHECK(store.loadNoteContent(id).isEmpty());
            }

            THEN("only the final content is on disk once the debounce window has passed")
            {
                QTest::qWait(kPastDebounceMs);
                CHECK(store.loadNoteContent(id) == "final draft");
            }
        }
    }
}

SCENARIO("Saved content round-trips through loadNoteContent", "[note_store]")
{
    GIVEN("a note with freshly saved content")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());
        const QString id = store.createNote();

        const QString markdown = "# Title\n\nSome *markdown* body text.\n";
        store.saveNoteContent(id, markdown);
        QTest::qWait(kPastDebounceMs);

        WHEN("the note content is loaded back")
        {
            const QString loaded = store.loadNoteContent(id);

            THEN("it matches exactly what was saved")
            {
                CHECK(loaded == markdown);
            }
        }
    }
}

SCENARIO("Deleting a note", "[note_store]")
{
    GIVEN("a NoteStore with one existing note")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());
        const QString id = store.createNote();
        REQUIRE(store.noteIds().contains(id));

        WHEN("deleteNote is called for that id")
        {
            store.deleteNote(id);

            THEN("the file no longer exists on disk")
            {
                CHECK_FALSE(QFile::exists(tempDir.filePath("Untitled.md")));
            }

            THEN("noteIds() no longer includes it")
            {
                CHECK_FALSE(store.noteIds().contains(id));
            }
        }
    }
}

SCENARIO("Saves are written atomically", "[note_store]")
{
    GIVEN("a note about to be saved")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());
        const QString id = store.createNote();

        WHEN("content is saved and the debounce window elapses")
        {
            store.saveNoteContent(id, "atomic write test");
            QTest::qWait(kPastDebounceMs);

            THEN("no leftover temp file from QSaveFile remains in the folder")
            {
                // QSaveFile writes to a temp file alongside the target and
                // renames it into place on commit(); a stray temp file here
                // would mean a save was interrupted mid-write. We can't
                // easily force a real crash mid-write in a unit test, but we
                // can confirm the happy path never leaves debris behind,
                // which is the observable symptom of non-atomic writes.
                const QDir dir(tempDir.path());
                const QStringList allFiles = dir.entryList(QDir::Files);
                for (const QString &fileName : allFiles)
                    CHECK((fileName.endsWith(".md") || fileName == QStringLiteral(".suopnotes-index.json")));
            }

            THEN("the final content is exactly what was saved, never partial")
            {
                CHECK(store.loadNoteContent(id) == "atomic write test");
            }
        }
    }
}

SCENARIO("External changes to the notes folder are detected", "[note_store]")
{
    GIVEN("a NoteStore watching a folder with one note")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());
        const QString id = store.createNote();
        QTest::qWait(100); // let the initial watch settle

        QSignalSpy externalChangeSpy(&store, &NoteStore::noteContentChangedExternally);

        WHEN("a file is modified directly on disk, bypassing NoteStore")
        {
            // A freshly-created, still-empty note derives to "Untitled.md" —
            // see NoteFilename::deriveBaseName's fallback.
            QFile file(tempDir.filePath("Untitled.md"));
            REQUIRE(file.open(QIODevice::WriteOnly));
            file.write("edited outside the app");
            file.close();

            QTest::qWait(kPastDebounceMs);

            THEN("noteContentChangedExternally fires for that id")
            {
                REQUIRE(externalChangeSpy.count() >= 1);
                const QString reportedId = externalChangeSpy.at(0).at(0).toString();
                CHECK(reportedId == id);
            }
        }

        WHEN("NoteStore performs its own debounced save")
        {
            store.saveNoteContent(id, "written via the app");
            QTest::qWait(kPastDebounceMs);

            THEN("noteContentChangedExternally does NOT fire for our own write")
            {
                CHECK(externalChangeSpy.count() == 0);
            }
        }
    }
}

SCENARIO("External creation/removal of files updates enumeration", "[note_store]")
{
    GIVEN("a NoteStore watching an empty folder")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());
        QSignalSpy idsChangedSpy(&store, &NoteStore::noteIdsChanged);

        WHEN("a .md file is dropped into the folder by something other than NoteStore")
        {
            QFile file(tempDir.filePath("external-note.md"));
            REQUIRE(file.open(QIODevice::WriteOnly));
            file.write("hello from outside");
            file.close();

            QTest::qWait(kPastDebounceMs);

            THEN("it shows up in noteIds() (under a freshly-minted id) and noteIdsChanged is emitted")
            {
                const QStringList ids = store.noteIds();
                REQUIRE(ids.size() == 1);
                CHECK(store.loadNoteContent(ids.first()) == "hello from outside");
                CHECK(idsChangedSpy.count() >= 1);
            }

            THEN("the file itself is left named as the external tool named it")
            {
                CHECK(QFile::exists(tempDir.filePath("external-note.md")));
            }
        }
    }
}

SCENARIO("Switching to a different notes folder mid-session", "[note_store]")
{
    GIVEN("a NoteStore watching a folder with one note")
    {
        QTemporaryDir firstDir;
        QTemporaryDir secondDir;
        REQUIRE(firstDir.isValid());
        REQUIRE(secondDir.isValid());

        NoteStore store;
        store.setNotesFolder(firstDir.path());
        store.createNote();
        REQUIRE(store.noteIds().size() == 1);

        // The second folder has different notes altogether.
        QFile other(secondDir.filePath("other-note.md"));
        REQUIRE(other.open(QIODevice::WriteOnly));
        other.write("from the other folder");
        other.close();

        WHEN("notesFolder is switched to the second directory")
        {
            store.setNotesFolder(secondDir.path());

            THEN("noteIds() reflects only the new folder's contents")
            {
                const QStringList ids = store.noteIds();
                REQUIRE(ids.size() == 1);
                CHECK(store.loadNoteContent(ids.first()) == "from the other folder");
            }

            THEN("notesFolder() reports the new path")
            {
                CHECK(store.notesFolder() == secondDir.path());
            }
        }
    }
}

SCENARIO("Loading content for a nonexistent note id", "[note_store]")
{
    GIVEN("a NoteStore with no notes")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());

        WHEN("loadNoteContent is called with an id that doesn't exist")
        {
            const QString content = store.loadNoteContent("no-such-id");

            THEN("it returns an empty string rather than crashing")
            {
                CHECK(content.isEmpty());
            }
        }
    }
}

SCENARIO("Pointing NoteStore at a not-yet-created folder", "[note_store]")
{
    GIVEN("a path that does not exist on disk yet")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        const QString notYetCreated = tempDir.filePath("fresh-config/notes");
        REQUIRE_FALSE(QDir(notYetCreated).exists());

        WHEN("setNotesFolder is called with that path")
        {
            NoteStore store;
            store.setNotesFolder(notYetCreated);

            THEN("NoteStore creates the folder rather than failing")
            {
                CHECK(QDir(notYetCreated).exists());
            }

            THEN("noteIds() is simply empty, with no crash")
            {
                CHECK(store.noteIds().isEmpty());
            }

            THEN("a note can still be created in it immediately")
            {
                const QString id = store.createNote();
                CHECK(store.noteIds().contains(id));
            }
        }
    }
}

SCENARIO("A note's on-disk filename tracks its content", "[note_store]")
{
    GIVEN("a newly created note")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());
        const QString id = store.createNote();
        REQUIRE(QFile::exists(tempDir.filePath("Untitled.md")));

        WHEN("content is saved and the debounce window elapses")
        {
            store.saveNoteContent(id, "Grocery list\n\n- milk\n- eggs");
            QTest::qWait(kPastDebounceMs);

            THEN("the file is renamed to match the first line")
            {
                CHECK(QFile::exists(tempDir.filePath("Grocery list.md")));
                CHECK_FALSE(QFile::exists(tempDir.filePath("Untitled.md")));
            }

            THEN("the id itself never changes")
            {
                CHECK(store.noteIds() == QStringList{id});
            }

            THEN("content is still reachable through the same id")
            {
                CHECK(store.loadNoteContent(id) == "Grocery list\n\n- milk\n- eggs");
            }
        }

        WHEN("the content has a markdown heading further down than the first line")
        {
            store.saveNoteContent(id, "some intro\n\n## Project Plan\n\nbody text");
            QTest::qWait(kPastDebounceMs);

            THEN("the first line still wins, the later heading is not given priority")
            {
                CHECK(QFile::exists(tempDir.filePath("some intro.md")));
                CHECK_FALSE(QFile::exists(tempDir.filePath("Project Plan.md")));
            }
        }

        WHEN("content is saved, then edited again to a different first line")
        {
            store.saveNoteContent(id, "First title");
            QTest::qWait(kPastDebounceMs);
            REQUIRE(QFile::exists(tempDir.filePath("First title.md")));

            store.saveNoteContent(id, "Second title");
            QTest::qWait(kPastDebounceMs);

            THEN("the file is renamed again, with no leftover from the previous name")
            {
                CHECK(QFile::exists(tempDir.filePath("Second title.md")));
                CHECK_FALSE(QFile::exists(tempDir.filePath("First title.md")));
            }
        }
    }
}

SCENARIO("Two notes deriving the same filename don't collide", "[note_store]")
{
    GIVEN("two notes that will both end up titled the same thing")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());
        const QString firstId = store.createNote();
        const QString secondId = store.createNote();

        WHEN("both are saved with identical first lines")
        {
            store.saveNoteContent(firstId, "Todo");
            store.saveNoteContent(secondId, "Todo");
            QTest::qWait(kPastDebounceMs);

            THEN("the second one gets a disambiguating suffix rather than clobbering the first")
            {
                CHECK(QFile::exists(tempDir.filePath("Todo.md")));
                CHECK(QFile::exists(tempDir.filePath("Todo (2).md")));
            }

            THEN("both ids still resolve to their own distinct content")
            {
                CHECK(store.loadNoteContent(firstId) == "Todo");
                CHECK(store.loadNoteContent(secondId) == "Todo");
            }
        }
    }
}

SCENARIO("The id<->filename index survives being deleted", "[note_store]")
{
    GIVEN("a folder with a note whose index file has been removed")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        {
            NoteStore store;
            store.setNotesFolder(tempDir.path());
            store.createNote();
            store.saveNoteContent(store.noteIds().first(), "Survives index loss");
            QTest::qWait(kPastDebounceMs);
        }
        REQUIRE(QFile::remove(tempDir.filePath(".suopnotes-index.json")));

        WHEN("a fresh NoteStore is pointed at that folder")
        {
            NoteStore store;
            store.setNotesFolder(tempDir.path());

            THEN("the note is still found (under a newly minted id) rather than lost")
            {
                const QStringList ids = store.noteIds();
                REQUIRE(ids.size() == 1);
                CHECK(store.loadNoteContent(ids.first()) == "Survives index loss");
            }
        }
    }
}
