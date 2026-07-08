#pragma once

#include <QString>

class QTextDocument;

// QTextDocument::toMarkdown() treats blank lines purely as block
// separators: it renders at most one blank line between any two blocks and
// drops leading/trailing blank blocks entirely, regardless of how many the
// document actually has. For a WYSIWYG notes editor that's a real loss —
// pressing Enter a few extra times to add vertical space (or leaving space
// at the top/bottom of a note) gets silently shrunk away on every save.
//
// toMarkdownPreservingBlankLines() walks the document's own block structure
// (which does still record exactly how many blank blocks the user left) and
// reinserts that count around Qt's own per-run markdown output, so plain
// blank spacing round-trips faithfully for as long as the document stays
// live in memory. It deliberately leaves blank lines inside list items,
// blockquotes, headings, and fenced code blocks untouched — those aren't
// bare vertical spacing, they're structural or literal content, and Qt's
// own writer already handles them correctly.
namespace MarkdownSerialization
{
QString toMarkdownPreservingBlankLines(QTextDocument *document);

// The mirror-image problem on load: QTextDocument::setMarkdown() discards
// blank-line runs entirely at parse time (0, 1, 2, or 3 blank lines between
// two paragraphs all produce the exact same two adjacent blocks with
// nothing in between), and leading/trailing blank lines vanish with no
// trace at all. Since a note's document is only ever parsed once (at
// open — see DocumentHandler's class comment), that loss would otherwise
// be invisible until the note is reopened. This re-derives the blank-line
// structure directly from the raw source text and rebuilds it as real
// blank blocks in the document, so a note saved with toMarkdownPreserving
// BlankLines() round-trips back to the same in-memory shape when reopened.
void setMarkdownPreservingBlankLines(QTextDocument *document, const QString &markdown);
}
