#include <catch2/catch_test_macros.hpp>

#include "core/NoteContentProvider.h"
#include "core/NoteStore.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// A provider lets something other than the notes folder supply notes, while
// every consumer keeps talking only to NoteStore. The routing has to be
// complete: anything that leaks through to the local path would write files
// into the notes folder for notes that do not live there.

namespace {
// Minimal in-memory provider. Claims ids beginning with "x:".
class FakeProvider : public NoteContentProvider
{
public:
    QHash<QString, QString> notes;
    bool acceptsSaves = false;
    bool acceptsDeletes = false;
    QStringList savedIds;
    QStringList deletedIds;

    bool ownsNoteId(const QString &id) const override { return id.startsWith("x:"); }

    QStringList noteIds() const override
    {
        QStringList ids = notes.keys();
        ids.sort();
        return ids;
    }

    QString loadNoteContent(const QString &id) const override { return notes.value(id); }

    bool saveNoteContent(const QString &id, const QString &content) override
    {
        savedIds.append(id);
        if (!acceptsSaves)
            return false;
        notes.insert(id, content);
        return true;
    }

    bool deleteNote(const QString &id) override
    {
        deletedIds.append(id);
        if (!acceptsDeletes)
            return false;
        notes.remove(id);
        return true;
    }
};

constexpr int kPastSaveDebounceMs = 600;
}

SCENARIO("A provider's notes appear alongside local ones", "[note_store][provider]")
{
    GIVEN("a store with one local note and a provider with two")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());
        const QString localId = store.createNote("local note");

        FakeProvider provider;
        provider.notes.insert("x:one", "provider one");
        provider.notes.insert("x:two", "provider two");
        store.registerProvider(&provider);

        THEN("all three are enumerated")
        {
            // Reconcile treats this list as the complete set of notes, so a
            // provider note missing from it would be force-closed on every
            // refresh.
            const QStringList ids = store.noteIds();
            CHECK(ids.size() == 3);
            CHECK(ids.contains(localId));
            CHECK(ids.contains("x:one"));
            CHECK(ids.contains("x:two"));
        }

        THEN("content loads from whichever source owns the note")
        {
            CHECK(store.loadNoteContent(localId) == "local note");
            CHECK(store.loadNoteContent("x:one") == "provider one");
        }

        THEN("a provider note has no file path")
        {
            // Empty is the documented "cannot reveal this" answer, and is
            // already what disables Reveal in File Manager.
            CHECK(store.noteFilePath("x:one").isEmpty());
            CHECK_FALSE(store.noteFilePath(localId).isEmpty());
        }
    }
}

SCENARIO("Registering a provider announces its notes", "[note_store][provider]")
{
    GIVEN("a store and a provider holding a note")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());

        FakeProvider provider;
        provider.notes.insert("x:one", "provider one");

        QSignalSpy idsSpy(&store, &NoteStore::noteIdsChanged);

        WHEN("it is registered")
        {
            store.registerProvider(&provider);

            THEN("the note list is reported as changed")
            {
                // Otherwise the first reconcile runs against a list that omits
                // the provider's notes and closes them immediately.
                CHECK(idsSpy.count() == 1);
            }
        }
    }
}

SCENARIO("A provider's own signals are re-broadcast by the store",
         "[note_store][provider]")
{
    GIVEN("a registered provider")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());

        FakeProvider provider;
        store.registerProvider(&provider);

        QSignalSpy idsSpy(&store, &NoteStore::noteIdsChanged);
        QSignalSpy contentSpy(&store, &NoteStore::noteContentChangedExternally);

        WHEN("the provider's note set changes")
        {
            emit provider.noteIdsChanged();

            THEN("the store reports it as its own")
            {
                // Consumers listen to one object; they never learn a provider
                // exists.
                CHECK(idsSpy.count() == 1);
            }
        }

        WHEN("a provider note changes underneath the app")
        {
            emit provider.noteContentChangedExternally("x:one");

            THEN("the store reports that too, naming the note")
            {
                REQUIRE(contentSpy.count() == 1);
                CHECK(contentSpy.at(0).at(0).toString() == "x:one");
            }
        }
    }
}

SCENARIO("Writes to a provider note never reach the local notes folder",
         "[note_store][provider]")
{
    GIVEN("a provider that declines to save")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());

        FakeProvider provider;
        provider.acceptsSaves = false;
        provider.notes.insert("x:one", "provider one");
        store.registerProvider(&provider);

        QSignalSpy savedSpy(&store, &NoteStore::noteSaved);

        WHEN("that note is saved")
        {
            store.saveNoteContent("x:one", "edited");
            QTest::qWait(kPastSaveDebounceMs);

            THEN("the provider was asked")
            {
                CHECK(provider.savedIds == QStringList{"x:one"});
            }

            THEN("no file was written to the notes folder")
            {
                // Falling through to the local path would fabricate a .md named
                // after a note the provider owns, which would then enumerate as
                // a duplicate local note.
                const QStringList files =
                    QDir(tempDir.path()).entryList({"*.md"}, QDir::Files);
                CHECK(files.isEmpty());
            }

            THEN("no save is announced, because nothing was persisted")
            {
                CHECK(savedSpy.count() == 0);
            }
        }
    }

    GIVEN("a provider that accepts saves")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());

        FakeProvider provider;
        provider.acceptsSaves = true;
        provider.notes.insert("x:one", "provider one");
        store.registerProvider(&provider);

        WHEN("that note is saved")
        {
            store.saveNoteContent("x:one", "edited");
            QTest::qWait(kPastSaveDebounceMs);

            THEN("the provider holds the new content")
            {
                CHECK(store.loadNoteContent("x:one") == "edited");
            }

            THEN("the notes folder is still untouched")
            {
                const QStringList files =
                    QDir(tempDir.path()).entryList({"*.md"}, QDir::Files);
                CHECK(files.isEmpty());
            }
        }
    }
}

SCENARIO("Deleting a provider note does not touch the notes folder",
         "[note_store][provider]")
{
    GIVEN("a local note and a provider note")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());
        const QString localId = store.createNote("local note");

        FakeProvider provider;
        provider.acceptsDeletes = true;
        provider.notes.insert("x:one", "provider one");
        store.registerProvider(&provider);

        WHEN("the provider note is deleted")
        {
            store.deleteNote("x:one");

            THEN("the provider handled it")
            {
                CHECK(provider.deletedIds == QStringList{"x:one"});
                CHECK_FALSE(store.noteIds().contains("x:one"));
            }

            THEN("the local note is untouched")
            {
                // The local delete path removes a file and sweeps images based
                // on content — running it for a note it does not own could take
                // an unrelated image with it.
                CHECK(store.noteIds().contains(localId));
                CHECK(store.loadNoteContent(localId) == "local note");
            }
        }
    }
}

SCENARIO("Local notes behave exactly as before when a provider is present",
         "[note_store][provider]")
{
    GIVEN("a store with a registered provider")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        NoteStore store;
        store.setNotesFolder(tempDir.path());

        FakeProvider provider;
        provider.notes.insert("x:one", "provider one");
        store.registerProvider(&provider);

        WHEN("a local note is created and saved")
        {
            const QString id = store.createNote("first");
            QSignalSpy savedSpy(&store, &NoteStore::noteSaved);

            store.saveNoteContent(id, "second");
            QTest::qWait(kPastSaveDebounceMs);

            THEN("it still debounces, writes and announces the save")
            {
                CHECK(savedSpy.count() == 1);
                CHECK(store.loadNoteContent(id) == "second");
                CHECK_FALSE(store.noteFilePath(id).isEmpty());
            }
        }
    }
}
