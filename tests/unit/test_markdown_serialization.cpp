#include <catch2/catch_test_macros.hpp>

#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
#include <QTextTableFormat>

#include "core/MarkdownSerialization.h"
#include "core/TextFormatting.h"

using MarkdownSerialization::setMarkdownPreservingBlankLines;
using MarkdownSerialization::toMarkdownPreservingBlankLines;

SCENARIO("A single blank line between two paragraphs round-trips as one blank line", "[markdown_serialization]")
{
    GIVEN("line1, Enter, Enter, line2 — exactly one blank block between them")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("line1");
        cursor.insertBlock();
        cursor.insertBlock();
        cursor.insertText("line2");

        THEN("the output has exactly one blank line, matching Qt's own default")
        {
            CHECK(toMarkdownPreservingBlankLines(&doc) == QStringLiteral("line1\n\nline2\n"));
        }
    }
}

SCENARIO("Multiple consecutive blank lines between paragraphs are preserved", "[markdown_serialization]")
{
    GIVEN("two full blank blocks left between two paragraphs")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("line1");
        cursor.insertBlock();
        cursor.insertBlock();
        cursor.insertBlock();
        cursor.insertText("line2");

        THEN("both blank lines survive, not just one")
        {
            CHECK(toMarkdownPreservingBlankLines(&doc) == QStringLiteral("line1\n\n\nline2\n"));
        }
    }
}

SCENARIO("Leading blank lines are preserved", "[markdown_serialization]")
{
    GIVEN("two blank blocks before any real content")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertBlock();
        cursor.insertBlock();
        cursor.insertText("line1");

        THEN("the output starts with two blank lines instead of dropping them")
        {
            CHECK(toMarkdownPreservingBlankLines(&doc) == QStringLiteral("\n\nline1\n"));
        }
    }
}

SCENARIO("Trailing blank lines are preserved", "[markdown_serialization]")
{
    GIVEN("two blank blocks after the last real content")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("line1");
        cursor.insertBlock();
        cursor.insertBlock();

        THEN("the output ends with two blank lines instead of dropping them")
        {
            CHECK(toMarkdownPreservingBlankLines(&doc) == QStringLiteral("line1\n\n\n"));
        }
    }
}

SCENARIO("A document with no blank spacer blocks matches Qt's own serialization", "[markdown_serialization]")
{
    GIVEN("two adjacent paragraphs with a single Enter between them")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("line1");
        cursor.insertBlock();
        cursor.insertText("line2");

        THEN("there is exactly one blank line, same as the default writer")
        {
            CHECK(toMarkdownPreservingBlankLines(&doc) == QStringLiteral("line1\n\nline2\n"));
        }
    }
}

SCENARIO("A blank line inside a fenced code block is left untouched", "[markdown_serialization]")
{
    GIVEN("a code block containing a blank line as literal content")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("intro");
        cursor.insertBlock();
        QTextBlockFormat codeFormat;
        codeFormat.setProperty(QTextFormat::BlockCodeLanguage, QStringLiteral("cpp"));
        codeFormat.setProperty(QTextFormat::BlockCodeFence, QChar('`'));
        cursor.setBlockFormat(codeFormat);
        cursor.insertText("foo();");
        cursor.insertBlock(codeFormat);
        cursor.insertBlock(codeFormat);
        cursor.insertText("bar();");
        cursor.insertBlock();
        cursor.setBlockFormat(QTextBlockFormat());
        cursor.insertText("outro");

        THEN("the fence and its internal blank line are preserved exactly as Qt renders them")
        {
            const QString md = toMarkdownPreservingBlankLines(&doc);
            CHECK(md.contains("foo();\n\nbar();"));
            CHECK(md.contains("```cpp"));
        }
    }
}

SCENARIO("An empty document produces empty markdown", "[markdown_serialization]")
{
    GIVEN("a freshly constructed, untouched document")
    {
        QTextDocument doc;

        THEN("the output is empty, not a stray blank line")
        {
            CHECK(toMarkdownPreservingBlankLines(&doc).isEmpty());
        }
    }
}

