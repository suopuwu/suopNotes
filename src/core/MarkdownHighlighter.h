#pragma once

#include "TextSearch.h"

#include <QColor>
#include <QList>
#include <QSet>
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
// The markdown pass has a gate (setMarkdownDecorationEnabled), added for code
// mode — in a code note `**` is an operator and `#` is a comment, so collapsing
// them as live-preview punctuation actively hides real syntax. It is
// deliberately NOT the setActive() gate SpellCheckHighlighter has: this class
// also paints find/replace's match highlight (see setSearchMatches), which is
// not markdown decoration and must keep working with the markdown pass off.
// The gate covers highlightMarkdown() alone; highlightBlock() says so.
//
// It also paints the find/replace panel's match highlighting (setSearchMatches
// below), which is not markdown decoration and would otherwise want a class of
// its own. It cannot have one: two QSyntaxHighlighters on one QTextDocument
// clobber rather than compose (DocumentHandler::setDocument() documents the
// empirical finding at length — whichever runs LAST erases the other's
// formats), so a third highlighter would either erase markdown decoration or be
// erased by it. Every char format this document renders has to be applied by
// this one class.
//
// The alternative — a QML overlay like NoteWindow.qml's spellcheck underline —
// exists only because TextArea won't render *underlines* from setFormat(). A
// background color it renders fine (inlineCodeFormat has relied on that for as
// long as it has existed), and the match highlight was confirmed the same way
// the underline's absence originally was: grabbing the editor before and after
// pushing matches and diffing the pixels, on a real session, which showed the
// amber landing on exactly the matched words and nothing else. That check can't
// live in the test suite — ctest runs the QML tests with
// QT_QPA_PLATFORM=offscreen (tests/qml/CMakeLists.txt), where grabToImage
// returns blank frames and every such comparison passes vacuously. The suite
// asserts the ranges and the panel's push/clear wiring instead; if this ever
// needs re-verifying, do it by hand against a real compositor.
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

    // The cursor's exact document position (-1 for "nowhere"), NOT just its
    // block — links and images need finer granularity than "which line" (bug
    // found live: with only block-level reveal, a link stayed expanded to its
    // raw "[text](url)" as long as the cursor was ANYWHERE on that line — e.g.
    // several words past it, or on the very trailing space auto-linking a typed
    // URL leaves behind — instead of only while actually on the link itself).
    // Headings/lists/emphasis still key off setCursorBlockNumber above
    // unchanged; only applyLinks() and highlightImageLine() read this.
    //
    // Internally resolves the block number and forwards to
    // setCursorBlockNumber() (so its own two-block repaint and no-op guard
    // still apply for the coarse case), then ALSO repaints the current block
    // when the position moved WITHIN it — needed because moving from touching
    // a link to just past it doesn't change the block number at all, and
    // without this the reveal would only update on the next line change. This
    // extra repaint only happens for a block whose text can actually contain a
    // link or image, so plain prose lines pay nothing extra for cursor moves
    // within them.
    void setCursorPosition(int position);

    // Whether the markdown decoration pass runs at all. True by default —
    // an ordinary note is a markdown note. Code mode turns this off (subject
    // to Settings::codeModeDisablesMarkdownHighlighting), leaving the buffer's
    // literal text rendered exactly as typed, in one uniform format.
    //
    // Does NOT gate the find/replace match highlight — see the class comment.
    void setMarkdownDecorationEnabled(bool enabled);
    bool markdownDecorationEnabled() const;

    // Every occurrence the find/replace panel is currently showing, in document
    // positions, plus which one of them is the "current" match (-1 for none).
    // All of them get a translucent highlight so the user can see at a glance
    // where else the term appears; the current one gets a stronger one, so it
    // stays identifiable even when the editor's own selection isn't painted
    // over it (i.e. once the user clicks back into the note and the selection
    // collapses). Pass an empty list to clear.
    //
    // Rehighlights only the blocks the old and new match lists actually touch,
    // not the whole document — same reasoning as setCursorBlockNumber's, and it
    // matters more here: this is called on every keystroke in the find field.
    //
    // The positions are a SNAPSHOT, not a live query: an edit that shifts text
    // leaves them stale until the panel re-searches and pushes again (~150ms,
    // its debounce). Only the edited block repaints in between, so at worst one
    // line's highlight sits at the pre-edit offset for that long and then
    // self-corrects. The match list driving the panel's own "3 of 7" is stale
    // for exactly the same window, so nothing here is newly wrong.
    void setSearchMatches(const QList<TextSearch::Match> &matches, int currentIndex);

    // Read back what setSearchMatches() last stored. Exposed for tests and for
    // DocumentHandler's QML-facing getters, which is the only way the QML test
    // suite can see this state at all — a char format inside a block's
    // QTextLayout is not reachable from QML.
    const QList<TextSearch::Match> &searchMatches() const;
    int currentSearchMatch() const;

    // Paints the whole line containing `position` at `opacity` (1 = normal,
    // 0 = invisible), so a caller can animate a line out of view without
    // touching a byte of the buffer. Pass position -1 to clear.
    //
    // `baseTextColor` is the editor's own text color, and has to be passed in:
    // most characters carry no explicit foreground at all (they inherit the
    // TextArea's), so there is no color here to take an alpha off — and
    // defaulting to black would turn a dark note's text black on its way out.
    //
    // Repaints only the old and new lines, like setSearchMatches above. Meant
    // to be called once per animation frame, which is why it is one
    // rehighlightBlock and nothing else.
    void setFadedLine(int position, qreal opacity, const QColor &baseTextColor);

