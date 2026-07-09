#include "TextFormatting.h"

#include <QFontMetricsF>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
#include <QTextTableFormat>

namespace TextFormatting
{

namespace
{

// Qt's default list indent (QTextListFormat::indent() == 1, times
// QTextDocument::indentWidth()'s default of 40px) pushes the marker itself
// ~40px right of the paragraph's own left edge — an "indentation before the
// number" nobody asked for. Sizing indentWidth to just fit a two-digit
// marker instead keeps the marker flush against the text's natural left
// edge while leaving Qt's own hanging-indent mechanism intact, so wrapped
// continuation lines still line up under the text after the marker rather
// than collapsing flush left with it (confirmed empirically: indent level 0
// clips the marker entirely, so a small nonzero indentWidth — not indent 0
// — is what "no indentation before the number" actually needs).
void tightenListIndent(const QTextCursor &cursor)
{
    if (QTextDocument *document = cursor.document()) {
        const QFontMetricsF metrics(cursor.charFormat().font());
        document->setIndentWidth(metrics.horizontalAdvance(QStringLiteral("99.")) + 4);
    }
}

}

void toggleBold(QTextCursor cursor)
{
    const bool isBold = cursor.charFormat().fontWeight() >= QFont::Bold;
    QTextCharFormat format;
    format.setFontWeight(isBold ? QFont::Normal : QFont::Bold);
    cursor.mergeCharFormat(format);
}

void toggleItalic(QTextCursor cursor)
{
    const bool isItalic = cursor.charFormat().fontItalic();
    QTextCharFormat format;
    format.setFontItalic(!isItalic);
    cursor.mergeCharFormat(format);
}

void toggleStrikethrough(QTextCursor cursor)
{
    const bool isStruck = cursor.charFormat().fontStrikeOut();
    QTextCharFormat format;
    format.setFontStrikeOut(!isStruck);
    cursor.mergeCharFormat(format);
}

void toggleBulletList(QTextCursor cursor)
{
    if (QTextList *list = cursor.currentList()) {
        if (list->format().style() == QTextListFormat::ListDisc) {
            QTextBlockFormat blockFormat = cursor.blockFormat();
            blockFormat.setIndent(0);
            cursor.mergeBlockFormat(blockFormat);
            list->remove(cursor.block());
        } else {
            // Already a list, just the wrong style (numbered) — convert this
            // one list object in place rather than remove+recreate, so a
            // toolbar click that switches style mid-list doesn't fragment it
            // into two separate list objects.
            QTextListFormat format = list->format();
            format.setStyle(QTextListFormat::ListDisc);
            list->setFormat(format);
        }
    } else {
        tightenListIndent(cursor);
        QTextListFormat format;
        format.setStyle(QTextListFormat::ListDisc);
        format.setIndent(1);
        cursor.createList(format);
    }
}

void toggleNumberedList(QTextCursor cursor, int startNumber)
{
    if (QTextList *list = cursor.currentList()) {
        if (list->format().style() == QTextListFormat::ListDecimal) {
            QTextBlockFormat blockFormat = cursor.blockFormat();
            blockFormat.setIndent(0);
            cursor.mergeBlockFormat(blockFormat);
            list->remove(cursor.block());
        } else {
            QTextListFormat format = list->format();
            format.setStyle(QTextListFormat::ListDecimal);
            list->setFormat(format);
        }
    } else {
        tightenListIndent(cursor);
        QTextListFormat format;
        format.setStyle(QTextListFormat::ListDecimal);
        format.setIndent(1);
        format.setStart(startNumber);
        cursor.createList(format);
    }
}

void setHeadingLevel(QTextCursor cursor, int level)
{
    QTextBlockFormat blockFormat = cursor.blockFormat();
    blockFormat.setHeadingLevel(level);
    cursor.mergeBlockFormat(blockFormat);

    // Qt's default stylesheet renders heading blocks bold/larger based on
    // headingLevel alone; no extra char-format changes needed for display,
    // and toMarkdown() serializes headingLevel back to "#"..."######" runs.
}

void insertTable(QTextCursor cursor)
{
    // If the cursor is sitting on an already-empty line (the common case:
    // the user pressed Enter to get a fresh line, then inserted a table),
    // QTextCursor::insertTable() does NOT consume that empty block — it
    // inserts the table's frame right after it, leaving the empty line
    // behind as an orphan blank paragraph between the preceding text and
    // the table. Collapsing it first (merging back into the previous
    // block, exactly like the cursor would sit if the user had inserted
    // the table without pressing Enter at all) avoids that phantom gap;
    // inserting at the true end of a non-empty block never has this
    // problem, which is why only "text, Enter, then table" showed it.
    if (cursor.atBlockStart() && cursor.atBlockEnd())
        cursor.deletePreviousChar();

    // Qt's markdown writer always renders a QTextTable's first row as the
    // GFM header row (with the "---" delimiter line right after it) — there
    // is no separate "is this the header" flag to set, so placeholder text
    // in row 0 is what marks it as a header once round-tripped through
    // toMarkdown().
    QTextTableFormat format;
    format.setBorder(1);
    format.setCellPadding(4);
    QTextTable *table = cursor.insertTable(2, 2, format);
    table->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("Header 1"));
    table->cellAt(0, 1).firstCursorPosition().insertText(QStringLiteral("Header 2"));
}

