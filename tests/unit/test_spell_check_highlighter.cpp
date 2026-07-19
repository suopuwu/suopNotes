#include <catch2/catch_test_macros.hpp>

#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include "core/SpellCheckHighlighter.h"
#include "core/SpellChecker.h"
#include "core/TextFormatting.h"

namespace
{

// Deterministic stand-in for a real Sonnet::Speller, mirroring
// test_spell_checker.cpp's own FakeSpellCheckerBackend. Not shared from
// there directly — that class lives in an anonymous namespace private to
// that translation unit, and this repo has no shared-test-helpers
// convention (checked: no "core/testing" style header, no other test file
// includes another test .cpp) — a small local copy is simpler than
// introducing one for a single reused fixture.
class FakeSpellCheckerBackend : public SpellCheckerBackend
{
public:
    FakeSpellCheckerBackend(bool valid, QStringList misspelledWords)
        : m_valid(valid), m_misspelledWords(std::move(misspelledWords))
    {
    }

    bool isValid() const override { return m_valid; }

    bool isMisspelled(const QString &word) const override { return m_misspelledWords.contains(word); }

    QStringList suggestions(const QString &) const override { return {}; }

private:
    bool m_valid;
    QStringList m_misspelledWords;
};

// QSyntaxHighlighter's setFormat() results land in the block's QTextLayout
// format ranges — a separate mechanism from the document's own
// QTextCursor::charFormat() (which reflects real character formatting like
// toggleBold, not syntax-highlighter overlays). This walks those ranges to
// find whichever one (if any) covers `position`, mirroring how Qt itself
// composites them at paint time. A default-constructed QTextCharFormat
// (no underline) is returned when nothing covers that position.
QTextCharFormat formatAt(const QTextBlock &block, int position)
{
    const QList<QTextLayout::FormatRange> ranges = block.layout()->formats();
    for (const QTextLayout::FormatRange &range : ranges) {
        if (position >= range.start && position < range.start + range.length)
            return range.format;
    }
    return QTextCharFormat();
}

bool isFlaggedMisspelled(const QTextBlock &block, int position)
{
    return formatAt(block, position).underlineStyle() == QTextCharFormat::SpellCheckUnderline;
}

}

SCENARIO("A misspelled word gets a wavy red underline over its exact character range", "[spell_check_highlighter]")
{
    GIVEN("a document containing one word the fake backend considers misspelled")
    {
        QTextDocument doc;
        doc.setPlainText("This has a mistaek in it");
        FakeSpellCheckerBackend backend(true, {"mistaek"});

        WHEN("a SpellCheckHighlighter is attached to the document")
        {
            SpellCheckHighlighter highlighter(&doc, &backend);
            // QSyntaxHighlighter's own initial highlight-on-attach (from
            // inside its constructor/setDocument) is queued via the Qt event
            // loop, which a real app gets for free once its event loop is
            // pumped, but this test runs no event loop between statements.
            // rehighlight() runs the same pass synchronously and is exactly
            // what setActive(true) below already relies on for the same
            // reason, so calling it explicitly here just makes the test
            // deterministic without waiting on QCoreApplication::processEvents.
            highlighter.rehighlight();

            const QTextBlock block = doc.begin();
            const int start = block.text().indexOf("mistaek");
            REQUIRE(start >= 0);

            THEN("every character of the misspelled word carries SpellCheckUnderline in red")
            {
                for (int i = start; i < start + 7; ++i) {
                    const QTextCharFormat fmt = formatAt(block, i);
                    CHECK(fmt.underlineStyle() == QTextCharFormat::SpellCheckUnderline);
                    CHECK(fmt.underlineColor() == QColor(Qt::red));
                }
            }

            THEN("the space immediately before the misspelled word is not flagged")
            {
                CHECK_FALSE(isFlaggedMisspelled(block, start - 1));
            }

            THEN("the space immediately after the misspelled word is not flagged")
            {
                CHECK_FALSE(isFlaggedMisspelled(block, start + 7));
            }
        }
    }
}

