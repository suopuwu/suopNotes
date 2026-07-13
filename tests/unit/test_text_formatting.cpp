#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFontMetricsF>
#include <QRegularExpression>
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

SCENARIO("Setting a heading level actually changes how it looks, not just how it saves", "[text_formatting]")
{
    // Feature request / bug: headingLevel alone has NO visual effect when
    // applied via the API directly — Qt's markdown PARSER bakes heading
    // appearance (bold + a scaled font size) into the actual CHAR format
    // only when building a heading fresh from "# " syntax; toggling
    // headingLevel through the toolbar bypassed that, so it saved
    // correctly but looked identical to plain text on screen.
    GIVEN("a document with a single paragraph at a known base font size")
    {
        QTextDocument doc;
        doc.setDefaultFont(QFont(QStringLiteral("Sans"), 14));
        doc.setPlainText("Section Title");
        const qreal baseSize = QTextCursor(&doc).charFormat().font().pointSizeF();

        WHEN("heading level 1 is applied")
        {
            QTextCursor cursor(&doc);
            TextFormatting::setHeadingLevel(cursor, 1);

            THEN("the line is bold and twice the base size, matching Qt's own h1 scaling")
            {
                QTextCursor checkCursor(&doc);
                checkCursor.select(QTextCursor::LineUnderCursor);
                const QFont font = checkCursor.charFormat().font();
                CHECK(font.bold());
                CHECK(font.pointSizeF() == Catch::Approx(baseSize * 2.0));
            }
        }

        WHEN("heading level 2 is applied")
        {
            QTextCursor cursor(&doc);
            TextFormatting::setHeadingLevel(cursor, 2);

            THEN("the line is bold and 1.5x the base size, matching Qt's own h2 scaling")
            {
                QTextCursor checkCursor(&doc);
                checkCursor.select(QTextCursor::LineUnderCursor);
                const QFont font = checkCursor.charFormat().font();
                CHECK(font.bold());
                CHECK(font.pointSizeF() == Catch::Approx(baseSize * 1.5));
            }
        }

        WHEN("heading level 1 is applied, then cleared back to level 0")
        {
            QTextCursor cursor(&doc);
            TextFormatting::setHeadingLevel(cursor, 1);
            TextFormatting::setHeadingLevel(cursor, 0);

            THEN("the size and weight are restored exactly, not left scaled up")
            {
                QTextCursor checkCursor(&doc);
                checkCursor.select(QTextCursor::LineUnderCursor);
                const QFont font = checkCursor.charFormat().font();
                CHECK_FALSE(font.bold());
                CHECK(font.pointSizeF() == Catch::Approx(baseSize));
            }
        }

        // Bug found live: pressing Enter at the end of a heading line (as
        // DocumentHandler::tryResetHeadingOnReturn does — insertBlock(),
        // then setHeadingLevel(0) on the brand new, still-empty block)
        // looked like the heading style carried onto the new line, and
        // still-bold text survived a reload even though the new block's
        // headingLevel itself correctly came back 0. Reproduces that exact
        // sequence directly against TextFormatting rather than through
        // DocumentHandler/QML.
        WHEN("heading level 1 is applied, Enter is pressed (insertBlock), and the new empty block is reset to level 0")
        {
            QTextCursor cursor(&doc);
            TextFormatting::setHeadingLevel(cursor, 1);

            cursor.movePosition(QTextCursor::EndOfBlock);
            cursor.insertBlock();
            TextFormatting::setHeadingLevel(cursor, 0);

            THEN("text subsequently typed into that new, still-empty block is not bold or heading-sized")
            {
                cursor.insertText("plain body text");

                QTextCursor checkCursor(&doc);
                checkCursor.movePosition(QTextCursor::End);
                checkCursor.select(QTextCursor::LineUnderCursor);
                const QFont font = checkCursor.charFormat().font();
                CHECK_FALSE(font.bold());
                CHECK(font.pointSizeF() == Catch::Approx(baseSize));
            }
        }
    }
}

