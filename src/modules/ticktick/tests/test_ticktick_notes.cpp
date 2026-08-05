#include <catch2/catch_test_macros.hpp>

#include "TickTickModule.h"
#include "TickTickNotes.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

// TickTick owns these notes' content; what is on disk is a cache, never a peer
// copy. The cache exists for two reasons that are worth keeping straight: it is
// what removes conflict resolution from the design (one authority, so nothing
// to merge), and it is what makes the notes openable at all, since note content
// is read synchronously during window construction.

namespace {
void writeRawCacheFile(const QString &dir, const QString &name, const QString &content)
{
    QDir().mkpath(dir);
    QFile file(QDir(dir).filePath(name));
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(content.toUtf8());
}
}

SCENARIO("There is exactly one TickTick-owned note: the aggregated task list",
         "[ticktick][notes]")
{
    // Feature request: one checkbox-list note for every open task, instead
    // of one note per task — noteIds() now reports at most a single,
    // fixed id rather than enumerating the cache directory.
    GIVEN("the task list has been cached")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        const QString cacheDir = tempDir.filePath("cache");
        writeRawCacheFile(cacheDir, "tasks.md", "- [ ] Buy milk");

        TickTickNotes notes(cacheDir);

        THEN("its single id is reported, prefixed so it can be told from local notes")
        {
            const QStringList ids = notes.noteIds();
            REQUIRE(ids.size() == 1);
            CHECK(ids.first() == TickTickModule::makeNoteId(TickTickNotes::taskListUuid()));
        }

        THEN("its content loads synchronously")
        {
            // Synchronous is the requirement, not an optimisation: this is
            // called during note-window construction and there is no
            // async load path in the app.
            CHECK(notes.loadNoteContent(TickTickModule::makeNoteId(TickTickNotes::taskListUuid()))
                  == "- [ ] Buy milk");
        }

        THEN("it claims only its own ids")
        {
            CHECK(notes.ownsNoteId(TickTickModule::makeNoteId(TickTickNotes::taskListUuid())));
            CHECK_FALSE(notes.ownsNoteId("some-local-uuid"));
        }
    }

    GIVEN("a cache directory that does not exist yet")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickNotes notes(tempDir.filePath("not-created"));

        THEN("there are simply no notes, and nothing fails")
        {
            // First run (or one not authorized yet) reaches here; it must
            // not look like an error.
            CHECK(notes.noteIds().isEmpty());
            CHECK(notes.loadNoteContent(TickTickModule::makeNoteId(TickTickNotes::taskListUuid()))
                  .isEmpty());
        }
    }

    GIVEN("a cache directory that exists but holds no task-list note yet")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        const QString cacheDir = tempDir.filePath("cache");
        writeRawCacheFile(cacheDir, "scratch.txt", "not a note");

        TickTickNotes notes(cacheDir);

        THEN("noteIds() is empty rather than inventing an id for a file that isn't the task list")
        {
            CHECK(notes.noteIds().isEmpty());
        }
    }
}

