#include "DocumentHandler.h"
#include "MarkdownSerialization.h"
#include "TextFormatting.h"

#include <QAbstractTextDocumentLayout>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>

namespace
{
// RAII helper for DocumentHandler::m_handlingListMutation — see that
// member's own comment for why this guard exists at all.
//
// Also wraps the whole call in a single QTextDocument edit block (bugfix):
// every list-mutating entry point below used to issue many separate,
// unbatched QTextCursor edits (one insertText/mergeBlockFormat per block
// TextFormatting::renumberRun touches, for instance) as a reaction to a
// single keystroke. Each one became its own entry on Qt's native undo
// stack, so repeatedly pressing Ctrl+Z inside a list mostly just replayed
// those auto-formatting micro-edits at the same handful of block
// positions instead of stepping back through real edit history — and
// QTextDocument::contentsChanged() (which QML's TextArea.onTextChanged
// listens to, and which re-enters these very functions) is deferred and
// coalesced to a single emission at endEditBlock() rather than firing
// once per edit, so this also cuts down on how often that reentrant path
// runs. Only begins/ends a block when this guard is actually the
// outermost (non-reentrant) one — a reentrant call returns immediately
// without editing anything, so there is nothing for it to batch.
class ListMutationGuard
{
public:
    ListMutationGuard(bool &flag, QTextDocument *document)
        : m_flag(flag)
        , m_reentrant(flag)
    {
        m_flag = true;
        if (!m_reentrant && document) {
            m_editBlockCursor = QTextCursor(document);
            m_editBlockCursor.beginEditBlock();
        }
    }
    ~ListMutationGuard()
    {
        if (!m_reentrant && !m_editBlockCursor.isNull())
            m_editBlockCursor.endEditBlock();
        m_flag = m_reentrant;
    }
    // True if a list-mutating call was ALREADY in progress when this guard
    // was constructed — callers bail out immediately in that case.
    bool reentrant() const { return m_reentrant; }

private:
    bool &m_flag;
    bool m_reentrant;
    // beginEditBlock()/endEditBlock() are QTextCursor methods (not
    // QTextDocument's, despite grouping the whole document's undo stack) —
    // this cursor exists purely to call them, never to edit through.
    QTextCursor m_editBlockCursor;
};
}

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
    emit formattingChanged();
}

void DocumentHandler::toggleItalic(int selectionStart, int selectionEnd)
{
    TextFormatting::toggleItalic(cursorFor(selectionStart, selectionEnd));
    emit formattingChanged();
}

void DocumentHandler::toggleStrikethrough(int selectionStart, int selectionEnd)
{
    TextFormatting::toggleStrikethrough(cursorFor(selectionStart, selectionEnd));
    emit formattingChanged();
}

void DocumentHandler::toggleBulletList(int selectionStart, int selectionEnd)
{
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return;
    TextFormatting::toggleBulletList(cursorFor(selectionStart, selectionEnd));
    emit formattingChanged();
}

void DocumentHandler::toggleNumberedList(int selectionStart, int selectionEnd)
{
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return;
    TextFormatting::toggleNumberedList(cursorFor(selectionStart, selectionEnd));
    emit formattingChanged();
}

void DocumentHandler::toggleCheckboxList(int selectionStart, int selectionEnd)
{
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return;
    TextFormatting::toggleCheckboxList(cursorFor(selectionStart, selectionEnd));
    emit formattingChanged();
}

void DocumentHandler::setHeadingLevel(int selectionStart, int selectionEnd, int level)
{
    QTextCursor cursor = cursorFor(selectionStart, selectionEnd);
    // A toggle, not a plain setter: clicking H1 on a line that's already H1
    // reverts it to a normal paragraph, matching every other toolbar toggle
    // (Bold, bullet list, ...). Callers that always want a specific level
    // unconditionally (e.g. an explicit "clear formatting" shortcut passing
    // level 0) still get exactly that level whenever the current level
    // differs from it — only the "already at the requested level" case
    // clears instead of re-applying it.
    const int currentLevel = TextFormatting::headingLevel(cursor);
    TextFormatting::setHeadingLevel(cursor, currentLevel == level ? 0 : level);
    emit formattingChanged();
}

void DocumentHandler::insertTabSpaces(int selectionStart, int selectionEnd)
{
    QTextCursor cursor = cursorFor(selectionStart, selectionEnd);
    TextFormatting::insertTabSpaces(cursor);
}

void DocumentHandler::insertTable(int position)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    TextFormatting::insertTable(cursor);
}

void DocumentHandler::insertTableRow(int position)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    TextFormatting::insertTableRow(cursor);
}

void DocumentHandler::deleteTableRow(int position)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    TextFormatting::deleteTableRow(cursor);
}

void DocumentHandler::insertTableColumn(int position)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    TextFormatting::insertTableColumn(cursor);
}

