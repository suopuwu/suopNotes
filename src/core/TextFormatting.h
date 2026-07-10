#pragma once

#include <QTextBlock>
#include <QTextCursor>

QT_BEGIN_NAMESPACE
class QTextDocument;
QT_END_NAMESPACE

// Pure QTextCursor operations behind the editor's formatting toolbar.
// Deliberately NOT typed-syntax-driven — Qt's MarkdownText mode doesn't
// live-convert typed "**"/"#" (confirmed in Spike A), so formatting is only
// ever reachable via these calls (toolbar buttons/shortcuts), never by
// typing raw markdown. That is what makes the editor genuinely WYSIWYG.
//
// Each takes the cursor by value (a cheap handle into the QTextDocument it
// was constructed from) and mutates the underlying document directly.
namespace TextFormatting
{
void toggleBold(QTextCursor cursor);
void toggleItalic(QTextCursor cursor);
void toggleStrikethrough(QTextCursor cursor);
void toggleBulletList(QTextCursor cursor);
// Unlike toggleBulletList (still QTextList-based), numbered-list markers are
// literal, selectable text ("1. ", "2. ", ...) tagged with a custom block
// property rather than a QTextList — Qt's own list markers are paint-time
// decoration with no character position, so they can never be selected or
// copied, and QTextDocument::indentWidth() (the only knob Qt exposes for
// marker spacing) is a single document-wide value, not something that can
// differ per list. Hand-rolling the marker as real text makes both a
// non-issue: selection works for free, and the reserved space is just
// whatever that list's own widest marker actually measures.
// startNumber only applies when creating a brand-new list (e.g. the "5. "
// shorthand should start counting at 5, not silently reset to 1); toggling
// off and back on, or converting an existing bullet list to numbered,
// renumbers from 1.
void toggleNumberedList(QTextCursor cursor, int startNumber = 1);
// Same hand-rolled convention as toggleNumberedList, with a literal
// "[ ] "/"[x] " marker instead of a number — GFM task-list items.
void toggleCheckboxList(QTextCursor cursor);
// Flips an existing checkbox item's checked state in place. Returns false
// (does nothing) if the cursor isn't on a checkbox item at all.
bool toggleCheckboxChecked(QTextCursor cursor);
// Click-to-toggle (feature request): like toggleCheckboxChecked, but only
// acts when cursor's own position sits within its block's checkbox glyph
// itself (the leading "[ ]"/"[x]", not the rest of the item's text) — a
// click anywhere else in a checkbox item should just place the cursor
// normally, not toggle it.
bool tryToggleCheckboxAtPosition(QTextCursor cursor);
// Inserts a new item after the cursor's current hand-rolled list item
// (numbered or checkbox), inheriting its kind, and renumbers the list.
// Returns false (does nothing) if the cursor isn't on a hand-rolled list
// item — the caller's cue to fall through to plain Enter handling.
bool continueHandRolledList(QTextCursor cursor);
// The other half, mirroring bullet lists' own "Enter on an empty item exits
// the list" convention: strips the current item's marker, reverting it to a
// plain paragraph, and renumbers whatever remains of the list on either
// side. Only meaningful when the item's text (after its marker) is empty —
// callers check isHandRolledListItemEmpty first, same convention
// DocumentHandler::tryContinueList already follows for bullet lists.
void exitHandRolledList(QTextCursor cursor);
// True if the cursor's block is a hand-rolled list item (numbered or
// checkbox) whose text is empty apart from its own marker.
bool isHandRolledListItemEmpty(QTextCursor cursor);
// level 0 clears heading formatting back to a normal paragraph; 1-6 are h1-h6.
void setHeadingLevel(QTextCursor cursor, int level);
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

// Called once per freshly-parsed markdown chunk (see
// MarkdownSerialization::insertChunk), after Qt's own markdown importer has
// built native QTextList-based lists from "1. "/"- [ ] " syntax — converts
// any numbered-list block into the hand-rolled selectable-marker
// representation toggleNumberedList uses live, and any bullet-list block
// whose text is a checkbox pattern into a hand-rolled checkbox item,
// leaving genuine bullet lists (no checkbox brackets) as real QTextList
// items, untouched. Idempotent, and safe to call on a document with no
// lists in it at all.
void convertNativeListsToHandRolled(QTextDocument *document);
// True if `block` is a hand-rolled list item (numbered or checkbox).
// Exposed at the QTextBlock level (not just QTextCursor) for
// MarkdownSerialization's own run-splitting, which needs it as a run-
// boundary condition on the SAVE side too: Qt's markdown writer escapes any
// plain paragraph that merely starts with what looks like list syntax
// ("1. " -> "1\. ", "- " -> "\- ", confirmed empirically) since it can't
// know the block was deliberately meant to render as a list marker rather
// than being coincidental prose — bypassing Qt's writer entirely for a run
// already known (by construction) to be nothing BUT hand-rolled list
// blocks sidesteps that unconditionally, without risking unescaping a
// genuinely-plain paragraph that just happens to look similar.
bool isHandRolledListBlock(const QTextBlock &block);
}