SCENARIO("A correctly-spelled word gets no special formatting", "[spell_check_highlighter]")
{
    GIVEN("a document whose only word is one the fake backend does not consider misspelled")
    {
        QTextDocument doc;
        doc.setPlainText("This text is fine");
        FakeSpellCheckerBackend backend(true, {"mistaek"});

        WHEN("a SpellCheckHighlighter is attached to the document")
        {
            SpellCheckHighlighter highlighter(&doc, &backend);
            highlighter.rehighlight(); // see comment in the scenario above

            THEN("no position in the block carries SpellCheckUnderline")
            {
                const QTextBlock block = doc.begin();
                for (int i = 0; i < block.text().length(); ++i) {
                    CHECK_FALSE(isFlaggedMisspelled(block, i));
                }
            }
        }
    }
}

SCENARIO("setActive(false) clears underline formatting and stops flagging new text; setActive(true) resumes it",
         "[spell_check_highlighter]")
{
    GIVEN("a document with a misspelled word, already flagged by an active highlighter")
    {
        QTextDocument doc;
        doc.setPlainText("A mistaek here");
        FakeSpellCheckerBackend backend(true, {"mistaek", "recieve"});
        SpellCheckHighlighter highlighter(&doc, &backend);
        highlighter.rehighlight(); // see comment in the first scenario above

        const int wordStart = doc.begin().text().indexOf("mistaek");
        REQUIRE(wordStart >= 0);
        REQUIRE(isFlaggedMisspelled(doc.begin(), wordStart));

        WHEN("setActive(false) is called")
        {
            highlighter.setActive(false);

            THEN("isActive() reports false")
            {
                CHECK_FALSE(highlighter.isActive());
            }

            THEN("the previously-flagged word no longer carries SpellCheckUnderline")
            {
                CHECK_FALSE(isFlaggedMisspelled(doc.begin(), wordStart));
            }

            AND_WHEN("more misspelled text is typed while inactive")
            {
                QTextCursor cursor(&doc);
                cursor.movePosition(QTextCursor::End);
                cursor.insertText(" recieve");

                THEN("the newly typed misspelled word is not flagged either")
                {
                    const QTextBlock block = doc.begin();
                    const int newWordStart = block.text().indexOf("recieve");
                    REQUIRE(newWordStart >= 0);
                    CHECK_FALSE(isFlaggedMisspelled(block, newWordStart));
                }
            }

            AND_WHEN("setActive(true) is called again")
            {
                highlighter.setActive(true);

                THEN("isActive() reports true")
                {
                    CHECK(highlighter.isActive());
                }

                THEN("the word is flagged again")
                {
                    CHECK(isFlaggedMisspelled(doc.begin(), wordStart));
                }
            }
        }
    }
}

SCENARIO("When the backend has no working dictionary, nothing gets flagged even for otherwise-misspelled words",
         "[spell_check_highlighter]")
{
    GIVEN("a fake backend simulating no hunspell dictionary installed (isValid() false), "
          "seeded with a word it would otherwise call misspelled")
    {
        QTextDocument doc;
        doc.setPlainText("This has a mistaek in it");
        FakeSpellCheckerBackend backend(false, {"mistaek"});

        WHEN("a SpellCheckHighlighter is attached to the document")
        {
            SpellCheckHighlighter highlighter(&doc, &backend);
            highlighter.rehighlight(); // see comment in the first scenario above

            THEN("no position in the block is flagged, degrading to a complete no-op")
            {
                const QTextBlock block = doc.begin();
                for (int i = 0; i < block.text().length(); ++i) {
                    CHECK_FALSE(isFlaggedMisspelled(block, i));
                }
            }
        }
    }
}

SCENARIO("A numbered-list marker is not tokenized or flagged as a word", "[spell_check_highlighter]")
{
    GIVEN("a numbered-list block whose text (after the marker) contains a misspelled word")
    {
        QTextDocument doc;
        doc.setPlainText("mistaek in the list");
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 9);

        REQUIRE(doc.begin().text() == "9. mistaek in the list");

        FakeSpellCheckerBackend backend(true, {"mistaek"});

        WHEN("a SpellCheckHighlighter is attached to the document")
        {
            SpellCheckHighlighter highlighter(&doc, &backend);
            highlighter.rehighlight(); // see comment in the first scenario above

            THEN("the \"9. \" marker prefix carries no SpellCheckUnderline formatting")
            {
                const QTextBlock block = doc.begin();
                for (int i = 0; i < 3; ++i) { // "9. "
                    CHECK_FALSE(isFlaggedMisspelled(block, i));
                }
            }

            THEN("the misspelled word after the marker is still flagged")
            {
                const QTextBlock block = doc.begin();
                const int start = block.text().indexOf("mistaek");
                REQUIRE(start == 3);
                for (int i = start; i < start + 7; ++i) {
                    CHECK(isFlaggedMisspelled(block, i));
                }
            }
        }
    }
}

