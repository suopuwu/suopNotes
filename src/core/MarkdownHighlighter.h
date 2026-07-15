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

    // Which line the cursor is on, as a block number (-1 for "nowhere", e.g.
    // the editor isn't focused). Markdown punctuation collapses to zero width
    // on every OTHER line and is revealed, dimmed, on this one — the
    // Obsidian/CodeMirror live-preview behavior: you read rendered text, and
    // the raw syntax appears only where you're actually editing it.
    //
    // Rehighlights just the two blocks whose appearance can change (the line
    // being left and the line being entered) rather than the whole document,
    // so this stays cheap enough to call on every cursor move. Setting the
    // same value twice is a no-op, which matters because a plain left/right
    // keypress within one line fires cursorPositionChanged too.
    void setCursorBlockNumber(int blockNumber);
    int cursorBlockNumber() const;

protected:
    void highlightBlock(const QString &text) override;

private:
    // Renders one row of a pipe table: collapses the pipes, and stretches each
    // cell so its column lines up with the same column in every other row.
    // Returns false if `text` isn't actually part of a valid table, in which
    // case the caller styles it as ordinary prose.
    //
    // The alignment is purely visual — not one byte of the buffer changes — so
    // a hand-typed ragged table renders as a neat grid and still saves exactly
    // as it was typed. NoteWindow.qml draws the grid lines themselves over the
    // top (see DocumentHandler::tableRanges).
    bool highlightTableRow(const QString &text);

    int m_cursorBlockNumber = -1;
};
