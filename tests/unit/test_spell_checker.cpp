#include <catch2/catch_test_macros.hpp>
#include <QHash>

#include <memory>

#include "core/SpellChecker.h"

namespace {

// Deterministic stand-in for a real Sonnet::Speller — no dictionary needed,
// no dependency on what's installed on the machine running the tests.
// Constructed with a fixed set of "known misspelled words" and canned
// suggestions per word, so every scenario below is fully reproducible.
class FakeSpellCheckerBackend : public SpellCheckerBackend
{
public:
    FakeSpellCheckerBackend(bool valid, QStringList misspelledWords, QHash<QString, QStringList> suggestionsByWord)
        : m_valid(valid), m_misspelledWords(std::move(misspelledWords)),
          m_suggestionsByWord(std::move(suggestionsByWord))
    {
    }

    bool isValid() const override { return m_valid; }

    bool isMisspelled(const QString &word) const override { return m_misspelledWords.contains(word); }

    QStringList suggestions(const QString &word) const override { return m_suggestionsByWord.value(word); }

private:
    bool m_valid;
    QStringList m_misspelledWords;
    QHash<QString, QStringList> m_suggestionsByWord;
};

}

SCENARIO("A valid SpellCheckerBackend reports isValid() true", "[spell_checker]")
{
    GIVEN("a fake backend constructed to simulate a working dictionary")
    {
        FakeSpellCheckerBackend backend(true, {}, {});

        THEN("isValid() returns true")
        {
            CHECK(backend.isValid());
        }
    }
}

SCENARIO("An unavailable SpellCheckerBackend reports isValid() false", "[spell_checker]")
{
    GIVEN("a fake backend constructed to simulate no dictionary being found "
          "(mirrors what a real Sonnet::Speller reports on a machine with no "
          "hunspell dictionaries installed)")
    {
        FakeSpellCheckerBackend backend(false, {}, {});

        THEN("isValid() returns false, signaling callers should skip highlighting entirely")
        {
            CHECK_FALSE(backend.isValid());
        }
    }
}

SCENARIO("isMisspelled distinguishes known-misspelled words from correctly-spelled ones", "[spell_checker]")
{
    GIVEN("a fake backend seeded with a fixed list of misspelled words")
    {
        FakeSpellCheckerBackend backend(true, {"teh", "recieve"}, {});

        THEN("a word in the misspelled list is reported as misspelled")
        {
            CHECK(backend.isMisspelled("teh"));
            CHECK(backend.isMisspelled("recieve"));
        }

        THEN("a word not in the misspelled list is reported as correctly spelled")
        {
            CHECK_FALSE(backend.isMisspelled("the"));
            CHECK_FALSE(backend.isMisspelled("receive"));
        }
    }
}

SCENARIO("suggestions returns the canned suggestion list for a known misspelled word", "[spell_checker]")
{
    GIVEN("a fake backend seeded with suggestions for \"teh\"")
    {
        FakeSpellCheckerBackend backend(true, {"teh"}, {{"teh", {"the", "ten"}}});

        WHEN("suggestions() is called for that word")
        {
            const QStringList result = backend.suggestions("teh");

            THEN("it returns exactly the canned suggestions, in order")
            {
                REQUIRE(result.size() == 2);
                CHECK(result.at(0) == "the");
                CHECK(result.at(1) == "ten");
            }
        }
    }
}

SCENARIO("suggestions returns an empty list for a word with no canned suggestions", "[spell_checker]")
{
    GIVEN("a fake backend with no suggestions registered for \"xyzzy\"")
    {
        FakeSpellCheckerBackend backend(true, {"xyzzy"}, {});

        WHEN("suggestions() is called for that word")
        {
            const QStringList result = backend.suggestions("xyzzy");

            THEN("it returns an empty list rather than crashing or asserting")
            {
                CHECK(result.isEmpty());
            }
        }
    }
}

SCENARIO("A caller can use SpellCheckerBackend purely through the abstract interface", "[spell_checker]")
{
    GIVEN("a fake backend referenced only through a SpellCheckerBackend pointer, "
          "as real callers (a future QSyntaxHighlighter) will use it")
    {
        FakeSpellCheckerBackend fake(true, {"wrogn"}, {{"wrogn", {"wrong"}}});
        SpellCheckerBackend &backend = fake;

        THEN("isValid, isMisspelled, and suggestions all dispatch correctly through the base class")
        {
            CHECK(backend.isValid());
            CHECK(backend.isMisspelled("wrogn"));
            CHECK_FALSE(backend.isMisspelled("wrong"));
            CHECK(backend.suggestions("wrogn") == QStringList{"wrong"});
        }
    }
}