SCENARIO("A checkbox-list marker is not tokenized or flagged as a word", "[spell_check_highlighter]")
{
    GIVEN("a checkbox-list block whose text (after the marker) contains a misspelled word")
    {
        QTextDocument doc;
        doc.setPlainText("mistaek task");
        QTextCursor cursor(&doc);
        TextFormatting::toggleCheckboxList(cursor);

        // Literal GFM, not a ☐ glyph: the buffer now holds exactly what gets
        // saved (see MarkdownList::checkboxMarker).
        REQUIRE(doc.begin().text() == QStringLiteral("- [ ] mistaek task"));

        FakeSpellCheckerBackend backend(true, {"mistaek"});

        WHEN("a SpellCheckHighlighter is attached to the document")
        {
            SpellCheckHighlighter highlighter(&doc, &backend);
            highlighter.rehighlight(); // see comment in the first scenario above

            THEN("the \"- [ ] \" checkbox marker carries no SpellCheckUnderline formatting")
            {
                const QTextBlock block = doc.begin();
                for (int i = 0; i < 6; ++i) // "- [ ] "
                    CHECK_FALSE(isFlaggedMisspelled(block, i));
            }

            THEN("the misspelled word after the marker is still flagged")
            {
                const QTextBlock block = doc.begin();
                const int start = block.text().indexOf("mistaek");
                REQUIRE(start == 6);
                for (int i = start; i < start + 7; ++i) {
                    CHECK(isFlaggedMisspelled(block, i));
                }
            }
        }
    }
}

SCENARIO("A run of bare apostrophes is not a word and is never flagged as a misspelling",
         "[spell_check_highlighter]")
{
    // The word pattern is letters-and-apostrophes, so "don't"/"it's" stay whole
    // — but that also matches a run of nothing but apostrophes ("'''", a stray
    // "'"). Those are not words: they must never be tokenized, so they can never
    // be handed to the dictionary and underlined. (This filter also stands in for
    // the empty/all-digits guards the pattern itself makes unreachable.)
    GIVEN("a block containing a bare apostrophe run and a normal contraction")
    {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("''' don't"));

        THEN("only the contraction is tokenized as a word")
        {
            const auto ranges = SpellCheckHighlighter::wordRangesInBlock(doc.begin());
            REQUIRE(ranges.size() == 1);
            CHECK(doc.begin().text().mid(ranges.first().first, ranges.first().second)
                  == QStringLiteral("don't"));
        }
    }

    GIVEN("a block that is only apostrophes, which a permissive backend would call misspelled")
    {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("'''"));

        // A backend that would flag the apostrophe run if it were ever asked:
        // proves the run is filtered BEFORE any dictionary lookup, not merely
        // absent from some particular dictionary.
        FakeSpellCheckerBackend backend(true, {QStringLiteral("'''")});

        WHEN("a SpellCheckHighlighter is attached")
        {
            SpellCheckHighlighter highlighter(&doc, &backend);
            highlighter.rehighlight();

            THEN("none of its characters carry a misspelling underline")
            {
                const QTextBlock block = doc.begin();
                for (int i = 0; i < block.text().length(); ++i)
                    CHECK_FALSE(isFlaggedMisspelled(block, i));
            }
        }
    }
}

