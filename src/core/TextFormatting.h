#pragma once

#include <QTextCursor>

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
// Symmetric with toggleBulletList: converts an existing bullet list in place
// to numbered (and vice versa) rather than removing+recreating, so an
// in-progress list keeps its identity when the user switches style mid-edit.
// startNumber only applies when this call creates a brand-new list (e.g. the
// "5. " shorthand should start counting at 5, not silently reset to 1); it's
// ignored when converting an already-existing list's style in place, which
// keeps whatever count it already had.
void toggleNumberedList(QTextCursor cursor, int startNumber = 1);
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
}