protected:
    void highlightBlock(const QString &text) override;

private:
    // The markdown pass — everything this class did before search highlighting
    // was folded in. Split out so applySearchHighlight() below can run after it
    // on EVERY block, including the ones this returns early from (an empty
    // line, a table row): a match inside a table cell is still a match.
    void highlightMarkdown(const QString &text);

    // Merges the match highlight over whatever the markdown pass already put
    // down, so highlighting a search hit inside a **bold** word keeps it bold.
    void applySearchHighlight(const QString &text);

    // Scales the alpha of every character on the faded line, LAST, over
    // everything the markdown and search passes put down.
    //
    // Multiplying the existing alpha rather than assigning one is load-bearing,
    // not a nicety: a collapsed marker is transparent text that still occupies
    // its slot (see hiddenMarkerFormat), so ASSIGNING an alpha to it would make
    // the literal "- [ ] " the user never sees fade INTO view as the line faded
    // out. Zero times anything is still zero, so multiplying keeps it hidden,
    // and dims revealed punctuation and link color proportionally for free.
    void applyLineFade(const QString &text);

    // Block numbers every match in `matches` touches — which lines
    // setSearchMatches() has to repaint. Adds to `out` rather than returning,
    // so the old and new match lists can accumulate into one set.
    void collectBlockNumbers(const QList<TextSearch::Match> &matches, QSet<int> &out) const;

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

    // A line that is nothing but an image, "![alt](src)". While the cursor is
    // NOT touching the image markdown itself (see m_cursorPosition — this is
    // position-based, not block-level: being elsewhere on an otherwise-blank
    // line doesn't count, though in practice an image is always alone on its
    // line) it collapses to a transparent band that NoteWindow.qml draws the
    // real picture (or a "Select image…" button, if empty) into — see
    // DocumentHandler::imageRanges. While touched, it stays raw, editable text
    // at normal size. Returns true if `text` was an image line (and so should
    // skip the heading/list/emphasis passes), false otherwise.
    bool highlightImageLine(const QString &text);

    int m_cursorBlockNumber = -1;
    // See setCursorPosition's own doc comment for why this exists alongside
    // m_cursorBlockNumber rather than being derivable from it.
    int m_cursorPosition = -1;
    bool m_markdownDecorationEnabled = true;
    QList<TextSearch::Match> m_searchMatches;
    int m_currentSearchMatch = -1;
    // -1 when nothing is fading, which is almost always.
    int m_fadedLinePosition = -1;
    qreal m_fadeOpacity = 1.0;
    QColor m_fadeBaseColor;
};
