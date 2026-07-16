#include <catch2/catch_test_macros.hpp>

#include <QTextDocument>

#include "core/TextSearch.h"

namespace
{
// The matched substrings, in order — lets an assertion say "it found these
// words" without hand-counting every position.
QStringList matchedTexts(const QString &text, const QList<TextSearch::Match> &matches)
{
    QStringList result;
    for (const TextSearch::Match &m : matches)
        result << text.mid(m.start, m.end - m.start);
    return result;
}
}

SCENARIO("Literal search finds every occurrence at its exact position", "[text_search]")
{
    GIVEN("text with three occurrences of a word")
    {
        const QString text = QStringLiteral("cat scattered, cat napped: cat");

        WHEN("searching for it literally")
        {
            const auto matches = TextSearch::allMatches(text, QStringLiteral("cat"), false, false);

            THEN("all three standalone-and-embedded occurrences are found in order")
            {
                REQUIRE(matches.size() == 4); // "cat", the one inside "scattered", "cat", "cat"
                CHECK(matches[0].start == 0);
                CHECK(matches[0].end == 3);
                CHECK(matches[1].start == 5); // "s[cat]tered"
                CHECK(matches[1].end == 8);
                CHECK(matches[2].start == 15);
                CHECK(matches[2].end == 18);
                CHECK(matches[3].start == 27);
                CHECK(matches[3].end == 30);
            }
        }
    }
}

SCENARIO("Search is case-insensitive by default, case-sensitive on request", "[text_search]")
{
    GIVEN("text mixing cases of the same word")
    {
        const QString text = QStringLiteral("Fish fish FISH");

        WHEN("searching without case sensitivity")
        {
            const auto matches = TextSearch::allMatches(text, QStringLiteral("fish"), false, false);

            THEN("every casing matches")
            {
                CHECK(matchedTexts(text, matches) == QStringList({"Fish", "fish", "FISH"}));
            }
        }

        WHEN("searching case-sensitively")
        {
            const auto matches = TextSearch::allMatches(text, QStringLiteral("fish"), false, true);

            THEN("only the exact-case occurrence matches")
            {
                REQUIRE(matches.size() == 1);
                CHECK(matches[0].start == 5);
                CHECK(matches[0].end == 9);
            }
        }
    }
}

SCENARIO("Literal search treats regex metacharacters as plain text", "[text_search]")
{
    GIVEN("text containing both a literal 'a.b' and an 'axb' a regex dot would match")
    {
        const QString text = QStringLiteral("axb a.b");

        WHEN("searching for 'a.b' literally")
        {
            const auto matches = TextSearch::allMatches(text, QStringLiteral("a.b"), false, false);

            THEN("only the literal 'a.b' is found — the dot is not a wildcard")
            {
                REQUIRE(matches.size() == 1);
                CHECK(matches[0].start == 4);
                CHECK(matches[0].end == 7);
            }
        }

        WHEN("searching for 'a.b' as a regex")
        {
            const auto matches = TextSearch::allMatches(text, QStringLiteral("a.b"), true, false);

            THEN("the dot is a wildcard and both match")
            {
                CHECK(matchedTexts(text, matches) == QStringList({"axb", "a.b"}));
            }
        }
    }
}

SCENARIO("Regex search finds matches with correct positions across multiline text", "[text_search]")
{
    GIVEN("a multi-line document's plain text (paragraph separators as \\n)")
    {
        QTextDocument doc;
        doc.setPlainText("room 12\nno numbers here\nfloor 345");
        const QString text = doc.toPlainText();

        WHEN("searching for runs of digits")
        {
            const auto matches = TextSearch::allMatches(text, QStringLiteral("\\d+"), true, false);

            THEN("both numbers are found, with positions valid past the newlines")
            {
                REQUIRE(matches.size() == 2);
                CHECK(matches[0].start == 5);
                CHECK(matches[0].end == 7);
                CHECK(text.mid(matches[1].start, matches[1].end - matches[1].start) == "345");
                CHECK(matches[1].start == 30);
                CHECK(matches[1].end == 33);
            }
        }
    }
}

