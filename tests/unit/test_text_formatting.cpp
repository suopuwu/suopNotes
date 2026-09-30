#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFontMetricsF>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
#include <QTextTableFormat>

#include "core/MarkdownList.h"
#include "core/TextFormatting.h"

namespace
{
QTextCursor selectAll(QTextDocument &doc)
{
    QTextCursor cursor(&doc);
    cursor.select(QTextCursor::Document);
    return cursor;
}

// The document as one line per block. Assertions below compare against this
// rather than a single string: the buffer IS the markdown source now, so
// spelling out the expected lines is both the clearest way to read a test and
// an exact statement of what would land on disk.
QStringList plainLines(const QTextDocument &doc)
{
    QStringList result;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
        result << b.text();
    return result;
}

// A cursor `offsetInLine` characters into the given line.
QTextCursor cursorAtLine(QTextDocument &doc, int lineNumber, int offsetInLine)
{
    const QTextBlock block = doc.findBlockByNumber(lineNumber);
    QTextCursor cursor(&doc);
    cursor.setPosition(block.position() + qMin(offsetInLine, block.text().size()));
    return cursor;
}
}

SCENARIO("Toggling bold on a selection", "[text_formatting]")
{
    GIVEN("a document with plain text, fully selected")
    {
        QTextDocument doc;
        doc.setPlainText("hello world");

        WHEN("bold is toggled on")
        {
            TextFormatting::toggleBold(selectAll(doc));

            THEN("the markdown serialization wraps it in **")
            {
                CHECK(doc.toMarkdown().contains("**hello world**"));
            }
        }

        WHEN("bold is toggled on, then off again")
        {
            TextFormatting::toggleBold(selectAll(doc));
            TextFormatting::toggleBold(selectAll(doc));

            THEN("no bold markers remain")
            {
                CHECK_FALSE(doc.toMarkdown().contains("**"));
            }
        }
    }
}

SCENARIO("Toggling italic on a selection", "[text_formatting]")
{
    GIVEN("a document with plain text, fully selected")
    {
        QTextDocument doc;
        doc.setPlainText("emphasis please");

        WHEN("italic is toggled on")
        {
            TextFormatting::toggleItalic(selectAll(doc));

            THEN("the markdown serialization wraps it in single asterisks")
            {
                const QString md = doc.toMarkdown();
                CHECK(md.contains("*emphasis please*"));
                CHECK_FALSE(md.contains("**emphasis please**"));
            }
        }
    }
}

SCENARIO("Toggling strikethrough on a selection", "[text_formatting]")
{
    GIVEN("a document with plain text, fully selected")
    {
        QTextDocument doc;
        doc.setPlainText("oops");

        WHEN("strikethrough is toggled on")
        {
            TextFormatting::toggleStrikethrough(selectAll(doc));

            THEN("the markdown serialization wraps it in ~~")
            {
                CHECK(doc.toMarkdown().contains("~~oops~~"));
            }
        }
    }
}

SCENARIO("Setting a heading level on the current block", "[text_formatting]")
{
    GIVEN("a document with a single paragraph")
    {
        QTextDocument doc;
        doc.setPlainText("Section Title");

        WHEN("heading level 1 is applied with the cursor in that block")
        {
            QTextCursor cursor(&doc);
            TextFormatting::setHeadingLevel(cursor, 1);

            THEN("the markdown serialization is an h1")
            {
                CHECK(doc.toMarkdown().contains("# Section Title"));
            }
        }

        WHEN("heading level 2 is applied")
        {
            QTextCursor cursor(&doc);
            TextFormatting::setHeadingLevel(cursor, 2);

            THEN("the markdown serialization is an h2")
            {
                CHECK(doc.toMarkdown().contains("## Section Title"));
            }
        }

        WHEN("heading level 1 is applied, then cleared back to level 0")
        {
            QTextCursor cursor(&doc);
            TextFormatting::setHeadingLevel(cursor, 1);
            TextFormatting::setHeadingLevel(cursor, 0);

            THEN("it is a plain paragraph again, with no # prefix")
            {
                const QString md = doc.toMarkdown().trimmed();
                CHECK(md == "Section Title");
            }
        }
    }
}

SCENARIO("Merging a heading with the line below it needs no repair pass", "[text_formatting]")
{
    GIVEN("a heading followed by a plain paragraph")
    {
        QTextDocument doc;
        doc.setPlainText("# Title\nplain body");

        WHEN("the newline between them is deleted, merging the two lines")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(QStringLiteral("# Title").size());
            cursor.deleteChar();

            THEN("the surviving line is exactly the two texts joined")
            {
                REQUIRE(doc.blockCount() == 1);
                CHECK(doc.begin().text() == QStringLiteral("# Titleplain body"));
            }

            THEN("it is still an H1, because the '#' is simply still there")
            {
                // This scenario replaces one that used to drive
                // maintainHeadingCharFormatAfterMerge, a function that existed
                // only to repair a merge. Back then a heading's LOOK lived in a
                // char format while its heading-NESS lived in a block format,
                // and a merge resolved the two differently — so absorbed text
                // didn't look like the heading it had joined, until a reload
                // re-derived everything. There is now only one source of truth:
                // the text. A merge can't desync it from itself, so the repair
                // pass has nothing left to repair and is gone.
                QTextCursor at(&doc);
                at.setPosition(1);
                CHECK(TextFormatting::headingLevel(at) == 1);
            }
        }
    }

    GIVEN("a plain paragraph followed by a heading")
    {
        QTextDocument doc;
        doc.setPlainText("intro\n# Title");

        WHEN("the two are merged")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(QStringLiteral("intro").size());
            cursor.deleteChar();

            THEN("the result is NOT a heading — the '#' no longer starts the line")
            {
                REQUIRE(doc.blockCount() == 1);
                CHECK(doc.begin().text() == QStringLiteral("intro# Title"));
                QTextCursor at(&doc);
                at.setPosition(1);
                CHECK(TextFormatting::headingLevel(at) == 0);
            }
        }
    }
}

SCENARIO("Toggling a bullet list on the current block", "[text_formatting]")
{
    GIVEN("a document with a single paragraph")
    {
        QTextDocument doc;
        doc.setPlainText("Buy milk");

        WHEN("bullet list is toggled on")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleBulletList(cursor);

            THEN("the markdown serialization is a dash-prefixed list item")
            {
                CHECK(doc.toMarkdown().contains("- Buy milk"));
            }
        }

        WHEN("bullet list is toggled on, then off again")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleBulletList(cursor);
            TextFormatting::toggleBulletList(cursor);

            THEN("it is a plain paragraph again")
            {
                const QString md = doc.toMarkdown().trimmed();
                CHECK(md == "Buy milk");
            }
        }

        WHEN("numbered list is toggled on while a bullet list is already active")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleBulletList(cursor);
            TextFormatting::toggleNumberedList(cursor);

            THEN("the same block converts in place to a numbered marker, not a dash bullet")
            {
                // Not doc.toMarkdown() — numbered markers are hand-rolled
                // plain text now (see TextFormatting.h), and Qt's own
                // writer defensively escapes a paragraph merely starting
                // with "1. " ("1\. ", confirmed empirically); this checks
                // the actual in-document text instead of Qt-writer noise
                // that's irrelevant to what this scenario is testing.
                CHECK(doc.begin().text() == "1. Buy milk");
                CHECK(TextFormatting::isNumberedList(cursor));
                CHECK_FALSE(TextFormatting::isBulletList(cursor));
            }
        }
    }
}

SCENARIO("Toggling a numbered list on the current block", "[text_formatting]")
{
    // Numbered-list markers are hand-rolled, literal, selectable text
    // ("1. ", "2. ", ...) tagged with a custom block property rather than
    // a QTextList (see TextFormatting.h's own comment on toggleNumberedList
    // for why) — these scenarios check the actual in-document block text
    // and isNumberedList directly, not doc.toMarkdown() (Qt's own writer
    // defensively escapes a plain paragraph merely starting with "1. ",
    // confirmed empirically, which is irrelevant Qt-writer noise here, not
    // anything toggleNumberedList itself does).
    GIVEN("a document with a single paragraph")
    {
        QTextDocument doc;
        doc.setPlainText("Buy milk");

        WHEN("numbered list is toggled on")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleNumberedList(cursor);

            THEN("the block's own text carries a literal, selectable \"1. \" marker")
            {
                CHECK(doc.begin().text() == "1. Buy milk");
                CHECK(TextFormatting::isNumberedList(cursor));
            }
        }

        WHEN("numbered list is toggled on, then off again")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleNumberedList(cursor);
            TextFormatting::toggleNumberedList(cursor);

            THEN("it is a plain paragraph again, marker text and all")
            {
                CHECK(doc.begin().text() == "Buy milk");
                CHECK_FALSE(TextFormatting::isNumberedList(cursor));
            }
        }

        WHEN("bullet list is toggled on while a numbered list is already active")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleNumberedList(cursor);
            TextFormatting::toggleBulletList(cursor);

            THEN("the same block converts in place to a dash bullet, not numbered")
            {
                // The bullet marker is real text now, so it shows up in the
                // block's own text — it used to be QTextList decoration, which
                // had no character position at all.
                CHECK(doc.begin().text() == "- Buy milk");
                CHECK(TextFormatting::isBulletList(cursor));
                CHECK_FALSE(TextFormatting::isNumberedList(cursor));
            }
        }
    }

    GIVEN("a document with two paragraphs, the second meant to start a list partway through")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("Intro");
        cursor.insertBlock();
        cursor.insertText("Fifth item");

        WHEN("a numbered list is created on the second block with an explicit start number")
        {
            QTextCursor blockCursor(&doc);
            blockCursor.setPosition(doc.findBlockByNumber(1).position());
            TextFormatting::toggleNumberedList(blockCursor, 5);

            THEN("the list starts counting from that number, not from 1")
            {
                CHECK(doc.findBlockByNumber(1).text() == "5. Fifth item");
                CHECK(TextFormatting::isNumberedList(blockCursor));
            }
        }

        WHEN("no start number is given")
        {
            QTextCursor blockCursor(&doc);
            blockCursor.setPosition(doc.findBlockByNumber(1).position());
            TextFormatting::toggleNumberedList(blockCursor);

            THEN("it defaults to starting at 1, same as before this parameter existed")
            {
                CHECK(doc.findBlockByNumber(1).text() == "1. Fifth item");
            }
        }

        WHEN("converting an already-existing bullet list's style in place")
        {
            QTextCursor blockCursor(&doc);
            blockCursor.setPosition(doc.findBlockByNumber(1).position());
            TextFormatting::toggleBulletList(blockCursor);
            TextFormatting::toggleNumberedList(blockCursor, 7);

            THEN("the startNumber argument still applies — there's no existing numbered run to join")
            {
                CHECK(doc.findBlockByNumber(1).text() == "7. Fifth item");
            }
        }
    }

    GIVEN("two adjacent numbered items, with the cursor on the second")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 1);
        cursor.insertBlock();
        cursor.insertText("second item");
        TextFormatting::toggleNumberedList(cursor);

        WHEN("the toolbar toggle is used (not Enter-continuation) to add this second item")
        {
            THEN("it joins the existing run as item 2, not a fresh item 1")
            {
                CHECK(doc.findBlockByNumber(0).text() == "1. ");
                CHECK(doc.findBlockByNumber(1).text() == "2. second item");
            }
        }
    }
}

