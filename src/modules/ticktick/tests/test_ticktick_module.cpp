#include <catch2/catch_test_macros.hpp>

#include "TickTickModule.h"

// A TickTick-backed note is a separate KIND of note rather than a local note
// synced to a remote one, and which system owns a given note is carried in the
// note id itself. That works because every id-keyed subsystem in the app
// (window geometry, per-note appearance, KWin rules) treats the id as an opaque
// string — so a prefixed id passes through all of them untouched, while
// anything that needs to know whose note it is can just ask.

SCENARIO("A note id says which system owns the note", "[ticktick]")
{
    GIVEN("an id minted for a TickTick-backed note")
    {
        const QString noteId = TickTickModule::makeNoteId("abc-123");

        THEN("the module recognises it as its own")
        {
            CHECK(TickTickModule::ownsNoteId(noteId));
        }

        THEN("the local uuid can be recovered from it")
        {
            CHECK(TickTickModule::localUuidFor(noteId) == "abc-123");
        }
    }

    GIVEN("a plain local note id")
    {
        const QString noteId = "abc-123";

        THEN("the module does not claim it")
        {
            // Load-bearing: note reconcile treats the local store's enumeration
            // as the complete set of notes, so a local note wrongly claimed
            // here would be excluded from it.
            CHECK_FALSE(TickTickModule::ownsNoteId(noteId));
        }

        THEN("asking for its local uuid yields nothing rather than a guess")
        {
            CHECK(TickTickModule::localUuidFor(noteId).isEmpty());
        }
    }
}

SCENARIO("Note id wrapping is total and reversible", "[ticktick]")
{
    GIVEN("an id that has already been wrapped")
    {
        const QString once = TickTickModule::makeNoteId("abc-123");

        WHEN("it is wrapped a second time")
        {
            const QString twice = TickTickModule::makeNoteId(once);

            THEN("it is returned unchanged rather than double-prefixed")
            {
                // A caller that has lost track of which form it holds cannot
                // corrupt the id by asking for the wrong one.
                CHECK(twice == once);
                CHECK(TickTickModule::localUuidFor(twice) == "abc-123");
            }
        }
    }

    GIVEN("an empty uuid")
    {
        THEN("no id is produced")
        {
            CHECK(TickTickModule::makeNoteId("").isEmpty());
        }
    }

    GIVEN("a bare prefix with no uuid after it")
    {
        const QString noteId = TickTickModule::prefix();

        THEN("it is not treated as an owned note")
        {
            // Rejecting this here is what lets makeNoteId and localUuidFor
            // assume an owned id always has a non-empty uuid.
            CHECK_FALSE(TickTickModule::ownsNoteId(noteId));
            CHECK(TickTickModule::localUuidFor(noteId).isEmpty());
        }
    }

    GIVEN("an id that merely contains the prefix somewhere inside it")
    {
        const QString noteId = "abc-tt:123";

        THEN("it is not treated as an owned note")
        {
            CHECK_FALSE(TickTickModule::ownsNoteId(noteId));
        }
    }
}

SCENARIO("The module reports whether secure token storage is available", "[ticktick]")
{
    GIVEN("a module instance")
    {
        TickTickModule module;

        THEN("it reports the build's qtkeychain support without asserting it")
        {
            // Both answers are valid: qtkeychain is optional, and without it
            // the module degrades to plaintext rather than switching itself
            // off. The point is that the answer is reported rather than
            // assumed, so the auth layer can warn instead of silently writing a
            // 180-day credential to plaintext config.
#ifdef SUOPNOTES_HAVE_QTKEYCHAIN
            CHECK(module.secureStorageAvailable());
#else
            CHECK_FALSE(module.secureStorageAvailable());
#endif
        }
    }
}