SCENARIO("A merged block's char format is reconciled to its own (post-merge) heading level", "[text_formatting]")
{
    // Bug found live: deleting the newline between a plain paragraph and a
    // heading (Backspace at the start of one, Delete at the end of the
    // other) merges them into a single block whose headingLevel is exactly
    // as if it had always been that way — QTextCursor::mergeBlockFormat
    // resolves cleanly to the surviving block's own format — but each side's
    // bold/size CHAR format is untouched by the merge, since that's a
    // per-character property, not a block one. The merged line visibly
    // disagreed with its own heading level until the next full markdown
    // round-trip (reload), which re-derives char format from headingLevel +
    // text fresh via Qt's own parser. maintainHeadingCharFormatAfterMerge is
    // what DocumentHandler::onContentsChange calls the instant it detects a
    // real block merge (blockCount going down), so the fix lands
    // immediately instead of only on reload.
    GIVEN("a heading followed by a plain paragraph")
    {
        QTextDocument doc;
        const qreal baseSize = QTextCursor(&doc).charFormat().font().pointSizeF();
        QTextCursor cursor(&doc);
        TextFormatting::setHeadingLevel(cursor, 1);
        cursor.insertText("Header Text");
        cursor.insertBlock();
        TextFormatting::setHeadingLevel(cursor, 0);
        cursor.insertText("plain text");

        WHEN("the newline between them is deleted (Backspace at the start of the plain line), merging it into the heading")
        {
            const int mergePoint = doc.begin().next().position();
            QTextCursor deleteSeparator(&doc);
            deleteSeparator.setPosition(mergePoint - 1);
            deleteSeparator.setPosition(mergePoint, QTextCursor::KeepAnchor);
            deleteSeparator.removeSelectedText(); // the actual Qt-default merge, format untouched

            QTextCursor mergeCursor(&doc);
            mergeCursor.setPosition(mergePoint - 1); // where the separator used to be — contentsChange's own `from`
            TextFormatting::maintainHeadingCharFormatAfterMerge(mergeCursor);

            THEN("the whole merged line — including the text absorbed from the plain paragraph — is bold and heading-sized")
            {
                REQUIRE(doc.blockCount() == 1);
                CHECK(doc.begin().text() == "Header Textplain text");
                QTextCursor checkCursor(&doc);
                checkCursor.select(QTextCursor::Document);
                const QFont font = checkCursor.charFormat().font();
                CHECK(font.bold());
                CHECK(font.pointSizeF() == Catch::Approx(baseSize * 2));
            }
        }
    }

    GIVEN("a plain paragraph followed by a heading")
    {
        QTextDocument doc;
        const qreal baseSize = QTextCursor(&doc).charFormat().font().pointSizeF();
        QTextCursor cursor(&doc);
        cursor.insertText("plain text");
        cursor.insertBlock();
        TextFormatting::setHeadingLevel(cursor, 1);
        cursor.insertText("Header Text");

        WHEN("the newline between them is deleted (Backspace at the start of the heading), demoting the merged line to a plain paragraph")
        {
            const int mergePoint = doc.begin().next().position();
            QTextCursor deleteSeparator(&doc);
            deleteSeparator.setPosition(mergePoint - 1);
            deleteSeparator.setPosition(mergePoint, QTextCursor::KeepAnchor);
            deleteSeparator.removeSelectedText(); // the actual Qt-default merge, format untouched

            QTextCursor mergeCursor(&doc);
            mergeCursor.setPosition(mergePoint - 1); // where the separator used to be — contentsChange's own `from`
            TextFormatting::maintainHeadingCharFormatAfterMerge(mergeCursor);

            THEN("the former heading text is no longer bold or oversized — it reads as ordinary text, matching its now-plain block")
            {
                REQUIRE(doc.blockCount() == 1);
                CHECK(doc.begin().text() == "plain textHeader Text");
                QTextCursor checkCursor(&doc);
                checkCursor.select(QTextCursor::Document);
                const QFont font = checkCursor.charFormat().font();
                CHECK_FALSE(font.bold());
                CHECK(font.pointSizeF() == Catch::Approx(baseSize));
            }
        }
    }

    GIVEN("two plain paragraphs, one with a deliberately bolded word in it (an unrelated toggleBold, not heading-driven)")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("plain start word");
        const int boldStart = doc.begin().text().indexOf("word");
        QTextCursor boldCursor(&doc);
        boldCursor.setPosition(boldStart);
        boldCursor.setPosition(boldStart + 4, QTextCursor::KeepAnchor);
        TextFormatting::toggleBold(boldCursor);
        cursor.insertBlock();
        cursor.insertText("second block");

        WHEN("the newline between the two plain blocks is deleted, merging them")
        {
            const int mergePoint = doc.begin().next().position();
            QTextCursor deleteSeparator(&doc);
            deleteSeparator.setPosition(mergePoint - 1);
            deleteSeparator.setPosition(mergePoint, QTextCursor::KeepAnchor);
            deleteSeparator.removeSelectedText();

            QTextCursor mergeCursor(&doc);
            mergeCursor.setPosition(mergePoint - 1);
            TextFormatting::maintainHeadingCharFormatAfterMerge(mergeCursor);

            THEN("the deliberately-bolded word survives untouched — only the merge boundary was ever in scope")
            {
                REQUIRE(doc.blockCount() == 1);
                QTextCursor checkCursor(&doc);
                checkCursor.setPosition(boldStart);
                checkCursor.setPosition(boldStart + 4, QTextCursor::KeepAnchor);
                CHECK(checkCursor.charFormat().font().bold());
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

    // Bugfix: growing a list from 9 to 10 items widens every item's own
    // marker-width buffer ("9. " -> "10. "), continueHandRolledList calls
    // renumberRun on every item as it's added — including item 9, every
    // time a later item joins the run. Regression coverage for item 9
    // specifically getting left behind at the old, narrower buffer.
    GIVEN("a numbered list grown one item at a time up to 10 items")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 1);
        cursor.insertText("item 1");
        for (int i = 2; i <= 10; ++i) {
            TextFormatting::continueHandRolledList(cursor);
            cursor.insertText(QStringLiteral("item %1").arg(i));
        }

        THEN("every item, including item 9, ends up with the same widened leftMargin as item 10")
        {
            const QTextBlockFormat item9Format = doc.findBlockByNumber(8).blockFormat();
            const QTextBlockFormat item10Format = doc.findBlockByNumber(9).blockFormat();
            CHECK(doc.findBlockByNumber(8).text() == "9. item 9");
            CHECK(doc.findBlockByNumber(9).text() == "10. item 10");
            CHECK(item9Format.leftMargin() == item10Format.leftMargin());
        }
    }
}