SCENARIO("Pressing Enter at or before a numbered item's own marker doesn't duplicate it", "[text_formatting]")
{
    // Bug: continueHandRolledList splits the block at the cursor's literal
    // position — with no clamp, a cursor sitting AT or BEFORE the marker
    // (not just after it, in the item's real text) carries that whole
    // marker into the new block whole, which then got ANOTHER fresh one
    // inserted in front of it ("1. some text" -> "0. 1. some text").
    GIVEN("a numbered item, cursor placed at the very start of its marker")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 1);
        cursor.insertText("some text");

        WHEN("Enter is pressed at position 0 (before the \"1. \" marker entirely)")
        {
            QTextCursor enterCursor(&doc);
            enterCursor.setPosition(0);
            const bool handled = TextFormatting::continueHandRolledList(enterCursor);

            THEN("a blank new item is inserted — the original text is untouched, not duplicated")
            {
                REQUIRE(handled);
                QStringList blockTexts;
                for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
                    blockTexts << b.text();
                CHECK(blockTexts == QStringList{"1. ", "2. some text"});
            }
        }
    }

    GIVEN("a numbered item, cursor placed inside the marker itself (between digit and period)")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 1);
        cursor.insertText("some text");

        WHEN("Enter is pressed at position 1 (between \"1\" and \". \")")
        {
            QTextCursor enterCursor(&doc);
            enterCursor.setPosition(1);
            TextFormatting::continueHandRolledList(enterCursor);

            THEN("the marker still isn't duplicated")
            {
                QStringList blockTexts;
                for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
                    blockTexts << b.text();
                CHECK(blockTexts == QStringList{"1. ", "2. some text"});
            }
        }
    }
}

SCENARIO("Deleting an item from the middle of a numbered list renumbers what's left", "[text_formatting]")
{
    // Bug: ordinary text editing (backspacing an item away, selecting one
    // and deleting it) merges/removes blocks through Qt's own default
    // handling, never through toggleNumberedList/continueHandRolledList —
    // maintainHandRolledListNumbering exists specifically to be called
    // after such edits and re-sync the surviving items' numbers.
    GIVEN("a 3-item numbered list, with the middle item selected and removed")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 1);
        cursor.insertText("first");
        TextFormatting::continueHandRolledList(cursor);
        cursor.insertText("second");
        TextFormatting::continueHandRolledList(cursor);
        cursor.insertText("third");

        // Select the whole "2. second" block, including its own trailing
        // block separator, and remove it — mirrors selecting a line in the
        // editor and pressing Delete/Backspace, bypassing every list-aware
        // function this file exposes, the same way real ad hoc editing
        // would.
        QTextBlock middle = doc.findBlockByNumber(1);
        QTextCursor deleteCursor(&doc);
        deleteCursor.setPosition(middle.position());
        deleteCursor.setPosition(middle.next().position(), QTextCursor::KeepAnchor);
        deleteCursor.removeSelectedText();

        WHEN("maintainHandRolledListNumbering is called with the cursor on the surviving list")
        {
            QTextCursor maintainCursor(&doc);
            maintainCursor.setPosition(doc.begin().position());
            TextFormatting::maintainHandRolledListNumbering(maintainCursor);

            THEN("the remaining two items renumber to 1 and 2, not 1 and 3")
            {
                QStringList blockTexts;
                for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
                    blockTexts << b.text();
                CHECK(blockTexts == QStringList{"1. first", "2. third"});
            }
        }
    }
}

SCENARIO("Querying whether bold is active at the cursor/selection", "[text_formatting]")
{
    GIVEN("a document with plain text, fully selected")
    {
        QTextDocument doc;
        doc.setPlainText("hello world");

        THEN("isBold reports false before any formatting is applied")
        {
            CHECK_FALSE(TextFormatting::isBold(selectAll(doc)));
        }

        WHEN("bold is toggled on")
        {
            TextFormatting::toggleBold(selectAll(doc));

            THEN("isBold reports true for that same selection")
            {
                CHECK(TextFormatting::isBold(selectAll(doc)));
            }
        }

        WHEN("bold is toggled on, then off again")
        {
            TextFormatting::toggleBold(selectAll(doc));
            TextFormatting::toggleBold(selectAll(doc));

            THEN("isBold reports false again")
            {
                CHECK_FALSE(TextFormatting::isBold(selectAll(doc)));
            }
        }
    }
}

SCENARIO("Querying whether italic/strikethrough are active at the cursor/selection", "[text_formatting]")
{
    GIVEN("a document with plain text, fully selected")
    {
        QTextDocument doc;
        doc.setPlainText("emphasis please");

        WHEN("italic is toggled on")
        {
            TextFormatting::toggleItalic(selectAll(doc));

            THEN("isItalic reports true, isStrikethrough stays false")
            {
                CHECK(TextFormatting::isItalic(selectAll(doc)));
                CHECK_FALSE(TextFormatting::isStrikethrough(selectAll(doc)));
            }
        }

        WHEN("strikethrough is toggled on")
        {
            TextFormatting::toggleStrikethrough(selectAll(doc));

            THEN("isStrikethrough reports true, isItalic stays false")
            {
                CHECK(TextFormatting::isStrikethrough(selectAll(doc)));
                CHECK_FALSE(TextFormatting::isItalic(selectAll(doc)));
            }
        }
    }
}

SCENARIO("Querying the heading level at the cursor", "[text_formatting]")
{
    GIVEN("a document with a single paragraph")
    {
        QTextDocument doc;
        doc.setPlainText("Section Title");

        THEN("headingLevel reports 0 for a plain paragraph")
        {
            QTextCursor cursor(&doc);
            CHECK(TextFormatting::headingLevel(cursor) == 0);
        }

        WHEN("heading level 1 is applied")
        {
            QTextCursor cursor(&doc);
            TextFormatting::setHeadingLevel(cursor, 1);

            THEN("headingLevel reports 1")
            {
                CHECK(TextFormatting::headingLevel(cursor) == 1);
            }
        }

        WHEN("heading level 2 is applied")
        {
            QTextCursor cursor(&doc);
            TextFormatting::setHeadingLevel(cursor, 2);

            THEN("headingLevel reports 2")
            {
                CHECK(TextFormatting::headingLevel(cursor) == 2);
            }
        }
    }
}

SCENARIO("Querying whether a bullet/numbered list is active at the cursor", "[text_formatting]")
{
    GIVEN("a document with a single paragraph")
    {
        QTextDocument doc;
        doc.setPlainText("Buy milk");

        THEN("neither list query reports true for a plain paragraph")
        {
            QTextCursor cursor(&doc);
            CHECK_FALSE(TextFormatting::isBulletList(cursor));
            CHECK_FALSE(TextFormatting::isNumberedList(cursor));
        }

        WHEN("bullet list is toggled on")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleBulletList(cursor);

            THEN("isBulletList reports true, isNumberedList stays false")
            {
                CHECK(TextFormatting::isBulletList(cursor));
                CHECK_FALSE(TextFormatting::isNumberedList(cursor));
            }
        }

        WHEN("numbered list is toggled on")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleNumberedList(cursor);

            THEN("isNumberedList reports true, isBulletList stays false")
            {
                CHECK(TextFormatting::isNumberedList(cursor));
                CHECK_FALSE(TextFormatting::isBulletList(cursor));
            }
        }

        WHEN("a bullet list is converted in place to numbered")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleBulletList(cursor);
            TextFormatting::toggleNumberedList(cursor);

            THEN("isNumberedList reports true, isBulletList reports false")
            {
                CHECK(TextFormatting::isNumberedList(cursor));
                CHECK_FALSE(TextFormatting::isBulletList(cursor));
            }
        }
    }
}

