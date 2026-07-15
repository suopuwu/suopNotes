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