SCENARIO("A numbered list reserves the same space for every item's marker, regardless of its own digit count", "[text_formatting]")
{
    // Bugfix: the hanging-indent leftMargin (previous scenario) is shared
    // across the whole run, sized to the widest marker ("10. ") — that
    // alone is enough to line up every WRAPPED continuation line and every
    // OTHER item's own first line, but item 9's own marker is still just
    // "9. " (one character narrower than "10. "). Since a block's first line
    // renders at leftMargin+textIndent while every wrapped line renders at
    // leftMargin alone, sharing the same textIndent as the rest of the run
    // put item 9's own first-line text one marker's-worth of width further
    // left than everything else — reported live, described as "the whole
    // line should be preindented, leaving some space to the left of the 9".
    // Sizing each block's own textIndent to ITS OWN marker's actual width
    // (not the run's shared one) reserves exactly as much blank space to
    // the left of a narrower marker as the wider one already occupies, so
    // the real text after the marker — first line or wrapped — always
    // starts at the same spot.
    GIVEN("a ten-item numbered list")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 1);
        cursor.insertText("item 1");
        for (int i = 2; i <= 10; ++i) {
            TextFormatting::continueHandRolledList(cursor);
            cursor.insertText(QStringLiteral("item %1").arg(i));
        }

        THEN("marker text itself is untouched — no literal padding characters")
        {
            CHECK(doc.findBlockByNumber(0).text() == "1. item 1");
            CHECK(doc.findBlockByNumber(8).text() == "9. item 9");
            CHECK(doc.findBlockByNumber(9).text() == "10. item 10");
        }

        THEN("every item shares the same leftMargin, but item 9 gets a narrower (less negative) textIndent than item 10")
        {
            const QTextBlockFormat item1Format = doc.findBlockByNumber(0).blockFormat();
            const QTextBlockFormat item9Format = doc.findBlockByNumber(8).blockFormat();
            const QTextBlockFormat item10Format = doc.findBlockByNumber(9).blockFormat();

            CHECK(item1Format.leftMargin() == item9Format.leftMargin());
            CHECK(item9Format.leftMargin() == item10Format.leftMargin());
            // item 10's own marker IS the run's widest, so its first line
            // still sits flush left, same as before this fix.
            CHECK(item10Format.leftMargin() == -item10Format.textIndent());
            // item 9's marker is one character narrower — its first line
            // is pulled left by less than the full leftMargin, leaving a
            // sliver of blank space to its own left that item 10 doesn't
            // have.
            CHECK(-item9Format.textIndent() < item9Format.leftMargin());
        }

        THEN("every item's actual text (after its own marker) starts at the same pixel position")
        {
            // The marker itself starts at leftMargin+textIndent — that's
            // where a narrower item's marker is deliberately pulled right
            // to leave blank space to ITS left (previous THEN). What must
            // line up across every item (and match the leftMargin every
            // wrapped continuation line hangs at) is where the real TEXT
            // right after the marker begins: marker-start + the marker's
            // own on-screen width.
            const QFontMetricsF metrics(doc.begin().charFormat().font());
            qreal contentStartX = -1;
            for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
                const QTextBlockFormat format = b.blockFormat();
                const QRegularExpressionMatch match =
                    QRegularExpression(QStringLiteral("^\\d+\\. ")).match(b.text());
                REQUIRE(match.hasMatch());
                const qreal ownMarkerWidth = metrics.horizontalAdvance(match.captured(0));
                const qreal markerStart = format.leftMargin() + format.textIndent();
                const qreal thisContentStart = markerStart + ownMarkerWidth;
                if (contentStartX < 0)
                    contentStartX = thisContentStart;
                else
                    CHECK(thisContentStart == Catch::Approx(contentStartX));
            }
        }
    }

    GIVEN("a single-item list starting above 1 (e.g. \"2. \" shorthand) — nothing to reconcile")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 2);
        cursor.insertText("only item");

        THEN("its own marker is the run's only (hence widest) one, so it sits flush left as usual")
        {
            const QTextBlockFormat format = doc.begin().blockFormat();
            CHECK(doc.begin().text() == "2. only item");
            CHECK(-format.textIndent() == format.leftMargin());
        }
    }
}