SCENARIO("Toggling a checkbox list on the current block", "[text_formatting]")
{
    GIVEN("a plain line")
    {
        QTextDocument doc;
        doc.setPlainText("Buy milk");

        WHEN("a checkbox list is toggled on")
        {
            TextFormatting::toggleCheckboxList(selectAll(doc));

            THEN("the line gains a literal, unchecked GFM checkbox marker")
            {
                // Literal "- [ ] " text, not a ☐ glyph: what's in the buffer is
                // exactly what gets saved. The pre-rewrite editor stored ☐ and
                // translated it to GFM only on save.
                CHECK(doc.begin().text() == QStringLiteral("- [ ] Buy milk"));
            }

            THEN("the cursor reports it as an unchecked checkbox item")
            {
                QTextCursor at(&doc);
                at.setPosition(8);
                CHECK(TextFormatting::isCheckboxList(at));
                CHECK_FALSE(TextFormatting::isCheckboxChecked(at));
            }
        }

        WHEN("it is toggled on and then off again")
        {
            TextFormatting::toggleCheckboxList(selectAll(doc));
            TextFormatting::toggleCheckboxList(selectAll(doc));

            THEN("the original line is restored exactly")
            {
                CHECK(doc.begin().text() == QStringLiteral("Buy milk"));
            }
        }
    }

    GIVEN("an existing bullet item")
    {
        QTextDocument doc;
        doc.setPlainText("- Buy milk");

        WHEN("a checkbox list is toggled on")
        {
            TextFormatting::toggleCheckboxList(selectAll(doc));

            THEN("its bullet marker is replaced, not stacked on top of")
            {
                CHECK(doc.begin().text() == QStringLiteral("- [ ] Buy milk"));
            }
        }
    }
}

SCENARIO("Toggling checkbox format across a mixed multi-line selection preserves checked state",
         "[text_formatting]")
{
    GIVEN("an already-checked item next to a plain line")
    {
        QTextDocument doc;
        doc.setPlainText("- [x] wash car\nbuy milk");

        WHEN("checkbox format is toggled across both lines")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(0);
            cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            TextFormatting::toggleCheckboxList(cursor);

            THEN("the already-checked line stays checked")
            {
                CHECK(doc.findBlockByNumber(0).text() == QStringLiteral("- [x] wash car"));
            }
            THEN("the plain line becomes an unchecked checkbox item")
            {
                CHECK(doc.findBlockByNumber(1).text() == QStringLiteral("- [ ] buy milk"));
            }
        }

        WHEN("the toolbar's active-state query is asked about this exact mixed selection")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(0);
            cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);

            THEN("it reports false, agreeing that toggling ADDS checkbox formatting rather than "
                 "stripping it")
            {
                // Before Fix B this only inspected the selection's last block
                // (a plain line), or worse, could disagree with allAlready's
                // whole-selection answer depending on which line the cursor
                // landed on — either way it could show "active" for a
                // selection where toggling actually adds formatting.
                CHECK_FALSE(TextFormatting::isCheckboxList(cursor));
            }
        }
    }
}

SCENARIO("Toggling a checkbox item's checked state", "[text_formatting]")
{
    GIVEN("an unchecked checkbox item")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] Buy milk");
        QTextCursor cursor(&doc);
        cursor.setPosition(8);

        WHEN("its checked state is toggled")
        {
            const bool handled = TextFormatting::toggleCheckboxChecked(cursor);

            THEN("only the marker changes — the item's own text is untouched")
            {
                CHECK(handled);
                CHECK(doc.begin().text() == QStringLiteral("- [x] Buy milk"));
            }

            THEN("it now reports as checked")
            {
                CHECK(TextFormatting::isCheckboxChecked(cursor));
            }
        }

        WHEN("it is toggled twice")
        {
            TextFormatting::toggleCheckboxChecked(cursor);
            TextFormatting::toggleCheckboxChecked(cursor);

            THEN("it is back to exactly its original text")
            {
                CHECK(doc.begin().text() == QStringLiteral("- [ ] Buy milk"));
            }
        }
    }

    GIVEN("a line that isn't a checkbox item at all")
    {
        QTextDocument doc;
        doc.setPlainText("just text");
        QTextCursor cursor(&doc);
        cursor.setPosition(2);

        WHEN("a checked-state toggle is attempted")
        {
            const bool handled = TextFormatting::toggleCheckboxChecked(cursor);

            THEN("it declines and leaves the line alone")
            {
                CHECK_FALSE(handled);
                CHECK(doc.begin().text() == QStringLiteral("just text"));
            }
        }
    }
}

SCENARIO("Click-to-toggle hit-tests against the checkbox marker itself", "[text_formatting]")
{
    GIVEN("an unchecked checkbox item, '- [ ] Buy milk'")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] Buy milk");

        WHEN("the click lands on the marker's brackets")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(3); // inside "[ ]"

            THEN("it toggles")
            {
                CHECK(TextFormatting::tryToggleCheckboxAtPosition(cursor));
                CHECK(doc.begin().text() == QStringLiteral("- [x] Buy milk"));
            }
        }

        WHEN("the click lands in the item's label text instead")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(9); // inside "Buy milk"

            THEN("it does NOT toggle — a click there should just place the caret")
            {
                CHECK_FALSE(TextFormatting::tryToggleCheckboxAtPosition(cursor));
                CHECK(doc.begin().text() == QStringLiteral("- [ ] Buy milk"));
            }
        }
    }

    GIVEN("a line that isn't a checkbox item")
    {
        QTextDocument doc;
        doc.setPlainText("plain line");

        WHEN("a click-toggle is attempted on it")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(1);

            THEN("nothing happens")
            {
                CHECK_FALSE(TextFormatting::tryToggleCheckboxAtPosition(cursor));
                CHECK(doc.begin().text() == QStringLiteral("plain line"));
            }
        }
    }
}

SCENARIO("A click inside a drawn marker places the caret at the item's text", "[text_formatting]")
{
    // The bug this pins down (reported as "the text cursor is inside of the
    // checkbox, causing any content typed to break it"): a bullet's "- " and a
    // checkbox's "- [ ] " are invisible text under a drawn glyph, so every
    // position inside one renders at that glyph's own left or right edge.
    // Measured on a fresh "- [ ] " at the default font, the box spans x=0 to
    // x=14.69 and the item's text starts at position 6, x=21.94 — so every
    // click left of x≈20 (the box, and the gap beside it where an empty item's
    // label looks like it begins) landed the caret INSIDE the marker, where
    // typing splits it ("- [ ] " + "b" at position 4 gives "- [ b] ", which no
    // longer parses as a checkbox at all, so the box silently disappears).

    GIVEN("an empty checkbox item, '- [ ] ' — a checkbox the instant it's added")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] ");

        WHEN("a click lands on the box itself")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(2); // the reserved slot the box is drawn into

            THEN("the caret goes to where the label starts, not into the marker")
            {
                CHECK(TextFormatting::listLabelPosition(cursor) == 6);
            }
        }

        WHEN("a click lands in the gap between the box and where its label begins")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(4); // "- [ |] " — renders at the box's right edge

            THEN("the caret still goes to the label start")
            {
                CHECK(TextFormatting::listLabelPosition(cursor) == 6);
            }
        }

        WHEN("a click lands at the label start already")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(6);

            THEN("it stays put")
            {
                CHECK(TextFormatting::listLabelPosition(cursor) == 6);
            }
        }
    }

    GIVEN("a checkbox item with a label, '- [ ] Buy milk'")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] Buy milk");

        WHEN("a click lands inside the item's own text")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(9); // inside "Buy milk"

            THEN("it is left exactly where the user aimed")
            {
                CHECK(TextFormatting::listLabelPosition(cursor) == 9);
            }
        }
    }

    GIVEN("a bullet item, '- Buy milk' — the same invisible marker under a drawn dot")
    {
        QTextDocument doc;
        doc.setPlainText("- Buy milk");

        WHEN("a click lands on the dot")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(0);

            THEN("the caret goes to the item's text")
            {
                CHECK(TextFormatting::listLabelPosition(cursor) == 2);
            }
        }
    }

    GIVEN("a numbered item, '1. Buy milk'")
    {
        QTextDocument doc;
        doc.setPlainText("1. Buy milk");

        WHEN("a click lands inside the '1. '")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(1);

            THEN("it is left alone — a number is visible text the user may be aiming to edit")
            {
                // Unlike a bullet/checkbox, a numbered marker is never replaced
                // by a drawn glyph (see MarkdownHighlighter): it renders as
                // ordinary text, so a click in it is a click on what the user
                // can actually see, and snapping past it would make the number
                // uneditable by mouse.
                CHECK(TextFormatting::listLabelPosition(cursor) == 1);
            }
        }
    }

    GIVEN("a plain paragraph")
    {
        QTextDocument doc;
        doc.setPlainText("plain line");

        WHEN("a click lands anywhere in it")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(3);

            THEN("nothing snaps")
            {
                CHECK(TextFormatting::listLabelPosition(cursor) == 3);
            }
        }
    }
}

