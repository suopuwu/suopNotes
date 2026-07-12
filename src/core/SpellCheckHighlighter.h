#pragma once

#include <QSyntaxHighlighter>
#include <QTextBlock>

#include <QList>
#include <QPair>

class SpellCheckerBackend;

// Underlines misspelled words with SpellCheckUnderline formatting. Attached directly
// to editor.textDocument's QTextDocument (a QML TextArea, not a QWidget
// QTextEdit) — deliberately hand-rolled rather than using Sonnet::Highlighter
// from SonnetUi, which requires a QTextEdit/QPlainTextEdit widget we don't
// have. Gets Qt's native contentsChange-driven rehighlighting for free once
// attached to a document — no manual trigger wiring needed.
class SpellCheckHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    // Does not take ownership of backend — caller (DocumentHandler) owns it.
    // Deliberately does NOT use QSyntaxHighlighter(QTextDocument*) — that
    // overload makes the document this highlighter's QObject *parent*, so
    // Qt would auto-delete the highlighter when the document is destroyed.
    // DocumentHandler already owns this highlighter exclusively via a
    // unique_ptr; fighting Qt for ownership caused a real, confirmed
    // double-free crash (DocumentHandler::~DocumentHandler() deleting an
    // already-Qt-deleted highlighter once its TextArea's own QTextDocument
    // was torn down first). Using the parent-less constructor + setDocument()
    // below just observes the document (via QSyntaxHighlighter's own
    // QPointer-guarded tracking) without taking a stake in its lifetime.
    SpellCheckHighlighter(QTextDocument *document, SpellCheckerBackend *backend);

    // Gates highlighting on/off (e.g. Settings.spellcheckEnabled). When set
    // false, clears all existing underline formatting (via rehighlight()) and
    // stops flagging new text. Does not destroy the highlighter object; set
    // true again to resume flagging (also triggers a rehighlight so already-
    // present text picks up underlines immediately, not just future edits).
    void setActive(bool active);
    bool isActive() const;

    // Word-like token ranges in `block`'s text, skipping any hand-rolled list
    // marker prefix — the same tokenization highlightBlock() uses internally,
    // exposed so callers (e.g. DocumentHandler::wordAt) can find exactly which
    // range a given document position falls inside without re-implementing the
    // regex. Each pair is (startOffsetWithinBlock, length).
    static QList<QPair<int, int>> wordRangesInBlock(const QTextBlock &block);

protected:
    void highlightBlock(const QString &text) override;

private:
    SpellCheckerBackend *m_backend;
    bool m_active = true;
};