SCENARIO("Invalid and empty patterns yield no matches instead of erroring", "[text_search]")
{
    GIVEN("some ordinary text")
    {
        const QString text = QStringLiteral("anything at all");

        WHEN("searching with a malformed regex")
        {
            const auto matches = TextSearch::allMatches(text, QStringLiteral("[unclosed"), true, false);

            THEN("there are no matches and the pattern reports invalid")
            {
                CHECK(matches.isEmpty());
                CHECK_FALSE(TextSearch::patternValid(QStringLiteral("[unclosed"), true));
            }
        }

        WHEN("the same malformed string is searched literally")
        {
            THEN("it is a perfectly valid literal pattern")
            {
                CHECK(TextSearch::patternValid(QStringLiteral("[unclosed"), false));
            }
        }

        WHEN("searching with an empty pattern")
        {
            THEN("neither mode matches anything, but neither reports an error state")
            {
                CHECK(TextSearch::allMatches(text, QString(), false, false).isEmpty());
                CHECK(TextSearch::allMatches(text, QString(), true, false).isEmpty());
                CHECK(TextSearch::patternValid(QString(), false));
                CHECK(TextSearch::patternValid(QString(), true));
            }
        }
    }
}

SCENARIO("A pattern that can match zero characters terminates instead of looping", "[text_search]")
{
    GIVEN("text where 'x*' matches zero-length almost everywhere")
    {
        const QString text = QStringLiteral("axb");

        WHEN("searching for 'x*' as a regex")
        {
            const auto matches = TextSearch::allMatches(text, QStringLiteral("x*"), true, false);

            THEN("the search returns a finite, position-ordered list including the real 'x'")
            {
                // Zero-length at 0, "x" at 1-2, zero-length at 2 and 3 (end).
                REQUIRE(matches.size() == 4);
                CHECK(matches[0].start == 0);
                CHECK(matches[0].end == 0);
                CHECK(matches[1].start == 1);
                CHECK(matches[1].end == 2);
                CHECK(matches[3].start == 3);
                CHECK(matches[3].end == 3);
            }
        }
    }
}

SCENARIO("replaceMatch replaces exactly the given range", "[text_search]")
{
    GIVEN("a document with two occurrences of a word")
    {
        QTextDocument doc;
        doc.setPlainText("hello world, hello moon");

        WHEN("replacing only the second occurrence's range")
        {
            const auto replaced = TextSearch::replaceMatch(&doc, 13, 18, QStringLiteral("hello"),
                                                           QStringLiteral("goodbye"), false, false);

            THEN("only that occurrence changes, and the returned range covers the new text")
            {
                CHECK(doc.toPlainText() == "hello world, goodbye moon");
                REQUIRE(replaced.has_value());
                CHECK(replaced->start == 13);
                CHECK(replaced->end == 20);
            }
        }
    }
}

SCENARIO("replaceMatch in regex mode expands capture references and $$", "[text_search]")
{
    GIVEN("a document with a 'key=value' pair")
    {
        QTextDocument doc;
        doc.setPlainText("width=200");

        WHEN("replacing it with the captures swapped via $1/$2")
        {
            const auto replaced = TextSearch::replaceMatch(&doc, 0, 9, QStringLiteral("(\\w+)=(\\w+)"),
                                                           QStringLiteral("$2 is the $1"), true, false);

            THEN("the backreferences expand to the captured texts")
            {
                CHECK(doc.toPlainText() == "200 is the width");
                REQUIRE(replaced.has_value());
                CHECK(replaced->start == 0);
                CHECK(replaced->end == 16);
            }
        }
    }

    GIVEN("a document with a number")
    {
        QTextDocument doc;
        doc.setPlainText("price: 42");

        WHEN("replacing the number with '$$$1' — an escaped dollar then capture 1")
        {
            const auto replaced = TextSearch::replaceMatch(&doc, 7, 9, QStringLiteral("(\\d+)"),
                                                           QStringLiteral("$$$1"), true, false);

            THEN("$$ becomes one literal dollar sign in front of the capture")
            {
                CHECK(doc.toPlainText() == "price: $42");
                REQUIRE(replaced.has_value());
            }
        }
    }

    GIVEN("a range that no longer matches the pattern (a stale match)")
    {
        QTextDocument doc;
        doc.setPlainText("letters only");

        WHEN("replaceMatch is asked to replace it as a \\d+ match")
        {
            const auto replaced = TextSearch::replaceMatch(&doc, 0, 7, QStringLiteral("\\d+"),
                                                           QStringLiteral("$1"), true, false);

            THEN("nothing is replaced and no range is returned")
            {
                CHECK_FALSE(replaced.has_value());
                CHECK(doc.toPlainText() == "letters only");
            }
        }
    }
}

