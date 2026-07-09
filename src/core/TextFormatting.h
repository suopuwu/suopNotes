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
void toggleNumberedList(QTextCursor cursor);
// level 0 clears heading formatting back to a normal paragraph; 1-6 are h1-h6.
void setHeadingLevel(QTextCursor cursor, int level);
// Inserts a starter 2x2 table (with a header row) at the cursor's position.
void insertTable(QTextCursor cursor);
}