void DocumentHandler::deleteTableColumn(int position)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    TextFormatting::deleteTableColumn(cursor);
}

void DocumentHandler::deleteTable(int position)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    TextFormatting::deleteTable(cursor);
}

bool DocumentHandler::tryContinueList(int cursorPosition)
{
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return false;

    if (!m_document || !m_document->textDocument())
        return false;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(cursorPosition);

    // Numbered/checkbox items are hand-rolled (see TextFormatting.h) and
    // handled independently of the QTextList-based bullet path below.
    if (TextFormatting::isNumberedList(cursor) || TextFormatting::isCheckboxList(cursor)) {
        if (TextFormatting::isHandRolledListItemEmpty(cursor)) {
            TextFormatting::exitHandRolledList(cursor);
            return true;
        }
        return TextFormatting::continueHandRolledList(cursor);
    }

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
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return false;

    if (!m_document || !m_document->textDocument())
        return false;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(cursorPosition);

    // Already inside a list — nothing to shorthand-convert. Numbered/
    // checkbox items are hand-rolled (no QTextList), hence the extra checks.
    if (cursor.currentList() || TextFormatting::isNumberedList(cursor) || TextFormatting::isCheckboxList(cursor))
        return false;

    const QTextBlock block = cursor.block();
    const QString textBeforeCursor = block.text().left(cursorPosition - block.position());

    static const QRegularExpression checkboxShorthand(QStringLiteral("^[-*] \\[[ xX]?\\] $"));
    static const QRegularExpression bulletShorthand(QStringLiteral("^[-*] $"));
    static const QRegularExpression numberedShorthand(QStringLiteral("^(\\d+)\\. $"));

    const bool isCheckbox = checkboxShorthand.match(textBeforeCursor).hasMatch();
    const bool isBullet = !isCheckbox && bulletShorthand.match(textBeforeCursor).hasMatch();
    const QRegularExpressionMatch numberedMatch = numberedShorthand.match(textBeforeCursor);
    const bool isNumbered = !isCheckbox && !isBullet && numberedMatch.hasMatch();
    if (!isCheckbox && !isBullet && !isNumbered)
        return false;

    QTextCursor stripCursor(m_document->textDocument());
    stripCursor.setPosition(block.position());
    stripCursor.setPosition(cursorPosition, QTextCursor::KeepAnchor);
    stripCursor.removeSelectedText();

    if (isCheckbox)
        TextFormatting::toggleCheckboxList(stripCursor);
    else if (isNumbered)
        // Preserves whatever number the user actually typed (e.g. "5. "
        // starts the list at 5, not silently resetting to 1) — Qt's own
        // auto-numbering still increments normally for every item after it.
        TextFormatting::toggleNumberedList(stripCursor, numberedMatch.captured(1).toInt());
    else
        TextFormatting::toggleBulletList(stripCursor);
    return true;
}

void DocumentHandler::maintainListNumbering(int cursorPosition)
{
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return;

    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(cursorPosition);
    TextFormatting::maintainHandRolledListNumbering(cursor);
}

bool DocumentHandler::tryResetHeadingOnReturn(int cursorPosition)
{
    if (!m_document || !m_document->textDocument())
        return false;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(cursorPosition);

    if (TextFormatting::headingLevel(cursor) == 0)
        return false;

    cursor.insertBlock();
    TextFormatting::setHeadingLevel(cursor, 0);
    return true;
}

bool DocumentHandler::isBold(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isBold(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::isItalic(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isItalic(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::isStrikethrough(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isStrikethrough(cursorFor(selectionStart, selectionEnd));
}

int DocumentHandler::headingLevel(int selectionStart, int selectionEnd) const
{
    return TextFormatting::headingLevel(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::isBulletList(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isBulletList(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::isNumberedList(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isNumberedList(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::isCheckboxList(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isCheckboxList(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::isCheckboxChecked(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isCheckboxChecked(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::tryToggleCheckboxAt(int position)
{
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return false;

    if (!m_document || !m_document->textDocument())
        return false;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    const bool toggled = TextFormatting::tryToggleCheckboxAtPosition(cursor);
    if (toggled)
        emit formattingChanged();
    return toggled;
}

bool DocumentHandler::isInTable(int position) const
{
    if (!m_document || !m_document->textDocument())
        return false;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    return TextFormatting::isInTable(cursor);
}

QRectF DocumentHandler::tableRect(int position) const
{
    if (!m_document || !m_document->textDocument())
        return QRectF();

    QTextDocument *document = m_document->textDocument();
    QTextCursor cursor(document);
    cursor.setPosition(position);
    QTextTable *table = cursor.currentTable();
    if (!table)
        return QRectF();

    return document->documentLayout()->frameBoundingRect(table);
}

QRectF DocumentHandler::checkboxRectAt(int position) const
{
    if (!m_document || !m_document->textDocument())
        return QRectF();

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    return TextFormatting::checkboxMarkerRect(cursor);
}