SCENARIO("replaceAll replaces every occurrence as exactly one undo step", "[text_search]")
{
    GIVEN("a multi-line document with several occurrences")
    {
        QTextDocument doc;
        doc.setPlainText("one fish\ntwo fish\nred fish");
        const QString original = doc.toPlainText();
        // setPlainText clears undo history, so the undo stack starts empty.
        REQUIRE_FALSE(doc.isUndoAvailable());

        WHEN("replacing all of them")
        {
            const int count = TextSearch::replaceAll(&doc, QStringLiteral("fish"),
                                                     QStringLiteral("cat"), false, false);

            THEN("every occurrence is replaced and the count reports it")
            {
                CHECK(count == 3);
                CHECK(doc.toPlainText() == "one cat\ntwo cat\nred cat");
            }

            THEN("a single undo restores the original text verbatim")
            {
                REQUIRE(doc.isUndoAvailable());
                doc.undo();
                CHECK(doc.toPlainText() == original);
                // Exactly ONE step: nothing else is left to undo. (No undo
                // drain loop here on purpose — an unbounded one once hung CI.)
                CHECK_FALSE(doc.isUndoAvailable());
            }
        }
    }
}

SCENARIO("replaceAll in regex mode expands captures per match", "[text_search]")
{
    GIVEN("a document with several key=value pairs")
    {
        QTextDocument doc;
        doc.setPlainText("a=1 b=2");

        WHEN("swapping every pair with $2=$1")
        {
            const int count = TextSearch::replaceAll(&doc, QStringLiteral("(\\w+)=(\\w+)"),
                                                     QStringLiteral("$2=$1"), true, false);

            THEN("each match expands its own captures")
            {
                CHECK(count == 2);
                CHECK(doc.toPlainText() == "1=a 2=b");
            }
        }
    }
}

SCENARIO("replaceAll terminates when the replacement contains the pattern", "[text_search]")
{
    GIVEN("a document full of 'a's")
    {
        QTextDocument doc;
        doc.setPlainText("banana");

        WHEN("replacing 'a' with 'aa'")
        {
            const int count = TextSearch::replaceAll(&doc, QStringLiteral("a"),
                                                     QStringLiteral("aa"), false, false);

            THEN("only the original occurrences are doubled — new ones aren't re-matched")
            {
                CHECK(count == 3);
                CHECK(doc.toPlainText() == "baanaanaa");
            }
        }
    }
}

SCENARIO("replaceAll matching nothing edits nothing", "[text_search]")
{
    GIVEN("a document without the pattern")
    {
        QTextDocument doc;
        doc.setPlainText("nothing to see");

        WHEN("replaceAll runs")
        {
            const int count = TextSearch::replaceAll(&doc, QStringLiteral("zebra"),
                                                     QStringLiteral("horse"), false, false);

            THEN("the count is zero, the text is untouched, and no undo step was recorded")
            {
                CHECK(count == 0);
                CHECK(doc.toPlainText() == "nothing to see");
                CHECK_FALSE(doc.isUndoAvailable());
            }
        }
    }
}

SCENARIO("replaceMatch on a numbered-list line leaves the rest of the line intact", "[text_search]")
{
    GIVEN("a document holding a hand-rolled numbered list")
    {
        QTextDocument doc;
        doc.setPlainText("1. first item\n2. second item");

        WHEN("replacing the word 'first' inside the first item")
        {
            const auto replaced = TextSearch::replaceMatch(&doc, 3, 8, QStringLiteral("first"),
                                                           QStringLiteral("earliest"), false, false);

            THEN("the markers and every other character survive verbatim")
            {
                REQUIRE(replaced.has_value());
                CHECK(doc.toPlainText() == "1. earliest item\n2. second item");
            }
        }
    }
}