void insertTableRow(QTextCursor cursor)
{
    QTextTable *table = cursor.currentTable();
    if (!table)
        return;
    const int row = table->cellAt(cursor).row();
    // insertRows(pos, ...) inserts BEFORE pos, so row + 1 lands the new row
    // directly after the one the cursor is in.
    table->insertRows(row + 1, 1);
}

void deleteTableRow(QTextCursor cursor)
{
    QTextTable *table = cursor.currentTable();
    if (!table)
        return;
    if (table->rows() <= 1) {
        deleteTable(cursor);
        return;
    }
    const int row = table->cellAt(cursor).row();
    table->removeRows(row, 1);
}

void insertTableColumn(QTextCursor cursor)
{
    QTextTable *table = cursor.currentTable();
    if (!table)
        return;
    const int column = table->cellAt(cursor).column();
    table->insertColumns(column + 1, 1);
}

void deleteTableColumn(QTextCursor cursor)
{
    QTextTable *table = cursor.currentTable();
    if (!table)
        return;
    if (table->columns() <= 1) {
        deleteTable(cursor);
        return;
    }
    const int column = table->cellAt(cursor).column();
    table->removeColumns(column, 1);
}

void deleteTable(QTextCursor cursor)
{
    QTextTable *table = cursor.currentTable();
    if (!table)
        return;

    // A QTextFrame (QTextTable's base) is delimited in the document's
    // underlying character stream by one hidden "frame start" character
    // right before firstPosition() and one "frame end" character right
    // after lastPosition() — selecting exactly that span and removing it
    // deletes the frame itself, not just the rows/cells inside it (which
    // is all removeRows()/removeColumns() alone would ever manage, since
    // QTextTable refuses to go below 1x1).
    QTextCursor removeCursor(table->document());
    removeCursor.setPosition(table->firstPosition() - 1);
    removeCursor.setPosition(table->lastPosition() + 1, QTextCursor::KeepAnchor);
    removeCursor.removeSelectedText();
}

bool isBold(QTextCursor cursor)
{
    return cursor.charFormat().fontWeight() >= QFont::Bold;
}

bool isItalic(QTextCursor cursor)
{
    return cursor.charFormat().fontItalic();
}

bool isStrikethrough(QTextCursor cursor)
{
    return cursor.charFormat().fontStrikeOut();
}

int headingLevel(QTextCursor cursor)
{
    return cursor.blockFormat().headingLevel();
}

bool isBulletList(QTextCursor cursor)
{
    QTextList *list = cursor.currentList();
    return list && list->format().style() == QTextListFormat::ListDisc;
}

bool isNumberedList(QTextCursor cursor)
{
    QTextList *list = cursor.currentList();
    return list && list->format().style() == QTextListFormat::ListDecimal;
}

}