SCENARIO("checkboxMarkerRect locates the checkbox marker for a hover highlight", "[text_formatting]")
{
    GIVEN("a checkbox item")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] Buy milk");

        WHEN("the marker's rect is queried from a position on the marker")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(3);
            const QRectF rect = TextFormatting::checkboxMarkerRect(cursor);

            THEN("a real, non-empty rect is returned at the line's left edge")
            {
                CHECK_FALSE(rect.isEmpty());
                CHECK(rect.width() > 0.0);
                CHECK(rect.height() > 0.0);
                CHECK(rect.left() >= 0.0);
            }
        }

        WHEN("the rect is queried from a position out in the label text")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(10);

            THEN("no rect is returned — there is no marker to highlight there")
            {
                CHECK(TextFormatting::checkboxMarkerRect(cursor).isEmpty());
            }
        }

        WHEN("the rect is queried at every position across the whole line")
        {
            THEN("it is offered at exactly the positions that actually toggle")
            {
                // The highlight is a promise that clicking here does something,
                // so the region it covers must be the region
                // tryToggleCheckboxAtPosition accepts — not one character more.
                // "- [ ] Buy milk": positions 0..5 are "- [ ]" and toggle;
                // position 5 is the closing bracket's far edge. Position 6 is
                // the marker's trailing space, which reads as label padding.
                for (int position = 0; position <= 5; ++position) {
                    QTextCursor cursor(&doc);
                    cursor.setPosition(position);
                    CAPTURE(position);
                    CHECK_FALSE(TextFormatting::checkboxMarkerRect(cursor).isEmpty());
                }
                for (int position = 6; position <= 14; ++position) {
                    QTextCursor cursor(&doc);
                    cursor.setPosition(position);
                    CAPTURE(position);
                    CHECK(TextFormatting::checkboxMarkerRect(cursor).isEmpty());
                }
            }
        }
    }

    GIVEN("a checkbox item with no label typed yet — a checkbox the instant it's added")
    {
        // The regression this pins down (reported as "adding a checkbox leaves
        // a blue highlight behind it"): TextEdit::positionAt() never fails, it
        // CLAMPS, so a pointer resting anywhere in the blank space below a note
        // resolves to the last line's final position. When the last line is a
        // marker with nothing after it, that position is the marker's trailing
        // space — and while the rect was offered there, the hover highlight lit
        // up behind the checkbox and stayed lit, over a spot where clicking did
        // nothing at all.
        QTextDocument doc;
        doc.setPlainText("- [ ] ");

        WHEN("the rect is queried at the end of the line, where a pointer below the text lands")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(6);

            THEN("no rect is returned, and clicking there agrees by doing nothing")
            {
                CHECK(TextFormatting::checkboxMarkerRect(cursor).isEmpty());
                CHECK_FALSE(TextFormatting::tryToggleCheckboxAtPosition(cursor));
            }
        }

        WHEN("the rect is queried on the box itself")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(2);

            THEN("the highlight is still offered — the box is still clickable")
            {
                CHECK_FALSE(TextFormatting::checkboxMarkerRect(cursor).isEmpty());
                CHECK(TextFormatting::tryToggleCheckboxAtPosition(cursor));
            }
        }
    }

    GIVEN("a line that isn't a checkbox item")
    {
        QTextDocument doc;
        doc.setPlainText("plain line");

        THEN("no rect is returned")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(1);
            CHECK(TextFormatting::checkboxMarkerRect(cursor).isEmpty());
        }
    }
}

SCENARIO("Inserting a starter table at the cursor", "[text_formatting]")
{
    GIVEN("a note with a paragraph in it")
    {
        QTextDocument doc;
        doc.setPlainText("intro");
        QTextCursor cursor(&doc);
        cursor.movePosition(QTextCursor::End);

        WHEN("a table is inserted")
        {
            TextFormatting::insertTable(cursor);

            THEN("a 2-column pipe table appears, with placeholder header text to type over")
            {
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("intro"),
                    QStringLiteral("| Header 1 | Header 2 |"),
                    QStringLiteral("| --- | --- |"),
                    QStringLiteral("|  |  |"),
                });
            }

            THEN("the cursor reports as being inside a table")
            {
                CHECK(TextFormatting::isInTable(cursorAtLine(doc, 1, 2)));
            }
        }
    }

    GIVEN("a cursor already sitting on a blank line")
    {
        QTextDocument doc;
        doc.setPlainText("intro\n");
        QTextCursor cursor(&doc);
        cursor.movePosition(QTextCursor::End);

        WHEN("a table is inserted")
        {
            TextFormatting::insertTable(cursor);

            THEN("that blank line is reused rather than left above the table as a stray paragraph")
            {
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("intro"),
                    QStringLiteral("| Header 1 | Header 2 |"),
                    QStringLiteral("| --- | --- |"),
                    QStringLiteral("|  |  |"),
                });
            }
        }
    }
}

SCENARIO("Inserting a table row", "[text_formatting]")
{
    GIVEN("a table with one body row")
    {
        QTextDocument doc;
        doc.setPlainText("| h1 | h2 |\n| --- | --- |\n| a | b |");

        WHEN("a row is inserted with the caret on the body row")
        {
            TextFormatting::insertTableRow(cursorAtLine(doc, 2, 2));

            THEN("an empty row appears below it, with the table's own column count")
            {
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("| h1 | h2 |"),
                    QStringLiteral("| --- | --- |"),
                    QStringLiteral("| a | b |"),
                    QStringLiteral("|  |  |"),
                });
            }
        }

        WHEN("a row is inserted with the caret on the header")
        {
            TextFormatting::insertTableRow(cursorAtLine(doc, 0, 2));

            THEN("the new row lands below the delimiter, never between it and the header")
            {
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("| h1 | h2 |"),
                    QStringLiteral("| --- | --- |"),
                    QStringLiteral("|  |  |"),
                    QStringLiteral("| a | b |"),
                });
            }
        }
    }

    GIVEN("a cursor outside any table")
    {
        QTextDocument doc;
        doc.setPlainText("just prose");

        WHEN("a table row insert is attempted")
        {
            TextFormatting::insertTableRow(cursorAtLine(doc, 0, 2));

            THEN("nothing happens — the operation silently no-ops off-table")
            {
                CHECK(plainLines(doc) == QStringList{QStringLiteral("just prose")});
            }
        }
    }
}

SCENARIO("Deleting a table row", "[text_formatting]")
{
    GIVEN("a table with two body rows")
    {
        QTextDocument doc;
        doc.setPlainText("| h1 | h2 |\n| --- | --- |\n| a | b |\n| c | d |");

        WHEN("a body row is deleted")
        {
            TextFormatting::deleteTableRow(cursorAtLine(doc, 2, 2));

            THEN("just that row goes")
            {
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("| h1 | h2 |"),
                    QStringLiteral("| --- | --- |"),
                    QStringLiteral("| c | d |"),
                });
            }
        }

        WHEN("the header row is deleted")
        {
            TextFormatting::deleteTableRow(cursorAtLine(doc, 0, 2));

            THEN("the first body row is promoted into the header, keeping the table valid")
            {
                // The header line can't simply be removed: that would leave the
                // delimiter as line 1 and the run would stop parsing as a table.
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("| a | b |"),
                    QStringLiteral("| --- | --- |"),
                    QStringLiteral("| c | d |"),
                });
            }
        }
    }

    GIVEN("a table down to its last row (header only)")
    {
        QTextDocument doc;
        doc.setPlainText("intro\n| h1 | h2 |\n| --- | --- |");

        WHEN("that last row is deleted")
        {
            TextFormatting::deleteTableRow(cursorAtLine(doc, 1, 2));

            THEN("the whole table goes — a table can't be left with zero rows")
            {
                CHECK(plainLines(doc) == QStringList{QStringLiteral("intro")});
            }
        }
    }
}

SCENARIO("Inserting a table column", "[text_formatting]")
{
    GIVEN("a 2-column table")
    {
        QTextDocument doc;
        doc.setPlainText("| h1 | h2 |\n| --- | --- |\n| a | b |");

        WHEN("a column is inserted with the caret in the first column")
        {
            TextFormatting::insertTableColumn(cursorAtLine(doc, 0, 2));

            THEN("every line of the table gains a cell, and the delimiter gains a '---'")
            {
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("| h1 |  | h2 |"),
                    QStringLiteral("| --- | --- | --- |"),
                    QStringLiteral("| a |  | b |"),
                });
            }
        }
    }
}

SCENARIO("Deleting a table column", "[text_formatting]")
{
    GIVEN("a 3-column table")
    {
        QTextDocument doc;
        doc.setPlainText("| h1 | h2 | h3 |\n| --- | --- | --- |\n| a | b | c |");

        WHEN("the middle column is deleted")
        {
            TextFormatting::deleteTableColumn(cursorAtLine(doc, 0, 7));

            THEN("that cell goes from every line, delimiter included")
            {
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("| h1 | h3 |"),
                    QStringLiteral("| --- | --- |"),
                    QStringLiteral("| a | c |"),
                });
            }
        }
    }

    GIVEN("a table down to its last column")
    {
        QTextDocument doc;
        doc.setPlainText("intro\n| h1 |\n| --- |\n| a |");

        WHEN("that last column is deleted")
        {
            TextFormatting::deleteTableColumn(cursorAtLine(doc, 1, 2));

            THEN("the whole table goes — a table can't be left with zero columns")
            {
                CHECK(plainLines(doc) == QStringList{QStringLiteral("intro")});
            }
        }
    }
}

SCENARIO("Deleting a whole table", "[text_formatting]")
{
    GIVEN("a table between two paragraphs")
    {
        QTextDocument doc;
        doc.setPlainText("intro\n| h1 | h2 |\n| --- | --- |\n| a | b |\noutro");

        WHEN("the table is deleted")
        {
            TextFormatting::deleteTable(cursorAtLine(doc, 2, 2));

            THEN("every one of its lines goes, leaving the surrounding prose joined up")
            {
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("intro"),
                    QStringLiteral("outro"),
                });
            }
        }
    }

    GIVEN("a cursor outside any table")
    {
        QTextDocument doc;
        doc.setPlainText("just prose");

        WHEN("a table delete is attempted")
        {
            TextFormatting::deleteTable(cursorAtLine(doc, 0, 2));

            THEN("nothing happens")
            {
                CHECK(plainLines(doc) == QStringList{QStringLiteral("just prose")});
            }
        }
    }
}

SCENARIO("Querying whether the cursor is inside a table", "[text_formatting]")
{
    GIVEN("a paragraph, a table, and another paragraph")
    {
        QTextDocument doc;
        doc.setPlainText("intro\n| h1 | h2 |\n| --- | --- |\n| a | b |\noutro");

        THEN("a cursor on any of the table's own lines reports true")
        {
            CHECK(TextFormatting::isInTable(cursorAtLine(doc, 1, 2)));
            CHECK(TextFormatting::isInTable(cursorAtLine(doc, 2, 2)));
            CHECK(TextFormatting::isInTable(cursorAtLine(doc, 3, 2)));
        }

        THEN("a cursor in the prose on either side reports false")
        {
            CHECK_FALSE(TextFormatting::isInTable(cursorAtLine(doc, 0, 2)));
            CHECK_FALSE(TextFormatting::isInTable(cursorAtLine(doc, 4, 2)));
        }
    }

    GIVEN("prose that merely contains pipes, with no delimiter row")
    {
        QTextDocument doc;
        doc.setPlainText("| this is not | a table |\n| nor is this | one |");

        THEN("it is not treated as a table")
        {
            CHECK_FALSE(TextFormatting::isInTable(cursorAtLine(doc, 0, 2)));
        }
    }
}