SCENARIO("A block that stops being a valid numbered/checkbox item loses its leftover hanging indent", "[text_formatting]")
{
    // Bugfix, reported live: clicking at the very start of a numbered item
    // (right before its "N. " marker) and typing ordinary prose there pushes
    // the marker away from position 0, so it no longer matches
    // numberedMarkerPattern's own "^" requirement — handRolledKind() then
    // correctly reports the block as a plain paragraph (isNumberedList false)
    // — but nothing had ever cleared the block's own leftMargin/textIndent,
    // set back when it WAS a genuine list item. That stale hanging indent
    // survived, silently indenting every WRAPPED continuation line of what
    // now reads as one ordinary (if oddly reformatted) paragraph — the first
    // line looked fine, since textIndent cancels leftMargin there, which is
    // exactly why the bug went unnoticed until the paragraph was long enough
    // to wrap. maintainHandRolledListNumbering (called reactively after
    // every edit, from DocumentHandler::maintainListNumbering) now cleans
    // this up as soon as it sees a block whose tag says it should be a list
    // item but whose marker text says otherwise.
    GIVEN("a numbered item, then plain prose typed directly before its own marker")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 2);
        cursor.insertText("lksjdflksdjf");

        QTextCursor typeCursor(&doc);
        typeCursor.setPosition(doc.begin().position());
        typeCursor.insertText("some regular text before the marker ");

        WHEN("maintainHandRolledListNumbering runs (as it does after every keystroke)")
        {
            TextFormatting::maintainHandRolledListNumbering(QTextCursor(doc.begin()));

            THEN("the block is a plain paragraph with no leftover margin/indent")
            {
                const QTextBlockFormat format = doc.begin().blockFormat();
                CHECK_FALSE(TextFormatting::isNumberedList(QTextCursor(doc.begin())));
                CHECK(format.leftMargin() == 0);
                CHECK(format.textIndent() == 0);
            }
        }
    }
}