SCENARIO("A link's URL is not spellchecked, but the link's display text is", "[spell_check_highlighter]")
{
    // A markdown link is "[label](url)". The url is a machine target, not prose
    // — its host/slug words must never be flagged as typos. The label, by
    // contrast, IS the language the reader sees and stays visible when the link
    // collapses off-cursor, so it must still be checked. (Checking the url would
    // also leave a "floating underline" behind once the "](url)" punctuation
    // collapses to zero width.)
    GIVEN("a link whose label AND url are both words the backend calls misspelled")
    {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("[mistaek](exmaple)"));
        FakeSpellCheckerBackend backend(true, {QStringLiteral("mistaek"), QStringLiteral("exmaple")});

        THEN("only the label is tokenized as a word; the url is excluded entirely")
        {
            const auto ranges = SpellCheckHighlighter::wordRangesInBlock(doc.begin());
            REQUIRE(ranges.size() == 1);
            CHECK(doc.begin().text().mid(ranges.first().first, ranges.first().second)
                  == QStringLiteral("mistaek"));
        }

        WHEN("a SpellCheckHighlighter is attached")
        {
            SpellCheckHighlighter highlighter(&doc, &backend);
            highlighter.rehighlight();
            const QTextBlock block = doc.begin();

            THEN("every character of the label carries the misspelling underline")
            {
                // "[mistaek](exmaple)" — label at offsets 1..7.
                for (int i = 1; i <= 7; ++i)
                    CHECK(isFlaggedMisspelled(block, i));
            }

            THEN("no character of the url carries a misspelling underline")
            {
                // url "exmaple" at offsets 10..16 — nothing to float on collapse.
                for (int i = 10; i <= 16; ++i)
                    CHECK_FALSE(isFlaggedMisspelled(block, i));
            }
        }
    }
}

SCENARIO("A whole-line image is not spellchecked at all (alt or src)", "[spell_check_highlighter]")
{
    // A whole-line image "![alt](src)" collapses to a drawn image band off the
    // cursor's line, so a squiggle under ANY word in it — the alt text or the
    // src — would float over the collapsed band. The entire span is excluded.
    GIVEN("an image line whose alt AND src are both words the backend calls misspelled")
    {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("![mistaek](exmaple)"));
        FakeSpellCheckerBackend backend(true, {QStringLiteral("mistaek"), QStringLiteral("exmaple")});

        THEN("no word in the image span is tokenized")
        {
            const auto ranges = SpellCheckHighlighter::wordRangesInBlock(doc.begin());
            CHECK(ranges.isEmpty());
        }

        WHEN("a SpellCheckHighlighter is attached")
        {
            SpellCheckHighlighter highlighter(&doc, &backend);
            highlighter.rehighlight();

            THEN("no position in the block carries a misspelling underline")
            {
                const QTextBlock block = doc.begin();
                for (int i = 0; i < block.text().length(); ++i)
                    CHECK_FALSE(isFlaggedMisspelled(block, i));
            }
        }
    }
}

SCENARIO("An ordinary misspelled word next to a link is still flagged", "[spell_check_highlighter]")
{
    // Regression guard: the url exclusion must be scoped to the url span only,
    // not the whole line — a plain misspelled word sitting beside a link must
    // still get its squiggle.
    GIVEN("a misspelled word followed by a link whose url is also misspelled")
    {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("teh [ok](exmaple)"));
        FakeSpellCheckerBackend backend(true, {QStringLiteral("teh"), QStringLiteral("exmaple")});

        THEN("the ordinary word is tokenized but the url is excluded")
        {
            // The link label "ok" is prose too, so it is also tokenized — the
            // point of this scenario is only that the exclusion is scoped to
            // the url span: "teh" is present, "exmaple" (the url) is not.
            const auto ranges = SpellCheckHighlighter::wordRangesInBlock(doc.begin());
            QStringList words;
            for (const auto &range : ranges)
                words << doc.begin().text().mid(range.first, range.second);
            CHECK(words.contains(QStringLiteral("teh")));
            CHECK_FALSE(words.contains(QStringLiteral("exmaple")));
        }

        WHEN("a SpellCheckHighlighter is attached")
        {
            SpellCheckHighlighter highlighter(&doc, &backend);
            highlighter.rehighlight();
            const QTextBlock block = doc.begin();

            THEN("the ordinary word is flagged")
            {
                // "teh" at offset 0.
                CHECK(isFlaggedMisspelled(block, 0));
            }

            THEN("the link's url is not flagged")
            {
                // url "exmaple" at offsets 9..15.
                for (int i = 9; i <= 15; ++i)
                    CHECK_FALSE(isFlaggedMisspelled(block, i));
            }
        }
    }
}
