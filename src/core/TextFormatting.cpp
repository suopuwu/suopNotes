#include "TextFormatting.h"

#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextList>

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
        QTextBlockFormat blockFormat = cursor.blockFormat();
        blockFormat.setIndent(0);
        cursor.mergeBlockFormat(blockFormat);
        list->remove(cursor.block());
    } else {
        cursor.createList(QTextListFormat::ListDisc);
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

}