SCENARIO("A numbered/checkbox block that loses its tag but keeps its marker text heals from a single nearby edit", "[text_formatting]")
{
    // Bugfix, reported live: occasionally a block in the middle of an
    // otherwise-intact numbered list loses its HandRolledKindProperty tag
    // outright (its marker TEXT is untouched — this is the mirror image of
    // the previous scenario, where the text broke but the tag survived) —
    // e.g. through Qt's own multi-block paste/merge machinery synthesizing a
    // fresh block format that doesn't carry custom properties over. Before
    // this fix, the only way to bring such a block back was to individually
    // re-edit ITS OWN marker (e.g. delete a digit and retype it, which
    // re-triggers the "N. " shorthand auto-conversion) — tedious one line at
    // a time for a whole run of lost items. renumberRun (already re-run
    // reactively after every edit — see DocumentHandler::maintainListNumbering)
    // now heals any adjacent block that still LOOKS like a marker, so a
    // single edit anywhere touching an intact part of the run cascades to
    // every lost item next to it, including ones on the far side of a lost
    // item from where the edit actually happened.
    GIVEN("a 3-item numbered list where the MIDDLE item has silently lost its tag")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleNumberedList(cursor, 1);
        cursor.insertText("item one");
        TextFormatting::continueHandRolledList(cursor);
        cursor.insertText("item two");
        TextFormatting::continueHandRolledList(cursor);
        cursor.insertText("item three");

        // Simulate the tag getting silently dropped from item two (its
        // marker TEXT is untouched) — not reachable through any of this
        // file's own public functions, so reproduced directly the way an
        // external Qt mechanism (e.g. a paste that synthesizes a fresh block
        // format) would leave it.
        const QTextBlock lostBlock = doc.findBlockByNumber(1);
        REQUIRE(lostBlock.text() == "2. item two");
        QTextBlockFormat strippedFormat = lostBlock.blockFormat();
        strippedFormat.setProperty(QTextFormat::UserProperty + 1, static_cast<int>(0)); // HandRolledKindProperty -> None
        QTextCursor(lostBlock).setBlockFormat(strippedFormat);
        REQUIRE_FALSE(TextFormatting::isNumberedList(QTextCursor(lostBlock)));

        WHEN("maintainHandRolledListNumbering runs from an edit on item ONE (not adjacent to the lost item)")
        {
            TextFormatting::maintainHandRolledListNumbering(QTextCursor(doc.findBlockByNumber(0)));

            THEN("item two is healed back to a real numbered item, in the right place in the sequence")
            {
                CHECK(TextFormatting::isNumberedList(QTextCursor(doc.findBlockByNumber(1))));
                CHECK(doc.findBlockByNumber(0).text() == "1. item one");
                CHECK(doc.findBlockByNumber(1).text() == "2. item two");
                CHECK(doc.findBlockByNumber(2).text() == "3. item three");
            }

            THEN("item three (already intact, on the far side of the healed item) keeps its own correct number")
            {
                CHECK(doc.findBlockByNumber(2).text() == "3. item three");
            }

            THEN("all three items share the same hanging-indent buffer")
            {
                const QTextBlockFormat item1 = doc.findBlockByNumber(0).blockFormat();
                const QTextBlockFormat item2 = doc.findBlockByNumber(1).blockFormat();
                const QTextBlockFormat item3 = doc.findBlockByNumber(2).blockFormat();
                CHECK(item1.leftMargin() == item2.leftMargin());
                CHECK(item2.leftMargin() == item3.leftMargin());
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
    GIVEN("a document with a single paragraph")
    {
        QTextDocument doc;
        doc.setPlainText("Buy milk");

        WHEN("checkbox list is toggled on")
        {
            QTextCursor cursor(&doc);
            TextFormatting::toggleCheckboxList(cursor);

            THEN("the block's own text carries a literal, real-checkbox-glyph \"☐ \" marker")
            {
                CHECK(doc.begin().text() == QStringLiteral("☐ Buy milk"));
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
                CHECK(doc.begin().text() == QStringLiteral("☑ Buy milk"));
                CHECK(TextFormatting::isCheckboxChecked(cursor));
            }

            WHEN("it's toggled again")
            {
                TextFormatting::toggleCheckboxChecked(cursor);

                THEN("it flips back to unchecked")
                {
                    CHECK(doc.begin().text() == QStringLiteral("☐ Buy milk"));
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
    GIVEN("a checkbox item, \"☐ Buy milk\"")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleCheckboxList(cursor);
        cursor.insertText("Buy milk");

        WHEN("the position is within the glyph itself (anywhere in \"☐ \")")
        {
            QTextCursor hitCursor(&doc);
            hitCursor.setPosition(1); // inside "☐ ", between the glyph and the space

            THEN("it toggles the checkbox")
            {
                CHECK(TextFormatting::tryToggleCheckboxAtPosition(hitCursor));
                CHECK(doc.begin().text() == QStringLiteral("☑ Buy milk"));
            }
        }

        WHEN("the position is past the glyph, in the item's real text")
        {
            QTextCursor missCursor(&doc);
            missCursor.setPosition(doc.begin().position() + 4); // inside "Buy milk"

            THEN("it does not toggle — a click there should just place the cursor")
            {
                CHECK_FALSE(TextFormatting::tryToggleCheckboxAtPosition(missCursor));
                CHECK(doc.begin().text() == QStringLiteral("☐ Buy milk"));
            }
        }
    }
}

SCENARIO("checkboxMarkerRect locates the checkbox glyph for a hover highlight", "[text_formatting]")
{
    GIVEN("a checkbox item, \"☐ Buy milk\"")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        TextFormatting::toggleCheckboxList(cursor);
        cursor.insertText("Buy milk");
        // Forces Qt to actually lay out the block's QTextLine(s) — a bare
        // QTextDocument with no view/width set never runs its line-breaking
        // pass otherwise, leaving block.layout()->lineCount() at 0.
        doc.setTextWidth(300);

        WHEN("queried at a position within the glyph itself")
        {
            QTextCursor hitCursor(&doc);
            hitCursor.setPosition(1);

            THEN("it returns a real, non-empty rect")
            {
                const QRectF rect = TextFormatting::checkboxMarkerRect(hitCursor);
                CHECK(rect.width() > 0);
                CHECK(rect.height() > 0);
            }
        }

        WHEN("queried past the glyph, in the item's real text")
        {
            QTextCursor missCursor(&doc);
            missCursor.setPosition(doc.begin().position() + 4);

            THEN("it returns an empty rect — nothing to highlight there")
            {
                CHECK(TextFormatting::checkboxMarkerRect(missCursor).isEmpty());
            }
        }
    }

    GIVEN("a plain paragraph, not a checkbox item at all")
    {
        QTextDocument doc;
        doc.setPlainText("just some text");

        WHEN("queried anywhere in it")
        {
            QTextCursor cursor(&doc);
            cursor.setPosition(2);

            THEN("it returns an empty rect")
            {
                CHECK(TextFormatting::checkboxMarkerRect(cursor).isEmpty());
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