SCENARIO("Rich formatting survives alongside preserved blank lines", "[markdown_serialization]")
{
    GIVEN("a bold first paragraph, two blank lines, then a plain paragraph")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("line1");
        cursor.insertBlock();
        cursor.insertBlock();
        cursor.insertBlock();
        cursor.insertText("line2");

        QTextCursor boldSelection(&doc);
        boldSelection.setPosition(0);
        boldSelection.setPosition(5, QTextCursor::KeepAnchor); // just "line1"
        TextFormatting::toggleBold(boldSelection);

        THEN("the bold marker and both blank lines are both present")
        {
            const QString md = toMarkdownPreservingBlankLines(&doc);
            CHECK(md == QStringLiteral("**line1**\n\n\nline2\n"));
        }
    }
}

SCENARIO("A single plain paragraph round-trips without spurious blank lines", "[markdown_serialization]")
{
    GIVEN("a document holding just one line of text")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("just one line");

        THEN("the output is that line plus a single trailing newline")
        {
            CHECK(toMarkdownPreservingBlankLines(&doc) == QStringLiteral("just one line\n"));
        }
    }
}

SCENARIO("Reloading saved markdown reproduces the same blank-line structure", "[markdown_serialization]")
{
    GIVEN("markdown text with multiple, leading, and trailing blank lines")
    {
        const QString markdown = QStringLiteral("\n\nline1\n\n\n\nline2\n\n\n");

        WHEN("it's loaded into a document with setMarkdownPreservingBlankLines")
        {
            QTextDocument doc;
            setMarkdownPreservingBlankLines(&doc, markdown);

            THEN("serializing it straight back out reproduces the exact same text")
            {
                CHECK(toMarkdownPreservingBlankLines(&doc) == markdown);
            }
        }
    }
}

SCENARIO("A note with many single-Enter lines and no deliberate blank lines survives a save/reload cycle unchanged", "[markdown_serialization]")
{
    GIVEN("a document built the way typing several short lines in a row would: one Enter "
          "between each, never two")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("first thought");
        cursor.insertBlock();
        cursor.insertText("second thought");
        cursor.insertBlock();
        cursor.insertText("third thought");
        cursor.insertBlock();
        cursor.insertText("fourth thought");

        WHEN("it's saved, then that saved text is reloaded into a fresh document")
        {
            const QString saved = toMarkdownPreservingBlankLines(&doc);
            QTextDocument reloaded;
            setMarkdownPreservingBlankLines(&reloaded, saved);

            THEN("the reloaded document still has exactly one block per line — no blank rows appeared")
            {
                CHECK(reloaded.blockCount() == 4);
            }

            THEN("saving again reproduces the exact same text, not something with extra blank lines")
            {
                CHECK(toMarkdownPreservingBlankLines(&reloaded) == saved);
            }
        }
    }
}

SCENARIO("A single blank line between paragraphs loads as tight adjacency, not a spacer block", "[markdown_serialization]")
{
    GIVEN("markdown with exactly one blank line between two paragraphs — the minimum "
          "CommonMark needs just to keep them apart, indistinguishable from a plain "
          "single-Enter paragraph break")
    {
        const QString markdown = QStringLiteral("line1\n\nline2\n");

        WHEN("it's loaded with setMarkdownPreservingBlankLines")
        {
            QTextDocument doc;
            setMarkdownPreservingBlankLines(&doc, markdown);

            THEN("no extra blank block is inserted — just the two paragraphs, directly adjacent")
            {
                CHECK(doc.blockCount() == 2);
            }

            THEN("re-saving reproduces the same single blank line, not two")
            {
                CHECK(toMarkdownPreservingBlankLines(&doc) == markdown);
            }
        }
    }
}

