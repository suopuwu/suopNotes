#pragma once

#include <QString>
#include <QStringList>

class QTextDocument;

// Pipe-table geometry over *literal* markdown text.
//
// The pre-rewrite editor got tables from QTextTable: an object model with real
// row/column indices, cellAt(cursor) hit-testing, and hidden frame-boundary
// characters. None of that survives the plain-text rewrite — a table is now
// just a run of adjacent lines that happen to start with "|", and every
// operation is a text splice. This namespace is the replacement for that
// geometry: it finds where a table starts and ends, and how its cells divide
// up, so the editing operations can be expressed as line edits.
//
// Everything here is a pure function over text. Nothing mutates a document,
// nothing touches formatting, and nothing depends on a cursor — which is what
// makes it directly testable and keeps the hard part (2-D region detection)
// separate from the cursor plumbing that will sit on top of it.
namespace MarkdownTable
{

// The extent of one contiguous pipe table, as block (line) numbers into the
// document. A table is only recognised when it has the GFM-required delimiter
// row ("|---|---|") as its *second* line — a lone "| a | b |" with nothing
// under it is deliberately not a table, matching GFM and avoiding claiming
// ordinary prose that happens to contain pipes.
struct Region
{
    int firstBlockNumber = -1; // the header row
    int lastBlockNumber = -1;  // the final body row (or the delimiter, if no body yet)
    int columnCount = 0;

    bool isValid() const { return firstBlockNumber >= 0; }
    // Body rows only — excludes the header and the delimiter row.
    int bodyRowCount() const
    {
        return isValid() ? lastBlockNumber - firstBlockNumber - 1 : 0;
    }
};

// Whether `line` looks like any row of a pipe table (its first non-space
// character is "|"). Cheap, syntactic, and deliberately does not care whether
// the surrounding lines make it a *valid* table — regionAt() decides that.
bool isTableRow(const QString &line);

// Whether `line` is a GFM delimiter row: every cell matches ---, :---, ---:
// or :---:. This is what separates a real table from prose containing pipes.
bool isDelimiterRow(const QString &line);

// The cell contents of one row line, outer pipes stripped and each cell
// trimmed. A backslash-escaped "\|" is content, not a boundary, so it never
// splits a cell.
QStringList splitCells(const QString &line);

// Renders cells back into a "| a | b |" row line. Inverse of splitCells for
// any cell text that does not itself need escaping.
QString formatRow(const QStringList &cells);

// A delimiter row for the given column count: "| --- | --- |".
QString delimiterRow(int columnCount);

// The table containing `blockNumber`, or an invalid Region if that line isn't
// part of one. Walks outward from the line to find the run's extent, then
// validates the delimiter row — this is the 2-D detection the whole table
// rewrite rests on.
Region regionAt(const QTextDocument *document, int blockNumber);

// Which cell a character offset within a row line falls in, or -1 if the line
// has no cells. This replaces QTextTable::cellAt(cursor): with no object model
// to ask, the column is derived by counting how many unescaped pipes sit to the
// left of the offset. Offsets inside the outer pipes clamp to the first/last
// cell rather than reporting "outside", so a click at the very edge of a row
// still lands somewhere sensible.
int columnAt(const QString &line, int offsetInLine);

// The [start, end) offsets of `column`'s content within `line` (trimmed cell
// text, excluding the surrounding pipes and padding). Returns false if the
// column doesn't exist. This is what lets a caller place a cursor *inside* a
// cell — the plain-text stand-in for QTextTable's cell geometry.
bool cellContentRange(const QString &line, int column, int *start, int *end);

}
