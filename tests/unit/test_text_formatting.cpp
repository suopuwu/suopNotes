#include <catch2/catch_test_macros.hpp>

#include <QFontMetricsF>
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
                CHECK(doc.begin().text() == "Buy milk"); // QTextList bullet marker isn't part of .text()
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

SCENARIO("Creating a bullet list tightens the document's indent width so the marker sits near the text's own left edge", "[text_formatting]")
{
    GIVEN("a document using Qt's oversized default indentWidth")
    {
        QTextDocument doc;
        doc.setPlainText("Buy milk");
        CHECK(doc.indentWidth() == 40.0); // Qt's default, confirms the fixture predates the fix

        WHEN("a bullet list is created")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleBulletList(cursor);

            THEN("indentWidth shrinks to fit the marker instead of Qt's oversized 40px default")
            {
                CHECK(doc.indentWidth() < 40.0);
                CHECK(doc.indentWidth() > 0.0);
            }
        }
    }
}

SCENARIO("A numbered list's hanging-indent buffer is sized per-list, to its own widest marker", "[text_formatting]")
{
    // Feature request: the buffer used to be a single value shared by the
    // WHOLE document (Qt's own indentWidth, sized to fit a two-digit
    // marker no matter what), so a document with any double-digit list
    // anywhere gave every single-digit list the same wide buffer too, and
    // it didn't persist across save/reload at all. Numbered markers are
    // literal text now (see TextFormatting.h), so the buffer is just a
    // per-block hanging indent (leftMargin/textIndent) sized from that
    // list's own actual widest marker — nothing document-wide involved.
    GIVEN("a document with only single-digit numbered items")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 1);
        cursor.insertBlock();
        TextFormatting::toggleNumberedList(cursor);

        THEN("the reserved buffer is sized for a single-digit marker, not two")
        {
            const QTextBlockFormat format = doc.begin().blockFormat();
            const QFontMetricsF metrics(doc.begin().charFormat().font());
            const qreal oneDigitWidth = metrics.horizontalAdvance(QStringLiteral("2. "));
            const qreal twoDigitWidth = metrics.horizontalAdvance(QStringLiteral("12. "));
            CHECK(format.leftMargin() < twoDigitWidth);
            CHECK(format.leftMargin() >= oneDigitWidth - 1.0); // small float slack
        }
    }

    GIVEN("a document with a double-digit numbered item")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 11);

        THEN("the reserved buffer grows to fit the two-digit marker")
        {
            const QTextBlockFormat format = doc.begin().blockFormat();
            const QFontMetricsF metrics(doc.begin().charFormat().font());
            const qreal oneDigitWidth = metrics.horizontalAdvance(QStringLiteral("2. "));
            CHECK(format.leftMargin() > oneDigitWidth);
        }
    }

    GIVEN("two SEPARATE single-digit lists, elsewhere in the same document a double-digit list also exists")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("first list:");
        cursor.insertBlock();
        TextFormatting::toggleNumberedList(cursor, 1);
        cursor.insertBlock();
        cursor.insertText("between the two lists");
        cursor.insertBlock();
        TextFormatting::toggleNumberedList(cursor, 11); // starts double-digit immediately

        THEN("the single-digit list's own buffer is unaffected by the unrelated double-digit list")
        {
            const QTextBlockFormat firstListFormat = doc.findBlockByNumber(1).blockFormat();
            const QTextBlockFormat secondListFormat = doc.findBlockByNumber(3).blockFormat();
            CHECK(firstListFormat.leftMargin() < secondListFormat.leftMargin());
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
    GIVEN("a document with a single paragraph")
    {
        QTextDocument doc;
        doc.setPlainText("Buy milk");

        WHEN("checkbox list is toggled on")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleCheckboxList(cursor);

            THEN("the block's own text carries a literal, valid-GFM \"- [ ] \" marker")
            {
                CHECK(doc.begin().text() == "- [ ] Buy milk");
                CHECK(TextFormatting::isCheckboxList(cursor));
                CHECK_FALSE(TextFormatting::isCheckboxChecked(cursor));
            }
        }

        WHEN("checkbox list is toggled on, then off again")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleCheckboxList(cursor);
            TextFormatting::toggleCheckboxList(cursor);

            THEN("it is a plain paragraph again, marker text and all")
            {
                CHECK(doc.begin().text() == "Buy milk");
                CHECK_FALSE(TextFormatting::isCheckboxList(cursor));
            }
        }

        WHEN("numbered list is toggled on while a checkbox list is already active")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleCheckboxList(cursor);
            TextFormatting::toggleNumberedList(cursor);

            THEN("the same block converts in place to numbered, not a checkbox")
            {
                CHECK(doc.begin().text() == "1. Buy milk");
                CHECK(TextFormatting::isNumberedList(cursor));
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
        QTextCursor cursor(&doc);
        TextFormatting::toggleCheckboxList(cursor);
        cursor.insertText("Buy milk");

        WHEN("its checked state is toggled")
        {
            const bool toggled = TextFormatting::toggleCheckboxChecked(cursor);

            THEN("it flips to checked, marker text and all")
            {
                CHECK(toggled);
                CHECK(doc.begin().text() == "- [x] Buy milk");
                CHECK(TextFormatting::isCheckboxChecked(cursor));
            }

            WHEN("it's toggled again")
            {
                TextFormatting::toggleCheckboxChecked(cursor);

                THEN("it flips back to unchecked")
                {
                    CHECK(doc.begin().text() == "- [ ] Buy milk");
                    CHECK_FALSE(TextFormatting::isCheckboxChecked(cursor));
                }
            }
        }
    }

    GIVEN("a plain paragraph, not a checkbox item at all")
    {
        QTextDocument doc;
        doc.setPlainText("just some text");

        WHEN("a checked-state toggle is attempted")
        {
            QTextCursor cursor(&doc);
            const bool toggled = TextFormatting::toggleCheckboxChecked(cursor);

            THEN("nothing happens — there's no checkbox to toggle")
            {
                CHECK_FALSE(toggled);
                CHECK(doc.begin().text() == "just some text");
            }
        }
    }
}

