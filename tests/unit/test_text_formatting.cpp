#include <catch2/catch_test_macros.hpp>

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

            THEN("the same list converts in place to numbered, not a dash bullet")
            {
                const QString md = doc.toMarkdown().trimmed();
                CHECK(md == "1.  Buy milk");
            }
        }
    }
}

SCENARIO("Toggling a numbered list on the current block", "[text_formatting]")
{
    GIVEN("a document with a single paragraph")
    {
        QTextDocument doc;
        doc.setPlainText("Buy milk");

        WHEN("numbered list is toggled on")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleNumberedList(cursor);

            THEN("the markdown serialization is a decimal-prefixed list item")
            {
                CHECK(doc.toMarkdown().contains("1.  Buy milk"));
            }
        }

        WHEN("numbered list is toggled on, then off again")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleNumberedList(cursor);
            TextFormatting::toggleNumberedList(cursor);

            THEN("it is a plain paragraph again")
            {
                const QString md = doc.toMarkdown().trimmed();
                CHECK(md == "Buy milk");
            }
        }

        WHEN("bullet list is toggled on while a numbered list is already active")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleNumberedList(cursor);
            TextFormatting::toggleBulletList(cursor);

            THEN("the same list converts in place to a dash bullet, not numbered")
            {
                const QString md = doc.toMarkdown().trimmed();
                CHECK(md == "- Buy milk");
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
                QTextList *list = blockCursor.currentList();
                REQUIRE(list != nullptr);
                CHECK(list->format().start() == 5);
            }

            THEN("the markdown reflects the custom start number")
            {
                CHECK(doc.toMarkdown().contains("5.  Fifth item"));
            }
        }

        WHEN("no start number is given")
        {
            QTextCursor blockCursor(&doc);
            blockCursor.setPosition(doc.findBlockByNumber(1).position());
            TextFormatting::toggleNumberedList(blockCursor);

            THEN("it defaults to starting at 1, same as before this parameter existed")
            {
                QTextList *list = blockCursor.currentList();
                REQUIRE(list != nullptr);
                CHECK(list->format().start() == 1);
            }
        }

        WHEN("converting an already-existing list's style in place")
        {
            QTextCursor blockCursor(&doc);
            blockCursor.setPosition(doc.findBlockByNumber(1).position());
            TextFormatting::toggleBulletList(blockCursor);
            TextFormatting::toggleNumberedList(blockCursor, 7);

            THEN("the startNumber argument is ignored — style-only conversions don't reset the count")
            {
                QTextList *list = blockCursor.currentList();
                REQUIRE(list != nullptr);
                CHECK(list->format().start() == 1);
            }
        }
    }
}

SCENARIO("Creating a list tightens the document's indent width so the marker sits near the text's own left edge", "[text_formatting]")
{
    GIVEN("a document using Qt's oversized default indentWidth")
    {
        QTextDocument doc;
        doc.setPlainText("Buy milk");
        CHECK(doc.indentWidth() == 40.0); // Qt's default, confirms the fixture predates the fix

        WHEN("a numbered list is created")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleNumberedList(cursor);

            THEN("indentWidth shrinks to fit the marker instead of Qt's oversized 40px default")
            {
                CHECK(doc.indentWidth() < 40.0);
                CHECK(doc.indentWidth() > 0.0);
            }
        }
    }

    GIVEN("a document using Qt's oversized default indentWidth")
    {
        QTextDocument doc;
        doc.setPlainText("Buy milk");

        WHEN("a bullet list is created")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleBulletList(cursor);

            THEN("indentWidth shrinks the same way as for a numbered list")
            {
                CHECK(doc.indentWidth() < 40.0);
                CHECK(doc.indentWidth() > 0.0);
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