SCENARIO("A note's markdown round-trips byte-for-byte through the buffer", "[text_formatting]")
{
    // The reason this whole rewrite exists. The old path parsed markdown into a
    // block/list/table object model and regenerated the text on save, which
    // could not tell one blank line between paragraphs from none — so a note
    // came back subtly different from what was typed. The buffer now IS the
    // markdown, and these cases pin that down.
    GIVEN("content whose exact blank-line counts carry meaning")
    {
        const QStringList cases = {
            QStringLiteral("one\n\ntwo"),
            QStringLiteral("one\ntwo"),
            QStringLiteral("one\n\n\n\ntwo"),
            QStringLiteral("trailing\n"),
            QStringLiteral("\nleading"),
            QStringLiteral("# Heading\n\n- a\n- b\n\n| h | h2 |\n| --- | --- |\n| a | b |"),
            QStringLiteral("**bold** and *italic* and `code`"),
            QStringLiteral("- [ ] todo\n- [x] done"),
            QStringLiteral("1. one\n2. two\n3. three"),
        };

        THEN("each survives a load/save cycle unchanged, character for character")
        {
            for (const QString &original : cases) {
                QTextDocument doc;
                doc.setPlainText(original);

                QString roundTripped = doc.toRawText();
                roundTripped.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
                roundTripped.replace(QChar::LineSeparator, QLatin1Char('\n'));

                CHECK(roundTripped == original);
            }
        }
    }

    GIVEN("content containing a no-break space, as pasted from a web page")
    {
        // toPlainText() would quietly rewrite U+00A0 to an ordinary space,
        // corrupting pasted text on save — which is why DocumentHandler's
        // toMarkdown() uses toRawText() instead. This pins that choice down.
        const QString original = QStringLiteral("a b");

        THEN("the no-break space survives the round-trip rather than being flattened")
        {
            QTextDocument doc;
            doc.setPlainText(original);

            QString roundTripped = doc.toRawText();
            roundTripped.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            roundTripped.replace(QChar::LineSeparator, QLatin1Char('\n'));

            CHECK(roundTripped == original);
            CHECK(roundTripped.contains(QChar(0x00A0)));
            // The old path's behaviour, spelled out so the difference is
            // unmistakable rather than implied.
            CHECK(doc.toPlainText() != original);
        }
    }
}

// --- Hanging indent ----------------------------------------------------
//
// A wrapped list item's continuation lines should line up under the item's own
// TEXT rather than under its marker. That is a leftMargin (where every line of
// the block starts) plus a negative textIndent (how far the FIRST line is
// pulled back left, to make room for the marker, which is real text living in
// the buffer like everything else).
//
// These read the indent off block.blockFormat(). That is deliberately NOT where
// MarkdownHighlighter's own work lands — a QSyntaxHighlighter writes to the
// block's LAYOUT — and the distinction is the whole architecture here: layout
// formats are not undoable, block formats are. See the live-lock scenarios at
// the bottom of this file.

namespace
{
// The width a marker actually occupies ON SCREEN, which is what the indent has
// to reserve — not the width of its literal text. MarkdownHighlighter re-renders
// bullet/checkbox markers: the marker's trailing space gets PercentageSpacing
// 200 (double advance), and the reserved glyph gets a slot that is 200 for a
// bullet but 320 for a checkbox — which is drawn bigger. The rest of the marker
// collapses to a 0.01pt font. A numbered marker is left alone and so measures as
// the ordinary text it is. These slot percentages are read from MarkdownList,
// the single source of truth all three renderers share.
qreal renderedMarkerWidthInTest(const QTextDocument &doc, const QTextBlock &block)
{
    const QFontMetricsF metrics(doc.defaultFont());
    const QString text = block.text();
    static const QRegularExpression numbered(QStringLiteral("^\\d+\\. "));
    if (const QRegularExpressionMatch m = numbered.match(text); m.hasMatch())
        return metrics.horizontalAdvance(m.captured(0));
    // Bullet "- ": the "-" is the reserved glyph. Checkbox "- [ ] ": the "[" is.
    const bool isCheckbox = text.startsWith(QStringLiteral("- ["));
    const QString glyph = isCheckbox ? QStringLiteral("[") : QStringLiteral("-");
    const qreal slot = (isCheckbox ? MarkdownList::kCheckboxSlotPercent
                                   : MarkdownList::kBulletSlotPercent)
        / 100.0;
    return slot * metrics.horizontalAdvance(glyph)
        + 2.0 * metrics.horizontalAdvance(QStringLiteral(" "));
}
}

SCENARIO("A list item's wrapped lines hang under its text, not under its marker", "[text_formatting]")
{
    GIVEN("a ten-item numbered list, built one Enter at a time")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 1);
        cursor.insertText("item 1");
        for (int i = 2; i <= 10; ++i) {
            TextFormatting::continueHandRolledList(cursor);
            cursor.insertText(QStringLiteral("item %1").arg(i));
        }

        THEN("the marker text itself is untouched — the indent adds no literal padding characters")
        {
            CHECK(doc.findBlockByNumber(0).text() == "1. item 1");
            CHECK(doc.findBlockByNumber(8).text() == "9. item 9");
            CHECK(doc.findBlockByNumber(9).text() == "10. item 10");
        }

        THEN("every item shares one leftMargin, so their wrapped lines all align")
        {
            const QTextBlockFormat item1 = doc.findBlockByNumber(0).blockFormat();
            const QTextBlockFormat item9 = doc.findBlockByNumber(8).blockFormat();
            const QTextBlockFormat item10 = doc.findBlockByNumber(9).blockFormat();
            CHECK(item1.leftMargin() == item9.leftMargin());
            CHECK(item9.leftMargin() == item10.leftMargin());
        }

        THEN("item 10's own marker is the run's widest, so its first line sits flush left")
        {
            const QTextBlockFormat item10 = doc.findBlockByNumber(9).blockFormat();
            CHECK(item10.leftMargin() == -item10.textIndent());
        }

        THEN("item 9's narrower marker gets a less-negative textIndent, leaving blank space to its own left")
        {
            const QTextBlockFormat item9 = doc.findBlockByNumber(8).blockFormat();
            CHECK(-item9.textIndent() < item9.leftMargin());
        }

        THEN("every item's actual text, after its own marker, starts at the same x")
        {
            // The invariant the two THENs above exist to produce. A block's
            // first line renders at leftMargin+textIndent and its wrapped lines
            // at leftMargin, so this is what makes "9. " and "10. " line their
            // content up with each other AND with every continuation line.
            qreal contentStartX = -1;
            for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
                const QTextBlockFormat format = b.blockFormat();
                const qreal thisContentStart =
                    format.leftMargin() + format.textIndent() + renderedMarkerWidthInTest(doc, b);
                if (contentStartX < 0)
                    contentStartX = thisContentStart;
                else
                    CHECK(thisContentStart == Catch::Approx(contentStartX));
            }
        }
    }
}

SCENARIO("A list's hanging indent is sized to its own widest marker, not the document's", "[text_formatting]")
{
    GIVEN("a list whose markers are all single-digit")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 1);
        cursor.insertText("only item");

        THEN("it reserves room for a single-digit marker, not a two-digit one")
        {
            const QFontMetricsF metrics(doc.defaultFont());
            const QTextBlockFormat format = doc.begin().blockFormat();
            CHECK(format.leftMargin() < metrics.horizontalAdvance(QStringLiteral("12. ")));
            CHECK(format.leftMargin() == Catch::Approx(metrics.horizontalAdvance(QStringLiteral("1. "))));
        }
    }

    GIVEN("a list that starts double-digit immediately")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 11);
        cursor.insertText("only item");

        THEN("the reserved space grows to fit the two-digit marker")
        {
            const QFontMetricsF metrics(doc.defaultFont());
            CHECK(doc.begin().blockFormat().leftMargin()
                  > metrics.horizontalAdvance(QStringLiteral("2. ")));
        }
    }

    GIVEN("a note loaded with a single-digit list and a double-digit list, a plain paragraph between them")
    {
        // Loaded rather than built with toggles, because that is the only way
        // the app can actually produce this shape: Qt's insertBlock() copies the
        // previous block's format, so a plain paragraph typed straight after a
        // list item inherits its indent, and it is whatever ends the list —
        // exitHandRolledList, or a load — that clears it again.
        QTextDocument doc;
        doc.setUndoRedoEnabled(false);
        doc.setPlainText(QStringLiteral("1. first list item\nbetween the two lists\n11. second list item"));
        TextFormatting::applyListIndentToDocument(&doc);
        doc.setUndoRedoEnabled(true);

        THEN("each list keeps its own margin — the wide one does not leak into the narrow one")
        {
            const QTextBlockFormat firstList = doc.findBlockByNumber(0).blockFormat();
            const QTextBlockFormat secondList = doc.findBlockByNumber(2).blockFormat();
            CHECK(firstList.leftMargin() < secondList.leftMargin());
        }

        THEN("the plain paragraph between them has no indent at all")
        {
            const QTextBlockFormat between = doc.findBlockByNumber(1).blockFormat();
            CHECK(between.leftMargin() == 0);
            CHECK(between.textIndent() == 0);
        }
    }

    GIVEN("a list grown one item at a time from 1 up to 10")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 1);
        cursor.insertText("item 1");
        for (int i = 2; i <= 10; ++i) {
            TextFormatting::continueHandRolledList(cursor);
            cursor.insertText(QStringLiteral("item %1").arg(i));
        }

        THEN("item 9 is widened along with the rest when item 10 joins the run")
        {
            // Regression: item 9 was already indented before item 10 existed,
            // so it is the one an "only size the block being edited" bug would
            // leave behind at the old, narrower margin.
            const QTextBlockFormat item9 = doc.findBlockByNumber(8).blockFormat();
            const QTextBlockFormat item10 = doc.findBlockByNumber(9).blockFormat();
            CHECK(item9.leftMargin() == item10.leftMargin());
        }
    }
}

