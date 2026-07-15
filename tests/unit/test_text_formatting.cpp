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
