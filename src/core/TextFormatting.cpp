#include "TextFormatting.h"

#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextList>
#include <QTextTable>
#include <QTextTableFormat>

namespace TextFormatting
{

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
        cursor.createList(QTextListFormat::ListDisc);
    }
}

void toggleNumberedList(QTextCursor cursor)
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
        cursor.createList(QTextListFormat::ListDecimal);
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

}
