#pragma once

#include <QPair>
#include <QRectF>
#include <QTextBlock>
#include <QTextCursor>

QT_BEGIN_NAMESPACE
class QTextDocument;
QT_END_NAMESPACE

// Pure QTextCursor operations behind the editor's formatting toolbar.
//
// Every function here edits the document's LITERAL MARKDOWN TEXT: bold really
// is inserting two asterisks, a heading really is a leading "# ", a list item
// really is a "- " prefix. MarkdownHighlighter paints the result, so the
// buffer and the saved file are always the same bytes. That is the whole point
// of the plain-text rewrite — there is no parallel structural model (char
// formats, QTextList, QTextTable, block properties) left to disagree with the
// text, which removes an entire class of "looks right, saves wrong" bugs.
//
// Marker parsing/rendering is deliberately NOT open-coded here — it lives in
// MarkdownList/MarkdownTable so this file, MarkdownHighlighter and
// SpellCheckHighlighter cannot disagree about where a marker ends.
//
// Each takes the cursor by value (a cheap handle into the QTextDocument it
// was constructed from) and mutates the underlying document directly.
namespace TextFormatting
{
// Each returns the [start, end) range the toggled content occupies afterwards,
// so a caller can restore the selection over the same TEXT. Qt does adjust an
// existing selection across these edits, but asymmetrically — the marker
// inserted at the selection's END is swallowed into it, while the one at its
// start just shifts it — so a plain "bold me" selection would come back as
// "bold me**" without this.
QPair<int, int> toggleBold(QTextCursor cursor);
QPair<int, int> toggleItalic(QTextCursor cursor);
QPair<int, int> toggleStrikethrough(QTextCursor cursor);
// Each list toggle applies its marker across the selection, or strips it if
// the selection is already entirely that kind. Bullets are ordinary "- " text
// like every other marker now — they used to be a real QTextList, whose
// paint-time markers had no character position and so could never be selected
// or copied.
void toggleBulletList(QTextCursor cursor);
// startNumber only applies when creating a brand-new list (e.g. the "5. "
// shorthand should start counting at 5, not silently reset to 1). An existing
// run always keeps counting from its own first item's number.
void toggleNumberedList(QTextCursor cursor, int startNumber = 1);
// GFM task list items, stored as the literal "- [ ] "/"- [x] " that gets
// saved. The pre-rewrite editor stored a Unicode ☐/☑ glyph in the document and
// translated it to GFM only on save — precisely the stored-vs-saved split this
// rewrite removes. MarkdownHighlighter dims the brackets in place instead.
void toggleCheckboxList(QTextCursor cursor);
// Flips an existing checkbox item's checked state in place. Returns false
// (does nothing) if the cursor isn't on a checkbox item at all.
bool toggleCheckboxChecked(QTextCursor cursor);
// Click-to-toggle (feature request): like toggleCheckboxChecked, but only
// acts when cursor's own position sits within the leading "- [x]" marker
// itself (up to and including the closing bracket, but not its trailing
// space, which reads as label padding) — a click anywhere else in a checkbox
// item should just place the cursor normally, not toggle it.
bool tryToggleCheckboxAtPosition(QTextCursor cursor);

// The result of a repair pass: whether anything was rewritten, and where the
// caret ends up once markers inserted ahead of it have shifted it along.
struct CheckboxRepair
{
    bool changed = false;
    int cursorPosition = 0;
};

// Re-asserts a checkbox marker on every non-empty line of `cursor`'s WHOLE
// document, and reports where `cursorPosition` moved to.
//
// This is the "you cannot delete the checkboxes" rule, and it is a repair
// rather than a prevention on purpose. A marker is literal document text
// ("- [ ] "), so the ways to destroy one are open-ended: backspacing into it,
// selecting the line and typing over it, pasting plain lines, dropping text
// in, undo. Intercepting each of those means fighting TextArea at every entry
// point and still missing one. Re-asserting the invariant after the edit
// covers all of them from a single call site, and is the same shape as the
// auto-list-shorthand and auto-link repairs the editor already runs.
//
// Per line:
//  - Already a checkbox item: left byte for byte alone, ticked state included.
//    This must hold, or every keystroke would untick the user's boxes.
//  - A bullet ("- item"): upgraded in place to "- [ ] item". Backspacing a
//    marker apart usually lands here first, so this is the common repair.
//  - Any other non-empty line: gets "- [ ] " prepended.
//  - A genuinely EMPTY line: left alone, deliberately. It is how the user gets
//    a blank line back, and forcing a marker on it would make pressing Enter
//    mint an empty item that cannot be removed. As soon as anything is typed
//    there the line stops being empty and the next pass marks it.
//
// Callers run this after real edits only, never after formatting-only changes
// (see DocumentHandler::contentEdited): re-writing the document from a
// highlighter pass would loop.
CheckboxRepair repairCheckboxList(QTextCursor cursor, int cursorPosition);

// Re-asserts a checkbox marker on the FIRST line specifically, even when that
// line is empty — the one place repairCheckboxList's "a blank line stays
// blank" rule must not apply.
//
// The TickTick task note's first line is its compose slot (see
// TickTickTaskList::kComposeIndex): permanent furniture the user types new
// tasks into, not a task of its own. Selecting the whole slot and deleting it
// used to leave a bare blank line — no glyph, no underline, no commit button —
// which repairCheckboxList deliberately left alone and Backspace deliberately
// refused to remove, so the only thing that ever restored it was the next
// pull's re-render, minutes later. That is what the reporter saw as the
// checkbox characters coming back on their own.
//
// Only the empty case is handled here: a first line with any text at all is
// already repairCheckboxList's business, and one that is already a checkbox is
// left byte for byte alone. Runs AFTER repairCheckboxList, so by the time it
// looks, "empty" really does mean empty.
CheckboxRepair ensureFirstLineCheckbox(QTextCursor cursor, int cursorPosition);
// Deletes the whole line `cursor` is on, but ONLY if that line is a CHECKED
// checkbox item — returns false and touches nothing otherwise.
//
// The check is the safety property, not a convenience: the caller is an
// animation that finished half a second after the click that started it, and in
// between the user may have unticked the box, retyped the line, or had a pull
// rewrite the note underneath it. Rather than making the caller re-verify what
// it is about to delete against a stale snapshot, this refuses to delete
// anything that is not still a ticked item.
//
// The line's own newline goes with it, so removing an item leaves no blank line
// behind: the FOLLOWING newline normally, or the PRECEDING one when this is the
// last line (there is no following one to take). Removing the only line leaves
// an empty document rather than a document containing one empty line.
bool removeCheckedCheckboxLine(QTextCursor cursor);
// Deletes the whole line `cursor` is on unconditionally — no checked-state
// check, unlike removeCheckedCheckboxLine above. Returns false only when the
// cursor has no valid block at all.
//
// The two exist separately because their callers have opposite needs.
// removeCheckedCheckboxLine serves a fade animation that decided to delete
// something half a second ago and must re-verify; this one serves a Backspace
// keystroke that is deciding right now, and whose whole point is to remove a
// line that is emphatically NOT ticked (an emptied-out task the user is
// backspacing away — see DocumentHandler::removeLineAt).
//
// Takes the line's own newline with it on exactly the same three-case rule
// removeCheckedCheckboxLine documents, so no blank line is left behind.
bool removeLine(QTextCursor cursor);
// Removes every whole line the range [start, end) touches, snapping EACH
// end outward to its own containing block's boundaries — a range that
// starts or ends mid-line still removes that entire line, rather than
// leaving the partial fragments on either side to be merged into one line
// the way a plain removeSelectedText() over such a range would (see
// NoteWindow.qml's Keys.onPressed, which calls this instead of letting a
// multi-line Backspace/Delete/Cut run TextArea's own default handling on a
// task-note selection whose endpoints don't sit on line boundaries).
//
// ALWAYS preserves the document's first block (block 0) — the TickTick task
// note's compose slot, permanent furniture in exactly the sense
// ensureFirstLineCheckbox's own comment describes. A range that reaches
// block 0 has its removal start from block 1 instead; a range that
// collapses entirely into block 0 removes nothing and returns false.
//
// `cursor` only needs to be attached to the right document — its own
// position/anchor are not read, only cursor.document(). One
// removeSelectedText() call, so one undo step no matter how many lines are
// removed.
bool removeLines(QTextCursor cursor, int start, int end);
// The text of a list item's label — everything after its "- ", "- [ ] " or
// "1. " marker — or the whole line verbatim when it isn't a list item at all.
// Distinct from listLabelPosition below, which answers WHERE that text starts;
// this answers what it says, which is what a caller asking "is this item empty"
// actually needs.
QString listLabelText(QTextCursor cursor);
// The document position where the label of `cursor`'s line begins — i.e. just
// past its "- ", "- [ ] " or "1. " marker, or the line's own start when it is
// not a list item at all.
//
// Distinct from listLabelPosition below, which answers the caret-placement
// question and therefore never moves a caret BACKWARDS (it returns the later of
// the two). A caller asking "is the caret sitting exactly at the start of this
// item's text" needs the raw answer, because the never-backwards one is equal
// to the caret's own position for every caret at or past the label.
int listLabelStart(QTextCursor cursor);
// Where a click that resolved to `cursor` should actually leave the caret.
//
// A bullet's "- " and a checkbox's "- [ ] " are invisible text sitting under a
// drawn glyph (see MarkdownList::Marker::glyphStart): every character of the
// marker except the reserved slot collapses to zero width, so EVERY position
// inside one renders at the glyph's own left or right edge. Measured on a
// checkbox at the default font: positions 0-2 all draw at x=0 and positions
// 3-5 all draw at x=14.69 — the box itself spans x=0 to x=14.69, and the
// item's text only starts at position 6, x=21.94. So a click on the box, or
// in the gap beside it where an empty item's label visibly begins, lands the
// caret INSIDE the marker while looking like it landed on (or just after) the
// checkbox. Typing then splits the marker — "- [ ] " with a "b" typed at
// position 4 becomes "- [ b] " — which no longer parses as a checkbox, so the
// box silently disappears (reported as "the cursor is inside of the checkbox,
// causing any content typed to break it").
//
// Returns the item's own text start for those two kinds, and `cursor`'s
// position unchanged everywhere else — including inside a NUMBERED item's
// "1. ", which MarkdownHighlighter deliberately leaves as ordinary visible
// text (its number is content, not syntax to read past), and which a user
// therefore has a real reason to click into and edit.
int listLabelPosition(QTextCursor cursor);
// Inserts a new item after the cursor's current list item, inheriting its
// kind, and renumbers the run. Now covers bullets too (they are marker text
// like everything else), so callers no longer need a separate QTextList path.
// A continued checkbox always starts unchecked. Returns false (does nothing)
// if the cursor isn't on a list item — the caller's cue to fall through to
// plain Enter handling.
bool continueHandRolledList(QTextCursor cursor);
// The other half, mirroring bullet lists' own "Enter on an empty item exits
// the list" convention: strips the current item's marker, reverting it to a
// plain paragraph, and renumbers whatever remains of the list on either
// side. Only meaningful when the item's text (after its marker) is empty —
// callers check isHandRolledListItemEmpty first, same convention
// DocumentHandler::tryContinueList already follows for bullet lists.
void exitHandRolledList(QTextCursor cursor);
// True if the cursor's block is a list item of any kind whose text is empty
// apart from its own marker.
bool isHandRolledListItemEmpty(QTextCursor cursor);
// Re-derives the current item's whole numbered-list run (its sequential
// numbers and shared marker width) if the cursor is on one — a no-op
// otherwise. Meant to be called after every edit (not just the dedicated
// toggle/continue/exit operations above), since ordinary text editing —
// backspacing an item away, selecting and deleting one outright — merges
// or removes blocks through Qt's own default editing, not through any of
// this file's own list-aware functions, and would otherwise leave the
// surviving items out of sequence.
void maintainHandRolledListNumbering(QTextCursor cursor);
// Gives every list item in `document` its hanging indent, so a wrapped item's
// continuation lines line up under its text rather than under its marker.
//
// Every list operation above already maintains this for the lines it touches;
// this exists for the one path that has no edit to hang off — loading a note,
// which replaces the whole buffer at once (DocumentHandler::setMarkdown). Call
// it there only from INSIDE that function's undo-disabled window and only
// AFTER setPlainText() has returned: indent is undoable document state, so
// applying it with undo enabled would make a freshly opened note's first
// Ctrl+Z undo its own indent, and applying it reentrantly from a
// contentsChange handler mid-setPlainText crashes.
//
// Deliberately NOT called on font change, even though a note's font size is
// what this is measured in — see the long comment in TextFormatting.cpp. Indent
// is knowingly stale after a font change until the line is next edited.
void applyListIndentToDocument(QTextDocument *document);
// level 0 clears heading formatting back to a normal paragraph; 1-6 are h1-h6.
void setHeadingLevel(QTextCursor cursor, int level);
// Tab (feature request): inserts four literal spaces at the cursor,
// replacing any selection. (The old warning here about TextEdit's own
// insert() silently dropping pure whitespace applied to MarkdownText mode's
// live renormalization, which no longer runs.)
void insertTabSpaces(QTextCursor cursor);
// Inserts a starter 2x2 table (with a header row) at the cursor's position.
void insertTable(QTextCursor cursor);

// The five functions below all no-op when the cursor isn't inside a table
// (e.g. a toolbar button clicked with the caret sitting in plain text) —
// there's nothing sensible to do, so they silently do nothing rather than
// asserting or requiring callers to check first.

// Inserts a new row directly below the row containing the cursor.
void insertTableRow(QTextCursor cursor);
// Removes the row containing the cursor. If it's the table's last
// remaining row, removes the whole table instead — mirrors common word
// processor behavior; a table can't be left with zero rows.
void deleteTableRow(QTextCursor cursor);
// Inserts a new column directly to the right of the column containing the
// cursor.
void insertTableColumn(QTextCursor cursor);
// Removes the column containing the cursor. If it's the table's last
// remaining column, removes the whole table instead, same reasoning as
// deleteTableRow.
void deleteTableColumn(QTextCursor cursor);
// Removes the entire table containing the cursor, including its
// surrounding frame (not just its rows/cells).
void deleteTable(QTextCursor cursor);

// Read-only counterparts to the toggles above, used to drive the toolbar's
// own "is this active at the cursor/selection" indicator (checked buttons,
// etc). Each queries the exact same QTextCursor state its matching toggle*
// function reads before deciding what to flip, so "does the button show as
// active" and "what will toggling do" can never disagree.
bool isBold(QTextCursor cursor);
bool isItalic(QTextCursor cursor);
bool isStrikethrough(QTextCursor cursor);
// 0 means "no heading" (a plain paragraph); 1-6 are h1-h6.
int headingLevel(QTextCursor cursor);
bool isBulletList(QTextCursor cursor);
bool isNumberedList(QTextCursor cursor);
bool isCheckboxList(QTextCursor cursor);
// Whether the cursor's checkbox item is currently checked. Returns false
// (not just "unchecked") when the cursor isn't on a checkbox item at all —
// callers that need to distinguish the two should check isCheckboxList
// first.
bool isCheckboxChecked(QTextCursor cursor);
// Whether the cursor currently sits inside a table — the same condition the
// five row/column mutators above already silently require before doing
// anything, exposed as its own query so the toolbar can hide those buttons
// entirely rather than showing them as permanent no-ops outside a table.
bool isInTable(QTextCursor cursor);



// The screen-space rect (the same document-layout coordinate space
// DocumentHandler::tableRect() already uses) of the checkbox glyph itself
// for the block containing `cursor`'s position — feature request: lets
// QML draw a hover highlight exactly over the glyph, matching
// tryToggleCheckboxAtPosition's own precision (only within the glyph
// itself, not the item's whole text). An empty QRectF if the cursor isn't
// positioned within a checkbox block's own marker.
QRectF checkboxMarkerRect(QTextCursor cursor);
}