SCENARIO("The old one-note-per-task cache is swept away once, in favor of the task list",
         "[ticktick][notes]")
{
    // Feature request: switching from one note per task to one aggregated
    // note. A user upgrading has old per-task cache files on disk that no
    // pull will ever reconcile away on its own (the pull only knows about
    // the single "tasks" id now) — left in place they would sit alongside
    // the new note as orphans forever. removeAllCachedNotesExcept is the
    // sweep; TickTickSync is responsible for calling it exactly once per
    // install (see TickTickPrefs::migratedToTaskList), but the sweep itself
    // is what this pins down.
    GIVEN("a cache holding several old per-task notes and no task-list note")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        const QString cacheDir = tempDir.filePath("cache");
        writeRawCacheFile(cacheDir, "old-task-1.md", "Buy milk");
        writeRawCacheFile(cacheDir, "old-task-2.md", "Walk dog");

        TickTickNotes notes(cacheDir);
        QSignalSpy idsSpy(&notes, &TickTickNotes::noteIdsChanged);

        WHEN("the migration sweep runs")
        {
            notes.removeAllCachedNotesExcept(TickTickNotes::taskListUuid());

            THEN("every old per-task cache file is gone")
            {
                CHECK_FALSE(notes.hasCachedNote("old-task-1"));
                CHECK_FALSE(notes.hasCachedNote("old-task-2"));
                CHECK(notes.noteIds().isEmpty());
            }

            THEN("the note-id set is reported changed, once")
            {
                CHECK(idsSpy.count() == 1);
            }
        }
    }

    GIVEN("a cache already holding the task-list note plus a stale per-task note")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        const QString cacheDir = tempDir.filePath("cache");
        writeRawCacheFile(cacheDir, "tasks.md", "- [ ] Buy milk");
        writeRawCacheFile(cacheDir, "old-task-1.md", "Buy milk");

        TickTickNotes notes(cacheDir);

        WHEN("the migration sweep runs")
        {
            notes.removeAllCachedNotesExcept(TickTickNotes::taskListUuid());

            THEN("the task-list note survives and the stale one is removed")
            {
                CHECK(notes.hasCachedNote(TickTickNotes::taskListUuid()));
                CHECK_FALSE(notes.hasCachedNote("old-task-1"));
                REQUIRE(notes.noteIds().size() == 1);
                CHECK(notes.noteIds().first()
                      == TickTickModule::makeNoteId(TickTickNotes::taskListUuid()));
            }
        }
    }

    GIVEN("a cache with nothing to sweep")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickNotes notes(tempDir.filePath("cache"));
        QSignalSpy idsSpy(&notes, &TickTickNotes::noteIdsChanged);

        WHEN("the migration sweep runs against an empty (even non-existent) cache")
        {
            notes.removeAllCachedNotesExcept(TickTickNotes::taskListUuid());

            THEN("nothing is reported changed, and nothing fails")
            {
                CHECK(idsSpy.count() == 0);
            }
        }
    }
}

SCENARIO("Writing to the cache announces the right kind of change", "[ticktick][notes]")
{
    GIVEN("an empty cache")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickNotes notes(tempDir.filePath("cache"));

        QSignalSpy idsSpy(&notes, &TickTickNotes::noteIdsChanged);
        QSignalSpy contentSpy(&notes, &TickTickNotes::noteContentChangedExternally);

        WHEN("a note is cached for the first time")
        {
            const QString id = notes.writeCachedNote("uuid-a", "hello");

            THEN("the set of notes is reported as changed")
            {
                // This is what makes a window open for it — reconcile runs off
                // the id list, not off content.
                CHECK(id == TickTickModule::makeNoteId("uuid-a"));
                CHECK(idsSpy.count() == 1);
                CHECK(contentSpy.count() == 0);
            }

            THEN("the cache directory is created on demand")
            {
                CHECK(notes.hasCachedNote("uuid-a"));
                CHECK(notes.loadNoteContent(id) == "hello");
            }
        }

        WHEN("an already-cached note is updated")
        {
            notes.writeCachedNote("uuid-a", "first");
            idsSpy.clear();
            contentSpy.clear();

            notes.writeCachedNote("uuid-a", "second");

            THEN("only its content is reported as changed")
            {
                // An open window reloads; the note list is untouched, so
                // reconcile has no reason to run.
                CHECK(contentSpy.count() == 1);
                CHECK(idsSpy.count() == 0);
                CHECK(notes.loadNoteContent(TickTickModule::makeNoteId("uuid-a")) == "second");
            }
        }

        WHEN("an already-cached note is rewritten with byte-identical content")
        {
            notes.writeCachedNote("uuid-a", "first");
            idsSpy.clear();
            contentSpy.clear();

            const QString id = notes.writeCachedNote("uuid-a", "first");

            THEN("nothing at all is announced")
            {
                // A pull that found nothing changed rewrites the same text it
                // already had. Announcing that as an external change made the
                // open note reload itself through setMarkdown(), rebuilding the
                // whole document — so every markdown style and every spellcheck
                // underline visibly blinked off and back on, hourly and on every
                // focus-in ("a pull will cause a rerender of the note, meaning
                // styling flashes on and off, spellcheck does as well").
                CHECK(contentSpy.count() == 0);
                CHECK(idsSpy.count() == 0);
            }

            THEN("the note is still there, and its id is still reported")
            {
                // Skipping the signal must not read as failure to the caller.
                CHECK(id == TickTickModule::makeNoteId("uuid-a"));
                CHECK(notes.loadNoteContent(id) == "first");
            }
        }

    }
}

