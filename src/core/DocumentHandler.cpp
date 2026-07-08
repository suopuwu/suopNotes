#include "DocumentHandler.h"
#include "MarkdownSerialization.h"
#include "TextFormatting.h"

#include <QTextDocument>

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

void DocumentHandler::setHeadingLevel(int selectionStart, int selectionEnd, int level)
{
    TextFormatting::setHeadingLevel(cursorFor(selectionStart, selectionEnd), level);
}