SCENARIO("Bullet, numbered and checkbox items all get a hanging indent", "[text_formatting]")
{
    GIVEN("a bullet list")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleBulletList(cursor);
        cursor.insertText("bullet item");

        THEN("its indent reserves the bullet's rendered width, and its text starts flush with it")
        {
            const QTextBlockFormat format = doc.begin().blockFormat();
            CHECK(format.leftMargin() > 0);
            CHECK(format.leftMargin() == -format.textIndent());
            CHECK(format.leftMargin()
                  == Catch::Approx(renderedMarkerWidthInTest(doc, doc.begin())));
        }
    }

    GIVEN("a checkbox list")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleCheckboxList(cursor);
        cursor.insertText("checkbox item");

        THEN("its indent reserves the checkbox's rendered width, which is wider than the literal text is")
        {
            const QTextBlockFormat format = doc.begin().blockFormat();
            CHECK(doc.begin().text() == "- [ ] checkbox item");
            CHECK(format.leftMargin() == -format.textIndent());
            CHECK(format.leftMargin()
                  == Catch::Approx(renderedMarkerWidthInTest(doc, doc.begin())));
        }

        THEN("the reserved width uses the WIDE checkbox slot (320), not a bullet's (200)")
        {
            // Regression: the indent once sized the "[" glyph at the bullet's
            // 200 while MarkdownHighlighter draws it at 320, so a wrapped
            // checkbox line's continuation sat ~1.2x the bracket advance too far
            // left — under the drawn box instead of under the item's own text.
            // The reserved marker column is glyph-slot + a double-width trailing
            // space; sizing it at the bullet slot instead would make it strictly
            // narrower, so the checkbox reserve must exceed the bullet reserve.
            const QFontMetricsF metrics(doc.defaultFont());
            const qreal bracket = metrics.horizontalAdvance(QStringLiteral("["));
            const qreal space = metrics.horizontalAdvance(QStringLiteral(" "));
            const qreal checkboxReserve =
                MarkdownList::kCheckboxSlotPercent / 100.0 * bracket + 2.0 * space;
            const qreal bulletSlotReserve =
                MarkdownList::kBulletSlotPercent / 100.0 * bracket + 2.0 * space;
            CHECK(MarkdownList::kCheckboxSlotPercent > MarkdownList::kBulletSlotPercent);
            CHECK(doc.begin().blockFormat().leftMargin() == Catch::Approx(checkboxReserve));
            CHECK(doc.begin().blockFormat().leftMargin() > bulletSlotReserve);
        }
    }

    GIVEN("a bullet item and a checkbox item adjacent to each other")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleBulletList(cursor);
        cursor.insertText("bullet item");
        cursor.insertBlock();
        TextFormatting::toggleCheckboxList(cursor);
        cursor.insertText("checkbox item");

        THEN("they read as one list and share a margin, so both items' text starts at the same x")
        {
            // Adjacent list lines are one run whatever their kinds, because
            // that is what a reader sees as a single list.
            const QTextBlockFormat bullet = doc.findBlockByNumber(0).blockFormat();
            const QTextBlockFormat checkbox = doc.findBlockByNumber(1).blockFormat();
            CHECK(bullet.leftMargin() == checkbox.leftMargin());
            CHECK(bullet.leftMargin() + bullet.textIndent()
                      + renderedMarkerWidthInTest(doc, doc.findBlockByNumber(0))
                  == Catch::Approx(checkbox.leftMargin() + checkbox.textIndent()
                                   + renderedMarkerWidthInTest(doc, doc.findBlockByNumber(1))));
        }
    }
}

SCENARIO("A block that stops being a list item loses its hanging indent", "[text_formatting]")
{
    // Bugfix, reported live: typing ordinary prose at the very start of a
    // numbered item pushes its marker off position 0, so the line is no longer
    // a list item — but nothing cleared the indent it was given back when it
    // was one. The stale indent only shows once the paragraph is long enough to
    // WRAP (textIndent cancels leftMargin on the first line), which is exactly
    // why it went unnoticed.
    GIVEN("a numbered item with prose typed in front of its marker")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 2);
        cursor.insertText("some item text");

        QTextCursor typeCursor(&doc);
        typeCursor.setPosition(doc.begin().position());
        typeCursor.insertText("some regular text before the marker ");

        WHEN("the reactive maintenance pass runs, as it does after every keystroke")
        {
            TextFormatting::maintainHandRolledListNumbering(QTextCursor(doc.begin()));

            THEN("the line is a plain paragraph with no leftover margin or indent")
            {
                CHECK_FALSE(TextFormatting::isNumberedList(QTextCursor(doc.begin())));
                CHECK(doc.begin().blockFormat().leftMargin() == 0);
                CHECK(doc.begin().blockFormat().textIndent() == 0);
            }
        }
    }

    GIVEN("a list item turned into a heading")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleBulletList(cursor);
        cursor.insertText("was a bullet");
        REQUIRE(doc.begin().blockFormat().leftMargin() > 0);

        WHEN("a heading prefix is applied, pushing the marker off position 0")
        {
            TextFormatting::setHeadingLevel(QTextCursor(doc.begin()), 1);

            THEN("it is no longer a list item and keeps no indent")
            {
                CHECK(doc.begin().text() == "# - was a bullet");
                CHECK(doc.begin().blockFormat().leftMargin() == 0);
                CHECK(doc.begin().blockFormat().textIndent() == 0);
            }
        }
    }

    GIVEN("an empty list item the user presses Enter on to leave the list")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleBulletList(cursor);
        REQUIRE(doc.begin().blockFormat().leftMargin() > 0);

        WHEN("the item exits the list")
        {
            TextFormatting::exitHandRolledList(cursor);

            THEN("the resulting plain paragraph has no indent")
            {
                CHECK(doc.begin().text() == "");
                CHECK(doc.begin().blockFormat().leftMargin() == 0);
                CHECK(doc.begin().blockFormat().textIndent() == 0);
            }
        }
    }
}

SCENARIO("Loading a note indents its lists without touching the undo stack", "[text_formatting]")
{
    // Regression: a previous attempt applied indent from MarkdownHighlighter::
    // highlightBlock(), which runs REENTRANTLY inside
    // QTextDocumentPrivate::finishEdit() during setPlainText() — with undo
    // disabled and no edit block to join. That crashed (SIGSEGV) on the note-
    // loading path. Indent is applied here after setPlainText() returns instead,
    // still inside the load's undo-disabled window.
    GIVEN("a buffer loaded the way DocumentHandler::setMarkdown loads one")
    {
        QTextDocument doc;
        doc.documentLayout();
        doc.setUndoRedoEnabled(false);
        doc.setPlainText(QStringLiteral("- first\n- second\n1. one\n2. two"));
        TextFormatting::applyListIndentToDocument(&doc);
        doc.setUndoRedoEnabled(true);

        THEN("every loaded list item comes back already indented")
        {
            CHECK(doc.findBlockByNumber(0).blockFormat().leftMargin() > 0);
            CHECK(doc.findBlockByNumber(1).blockFormat().leftMargin() > 0);
            CHECK(doc.findBlockByNumber(2).blockFormat().leftMargin() > 0);
            CHECK(doc.findBlockByNumber(3).blockFormat().leftMargin() > 0);
        }

        THEN("the load itself is not undoable — a freshly opened note's first Ctrl+Z is a no-op")
        {
            CHECK_FALSE(doc.isUndoAvailable());
        }
    }
}

SCENARIO("A reactive maintenance pass leaves nothing on the undo stack when nothing changed", "[text_formatting]")
{
    // Bugfix: this pass runs after every keystroke anywhere near a list, and
    // almost every run has nothing to fix. Merging a block format
    // unconditionally would still be a real edit, so each no-op pass pushed its
    // own undo entry — which is what made Ctrl+Z feel like it was cycling
    // through invisible steps instead of stepping back through real edits.
    GIVEN("an already-correctly-indented list")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 1);
        cursor.insertText("item");
        const qreal leftMargin = doc.begin().blockFormat().leftMargin();

        WHEN("the maintenance pass runs many times over, as typing would run it")
        {
            QTextDocument scratch;
            scratch.setPlainText(doc.toPlainText());
            for (int i = 0; i < 25; ++i)
                TextFormatting::maintainHandRolledListNumbering(QTextCursor(doc.begin()));

            THEN("the indent is unchanged")
            {
                CHECK(doc.begin().blockFormat().leftMargin() == leftMargin);
            }

            THEN("not one of those passes was a document edit")
            {
                // Bounded on purpose: never `while (isUndoAvailable())`. If a
                // no-op pass ever starts pushing entries again, this must fail,
                // not spin.
                int undos = 0;
                while (doc.isUndoAvailable() && undos < 50) {
                    doc.undo();
                    ++undos;
                }
                // Only the two real edits (the toggle, and typing "item") are
                // undoable; 25 maintenance passes added nothing.
                CHECK(undos <= 2);
            }
        }
    }
}

