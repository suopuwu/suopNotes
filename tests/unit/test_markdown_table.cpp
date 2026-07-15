#include <catch2/catch_test_macros.hpp>

#include <QTextDocument>

#include "core/MarkdownTable.h"

// These tests document how a table is recognised once it is nothing but literal
// text. The pre-rewrite editor asked QTextTable which cell a cursor was in; a
// pipe table has no object model to ask, so "where does this table start and
// end, and how many columns does it have" has to be answered by reading the
// lines themselves. That detection is what everything else in the table rewrite
// is built on, so it is specified here in isolation.

SCENARIO("A row's cells are split on unescaped pipes", "[markdown_table]")
{
    GIVEN("an ordinary two-cell row")
    {
        THEN("the outer pipes are delimiters, not cells, and each cell is trimmed")
        {
            const QStringList cells = MarkdownTable::splitCells(QStringLiteral("| a | b |"));
            CHECK(cells == QStringList{QStringLiteral("a"), QStringLiteral("b")});
        }
    }

    GIVEN("a row still being typed, with no closing pipe")
    {
        THEN("the last cell is still counted rather than dropped")
        {
            const QStringList cells = MarkdownTable::splitCells(QStringLiteral("| a | b"));
            CHECK(cells == QStringList{QStringLiteral("a"), QStringLiteral("b")});
        }
    }

    GIVEN("a cell whose text contains an escaped pipe")
    {
        THEN("the escaped pipe is content and does not split the cell")
        {
            const QStringList cells = MarkdownTable::splitCells(QStringLiteral("| a \\| b | c |"));
            CHECK(cells == QStringList{QStringLiteral("a \\| b"), QStringLiteral("c")});
        }
    }

    GIVEN("a row with an empty cell")
    {
        THEN("the empty cell is preserved, keeping the column count honest")
        {
            const QStringList cells = MarkdownTable::splitCells(QStringLiteral("| a |  | c |"));
            CHECK(cells.size() == 3);
            CHECK(cells.at(1).isEmpty());
        }
    }
}

SCENARIO("Only a GFM delimiter row counts as a table's separator", "[markdown_table]")
{
    THEN("plain dashes, and the alignment forms, are all delimiter rows")
    {
        CHECK(MarkdownTable::isDelimiterRow(QStringLiteral("| --- | --- |")));
        CHECK(MarkdownTable::isDelimiterRow(QStringLiteral("|:---|---:|")));
        CHECK(MarkdownTable::isDelimiterRow(QStringLiteral("| :---: | --- |")));
    }

    THEN("a row of ordinary text is not")
    {
        CHECK_FALSE(MarkdownTable::isDelimiterRow(QStringLiteral("| a | b |")));
    }

    THEN("a row mixing dashes with text is not — every cell must be a delimiter")
    {
        CHECK_FALSE(MarkdownTable::isDelimiterRow(QStringLiteral("| --- | b |")));
    }
}

SCENARIO("A table's extent is found by walking outward from any line inside it", "[markdown_table]")
{
    GIVEN("a 2-column table with two body rows, surrounded by prose")
    {
        QTextDocument doc;
        doc.setPlainText(
            "intro paragraph\n"
            "| h1 | h2 |\n"
            "| --- | --- |\n"
            "| a | b |\n"
            "| c | d |\n"
            "outro paragraph");

        WHEN("asked from the header row")
        {
            const MarkdownTable::Region region = MarkdownTable::regionAt(&doc, 1);

            THEN("it spans exactly the pipe lines and knows its shape")
            {
                REQUIRE(region.isValid());
                CHECK(region.firstBlockNumber == 1);
                CHECK(region.lastBlockNumber == 4);
                CHECK(region.columnCount == 2);
                CHECK(region.bodyRowCount() == 2);
            }
        }

        WHEN("asked from a body row in the middle")
        {
            const MarkdownTable::Region region = MarkdownTable::regionAt(&doc, 3);

            THEN("it finds the same table by walking both directions")
            {
                REQUIRE(region.isValid());
                CHECK(region.firstBlockNumber == 1);
                CHECK(region.lastBlockNumber == 4);
            }
        }

        WHEN("asked from the surrounding prose")
        {
            THEN("no table is reported, above or below")
            {
                CHECK_FALSE(MarkdownTable::regionAt(&doc, 0).isValid());
                CHECK_FALSE(MarkdownTable::regionAt(&doc, 5).isValid());
            }
        }
    }
}

SCENARIO("Prose containing pipes is not mistaken for a table", "[markdown_table]")
{
    GIVEN("lines that start with a pipe but have no delimiter row")
    {
        QTextDocument doc;
        doc.setPlainText("| not a table |\n| still not one |");

        WHEN("asked whether the first line is in a table")
        {
            THEN("it is not — GFM requires a delimiter row as the second line")
            {
                CHECK_FALSE(MarkdownTable::regionAt(&doc, 0).isValid());
            }
        }
    }

    GIVEN("a single pipe line alone in the document")
    {
        QTextDocument doc;
        doc.setPlainText("| lonely |");

        THEN("it is not a table — there is no second line to be a delimiter")
        {
            CHECK_FALSE(MarkdownTable::regionAt(&doc, 0).isValid());
        }
    }
}