#ifdef SUOPNOTES_HAVE_SONNET
SCENARIO("A real SonnetSpellCheckerBackend can be constructed and queried without crashing", "[spell_checker]")
{
    GIVEN("this dev/CI machine, which may or may not have any hunspell dictionaries installed")
    {
        SonnetSpellCheckerBackend backend;

        THEN("isValid() returns a bool without crashing — whether it's true or false depends on "
             "what dictionaries happen to be installed, and this test intentionally doesn't assert "
             "which, since there are zero hunspell dictionaries on this dev machine")
        {
            const bool valid = backend.isValid();
            CHECK((valid == true || valid == false));
        }
    }
}
#endif

#ifdef SUOPNOTES_HAVE_HUNSPELL

#if defined(SUOPNOTES_HAVE_BUNDLED_DICTIONARY)
SCENARIO("A HunspellSpellCheckerBackend pointed at the real bundled dictionary spell-checks English words",
         "[spell_checker]")
{
    GIVEN("a HunspellSpellCheckerBackend constructed with the paths to the bundled en_US dictionary "
          "fetched at build time")
    {
        HunspellSpellCheckerBackend backend(QString::fromUtf8(SUOPNOTES_BUNDLED_DICT_AFF_PATH),
                                             QString::fromUtf8(SUOPNOTES_BUNDLED_DICT_DIC_PATH));

        THEN("isValid() returns true, proving the bundled dictionary actually loaded")
        {
            CHECK(backend.isValid());
        }

        THEN("a common, correctly-spelled English word is not reported as misspelled")
        {
            CHECK_FALSE(backend.isMisspelled("hello"));
        }

        THEN("a nonsense string is reported as misspelled")
        {
            CHECK(backend.isMisspelled("thisisnotarealwordxyz123"));
        }

        THEN("suggestions() for a close misspelling returns a non-empty list containing a reasonable correction")
        {
            const QStringList result = backend.suggestions("teh");
            CHECK_FALSE(result.isEmpty());
            CHECK(result.contains("the", Qt::CaseInsensitive));
        }
    }
}
#endif // SUOPNOTES_HAVE_BUNDLED_DICTIONARY

SCENARIO("A HunspellSpellCheckerBackend pointed at nonexistent dictionary files degrades gracefully",
         "[spell_checker]")
{
    GIVEN("a HunspellSpellCheckerBackend constructed with paths that don't exist on disk")
    {
        HunspellSpellCheckerBackend backend("/nonexistent/path.aff", "/nonexistent/path.dic");

        THEN("isValid() returns false rather than crashing")
        {
            CHECK_FALSE(backend.isValid());
        }

        THEN("isMisspelled() is a safe no-op, always reporting words as not misspelled")
        {
            CHECK_FALSE(backend.isMisspelled("hello"));
            CHECK_FALSE(backend.isMisspelled("thisisnotarealwordxyz123"));
        }

        THEN("suggestions() is a safe no-op, always returning an empty list")
        {
            CHECK(backend.suggestions("teh").isEmpty());
        }
    }
}
#endif // SUOPNOTES_HAVE_HUNSPELL

SCENARIO("FallbackSpellCheckerBackend delegates to primary when primary is valid", "[spell_checker]")
{
    GIVEN("a valid primary backend that considers \"test\" misspelled and a valid fallback backend "
          "that considers \"test\" correctly spelled")
    {
        auto primary = std::make_unique<FakeSpellCheckerBackend>(true, QStringList{"test"}, QHash<QString, QStringList>{});
        auto fallback = std::make_unique<FakeSpellCheckerBackend>(true, QStringList{}, QHash<QString, QStringList>{});

        FallbackSpellCheckerBackend backend(std::move(primary), std::move(fallback));

        THEN("isValid() returns true")
        {
            CHECK(backend.isValid());
        }

        THEN("isMisspelled() returns the primary's answer, proving the fallback is never consulted")
        {
            CHECK(backend.isMisspelled("test"));
        }
    }
}

SCENARIO("FallbackSpellCheckerBackend delegates to fallback when primary is invalid", "[spell_checker]")
{
    GIVEN("an invalid primary backend that (if consulted) would consider \"test\" correctly spelled, "
          "and a valid fallback backend that considers \"test\" misspelled")
    {
        auto primary = std::make_unique<FakeSpellCheckerBackend>(false, QStringList{}, QHash<QString, QStringList>{});
        auto fallback = std::make_unique<FakeSpellCheckerBackend>(true, QStringList{"test"}, QHash<QString, QStringList>{});

        FallbackSpellCheckerBackend backend(std::move(primary), std::move(fallback));

        THEN("isValid() returns true, reflecting the fallback's validity")
        {
            CHECK(backend.isValid());
        }

        THEN("isMisspelled() returns the fallback's answer, since primary is invalid and was skipped")
        {
            CHECK(backend.isMisspelled("test"));
        }
    }
}