SCENARIO("A local edit is cached immediately and reported for upload",
         "[ticktick][notes]")
{
    GIVEN("a cached note")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickNotes notes(tempDir.filePath("cache"));
        notes.writeCachedNote("uuid-a", "Title\noriginal");

        const QString noteId = TickTickModule::makeNoteId("uuid-a");
        QSignalSpy editedSpy(&notes, &TickTickNotes::noteEditedLocally);
        QSignalSpy externalSpy(&notes, &TickTickNotes::noteContentChangedExternally);

        WHEN("it is edited")
        {
            const bool accepted = notes.saveNoteContent(noteId, "Title\nedited");

            THEN("the edit is accepted, so NoteStore does not write a local file")
            {
                // Returning false here would leave the edit unsaved; falling
                // through to NoteStore's local path would create a stray .md.
                CHECK(accepted);
            }

            THEN("the new text is already on disk before any upload happens")
            {
                // The cache write is atomic and local, so a crash or a dead
                // network loses nothing — the upload is only a delivery
                // problem.
                CHECK(notes.loadNoteContent(noteId) == "Title\nedited");
            }

            THEN("it is reported for upload")
            {
                REQUIRE(editedSpy.count() == 1);
                CHECK(editedSpy.at(0).at(0).toString() == "uuid-a");
            }

            THEN("it is NOT reported as an external change")
            {
                // That signal makes an open window reload from storage, which
                // during typing would yank the text out from under the user.
                CHECK(externalSpy.count() == 0);
            }
        }
    }

    GIVEN("a note id this provider does not own")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickNotes notes(tempDir.filePath("cache"));

        THEN("the edit is declined rather than cached under a bogus name")
        {
            CHECK_FALSE(notes.saveNoteContent("some-local-uuid", "text"));
        }
    }
}

SCENARIO("An empty cached note is distinguishable from a missing one",
         "[ticktick][notes]")
{
    GIVEN("a note cached with empty content")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickNotes notes(tempDir.filePath("cache"));
        notes.writeCachedNote("uuid-a", "");

        THEN("it exists, even though its content reads as empty")
        {
            // A bare content read cannot tell these apart, and conflating them
            // is how a failed load becomes an overwrite with empty text.
            CHECK(notes.hasCachedNote("uuid-a"));
            CHECK(notes.loadNoteContent(TickTickModule::makeNoteId("uuid-a")).isEmpty());
            CHECK_FALSE(notes.hasCachedNote("uuid-never-written"));
        }
    }
}

SCENARIO("A hand-edited note id cannot escape the cache directory",
         "[ticktick][notes]")
{
    GIVEN("a provider with a cache directory")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickNotes notes(tempDir.filePath("cache"));

        THEN("path separators and dot-files are refused")
        {
            // These values reach the module from a config file a user can edit,
            // so a traversal attempt must not be able to read or write outside
            // the cache.
            CHECK(notes.writeCachedNote("../escape", "x").isEmpty());
            CHECK(notes.writeCachedNote("sub/dir", "x").isEmpty());
            CHECK(notes.writeCachedNote(".hidden", "x").isEmpty());
            CHECK(notes.writeCachedNote("", "x").isEmpty());
            CHECK(notes.loadNoteContent(TickTickModule::makeNoteId("../escape")).isEmpty());
        }
    }
}