SCENARIO("A protected checkbox list puts back boxes an edit destroyed", "[textformatting][checkbox]")
{
    GIVEN("a document of intact checkbox items")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] milk\n- [x] eggs");
        QTextCursor cursor(&doc);

        WHEN("the repair pass runs")
        {
            const TextFormatting::CheckboxRepair repair = TextFormatting::repairCheckboxList(cursor, 0);

            THEN("it reports no change, and the lines are byte-identical")
            {
                // If this fails, every keystroke rewrites the note — repair
                // must be a true no-op over text that's already correct.
                CHECK_FALSE(repair.changed);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] milk"),
                    QStringLiteral("- [x] eggs"),
                });
            }
        }
    }

    GIVEN("a checked item, '- [x] done'")
    {
        QTextDocument doc;
        doc.setPlainText("- [x] done");
        QTextCursor cursor(&doc);

        WHEN("the repair pass runs")
        {
            TextFormatting::repairCheckboxList(cursor, 0);

            THEN("the tick survives repair")
            {
                // Catches a naive implementation that re-marks every line
                // unchecked instead of leaving an already-valid marker alone.
                CHECK(doc.begin().text() == QStringLiteral("- [x] done"));
            }
        }
    }

    GIVEN("a half-backspaced marker, '- [milk'")
    {
        // MarkdownList::parse's bullet pattern is "^[-*+]\\s" — it only looks
        // at the dash and the single space after it, not at what follows. So
        // "- [milk" still matches as an ordinary BULLET (marker length 2,
        // covering just "- "); the checkbox pattern requires a closing "]"
        // that "- [milk" never supplies, so it never matches as a checkbox.
        // Confirmed by reading MarkdownList::parse directly, not guessed: this
        // is the deliberately-left-open case the plan asked to pin down.
        QTextDocument doc;
        doc.setPlainText("- [milk");
        QTextCursor cursor(&doc);

        WHEN("the repair pass runs")
        {
            TextFormatting::repairCheckboxList(cursor, 0);

            THEN("the bullet's own \"- \" is upgraded to a checkbox marker, "
                 "leaving the stray \"[milk\" as the item's own (unparsed) label text")
            {
                CHECK(doc.begin().text() == QStringLiteral("- [ ] [milk"));
            }
        }
    }

    GIVEN("a bullet, '- milk'")
    {
        QTextDocument doc;
        doc.setPlainText("- milk");
        QTextCursor cursor(&doc);

        WHEN("the repair pass runs")
        {
            TextFormatting::repairCheckboxList(cursor, 0);

            THEN("it becomes '- [ ] milk' — upgraded, not double-marked")
            {
                CHECK(doc.begin().text() == QStringLiteral("- [ ] milk"));
            }
        }
    }

    GIVEN("a plain line, 'milk'")
    {
        QTextDocument doc;
        doc.setPlainText("milk");
        QTextCursor cursor(&doc);

        WHEN("the repair pass runs")
        {
            TextFormatting::repairCheckboxList(cursor, 0);

            THEN("it becomes '- [ ] milk'")
            {
                CHECK(doc.begin().text() == QStringLiteral("- [ ] milk"));
            }
        }
    }

    GIVEN("an empty line between two checkbox items")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] milk\n\n- [ ] eggs");
        QTextCursor cursor(&doc);

        WHEN("the repair pass runs")
        {
            TextFormatting::repairCheckboxList(cursor, 0);

            THEN("the blank line stays empty — Enter must still be usable")
            {
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] milk"),
                    QStringLiteral(""),
                    QStringLiteral("- [ ] eggs"),
                });
            }
        }
    }

    GIVEN("a caret sitting inside a plain line's text, about to gain a marker ahead of it")
    {
        QTextDocument doc;
        doc.setPlainText("milk");
        QTextCursor cursor(&doc);

        WHEN("the repair pass runs with the caret at position 2 (inside \"mi|lk\")")
        {
            const TextFormatting::CheckboxRepair repair = TextFormatting::repairCheckboxList(cursor, 2);

            THEN("the returned cursorPosition has moved by the marker's length, "
                 "so the caret is still on the same character")
            {
                REQUIRE(repair.changed);
                // "- [ ] " is 6 characters, inserted ahead of the caret.
                CHECK(repair.cursorPosition == 2 + 6);
                CHECK(doc.begin().text() == QStringLiteral("- [ ] milk"));
                // Confirms "the same character": position 8 in the repaired
                // line is still between 'm' and 'i'.
                CHECK(doc.begin().text().left(repair.cursorPosition) == QStringLiteral("- [ ] mi"));
            }
        }
    }

    GIVEN("a caret on a later line than the one being repaired")
    {
        QTextDocument doc;
        doc.setPlainText("milk\neggs");
        QTextCursor cursor(&doc);
        const int secondLineStart = doc.findBlockByNumber(1).position();

        WHEN("the repair pass runs with the caret on the second line")
        {
            const TextFormatting::CheckboxRepair repair =
                TextFormatting::repairCheckboxList(cursor, secondLineStart + 2);

            THEN("it shifts too — every earlier insertion counts")
            {
                REQUIRE(repair.changed);
                // Both lines gain a 6-character marker ahead of the caret's
                // own line, so the caret moves by 6 twice over.
                CHECK(repair.cursorPosition == secondLineStart + 2 + 2 * 6);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] milk"),
                    QStringLiteral("- [ ] eggs"),
                });
            }
        }
    }
}

SCENARIO("The compose slot's checkbox survives having the whole line deleted", "[textformatting][checkbox]")
{
    GIVEN("a task note with the compose slot cleared but its tasks intact")
    {
        QTextDocument doc;
        doc.setPlainText("\n- [ ] Walk dog\n- [ ] Buy milk");
        QTextCursor cursor(&doc);

        WHEN("ensureFirstLineCheckbox runs with the caret at the start of the empty slot")
        {
            const TextFormatting::CheckboxRepair repair = TextFormatting::ensureFirstLineCheckbox(cursor, 0);

            THEN("the slot regains its marker, and the caret lands where the user types, "
                 "not inside the marker")
            {
                CHECK(repair.changed);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] "),
                    QStringLiteral("- [ ] Walk dog"),
                    QStringLiteral("- [ ] Buy milk"),
                });
                CHECK(repair.cursorPosition == 6);
            }
        }
    }

    GIVEN("a compose slot that is already well-formed")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] \n- [ ] Walk dog");
        QTextCursor cursor(&doc);

        WHEN("ensureFirstLineCheckbox runs")
        {
            const TextFormatting::CheckboxRepair repair = TextFormatting::ensureFirstLineCheckbox(cursor, 0);

            THEN("it reports no change, and the text is byte-identical")
            {
                // If this fails, every keystroke rewrites the note — repair
                // must be a true no-op over a slot that's already correct.
                CHECK_FALSE(repair.changed);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] "),
                    QStringLiteral("- [ ] Walk dog"),
                });
            }
        }
    }

    GIVEN("a compose slot with text already typed into it")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] Call dentist");
        QTextCursor cursor(&doc);

        WHEN("ensureFirstLineCheckbox runs")
        {
            const TextFormatting::CheckboxRepair repair = TextFormatting::ensureFirstLineCheckbox(cursor, 0);

            THEN("it is left untouched — that case belongs to repairCheckboxList, and "
                 "prepending here would give \"- [ ] - [ ] Call dentist\"")
            {
                CHECK_FALSE(repair.changed);
                CHECK(doc.begin().text() == QStringLiteral("- [ ] Call dentist"));
            }
        }
    }

    GIVEN("a well-formed compose slot followed by a blank SECOND line")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] \n\n- [ ] Buy milk");
        QTextCursor cursor(&doc);

        WHEN("ensureFirstLineCheckbox runs")
        {
            const TextFormatting::CheckboxRepair repair = TextFormatting::ensureFirstLineCheckbox(cursor, 0);

            THEN("only the first line is furniture — the blank second line stays blank, "
                 "since a blank task line is still how tryRemoveTaskLine gets its chance "
                 "to remove one")
            {
                CHECK_FALSE(repair.changed);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] "),
                    QStringLiteral(""),
                    QStringLiteral("- [ ] Buy milk"),
                });
            }
        }
    }

    GIVEN("an entirely empty document, as if the whole note had been selected and deleted")
    {
        QTextDocument doc;
        doc.setPlainText("");
        QTextCursor cursor(&doc);

        WHEN("ensureFirstLineCheckbox runs")
        {
            const TextFormatting::CheckboxRepair repair = TextFormatting::ensureFirstLineCheckbox(cursor, 0);

            THEN("the note always has somewhere to type")
            {
                CHECK(repair.changed);
                CHECK(doc.begin().text() == QStringLiteral("- [ ] "));
            }
        }
    }
}