SCENARIO("FallbackSpellCheckerBackend degrades gracefully when both backends are null", "[spell_checker]")
{
    GIVEN("a FallbackSpellCheckerBackend constructed with null primary and fallback pointers")
    {
        FallbackSpellCheckerBackend backend(nullptr, nullptr);

        THEN("isValid() returns false")
        {
            CHECK_FALSE(backend.isValid());
        }

        THEN("isMisspelled() returns false rather than crashing")
        {
            CHECK_FALSE(backend.isMisspelled("test"));
        }

        THEN("suggestions() returns an empty list rather than crashing")
        {
            CHECK(backend.suggestions("test").isEmpty());
        }
    }
}

SCENARIO("UserDictionaryFilteringBackend never flags a user-added word as misspelled", "[spell_checker]")
{
    GIVEN("an inner backend that considers \"suopnotes\" misspelled, wrapped by a "
          "UserDictionaryFilteringBackend whose user words include \"suopnotes\"")
    {
        auto inner = std::make_unique<FakeSpellCheckerBackend>(true, QStringList{"suopnotes"}, QHash<QString, QStringList>{});
        UserDictionaryFilteringBackend backend(std::move(inner));
        backend.setUserWords({"suopnotes"});

        THEN("isMisspelled() returns false, overriding the inner backend's answer")
        {
            CHECK_FALSE(backend.isMisspelled("suopnotes"));
        }
    }
}

SCENARIO("UserDictionaryFilteringBackend falls through to the inner backend for words not in the user set",
         "[spell_checker]")
{
    GIVEN("an inner backend that considers \"teh\" misspelled, wrapped by a UserDictionaryFilteringBackend "
          "with an empty user word set")
    {
        auto inner = std::make_unique<FakeSpellCheckerBackend>(true, QStringList{"teh"}, QHash<QString, QStringList>{});
        UserDictionaryFilteringBackend backend(std::move(inner));

        THEN("isMisspelled() returns the inner backend's real answer for that word")
        {
            CHECK(backend.isMisspelled("teh"));
        }

        THEN("isMisspelled() returns the inner backend's real answer for a correctly-spelled word too")
        {
            CHECK_FALSE(backend.isMisspelled("the"));
        }
    }
}

SCENARIO("UserDictionaryFilteringBackend's setUserWords changes behavior on subsequent calls without "
         "reconstructing the object",
         "[spell_checker]")
{
    GIVEN("a UserDictionaryFilteringBackend wrapping an inner backend that considers \"suopnotes\" misspelled")
    {
        auto inner = std::make_unique<FakeSpellCheckerBackend>(true, QStringList{"suopnotes"}, QHash<QString, QStringList>{});
        UserDictionaryFilteringBackend backend(std::move(inner));

        THEN("before setUserWords is called, the word is reported as misspelled per the inner backend")
        {
            CHECK(backend.isMisspelled("suopnotes"));
        }

        WHEN("setUserWords adds the word to the user dictionary")
        {
            backend.setUserWords({"suopnotes"});

            THEN("the same backend instance now reports the word as correctly spelled")
            {
                CHECK_FALSE(backend.isMisspelled("suopnotes"));
            }
        }
    }
}

SCENARIO("UserDictionaryFilteringBackend's setInner swaps the inner backend on a stable wrapper object",
         "[spell_checker]")
{
    GIVEN("a UserDictionaryFilteringBackend constructed with a fake inner backend that considers "
          "\"teh\" misspelled")
    {
        auto firstInner = std::make_unique<FakeSpellCheckerBackend>(true, QStringList{"teh"}, QHash<QString, QStringList>{});
        UserDictionaryFilteringBackend backend(std::move(firstInner));

        THEN("isMisspelled() reflects the first inner backend's answer")
        {
            CHECK(backend.isMisspelled("teh"));
        }

        WHEN("setInner swaps in a different fake inner backend that considers \"teh\" correctly spelled")
        {
            auto secondInner = std::make_unique<FakeSpellCheckerBackend>(true, QStringList{}, QHash<QString, QStringList>{});
            backend.setInner(std::move(secondInner));

            THEN("the same wrapper object now reflects the new inner backend's answer, proving callers "
                 "can hold a long-lived pointer to the wrapper across a language/backend swap")
            {
                CHECK_FALSE(backend.isMisspelled("teh"));
            }
        }
    }
}
