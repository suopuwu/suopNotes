#include "DocumentHandler.h"
#include "MarkdownSerialization.h"
#include "TextFormatting.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>

DocumentHandler::DocumentHandler(QObject *parent)
    : QObject(parent)
{
}

QQuickTextDocument *DocumentHandler::document() const
{
    return m_document;
}

void DocumentHandler::setDocument(QQuickTextDocument *document)
{
    if (m_document == document)
        return;
    m_document = document;
    emit documentChanged();
}

QString DocumentHandler::toMarkdown() const
{
    if (!m_document || !m_document->textDocument())
        return QString();
    return MarkdownSerialization::toMarkdownPreservingBlankLines(m_document->textDocument());
}

void DocumentHandler::setMarkdown(const QString &markdown)
{
    if (!m_document || !m_document->textDocument())
        return;
    MarkdownSerialization::setMarkdownPreservingBlankLines(m_document->textDocument(), markdown);
}

QTextCursor DocumentHandler::cursorFor(int selectionStart, int selectionEnd) const
{
    if (!m_document || !m_document->textDocument())
        return QTextCursor();

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(selectionStart);
    cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    return cursor;
}

void DocumentHandler::toggleBold(int selectionStart, int selectionEnd)
{
    TextFormatting::toggleBold(cursorFor(selectionStart, selectionEnd));
}

void DocumentHandler::toggleItalic(int selectionStart, int selectionEnd)
{
    TextFormatting::toggleItalic(cursorFor(selectionStart, selectionEnd));
}

void DocumentHandler::toggleStrikethrough(int selectionStart, int selectionEnd)
{
    TextFormatting::toggleStrikethrough(cursorFor(selectionStart, selectionEnd));
}

void DocumentHandler::toggleBulletList(int selectionStart, int selectionEnd)
{
    TextFormatting::toggleBulletList(cursorFor(selectionStart, selectionEnd));
}

void DocumentHandler::toggleNumberedList(int selectionStart, int selectionEnd)
{
    TextFormatting::toggleNumberedList(cursorFor(selectionStart, selectionEnd));
}

void DocumentHandler::setHeadingLevel(int selectionStart, int selectionEnd, int level)
{
    TextFormatting::setHeadingLevel(cursorFor(selectionStart, selectionEnd), level);
}

void DocumentHandler::insertTable(int position)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    TextFormatting::insertTable(cursor);
}

bool DocumentHandler::tryContinueList(int cursorPosition)
{
    if (!m_document || !m_document->textDocument())
        return false;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(cursorPosition);

    QTextList *list = cursor.currentList();
    if (!list)
        return false;

    if (cursor.block().text().isEmpty()) {
        // An empty item is the standard "press Enter again to leave the
        // list" escape hatch — otherwise there would be no way to get back
        // to a plain paragraph without reaching for the toolbar.
        QTextBlockFormat blockFormat = cursor.blockFormat();
        blockFormat.setIndent(0);
        cursor.mergeBlockFormat(blockFormat);
        list->remove(cursor.block());
        return true;
    }

    cursor.insertBlock();
    list->add(cursor.block());
    return true;
}

bool DocumentHandler::tryAutoConvertListShorthand(int cursorPosition)
{
    if (!m_document || !m_document->textDocument())
        return false;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(cursorPosition);

    // Already inside a list — nothing to shorthand-convert.
    if (cursor.currentList())
        return false;

    const QTextBlock block = cursor.block();
    const QString textBeforeCursor = block.text().left(cursorPosition - block.position());

    static const QRegularExpression bulletShorthand(QStringLiteral("^[-*] $"));
    static const QRegularExpression numberedShorthand(QStringLiteral("^\\d+\\. $"));

    const bool isBullet = bulletShorthand.match(textBeforeCursor).hasMatch();
    const bool isNumbered = !isBullet && numberedShorthand.match(textBeforeCursor).hasMatch();
    if (!isBullet && !isNumbered)
        return false;

    QTextCursor stripCursor(m_document->textDocument());
    stripCursor.setPosition(block.position());
    stripCursor.setPosition(cursorPosition, QTextCursor::KeepAnchor);
    stripCursor.removeSelectedText();

    if (isNumbered)
        TextFormatting::toggleNumberedList(stripCursor);
    else
        TextFormatting::toggleBulletList(stripCursor);
    return true;
}