SCENARIO("Click-to-toggle hit-tests against the checkbox glyph itself", "[text_formatting]")
{
    GIVEN("a checkbox item, \"- [ ] Buy milk\"")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleCheckboxList(cursor);
        cursor.insertText("Buy milk");

        WHEN("the position is within the glyph itself (anywhere in \"- [ ] \")")
        {
            QTextCursor hitCursor(&doc);
            hitCursor.setPosition(3); // inside "[ ]"

            THEN("it toggles the checkbox")
            {
                CHECK(TextFormatting::tryToggleCheckboxAtPosition(hitCursor));
                CHECK(doc.begin().text() == "- [x] Buy milk");
            }
        }

        WHEN("the position is past the glyph, in the item's real text")
        {
            QTextCursor missCursor(&doc);
            missCursor.setPosition(doc.begin().position() + 8); // inside "Buy milk"

            THEN("it does not toggle — a click there should just place the cursor")
            {
                CHECK_FALSE(TextFormatting::tryToggleCheckboxAtPosition(missCursor));
                CHECK(doc.begin().text() == "- [ ] Buy milk");
            }
        }
    }
}

SCENARIO("Inserting a starter table at the cursor", "[text_formatting]")
{
    GIVEN("an empty document")
    {
        QTextDocument doc;

        WHEN("a table is inserted")
        {
            QTextCursor cursor(&doc);
            TextFormatting::insertTable(cursor);

            THEN("the markdown serialization is a 2x2 GFM table with a header row")
            {
                const QString md = doc.toMarkdown();
                CHECK(md.contains("Header 1"));
                CHECK(md.contains("Header 2"));
                CHECK(md.contains("---"));
            }
        }
    }
}

SCENARIO("Inserting a table row", "[text_formatting]")
{
    GIVEN("a 2x2 table with the cursor in its first cell")
    {
        // Built via QTextCursor::insertTable() directly (not
        // TextFormatting::insertTable(), which takes its cursor by value —
        // the mutation that lands it inside the new table happens on that
        // internal copy, not on the caller's own `cursor`, so a test that
        // needs `cursor` to genuinely be inside the table afterward has to
        // put it there itself).
        QTextDocument doc;
        QTextCursor cursor(&doc);
        QTextTableFormat format;
        QTextTable *table = cursor.insertTable(2, 2, format);
        REQUIRE(table != nullptr);

        WHEN("a row is inserted")
        {
            TextFormatting::insertTableRow(cursor);

            THEN("the table gains a third row")
            {
                CHECK(table->rows() == 3);
            }
        }
    }

    GIVEN("a document with no table at all")
    {
        QTextDocument doc;
        doc.setPlainText("just some text");

        WHEN("a row insertion is attempted at the cursor")
        {
            QTextCursor cursor(&doc);
            TextFormatting::insertTableRow(cursor);

            THEN("nothing changes — there's no table to add a row to")
            {
                CHECK(doc.toPlainText() == "just some text");
            }
        }
    }
}