SCENARIO("Two or more blank lines between paragraphs load as real spacer blocks", "[markdown_serialization]")
{
    GIVEN("markdown with two blank lines between two paragraphs — unambiguous evidence of "
          "a deliberately-left blank row, since a plain paragraph break only ever needs one")
    {
        const QString markdown = QStringLiteral("line1\n\n\nline2\n");

        WHEN("it's loaded with setMarkdownPreservingBlankLines")
        {
            QTextDocument doc;
            setMarkdownPreservingBlankLines(&doc, markdown);

            THEN("both real blank blocks sit between the two paragraphs")
            {
                CHECK(doc.blockCount() == 4);
            }

            THEN("re-saving reproduces both blank lines")
            {
                CHECK(toMarkdownPreservingBlankLines(&doc) == markdown);
            }
        }
    }
}

SCENARIO("A note reopened after being saved with rich formatting still round-trips", "[markdown_serialization]")
{
    GIVEN("previously-saved markdown mixing a heading, blank lines, and a bold word")
    {
        const QString markdown = QStringLiteral("# Title\n\n\nSome **bold** text\n\nmore text\n");

        WHEN("it's loaded and immediately re-serialized")
        {
            QTextDocument doc;
            setMarkdownPreservingBlankLines(&doc, markdown);
            const QString reserialized = toMarkdownPreservingBlankLines(&doc);

            THEN("the heading, bold marker, and blank-line count all survive")
            {
                CHECK(reserialized == markdown);
            }
        }
    }
}

SCENARIO("A fenced code block with an internal blank line survives a reload", "[markdown_serialization]")
{
    GIVEN("markdown with a code fence sitting between two blank-separated paragraphs")
    {
        const QString markdown = QStringLiteral("intro\n\n```cpp\nfoo();\n\nbar();\n```\n\noutro\n");

        WHEN("it's loaded and re-serialized")
        {
            QTextDocument doc;
            setMarkdownPreservingBlankLines(&doc, markdown);
            const QString reserialized = toMarkdownPreservingBlankLines(&doc);

            THEN("the fence, its internal blank line, and the surrounding spacing all survive")
            {
                CHECK(reserialized == markdown);
            }
        }
    }
}

SCENARIO("A table sitting between two paragraphs survives a save/reload cycle", "[markdown_serialization]")
{
    GIVEN("a document with a paragraph, a 2x2 table, and another paragraph")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("intro");
        cursor.insertBlock();
        TextFormatting::insertTable(cursor);
        // insertTable() leaves the cursor inside the table's first cell — move
        // past the table before continuing, mirroring how a real user would
        // click below it to keep typing.
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
        cursor.insertText("outro");

        THEN("the table's header row and cell text both survive serialization")
        {
            const QString md = toMarkdownPreservingBlankLines(&doc);
            CHECK(md.contains("Header 1"));
            CHECK(md.contains("Header 2"));
            CHECK(md.contains("intro"));
            CHECK(md.contains("outro"));
        }

        THEN("reloading that markdown reproduces the same table and surrounding text")
        {
            const QString saved = toMarkdownPreservingBlankLines(&doc);
            QTextDocument reloaded;
            setMarkdownPreservingBlankLines(&reloaded, saved);
            CHECK(toMarkdownPreservingBlankLines(&reloaded) == saved);
        }
    }
}

SCENARIO("Deleting a table between two paragraphs leaves them intact after a save/reload cycle", "[markdown_serialization]")
{
    GIVEN("a document with a paragraph, a 2x2 table, and another paragraph")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("intro");
        cursor.insertBlock();
        QTextTableFormat format;
        QTextTable *table = cursor.insertTable(2, 2, format);
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
        cursor.insertText("outro");

        WHEN("the table is deleted, then the document is saved and reloaded")
        {
            QTextCursor tableCursor(&doc);
            tableCursor.setPosition(table->firstPosition());
            TextFormatting::deleteTable(tableCursor);

            const QString saved = toMarkdownPreservingBlankLines(&doc);
            QTextDocument reloaded;
            setMarkdownPreservingBlankLines(&reloaded, saved);

            THEN("intro and outro survive, the table doesn't, and reloading is stable")
            {
                CHECK(saved.contains("intro"));
                CHECK(saved.contains("outro"));
                CHECK(!saved.contains("Header"));
                CHECK(!saved.contains("---"));
                CHECK(toMarkdownPreservingBlankLines(&reloaded) == saved);
            }
        }
    }
}

