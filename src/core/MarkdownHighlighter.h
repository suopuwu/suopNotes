#pragma once

#include <QSyntaxHighlighter>

// Paints markdown decoration (bold/italic/strikethrough/inline code/headings/
// list markers) by scanning the document's *literal markdown source* text —
// the document itself stays plain text, so what the user typed is exactly what
// gets saved. This replaces TextEdit.MarkdownText's structural parsing, which
// built a QTextDocument block/list/table object model and regenerated markdown
// on save (lossy: blank-line counts couldn't survive the round-trip).
//
// Attached directly to editor.textDocument's QTextDocument (a QML TextArea,
// not a QWidget QTextEdit), exactly like SpellCheckHighlighter. Gets Qt's
// native contentsChange-driven rehighlighting for free once attached — no
// manual trigger wiring, and crucially no timer: nothing runs while idle,
// which the repo's no-polling rule requires.
//
// Unlike SpellCheckHighlighter there is deliberately no setActive() gate —
// spellcheck is a user-facing preference, but visual formatting is not
// optional, so the highlighter is always live once attached.
class MarkdownHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    // Deliberately does NOT use QSyntaxHighlighter(QTextDocument*) — that
    // overload makes the document this highlighter's QObject *parent*, so Qt
    // would auto-delete the highlighter when the document is destroyed, racing
    // DocumentHandler's own unique_ptr ownership. That combination caused a
    // real, confirmed double-free crash for SpellCheckHighlighter already (see
    // its header); this uses the same parent-less ctor + setDocument() fix.
    explicit MarkdownHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;
};