SCENARIO("A blank line ends a table, so two tables in a row stay separate", "[markdown_table]")
{
    GIVEN("two tables separated by a blank line")
    {
        QTextDocument doc;
        doc.setPlainText(
            "| a | b |\n"
            "| --- | --- |\n"
            "| 1 | 2 |\n"
            "\n"
            "| x | y | z |\n"
            "| --- | --- | --- |\n"
            "| 7 | 8 | 9 |");

        WHEN("each table is probed from its own body row")
        {
            const MarkdownTable::Region first = MarkdownTable::regionAt(&doc, 2);
            const MarkdownTable::Region second = MarkdownTable::regionAt(&doc, 6);

            THEN("the first table stops at the blank line")
            {
                REQUIRE(first.isValid());
                CHECK(first.firstBlockNumber == 0);
                CHECK(first.lastBlockNumber == 2);
                CHECK(first.columnCount == 2);
            }

            THEN("the second table is found independently, with its own column count")
            {
                REQUIRE(second.isValid());
                CHECK(second.firstBlockNumber == 4);
                CHECK(second.lastBlockNumber == 6);
                CHECK(second.columnCount == 3);
            }
        }
    }
}

SCENARIO("A table with only a header and delimiter is still a table", "[markdown_table]")
{
    GIVEN("a freshly inserted table with no body rows yet")
    {
        QTextDocument doc;
        doc.setPlainText("| h1 | h2 |\n| --- | --- |");

        THEN("it is recognised, and reports zero body rows")
        {
            const MarkdownTable::Region region = MarkdownTable::regionAt(&doc, 0);
            REQUIRE(region.isValid());
            CHECK(region.firstBlockNumber == 0);
            CHECK(region.lastBlockNumber == 1);
            CHECK(region.bodyRowCount() == 0);
        }
    }
}

SCENARIO("Rows render back to canonical pipe syntax", "[markdown_table]")
{
    THEN("cells are rebuilt with single-space padding")
    {
        CHECK(MarkdownTable::formatRow({QStringLiteral("a"), QStringLiteral("b")})
              == QStringLiteral("| a | b |"));
    }

    THEN("a delimiter row is generated for a given column count")
    {
        CHECK(MarkdownTable::delimiterRow(3) == QStringLiteral("| --- | --- | --- |"));
    }

    THEN("formatRow and splitCells round-trip an ordinary row")
    {
        const QStringList cells{QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three")};
        CHECK(MarkdownTable::splitCells(MarkdownTable::formatRow(cells)) == cells);
    }
}

SCENARIO("A character offset in a row resolves to the cell it sits in", "[markdown_table]")
{
    GIVEN("the row '| a | b |'")
    {
        //             0123456789
        const QString row = QStringLiteral("| a | b |");

        THEN("an offset on each cell's text reports that cell")
        {
            CHECK(MarkdownTable::columnAt(row, 2) == 0); // on 'a'
            CHECK(MarkdownTable::columnAt(row, 6) == 1); // on 'b'
        }

        THEN("an offset resting on a closing pipe belongs to the cell it closes")
        {
            CHECK(MarkdownTable::columnAt(row, 4) == 0);
            CHECK(MarkdownTable::columnAt(row, 8) == 1);
        }

        THEN("offsets at either edge of the row clamp into a real cell rather than reporting none")
        {
            CHECK(MarkdownTable::columnAt(row, 0) == 0);
            CHECK(MarkdownTable::columnAt(row, row.size()) == 1);
        }
    }

    GIVEN("a row whose cell contains an escaped pipe")
    {
        const QString row = QStringLiteral("| a \\| b | c |");

        THEN("the escaped pipe does not start a new column")
        {
            const int escapedPipe = row.indexOf(QStringLiteral("\\|"));
            CHECK(MarkdownTable::columnAt(row, escapedPipe + 1) == 0);
            CHECK(MarkdownTable::columnAt(row, row.indexOf(QLatin1Char('c'))) == 1);
        }
    }

    GIVEN("a line that is not a table row at all")
    {
        THEN("no column is reported")
        {
            CHECK(MarkdownTable::columnAt(QStringLiteral("just prose"), 3) == -1);
        }
    }
}

SCENARIO("A cell's content range locates the text inside it, excluding pipes and padding", "[markdown_table]")
{
    GIVEN("the row '| a | b |'")
    {
        const QString row = QStringLiteral("| a | b |");

        THEN("each range covers exactly the cell's text")
        {
            int start = -1;
            int end = -1;

            REQUIRE(MarkdownTable::cellContentRange(row, 0, &start, &end));
            CHECK(row.mid(start, end - start) == QStringLiteral("a"));

            REQUIRE(MarkdownTable::cellContentRange(row, 1, &start, &end));
            CHECK(row.mid(start, end - start) == QStringLiteral("b"));
        }

        THEN("asking for a column that doesn't exist fails rather than guessing")
        {
            CHECK_FALSE(MarkdownTable::cellContentRange(row, 2, nullptr, nullptr));
            CHECK_FALSE(MarkdownTable::cellContentRange(row, -1, nullptr, nullptr));
        }
    }

    GIVEN("a row with an empty cell")
    {
        const QString row = QStringLiteral("| a |   | c |");

        THEN("the empty cell reports a zero-width range inside its padding — a place to type")
        {
            int start = -1;
            int end = -1;
            REQUIRE(MarkdownTable::cellContentRange(row, 1, &start, &end));
            CHECK(start == end);
            CHECK(start > row.indexOf(QLatin1Char('a')));
            CHECK(end < row.indexOf(QLatin1Char('c')));
        }
    }

    GIVEN("round-tripping hit-testing against cell ranges")
    {
        const QString row = QStringLiteral("| alpha | beta | gamma |");

        THEN("every cell's own content offsets hit-test back to that same cell")
        {
            for (int column = 0; column < 3; ++column) {
                int start = -1;
                int end = -1;
                REQUIRE(MarkdownTable::cellContentRange(row, column, &start, &end));
                CHECK(MarkdownTable::columnAt(row, start) == column);
                CHECK(MarkdownTable::columnAt(row, end) == column);
            }
        }
    }
}