SCENARIO("Deleting a table row", "[text_formatting]")
{
    GIVEN("a 2x2 table with the cursor in its first cell")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        QTextTableFormat format;
        QTextTable *table = cursor.insertTable(2, 2, format);
        REQUIRE(table != nullptr);

        WHEN("the row under the cursor is deleted")
        {
            TextFormatting::deleteTableRow(cursor);

            THEN("the table drops to a single row and the table itself survives")
            {
                CHECK(table->rows() == 1);
            }
        }

        WHEN("the table's only remaining row is deleted")
        {
            TextFormatting::deleteTableRow(cursor);
            TextFormatting::deleteTableRow(cursor);

            THEN("the whole table is removed rather than left with zero rows")
            {
                CHECK(cursor.currentTable() == nullptr);
            }
        }
    }
}

SCENARIO("Inserting a table column", "[text_formatting]")
{
    GIVEN("a 2x2 table with the cursor in its first cell")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        QTextTableFormat format;
        QTextTable *table = cursor.insertTable(2, 2, format);
        REQUIRE(table != nullptr);

        WHEN("a column is inserted")
        {
            TextFormatting::insertTableColumn(cursor);

            THEN("the table gains a third column")
            {
                CHECK(table->columns() == 3);
            }
        }
    }
}

SCENARIO("Deleting a table column", "[text_formatting]")
{
    GIVEN("a 2x2 table with the cursor in its first cell")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        QTextTableFormat format;
        QTextTable *table = cursor.insertTable(2, 2, format);
        REQUIRE(table != nullptr);

        WHEN("the column under the cursor is deleted")
        {
            TextFormatting::deleteTableColumn(cursor);

            THEN("the table drops to a single column and the table itself survives")
            {
                CHECK(table->columns() == 1);
            }
        }

        WHEN("the table's only remaining column is deleted")
        {
            TextFormatting::deleteTableColumn(cursor);
            TextFormatting::deleteTableColumn(cursor);

            THEN("the whole table is removed rather than left with zero columns")
            {
                CHECK(cursor.currentTable() == nullptr);
            }
        }
    }
}

SCENARIO("Deleting a whole table", "[text_formatting]")
{
    GIVEN("a paragraph, a 2x2 table, and another paragraph")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("intro");
        cursor.insertBlock();
        QTextTableFormat format;
        QTextTable *table = cursor.insertTable(2, 2, format);
        REQUIRE(table != nullptr);
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
        cursor.insertText("outro");

        WHEN("the table is deleted from a fresh cursor placed inside it")
        {
            QTextCursor tableCursor(&doc);
            tableCursor.setPosition(table->firstPosition());
            REQUIRE(tableCursor.currentTable() == table);

            TextFormatting::deleteTable(tableCursor);

            THEN("the table is gone but the surrounding paragraphs remain")
            {
                const QString text = doc.toPlainText();
                CHECK(text.contains("intro"));
                CHECK(text.contains("outro"));
                CHECK(!text.contains("Header"));
            }
        }
    }

    GIVEN("a document with no table at all")
    {
        QTextDocument doc;
        doc.setPlainText("just some text");

        WHEN("a table deletion is attempted at the cursor")
        {
            QTextCursor cursor(&doc);
            TextFormatting::deleteTable(cursor);

            THEN("nothing changes — there's no table to delete")
            {
                CHECK(doc.toPlainText() == "just some text");
            }
        }
    }
}

SCENARIO("Querying whether the cursor is inside a table", "[text_formatting]")
{
    GIVEN("a paragraph, a 2x2 table, and another paragraph")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("intro");
        cursor.insertBlock();
        QTextTableFormat format;
        QTextTable *table = cursor.insertTable(2, 2, format);
        REQUIRE(table != nullptr);
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
        cursor.insertText("outro");

        WHEN("the cursor sits inside the table")
        {
            QTextCursor tableCursor(&doc);
            tableCursor.setPosition(table->firstPosition());

            THEN("isInTable reports true")
            {
                CHECK(TextFormatting::isInTable(tableCursor) == true);
            }
        }

        WHEN("the cursor sits in the paragraph before the table")
        {
            QTextCursor beforeCursor(&doc);
            beforeCursor.setPosition(0);

            THEN("isInTable reports false")
            {
                CHECK(TextFormatting::isInTable(beforeCursor) == false);
            }
        }

        WHEN("the cursor sits in the paragraph after the table")
        {
            QTextCursor afterCursor(&doc);
            afterCursor.setPosition(doc.characterCount() - 1);

            THEN("isInTable reports false")
            {
                CHECK(TextFormatting::isInTable(afterCursor) == false);
            }
        }
    }

    GIVEN("a document with no table at all")
    {
        QTextDocument doc;
        doc.setPlainText("just some text");

        WHEN("isInTable is queried anywhere in it")
        {
            QTextCursor cursor(&doc);

            THEN("it reports false")
            {
                CHECK(TextFormatting::isInTable(cursor) == false);
            }
        }
    }
}