SCENARIO("A ticked checkbox line can be removed outright", "[textformatting][checkbox]")
{
    GIVEN("three items with the middle one ticked")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] milk\n- [x] eggs\n- [ ] bread");

        WHEN("removal is requested at a position on the ticked line")
        {
            QTextCursor cursor = cursorAtLine(doc, 1, 2);
            const bool removed = TextFormatting::removeCheckedCheckboxLine(cursor);

            THEN("it returns true, and exactly the other two lines remain, in order, "
                 "with no blank line left where it was")
            {
                CHECK(removed);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] milk"),
                    QStringLiteral("- [ ] bread"),
                });
            }
        }
    }

    GIVEN("an unticked item, '- [ ] milk'")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] milk");

        WHEN("removal is requested on it")
        {
            QTextCursor cursor = cursorAtLine(doc, 0, 2);
            const bool removed = TextFormatting::removeCheckedCheckboxLine(cursor);

            THEN("it returns false and the document is byte-identical")
            {
                // This is the guard the whole completion-fade animation leans
                // on: if it fails, a slow click could delete a task the user
                // just un-ticked.
                CHECK_FALSE(removed);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] milk"),
                });
            }
        }
    }

    GIVEN("a plain, non-checkbox line")
    {
        QTextDocument doc;
        doc.setPlainText("just some prose");

        WHEN("removal is requested on it")
        {
            QTextCursor cursor = cursorAtLine(doc, 0, 3);
            const bool removed = TextFormatting::removeCheckedCheckboxLine(cursor);

            THEN("it returns false and the document is unchanged")
            {
                CHECK_FALSE(removed);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("just some prose"),
                });
            }
        }
    }

    GIVEN("two items with the LAST one ticked")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] milk\n- [x] eggs");

        WHEN("removal is requested on the last, ticked line")
        {
            QTextCursor cursor = cursorAtLine(doc, 1, 2);
            const bool removed = TextFormatting::removeCheckedCheckboxLine(cursor);

            THEN("it goes, and the line before it does not gain a trailing empty line")
            {
                CHECK(removed);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] milk"),
                });
            }
        }
    }

    GIVEN("the only line in the document, ticked")
    {
        QTextDocument doc;
        doc.setPlainText("- [x] done");

        WHEN("removal is requested on it")
        {
            QTextCursor cursor = cursorAtLine(doc, 0, 2);
            const bool removed = TextFormatting::removeCheckedCheckboxLine(cursor);

            THEN("the document ends up empty, not a document containing a stray blank line")
            {
                CHECK(removed);
                CHECK(doc.blockCount() == 1);
                CHECK(doc.begin().text() == QStringLiteral(""));
                CHECK(plainLines(doc) == QStringList{ QStringLiteral("") });
            }
        }
    }

    GIVEN("a ticked line, '- [x] eggs'")
    {
        WHEN("the caret sits in the marker (position 2)")
        {
            QTextDocument doc;
            doc.setPlainText("- [ ] milk\n- [x] eggs");
            QTextCursor cursor = cursorAtLine(doc, 1, 2);
            const bool removed = TextFormatting::removeCheckedCheckboxLine(cursor);

            THEN("the line is removed just the same")
            {
                CHECK(removed);
                CHECK(plainLines(doc) == QStringList{ QStringLiteral("- [ ] milk") });
            }
        }

        WHEN("the caret sits mid-label")
        {
            QTextDocument doc;
            doc.setPlainText("- [ ] milk\n- [x] eggs");
            QTextCursor cursor = cursorAtLine(doc, 1, 8);
            const bool removed = TextFormatting::removeCheckedCheckboxLine(cursor);

            THEN("the line is removed just the same")
            {
                CHECK(removed);
                CHECK(plainLines(doc) == QStringList{ QStringLiteral("- [ ] milk") });
            }
        }

        WHEN("the caret sits at the very end of the line")
        {
            QTextDocument doc;
            doc.setPlainText("- [ ] milk\n- [x] eggs");
            QTextCursor cursor = cursorAtLine(doc, 1, 10);
            const bool removed = TextFormatting::removeCheckedCheckboxLine(cursor);

            THEN("the line is removed just the same — the caller never has to aim")
            {
                CHECK(removed);
                CHECK(plainLines(doc) == QStringList{ QStringLiteral("- [ ] milk") });
            }
        }
    }
}

SCENARIO("Any line can be removed outright, ticked or not", "[textformatting][checkbox]")
{
    // The unguarded counterpart to removeCheckedCheckboxLine above, and it has
    // to be unguarded: its caller is Backspace on an emptied-out task line,
    // where the line is by definition NOT ticked. Without it, the checkbox
    // repair pass immediately regrew every marker character the user backspaced
    // into, so a task line could never actually be deleted from the note ("you
    // cannot use backspace to delete a note, it will spawn in the [ ]
    // characters").
    GIVEN("three items, none of them ticked")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] milk\n- [ ] \n- [ ] bread");

        WHEN("removal is requested on the emptied middle line")
        {
            QTextCursor cursor = cursorAtLine(doc, 1, 6);
            const bool removed = TextFormatting::removeLine(cursor);

            THEN("it goes, leaving no blank line behind")
            {
                CHECK(removed);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] milk"),
                    QStringLiteral("- [ ] bread"),
                });
            }
        }
    }

    GIVEN("a single-line document")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] milk");

        WHEN("its only line is removed")
        {
            QTextCursor cursor = cursorAtLine(doc, 0, 2);
            const bool removed = TextFormatting::removeLine(cursor);

            THEN("the document ends up empty rather than holding one blank line")
            {
                CHECK(removed);
                CHECK(doc.toPlainText().isEmpty());
            }
        }
    }
}

SCENARIO("A range spanning several lines snaps to whole-line removal",
         "[textformatting][checkbox]")
{
    // Backs NoteWindow.qml's multi-line Backspace/Delete/Cut handling in the
    // TickTick task note (item 007's work order): a selection that starts or
    // ends mid-line, if simply deleted, merges the leftover fragment at each
    // end into a line matching nothing in TickTickTaskList's last render —
    // silently reverting on the next pull with no visible error. removeLines
    // snaps each end outward to its own whole line instead, so the result is
    // always a clean removal of complete task lines, feeding the diff's
    // already-tested bulk-completion path.
    GIVEN("four checkbox items")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] compose\n- [ ] Walk dog\n- [ ] Buy milk\n- [ ] Water plants");

        WHEN("the range starts and ends mid-line, spanning the two middle items")
        {
            QTextCursor cursor(&doc);
            const int start = doc.findBlockByNumber(1).position() + 2; // mid "Walk dog"
            const int end = doc.findBlockByNumber(2).position() + 2;   // mid "Buy milk"
            const bool removed = TextFormatting::removeLines(cursor, start, end);

            THEN("both whole lines are gone — nothing merged, no partial fragment left behind")
            {
                CHECK(removed);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] compose"),
                    QStringLiteral("- [ ] Water plants"),
                });
            }
        }

        WHEN("the range reaches all the way to the document's last line")
        {
            QTextCursor cursor(&doc);
            const int start = doc.findBlockByNumber(1).position() + 2;
            const int end = doc.findBlockByNumber(3).position() + 2; // mid "Water plants"
            const bool removed = TextFormatting::removeLines(cursor, start, end);

            THEN("every line from the first touched through the last goes, with no trailing blank line")
            {
                CHECK(removed);
                CHECK(plainLines(doc) == QStringList{ QStringLiteral("- [ ] compose") });
            }
        }

        WHEN("start and end are given in reverse order")
        {
            QTextCursor cursor(&doc);
            const int later = doc.findBlockByNumber(2).position() + 2;
            const int earlier = doc.findBlockByNumber(1).position() + 2;
            const bool removed = TextFormatting::removeLines(cursor, later, earlier);

            THEN("the result is identical to passing them start-before-end")
            {
                CHECK(removed);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] compose"),
                    QStringLiteral("- [ ] Water plants"),
                });
            }
        }

        WHEN("the removal is undone")
        {
            QTextCursor cursor(&doc);
            const int start = doc.findBlockByNumber(1).position() + 2;
            const int end = doc.findBlockByNumber(2).position() + 2;
            TextFormatting::removeLines(cursor, start, end);

            THEN("one undo restores every line removed, not just part of it")
            {
                REQUIRE(doc.isUndoAvailable());
                doc.undo();
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] compose"),
                    QStringLiteral("- [ ] Walk dog"),
                    QStringLiteral("- [ ] Buy milk"),
                    QStringLiteral("- [ ] Water plants"),
                });
            }
        }
    }

    GIVEN("a range that reaches back to line 0 — the compose slot")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] compose\n- [ ] Walk dog\n- [ ] Buy milk");

        WHEN("the range starts inside line 0 and ends inside line 1")
        {
            QTextCursor cursor(&doc);
            const int start = 2; // inside "compose"
            const int end = doc.findBlockByNumber(1).position() + 2; // mid "Walk dog"
            const bool removed = TextFormatting::removeLines(cursor, start, end);

            THEN("line 0 is left byte-for-byte alone — removal starts from line 1 instead")
            {
                CHECK(removed);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] compose"),
                    QStringLiteral("- [ ] Buy milk"),
                });
            }
        }

        WHEN("the whole range falls inside line 0 alone")
        {
            QTextCursor cursor(&doc);
            const bool removed = TextFormatting::removeLines(cursor, 0, 6);

            THEN("nothing is removed — line 0 can never be the line that goes")
            {
                CHECK_FALSE(removed);
                CHECK(plainLines(doc) == QStringList{
                    QStringLiteral("- [ ] compose"),
                    QStringLiteral("- [ ] Walk dog"),
                    QStringLiteral("- [ ] Buy milk"),
                });
            }
        }
    }
}

SCENARIO("A list item's label can be read, and its start located",
         "[textformatting][list]")
{
    // Two different questions about the same boundary. listLabelText answers
    // "is this item blank" (what decides whether the compose slot shows a check
    // button); listLabelStart answers "is the caret exactly at the start of this
    // item's text" (what decides whether Backspace removes the line). The
    // caret-placement listLabelPosition can answer neither, because it never
    // moves a caret backwards — it equals the caret's own position for every
    // caret at or past the label.
    GIVEN("a checkbox item with a title, an empty one, and a plain line")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] milk\n- [ ] \njust prose");

        THEN("listLabelText returns the text after the marker, or the whole line")
        {
            CHECK(TextFormatting::listLabelText(cursorAtLine(doc, 0, 0)) == "milk");
            CHECK(TextFormatting::listLabelText(cursorAtLine(doc, 1, 0)).isEmpty());
            CHECK(TextFormatting::listLabelText(cursorAtLine(doc, 2, 0)) == "just prose");
        }

        THEN("listLabelStart returns the boundary itself, wherever the caret is")
        {
            // Same answer from a caret inside the marker and from one past it —
            // which is exactly what listLabelPosition cannot do.
            const int expected = doc.findBlockByNumber(0).position() + 6;
            CHECK(TextFormatting::listLabelStart(cursorAtLine(doc, 0, 2)) == expected);
            CHECK(TextFormatting::listLabelStart(cursorAtLine(doc, 0, 8)) == expected);
            CHECK(TextFormatting::listLabelPosition(cursorAtLine(doc, 0, 8))
                  != expected);
        }

        THEN("a non-list line's label starts at the line itself")
        {
            CHECK(TextFormatting::listLabelStart(cursorAtLine(doc, 2, 4))
                  == doc.findBlockByNumber(2).position());
        }
    }
}
