#include "MarkdownTable.h"

#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>

namespace
{

// A cell boundary is an unescaped "|". Walking character by character (rather
// than QString::split) is what lets "\|" stay content: split() has no notion of
// escaping and would cut the cell in half.
QList<int> unescapedPipePositions(const QString &line)
{
    QList<int> positions;
    for (int i = 0; i < line.size(); ++i) {
        if (line.at(i) != QLatin1Char('|'))
            continue;
        int backslashes = 0;
        for (int j = i - 1; j >= 0 && line.at(j) == QLatin1Char('\\'); --j)
            ++backslashes;
        // An odd run of backslashes escapes the pipe; an even run escapes the
        // backslashes themselves and leaves the pipe as a real boundary.
        if (backslashes % 2 == 0)
            positions.append(i);
    }
    return positions;
}

const QRegularExpression &delimiterCellPattern()
{
    // ---, :---, ---:, :---: — GFM's alignment markers. At least one dash.
    static const QRegularExpression pattern(QStringLiteral("^:?-+:?$"));
    return pattern;
}

// Each cell's [start, end) span in `line`'s own coordinates — the single place
// cell boundaries are decided, so splitting text and hit-testing an offset can
// never disagree about where a cell begins. Spans are raw (padding included);
// callers trim if they want content.
QList<QPair<int, int>> cellSpans(const QString &line)
{
    const QList<int> pipes = unescapedPipePositions(line);
    if (pipes.isEmpty())
        return {};

    QList<QPair<int, int>> spans;
    for (int i = 0; i + 1 < pipes.size(); ++i)
        spans.append(qMakePair(pipes.at(i) + 1, pipes.at(i + 1)));

    // Text after the final pipe means the row is still being typed and its last
    // cell isn't closed yet ("| a | b"). Counting it keeps the column count
    // stable mid-keystroke; a closed row's trailing "" is correctly ignored.
    const int tailStart = pipes.last() + 1;
    if (!line.mid(tailStart).trimmed().isEmpty())
        spans.append(qMakePair(tailStart, line.size()));

    return spans;
}

}

namespace MarkdownTable
{

bool isTableRow(const QString &line)
{
    return line.trimmed().startsWith(QLatin1Char('|'));
}

QStringList splitCells(const QString &line)
{
    QStringList cells;
    for (const auto &span : cellSpans(line))
        cells.append(line.mid(span.first, span.second - span.first).trimmed());
    return cells;
}

int columnAt(const QString &line, int offsetInLine)
{
    const QList<QPair<int, int>> spans = cellSpans(line);
    if (spans.isEmpty())
        return -1;

    for (int i = 0; i < spans.size(); ++i) {
        // <= end, so an offset resting directly on a closing pipe still counts
        // as the cell it closes rather than falling through to the next one.
        if (offsetInLine <= spans.at(i).second)
            return i;
    }
    // Past the final pipe — clamp to the last cell rather than reporting
    // "outside": a click at the row's right edge should still land in a cell.
    return spans.size() - 1;
}

bool cellContentRange(const QString &line, int column, int *start, int *end)
{
    const QList<QPair<int, int>> spans = cellSpans(line);
    if (column < 0 || column >= spans.size())
        return false;

    int contentStart = spans.at(column).first;
    int contentEnd = spans.at(column).second;
    while (contentStart < contentEnd && line.at(contentStart).isSpace())
        ++contentStart;
    while (contentEnd > contentStart && line.at(contentEnd - 1).isSpace())
        --contentEnd;

    if (start)
        *start = contentStart;
    if (end)
        *end = contentEnd;
    return true;
}

bool isDelimiterRow(const QString &line)
{
    if (!isTableRow(line))
        return false;
    const QStringList cells = splitCells(line);
    if (cells.isEmpty())
        return false;
    for (const QString &cell : cells) {
        if (!delimiterCellPattern().match(cell).hasMatch())
            return false;
    }
    return true;
}

QString formatRow(const QStringList &cells)
{
    QString row = QStringLiteral("|");
    for (const QString &cell : cells)
        row += QStringLiteral(" %1 |").arg(cell);
    return row;
}

QString delimiterRow(int columnCount)
{
    QStringList cells;
    cells.reserve(columnCount);
    for (int i = 0; i < columnCount; ++i)
        cells.append(QStringLiteral("---"));
    return formatRow(cells);
}

Region regionAt(const QTextDocument *document, int blockNumber)
{
    Region region;
    if (!document)
        return region;

    const QTextBlock block = document->findBlockByNumber(blockNumber);
    if (!block.isValid() || !isTableRow(block.text()))
        return region;

    // Walk outward to the run's extent. A blank line, prose, or the document
    // edge ends the table — there is no frame boundary to consult any more,
    // the run of "|" lines *is* the table.
    int first = blockNumber;
    for (QTextBlock b = block.previous(); b.isValid() && isTableRow(b.text()); b = b.previous())
        first = b.blockNumber();

    int last = blockNumber;
    for (QTextBlock b = block.next(); b.isValid() && isTableRow(b.text()); b = b.next())
        last = b.blockNumber();

    // GFM: the delimiter must be the second line of the run. Without it this is
    // just prose containing pipes, so claiming it would be wrong — and would let
    // a toolbar table action fire on a line the user doesn't consider a table.
    if (last - first < 1)
        return region;
    const QTextBlock delimiter = document->findBlockByNumber(first + 1);
    if (!delimiter.isValid() || !isDelimiterRow(delimiter.text()))
        return region;

    // The delimiter row itself is structure, not a cell the caller can sit in,
    // but it is still inside the table for isInTable purposes.
    region.firstBlockNumber = first;
    region.lastBlockNumber = last;
    region.columnCount = splitCells(document->findBlockByNumber(first).text()).size();
    return region;
}

}