SCENARIO("A table with an empty cell is not mistaken for a blank spacer block", "[markdown_serialization]")
{
    GIVEN("a table whose second header cell is left empty")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("intro");
        cursor.insertBlock();
        QTextTableFormat format;
        QTextTable *table = cursor.insertTable(1, 2, format);
        table->cellAt(0, 0).firstCursorPosition().insertText("Filled");
        // Cell (0, 1) is deliberately left with an empty block — this is the
        // case isBlankSpacerBlock() must not misidentify as vertical spacing.
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
        cursor.insertText("outro");

        THEN("the run splitter keeps intro/table/outro as one contiguous run, not sliced apart")
        {
            const QString md = toMarkdownPreservingBlankLines(&doc);
            CHECK(md.contains("Filled"));
            CHECK(md.contains("intro"));
            CHECK(md.contains("outro"));
        }
    }
}

SCENARIO("Loading empty markdown produces an empty document", "[markdown_serialization]")
{
    GIVEN("an empty string, as a brand-new note would have")
    {
        QTextDocument doc;

        WHEN("it's loaded with setMarkdownPreservingBlankLines")
        {
            setMarkdownPreservingBlankLines(&doc, QString());

            THEN("the document has a single empty block, same as a fresh QTextDocument")
            {
                CHECK(doc.blockCount() == 1);
                CHECK(doc.begin().text().isEmpty());
            }
        }
    }
}

SCENARIO("Inserting a table right after pressing Enter leaves no phantom blank paragraph", "[markdown_serialization]")
{
    GIVEN("text, a single Enter to a fresh empty line, then a table inserted on that line")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("intro");
        cursor.insertBlock();
        TextFormatting::insertTable(cursor);

        THEN("the table sits directly after \"intro\" — no orphan empty block between them")
        {
            const QTextBlock first = doc.begin();
            CHECK(first.text() == QStringLiteral("intro"));
            const QTextBlock second = first.next();
            CHECK(QTextCursor(second).currentTable() != nullptr);
        }
    }
}

SCENARIO("A table right after text (single Enter, no deliberate blank line) survives a save/reload cycle", "[markdown_serialization]")
{
    GIVEN("intro, Enter, a table, Enter, outro — matching the toolbar's own insert-table flow")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("intro");
        cursor.insertBlock();
        TextFormatting::insertTable(cursor);
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
        cursor.insertText("outro");

        const QString saved = toMarkdownPreservingBlankLines(&doc);

        THEN("exactly one blank line separates intro/table and table/outro — matching the single Enter")
        {
            CHECK(saved == QStringLiteral("intro\n\n|Header 1|Header 2|\n|--------|--------|\n"
                                           "|        |        |\n\noutro\n"));
        }

        THEN("reloading it reproduces the exact same text — no growth on repeated save/reload")
        {
            QTextDocument reloaded;
            setMarkdownPreservingBlankLines(&reloaded, saved);
            CHECK(toMarkdownPreservingBlankLines(&reloaded) == saved);
        }
    }
}

SCENARIO("A table inserted with the cursor still mid-paragraph (no Enter pressed) round-trips the same way", "[markdown_serialization]")
{
    GIVEN("intro with the cursor at its end, a table inserted with no Enter first")
    {
        QTextDocument doc;
        QTextCursor cursor(&doc);
        cursor.insertText("intro");
        TextFormatting::insertTable(cursor);
        cursor.movePosition(QTextCursor::End);
        cursor.insertText("outro");

        THEN("the saved markdown matches the single-Enter case exactly — same visible spacing either way")
        {
            CHECK(toMarkdownPreservingBlankLines(&doc)
                  == QStringLiteral("intro\n\n|Header 1|Header 2|\n|--------|--------|\n"
                                     "|        |        |\n\noutro\n"));
        }
    }
}
