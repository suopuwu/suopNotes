#include "TextFormatting.h"

#include "MarkdownList.h"
#include "MarkdownTable.h"

#include <QAbstractTextDocumentLayout>
#include <QFont>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QStringList>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextLine>

namespace TextFormatting
{

namespace
{

// --- Headings ----------------------------------------------------------

// A line's heading level: the number of leading "#" characters, but only when
// they're followed by whitespace. GFM requires that space, so "#hashtag" is
// ordinary text, not an H1 — and more than six hashes isn't a heading at all.
int leadingHashCount(const QString &text)
{
    int hashes = 0;
    while (hashes < text.size() && text.at(hashes) == QLatin1Char('#'))
        ++hashes;
    if (hashes == 0 || hashes > 6)
        return 0;
    if (hashes >= text.size() || !text.at(hashes).isSpace())
        return 0;
    return hashes;
}

// --- Inline emphasis, as literal markdown markers -----------------------
//
// Emphasis used to be a QTextCharFormat merge over the selection. Under the
// plain-text buffer there is no character formatting to carry it: bold IS the
// two asterisks, and the highlighter re-derives the look from them. So every
// toggle below is a text edit, and every query below is a text scan.

QString boldMarker()
{
    return QStringLiteral("**");
}

QString italicMarker()
{
    return QStringLiteral("*");
}

QString strikethroughMarker()
{
    return QStringLiteral("~~");
}

// Bold's alternate GFM spelling. Toggling always writes "**", but a query has
// to recognise "__bold__" too — a user may have typed it, or pasted it.
QString boldUnderscoreMarker()
{
    return QStringLiteral("__");
}

QString italicUnderscoreMarker()
{
    return QStringLiteral("_");
}

QString documentTextAt(const QTextDocument *document, int position, int length)
{
    if (!document || length <= 0 || position < 0)
        return QString();
    // characterCount() includes the document's final block separator, so the
    // last real character sits at characterCount() - 2.
    if (position + length > document->characterCount() - 1)
        return QString();

    QTextCursor cursor(const_cast<QTextDocument *>(document));
    cursor.setPosition(position);
    cursor.setPosition(position + length, QTextCursor::KeepAnchor);
    return cursor.selectedText();
}

// Whether `marker` sits immediately outside the selection on both sides —
// "**bold**" with "bold" selected. This is the shape the toggle itself
// produces, so it's the common case.
bool isWrappedOutside(const QTextCursor &cursor, const QString &marker)
{
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    return documentTextAt(cursor.document(), start - marker.size(), marker.size()) == marker
        && documentTextAt(cursor.document(), end, marker.size()) == marker;
}

// Whether the selection itself spans the markers too — the shape you get by
// selecting a whole "**bold**" run, e.g. by double-clicking through it or
// selecting a whole line. Without this, toggling such a selection would nest a
// second pair of markers instead of removing the existing one.
bool isWrappedInside(const QTextCursor &cursor, const QString &marker)
{
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    if (end - start < 2 * marker.size())
        return false;
    return documentTextAt(cursor.document(), start, marker.size()) == marker
        && documentTextAt(cursor.document(), end - marker.size(), marker.size()) == marker;
}

// A single "*" must not report the "**" of a bold run as italic, and a single
// "_" must not report "__". Since the italic markers are prefixes of the bold
// ones, a naive scan for "*" matches "**" every time — so an italic query has
// to explicitly reject the doubled form.
bool isDoubledAround(const QTextCursor &cursor, const QString &marker)
{
    const QString doubled = marker + marker;
    return isWrappedOutside(cursor, doubled) || isWrappedInside(cursor, doubled);
}

// Each of these returns the [start, end) range the content occupies once the
// edit is done, so a caller can put the selection back over the same TEXT. Qt
// adjusts an existing selection for them automatically, but asymmetrically: the
// marker inserted at the selection's end gets swallowed INTO the selection
// while the one at its start merely pushes it along — leaving "bold me**"
// selected, which is both visibly wrong and makes isBold() read false on the
// very selection it just emboldened.
QPair<int, int> removeMarkersOutside(QTextCursor cursor, const QString &marker)
{
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    QTextCursor edit(cursor.document());
    edit.beginEditBlock();
    // Trailing marker first: removing it leaves every offset before it
    // untouched, so `start` is still correct on the second removal.
    edit.setPosition(end);
    edit.setPosition(end + marker.size(), QTextCursor::KeepAnchor);
    edit.removeSelectedText();
    edit.setPosition(start - marker.size());
    edit.setPosition(start, QTextCursor::KeepAnchor);
    edit.removeSelectedText();
    edit.endEditBlock();
    return qMakePair(start - marker.size(), end - marker.size());
}

QPair<int, int> removeMarkersInside(QTextCursor cursor, const QString &marker)
{
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    QTextCursor edit(cursor.document());
    edit.beginEditBlock();
    edit.setPosition(end - marker.size());
    edit.setPosition(end, QTextCursor::KeepAnchor);
    edit.removeSelectedText();
    edit.setPosition(start);
    edit.setPosition(start + marker.size(), QTextCursor::KeepAnchor);
    edit.removeSelectedText();
    edit.endEditBlock();
    return qMakePair(start, end - 2 * marker.size());
}

QPair<int, int> toggleInlineMarker(QTextCursor cursor, const QString &marker)
{
    if (!cursor.document())
        return qMakePair(cursor.selectionStart(), cursor.selectionEnd());

    if (isWrappedOutside(cursor, marker))
        return removeMarkersOutside(cursor, marker);
    if (isWrappedInside(cursor, marker))
        return removeMarkersInside(cursor, marker);

    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    QTextCursor edit(cursor.document());
    edit.beginEditBlock();
    // End first, for the same reason as removeMarkersOutside: inserting at the
    // end can't shift `start`, but inserting at the start would shift `end`.
    edit.setPosition(end);
    edit.insertText(marker);
    edit.setPosition(start);
    edit.insertText(marker);
    edit.endEditBlock();
    return qMakePair(start + marker.size(), end + marker.size());
}

// --- Lists -------------------------------------------------------------
//
// Every list operation is now "rewrite this line's leading marker text".
// The old implementation tagged each block with a custom format property and
// needed a whole "healing" pass to re-stamp blocks whose tag Qt's paste/merge
// machinery had silently dropped while leaving the text intact. With the text
// as the only truth, that entire class of desync is gone — nothing to lose.

// The blocks a selection touches. Collected up front because the edits below
// rewrite line prefixes, which shifts the positions of later blocks.
QList<QTextBlock> selectedBlocks(const QTextCursor &cursor)
{
    QList<QTextBlock> blocks;
    QTextDocument *document = cursor.document();
    if (!document)
        return blocks;

    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    const QTextBlock firstBlock = document->findBlock(start);
    // end - 1 when there's a real selection: a selection ending exactly at the
    // start of the next block (i.e. it only swallowed the previous block's
    // trailing newline) shouldn't pull that next, unselected block in too.
    const QTextBlock lastBlock = document->findBlock(end > start ? end - 1 : end);

    for (QTextBlock b = firstBlock; b.isValid(); b = b.next()) {
        blocks.append(b);
        if (b.blockNumber() == lastBlock.blockNumber())
            break;
    }
    return blocks;
}

// Swaps `block`'s existing list marker (if any) for `marker` — an empty
// `marker` just strips it. `edit` must already be inside an edit block.
void replaceMarker(QTextCursor &edit, const QTextBlock &block, const QString &marker)
{
    const MarkdownList::Marker existing = MarkdownList::parse(block.text());
    edit.setPosition(block.position());
    edit.setPosition(block.position() + existing.length, QTextCursor::KeepAnchor);
    if (edit.selectedText() == marker) {
        edit.clearSelection();
        return;
    }
    edit.insertText(marker); // replaces the selection
}

// Renumbers the whole contiguous run of numbered items containing
// `blockNumber`, counting up from whatever the run's FIRST item says. Keeping
// the first item's own number (rather than forcing 1) is what lets a list
// deliberately started at "5." stay at 5.
//
// This is the plain-text replacement for the old renumberRun + walkRunHealing
// pair: a run is just "adjacent lines that parse as numbered", so there is no
// tag to lose and nothing to heal.
void renumberRunAt(QTextDocument *document, int blockNumber)
{
    if (!document)
        return;
    const QTextBlock at = document->findBlockByNumber(blockNumber);
    if (!at.isValid() || MarkdownList::parse(at.text()).kind != MarkdownList::Kind::Numbered)
        return;

    int first = blockNumber;
    while (first > 0
           && MarkdownList::parse(document->findBlockByNumber(first - 1).text()).kind
               == MarkdownList::Kind::Numbered)
        --first;

    int last = blockNumber;
    while (last + 1 < document->blockCount()
           && MarkdownList::parse(document->findBlockByNumber(last + 1).text()).kind
               == MarkdownList::Kind::Numbered)
        ++last;

    const int startNumber = MarkdownList::parse(document->findBlockByNumber(first).text()).number;

    QTextCursor edit(document);
    edit.beginEditBlock();
    // Bottom-up: rewriting a marker can change its width ("9. " -> "10. "),
    // which shifts every later block's position but never an earlier one's.
    for (int i = last; i >= first; --i) {
        const QTextBlock block = document->findBlockByNumber(i);
        replaceMarker(edit, block, MarkdownList::numberedMarker(startNumber + (i - first)));
    }
    edit.endEditBlock();
}

// Applies `kind` across the selection, or strips it if the selection is
// already entirely that kind — the toggle semantics every list button wants.
void toggleListKind(QTextCursor cursor, MarkdownList::Kind kind, int startNumber)
{
    QTextDocument *document = cursor.document();
    if (!document)
        return;

    const QList<QTextBlock> blocks = selectedBlocks(cursor);
    if (blocks.isEmpty())
        return;

    bool allAlready = true;
    for (const QTextBlock &block : blocks) {
        if (MarkdownList::parse(block.text()).kind != kind) {
            allAlready = false;
            break;
        }
    }

    QTextCursor edit(document);
    edit.beginEditBlock();
    // Bottom-up, same reason as renumberRunAt: marker widths differ, so
    // rewriting one shifts every block after it.
    for (int i = blocks.size() - 1; i >= 0; --i) {
        const QTextBlock &block = blocks.at(i);
        QString marker;
        if (!allAlready) {
            switch (kind) {
            case MarkdownList::Kind::Bullet:
                marker = MarkdownList::bulletMarker();
                break;
            case MarkdownList::Kind::Numbered:
                // Sequential from the top of the selection; renumberRunAt
                // below then reconciles this against any adjacent run.
                marker = MarkdownList::numberedMarker(startNumber + i);
                break;
            case MarkdownList::Kind::Checkbox:
                marker = MarkdownList::checkboxMarker(false);
                break;
            case MarkdownList::Kind::None:
                break;
            }
        }
        replaceMarker(edit, block, marker);
    }
    edit.endEditBlock();

    if (!allAlready && kind == MarkdownList::Kind::Numbered)
        renumberRunAt(document, blocks.first().blockNumber());
}

// --- Tables ------------------------------------------------------------

// Rewrites every line of `region` through `transform`, which maps a row's
// cells to its new cells. `isDelimiter` lets a caller treat the "---" row
// differently from a content row (a new column needs "---" there, not "").
void rewriteTableRows(QTextDocument *document, const MarkdownTable::Region &region,
                      const std::function<QStringList(QStringList, bool)> &transform)
{
    QTextCursor edit(document);
    edit.beginEditBlock();
    // Bottom-up: rewriting a row changes its length, shifting every row below.
    for (int i = region.lastBlockNumber; i >= region.firstBlockNumber; --i) {
        const QTextBlock block = document->findBlockByNumber(i);
        const bool isDelimiter = MarkdownTable::isDelimiterRow(block.text());
        const QStringList cells =
            transform(MarkdownTable::splitCells(block.text()), isDelimiter);

        edit.setPosition(block.position());
        edit.setPosition(block.position() + block.text().size(), QTextCursor::KeepAnchor);
        edit.insertText(MarkdownTable::formatRow(cells));
    }
    edit.endEditBlock();
}

// Removes an entire table, including the newline that joined it to the rest of
// the document. This is the plain-text stand-in for the old implementation's
// trick of selecting from the QTextFrame's hidden boundary characters
// (firstPosition() - 1 to lastPosition() + 1), which existed because
// removeRows()/removeColumns() can never take a QTextTable below 1x1.
void removeTableRegion(QTextDocument *document, const MarkdownTable::Region &region)
{
    const QTextBlock first = document->findBlockByNumber(region.firstBlockNumber);
    const QTextBlock last = document->findBlockByNumber(region.lastBlockNumber);

    QTextCursor edit(document);
    edit.setPosition(first.position());
    // Swallow the trailing newline so the table's removal doesn't leave a
    // blank line behind; if the table ends the document there's no trailing
    // newline to take, so take the leading one instead.
    if (last.next().isValid())
        edit.setPosition(last.position() + last.length(), QTextCursor::KeepAnchor);
    else {
        if (first.previous().isValid())
            edit.setPosition(first.position() - 1);
        edit.setPosition(last.position() + last.text().size(), QTextCursor::KeepAnchor);
    }
    edit.removeSelectedText();
}

MarkdownTable::Region regionFor(const QTextCursor &cursor)
{
    if (!cursor.document())
        return {};
    return MarkdownTable::regionAt(cursor.document(), cursor.block().blockNumber());
}

int columnFor(const QTextCursor &cursor)
{
    const QTextBlock block = cursor.block();
    return MarkdownTable::columnAt(block.text(), cursor.position() - block.position());
}

}

QPair<int, int> toggleBold(QTextCursor cursor)
{
    return toggleInlineMarker(cursor, boldMarker());
}

QPair<int, int> toggleItalic(QTextCursor cursor)
{
    return toggleInlineMarker(cursor, italicMarker());
}

QPair<int, int> toggleStrikethrough(QTextCursor cursor)
{
    return toggleInlineMarker(cursor, strikethroughMarker());
}

void toggleBulletList(QTextCursor cursor)
{
    toggleListKind(cursor, MarkdownList::Kind::Bullet, 1);
}

void toggleNumberedList(QTextCursor cursor, int startNumber)
{
    toggleListKind(cursor, MarkdownList::Kind::Numbered, startNumber);
}

void toggleCheckboxList(QTextCursor cursor)
{
    toggleListKind(cursor, MarkdownList::Kind::Checkbox, 1);
}

bool toggleCheckboxChecked(QTextCursor cursor)
{
    const QTextBlock block = cursor.block();
    const MarkdownList::Marker marker = MarkdownList::parse(block.text());
    if (marker.kind != MarkdownList::Kind::Checkbox)
        return false;

    QTextCursor edit(cursor.document());
    edit.setPosition(block.position());
    edit.setPosition(block.position() + marker.length, QTextCursor::KeepAnchor);
    edit.insertText(MarkdownList::checkboxMarker(!marker.checked));
    return true;
}

bool tryToggleCheckboxAtPosition(QTextCursor cursor)
{
    const QTextBlock block = cursor.block();
    const MarkdownList::Marker marker = MarkdownList::parse(block.text());
    if (marker.kind != MarkdownList::Kind::Checkbox)
        return false;

    // Only a click on the marker itself toggles — a click in the item's label
    // should just place the caret. The marker's "- [x] " includes a trailing
    // space that reads as label padding, so the hit region stops at the
    // closing bracket rather than covering the whole marker.
    const int offsetInBlock = cursor.position() - block.position();
    const int bracketEnd = block.text().indexOf(QLatin1Char(']')) + 1;
    if (offsetInBlock < 0 || bracketEnd <= 0 || offsetInBlock > bracketEnd)
        return false;

    return toggleCheckboxChecked(cursor);
}

bool continueHandRolledList(QTextCursor cursor)
{
    const QTextBlock block = cursor.block();
    const MarkdownList::Marker marker = MarkdownList::parse(block.text());
    if (!marker.isValid())
        return false;

    QString nextMarker;
    switch (marker.kind) {
    case MarkdownList::Kind::Bullet:
        nextMarker = MarkdownList::bulletMarker();
        break;
    case MarkdownList::Kind::Numbered:
        nextMarker = MarkdownList::numberedMarker(marker.number + 1);
        break;
    case MarkdownList::Kind::Checkbox:
        // A continued checkbox always starts unchecked — inheriting the
        // previous item's checked state would silently pre-tick a brand-new
        // to-do.
        nextMarker = MarkdownList::checkboxMarker(false);
        break;
    case MarkdownList::Kind::None:
        return false;
    }

    // Clamp the split to at least the end of this item's own marker. Pressing
    // Enter with the caret at or inside the marker ("1|. text") would otherwise
    // split the marker itself, leaving a fragment behind and duplicating the
    // rest — so the line is always split after the marker, which turns the
    // current item into an empty one and carries its text down to the new item.
    const int markerEnd = block.position() + marker.length;
    QTextCursor edit(cursor.document());
    edit.setPosition(qMax(cursor.position(), markerEnd));
    edit.beginEditBlock();
    edit.insertBlock();
    edit.insertText(nextMarker);
    edit.endEditBlock();

    if (marker.kind == MarkdownList::Kind::Numbered)
        renumberRunAt(cursor.document(), edit.block().blockNumber());
    return true;
}

void exitHandRolledList(QTextCursor cursor)
{
    QTextDocument *document = cursor.document();
    if (!document)
        return;

    const QTextBlock block = cursor.block();
    const MarkdownList::Marker marker = MarkdownList::parse(block.text());
    if (!marker.isValid())
        return;

    const int blockNumber = block.blockNumber();
    QTextCursor edit(document);
    edit.beginEditBlock();
    replaceMarker(edit, block, QString());
    edit.endEditBlock();

    // The run this item used to belong to now stops short of it, so whatever
    // is left below has to start counting again from its own first item.
    if (marker.kind == MarkdownList::Kind::Numbered && blockNumber + 1 < document->blockCount())
        renumberRunAt(document, blockNumber + 1);
}

bool isHandRolledListItemEmpty(QTextCursor cursor)
{
    const QString text = cursor.block().text();
    const MarkdownList::Marker marker = MarkdownList::parse(text);
    return marker.isValid() && text.mid(marker.length).trimmed().isEmpty();
}

void maintainHandRolledListNumbering(QTextCursor cursor)
{
    renumberRunAt(cursor.document(), cursor.block().blockNumber());
}

void setHeadingLevel(QTextCursor cursor, int level)
{
    QTextDocument *document = cursor.document();
    if (!document)
        return;

    // A heading is now just a line that starts with "# " — no block format, no
    // char format, no size/weight to apply. That single fact deletes the whole
    // machinery this function used to need: setting HeadingLevel had no visual
    // effect on its own, so it also had to merge a bold/FontSizeAdjustment char
    // format over the selection, then re-merge it as a *block* char format so an
    // empty line wouldn't keep a stale one, and maintainHeadingCharFormatAfterMerge
    // existed purely to repair the desync when two blocks merged. The
    // highlighter re-derives all of it from the "#" characters instead.
    const QList<QTextBlock> blocks = selectedBlocks(cursor);

    QTextCursor edit(document);
    edit.beginEditBlock();
    // Bottom-up: a prefix edit shifts the positions of every block after it,
    // but never of those before it.
    for (int i = blocks.size() - 1; i >= 0; --i) {
        const QTextBlock block = blocks.at(i);
        const QString text = block.text();
        const int existing = leadingHashCount(text);

        // Replace whatever prefix is already there (hashes plus the space after
        // them) so re-heading an existing heading swaps its level rather than
        // stacking a second "#" run onto it.
        int prefixLength = 0;
        if (existing > 0) {
            prefixLength = existing;
            while (prefixLength < text.size() && text.at(prefixLength).isSpace())
                ++prefixLength;
        }

        const QString replacement =
            level > 0 ? QString(level, QLatin1Char('#')) + QLatin1Char(' ') : QString();

        edit.setPosition(block.position());
        edit.setPosition(block.position() + prefixLength, QTextCursor::KeepAnchor);
        if (edit.selectedText() == replacement) {
            edit.clearSelection();
            continue;
        }
        edit.insertText(replacement); // replaces the selection
    }
    edit.endEditBlock();
}

void insertTabSpaces(QTextCursor cursor)
{
    // A plain text insert now. The old comment here explained that TextEdit's
    // own insert() had to be avoided because MarkdownText mode renormalized
    // whitespace and silently dropped a pure-whitespace insertion — with no
    // live markdown reparsing left, there is nothing to drop it.
    cursor.insertText(QStringLiteral("    "));
}

void insertTable(QTextCursor cursor)
{
    QTextDocument *document = cursor.document();
    if (!document)
        return;

    // Placeholder header text, kept from the pre-rewrite implementation: an
    // all-empty header row reads as a broken table, and these give the user
    // something to select and type over. (Their old structural purpose — being
    // the only thing that marked row 0 as a header across a round-trip — is
    // gone: the "---" delimiter row says so outright now.)
    const QString table =
        MarkdownTable::formatRow({QStringLiteral("Header 1"), QStringLiteral("Header 2")})
        + QLatin1Char('\n') + MarkdownTable::delimiterRow(2) + QLatin1Char('\n')
        + MarkdownTable::formatRow({QString(), QString()});

    QTextCursor edit(document);
    edit.setPosition(cursor.position());
    edit.beginEditBlock();
    // Reuse the current line if it's already blank — the common case is the
    // user pressing Enter for a fresh line and then inserting a table, and
    // leaving that now-empty line above the table reads as a stray blank
    // paragraph.
    if (!edit.block().text().isEmpty()) {
        edit.movePosition(QTextCursor::EndOfBlock);
        edit.insertBlock();
    }
    edit.insertText(table);
    edit.endEditBlock();
}

void insertTableRow(QTextCursor cursor)
{
    const MarkdownTable::Region region = regionFor(cursor);
    if (!region.isValid())
        return;

    QStringList cells;
    for (int i = 0; i < region.columnCount; ++i)
        cells.append(QString());

    // A new row always lands below a *content* row. With the caret on the
    // header or the delimiter, that means directly under the delimiter — a row
    // inserted between the header and the delimiter would break the table.
    int afterBlock = cursor.block().blockNumber();
    if (afterBlock <= region.firstBlockNumber + 1)
        afterBlock = region.firstBlockNumber + 1;

    const QTextBlock block = cursor.document()->findBlockByNumber(afterBlock);
    QTextCursor edit(cursor.document());
    edit.setPosition(block.position() + block.text().size());
    edit.beginEditBlock();
    edit.insertBlock();
    edit.insertText(MarkdownTable::formatRow(cells));
    edit.endEditBlock();
}

void deleteTableRow(QTextCursor cursor)
{
    QTextDocument *document = cursor.document();
    const MarkdownTable::Region region = regionFor(cursor);
    if (!region.isValid())
        return;

    // A table that is only a header (plus its delimiter) has no row left to
    // give: removing that last one removes the table, mirroring the old
    // QTextTable behaviour where a table could never be left with zero rows.
    if (region.bodyRowCount() == 0) {
        removeTableRegion(document, region);
        return;
    }

    const int blockNumber = cursor.block().blockNumber();
    if (blockNumber <= region.firstBlockNumber + 1) {
        // Deleting the header: promote the first body row into it, rather than
        // removing the header line itself — that would leave the delimiter as
        // line 1 and stop the run parsing as a table at all.
        const QTextBlock header = document->findBlockByNumber(region.firstBlockNumber);
        const QTextBlock firstBody = document->findBlockByNumber(region.firstBlockNumber + 2);

        QTextCursor edit(document);
        edit.beginEditBlock();
        // Bottom-up again: replacing the header first would shift the body
        // row's position out from under this cursor.
        edit.setPosition(firstBody.position());
        edit.setPosition(firstBody.position() + firstBody.length(), QTextCursor::KeepAnchor);
        const QString promoted = firstBody.text();
        edit.removeSelectedText();
        edit.setPosition(header.position());
        edit.setPosition(header.position() + header.text().size(), QTextCursor::KeepAnchor);
        edit.insertText(promoted);
        edit.endEditBlock();
        return;
    }

    const QTextBlock block = document->findBlockByNumber(blockNumber);
    QTextCursor edit(document);
    edit.setPosition(block.position());
    edit.setPosition(block.position() + block.length(), QTextCursor::KeepAnchor);
    edit.removeSelectedText();
}

void insertTableColumn(QTextCursor cursor)
{
    const MarkdownTable::Region region = regionFor(cursor);
    if (!region.isValid())
        return;

    const int column = columnFor(cursor);
    const int insertAt = qBound(0, column + 1, region.columnCount);
    rewriteTableRows(cursor.document(), region,
                     [insertAt](QStringList cells, bool isDelimiter) {
                         cells.insert(qMin(insertAt, cells.size()),
                                      isDelimiter ? QStringLiteral("---") : QString());
                         return cells;
                     });
}

void deleteTableColumn(QTextCursor cursor)
{
    QTextDocument *document = cursor.document();
    const MarkdownTable::Region region = regionFor(cursor);
    if (!region.isValid())
        return;

    // Same reasoning as deleteTableRow's last row: a table can't be left with
    // zero columns, so removing the only one removes the table.
    if (region.columnCount <= 1) {
        removeTableRegion(document, region);
        return;
    }

    const int column = columnFor(cursor);
    if (column < 0)
        return;

    rewriteTableRows(document, region, [column](QStringList cells, bool) {
        if (column < cells.size())
            cells.removeAt(column);
        return cells;
    });
}

void deleteTable(QTextCursor cursor)
{
    const MarkdownTable::Region region = regionFor(cursor);
    if (!region.isValid())
        return;
    removeTableRegion(cursor.document(), region);
}

bool isBold(QTextCursor cursor)
{
    // Either GFM spelling counts — the toggle only ever writes "**", but the
    // text may have been typed or pasted with "__".
    return isWrappedOutside(cursor, boldMarker()) || isWrappedInside(cursor, boldMarker())
        || isWrappedOutside(cursor, boldUnderscoreMarker())
        || isWrappedInside(cursor, boldUnderscoreMarker());
}

bool isItalic(QTextCursor cursor)
{
    // "**bold**" is wrapped in "*" as far as a naive scan can tell, since the
    // italic marker is a prefix of the bold one — so the doubled form has to be
    // excluded explicitly, or every bold run would light the italic button too.
    const bool asterisk = (isWrappedOutside(cursor, italicMarker())
                           || isWrappedInside(cursor, italicMarker()))
        && !isDoubledAround(cursor, italicMarker());
    const bool underscore = (isWrappedOutside(cursor, italicUnderscoreMarker())
                             || isWrappedInside(cursor, italicUnderscoreMarker()))
        && !isDoubledAround(cursor, italicUnderscoreMarker());
    return asterisk || underscore;
}

bool isStrikethrough(QTextCursor cursor)
{
    return isWrappedOutside(cursor, strikethroughMarker())
        || isWrappedInside(cursor, strikethroughMarker());
}

int headingLevel(QTextCursor cursor)
{
    // Heading-ness is now literally "how many '#' does this line start with",
    // which is also why it survives a block merge for free — there is no
    // separate structural state left to drift out of sync with the text.
    return leadingHashCount(cursor.block().text());
}

bool isBulletList(QTextCursor cursor)
{
    return MarkdownList::parse(cursor.block().text()).kind == MarkdownList::Kind::Bullet;
}

bool isNumberedList(QTextCursor cursor)
{
    return MarkdownList::parse(cursor.block().text()).kind == MarkdownList::Kind::Numbered;
}

bool isCheckboxList(QTextCursor cursor)
{
    return MarkdownList::parse(cursor.block().text()).kind == MarkdownList::Kind::Checkbox;
}

bool isCheckboxChecked(QTextCursor cursor)
{
    const MarkdownList::Marker marker = MarkdownList::parse(cursor.block().text());
    return marker.kind == MarkdownList::Kind::Checkbox && marker.checked;
}

bool isInTable(QTextCursor cursor)
{
    return regionFor(cursor).isValid();
}

QRectF checkboxMarkerRect(QTextCursor cursor)
{
    const QTextBlock block = cursor.block();
    const MarkdownList::Marker marker = MarkdownList::parse(block.text());
    if (marker.kind != MarkdownList::Kind::Checkbox)
        return QRectF();

    const int offsetInBlock = cursor.position() - block.position();
    if (offsetInBlock < 0 || offsetInBlock > marker.length)
        return QRectF();

    const QTextDocument *document = block.document();
    if (!document || !document->documentLayout())
        return QRectF();

    // blockBoundingRect() is what actually forces Qt to lay the block's
    // QTextLine(s) out in the first place — querying block.layout() first
    // (before this ever runs) sees a stale, not-yet-laid-out lineCount() of
    // 0 and returns early instead (confirmed empirically).
    const QRectF blockRect = document->documentLayout()->blockBoundingRect(block);
    QTextLayout *layout = block.layout();
    if (!layout || layout->lineCount() == 0)
        return QRectF();

    // The marker is always on the block's own first visual line — a checkbox
    // item's marker never wraps, however long its label text runs on
    // afterward. The rect covers "- [x]" but not its trailing space, matching
    // the hit region tryToggleCheckboxAtPosition accepts.
    const int bracketEnd = block.text().indexOf(QLatin1Char(']')) + 1;
    const QTextLine line = layout->lineAt(0);
    const qreal startX = line.cursorToX(0);
    const qreal endX = line.cursorToX(bracketEnd);
    return QRectF(blockRect.left() + qMin(startX, endX), blockRect.top() + line.y(),
                  qAbs(endX - startX), line.height());
}

}
