#include "TextFormatting.h"

#include <QFontMetricsF>
#include <QRegularExpression>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
#include <QTextTableFormat>

namespace TextFormatting
{

namespace
{

// Qt's default list indent (QTextListFormat::indent() == 1, times
// QTextDocument::indentWidth()'s default of 40px) pushes the marker itself
// ~40px right of the paragraph's own left edge — an "indentation before the
// number" nobody asked for. Sizing indentWidth to just fit a two-digit
// marker instead keeps the marker flush against the text's natural left
// edge while leaving Qt's own hanging-indent mechanism intact, so wrapped
// continuation lines still line up under the text after the marker rather
// than collapsing flush left with it (confirmed empirically: indent level 0
// clips the marker entirely, so a small nonzero indentWidth — not indent 0
// — is what "no indentation before the number" actually needs). Bullet
// lists are the only remaining QTextList-based kind (see toggleNumberedList
// below for why numbered/checkbox lists no longer are), so this only ever
// runs for them now.
void tightenListIndent(const QTextCursor &cursor)
{
    if (QTextDocument *document = cursor.document()) {
        const QFontMetricsF metrics(cursor.charFormat().font());
        document->setIndentWidth(metrics.horizontalAdvance(QStringLiteral("99.")) + 4);
    }
}

// Numbered and checkbox lists don't use QTextList at all — see
// toggleNumberedList's own header comment (TextFormatting.h) for why. A
// block's membership is tracked instead via this custom QTextBlockFormat
// property; "the list" for any given block is the maximal run of adjacent
// blocks sharing the same one, mirroring how QTextList's own
// cursor.currentList() concept works via block adjacency in the same list
// object.
enum class HandRolledKind { None = 0, Numbered = 1, Checkbox = 2 };
constexpr int HandRolledKindProperty = QTextFormat::UserProperty + 1;

// The two literal marker prefixes hand-rolled checkbox blocks carry as
// real, selectable text — chosen to already BE valid GFM task-list syntax
// verbatim ("- [ ] "/"- [x] "), so saving one needs no special-case
// translation at all (see MarkdownSerialization's own use of
// convertNativeListsToHandRolled for the other direction, loading).
const QString &uncheckedMarker()
{
    static const QString marker = QStringLiteral("- [ ] ");
    return marker;
}
const QString &checkedMarker()
{
    static const QString marker = QStringLiteral("- [x] ");
    return marker;
}
constexpr int checkboxMarkerLength = 6; // both markers above are the same length

// QTextCursor::mergeBlockFormat() merges: a property ABSENT from the
// argument format is left completely untouched on the destination, so
// "clearing" a property via QTextBlockFormat::clearProperty() then merging
// that (now-absent-again) format is a no-op — it never actually removes
// anything already set. Explicitly setting the property to None's own
// value (0) instead, which the merge DOES carry over, is what actually
// clears it; handRolledKind()'s HandRolledKind::None case is written to
// treat an explicit 0 exactly the same as a wholly absent property, so
// this is safe either way a fresh block arrives.
void setHandRolledKind(QTextCursor cursor, HandRolledKind kind)
{
    QTextBlockFormat format = cursor.blockFormat();
    format.setProperty(HandRolledKindProperty, static_cast<int>(kind));
    cursor.mergeBlockFormat(format);
}

const QRegularExpression &numberedMarkerPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d+)\\. "));
    return pattern;
}

bool isCheckedMarkerText(const QString &text)
{
    return text.startsWith(checkedMarker()) || text.startsWith(QLatin1String("- [X] "));
}

bool isHandRolledCheckboxText(const QString &text)
{
    return text.startsWith(uncheckedMarker()) || isCheckedMarkerText(text);
}

// Length of the marker prefix a block of the given kind actually has right
// now (0 if it doesn't start with one at all).
int markerLength(HandRolledKind kind, const QString &text)
{
    if (kind == HandRolledKind::Numbered) {
        const auto match = numberedMarkerPattern().match(text);
        return match.hasMatch() ? match.capturedLength(0) : 0;
    }
    if (kind == HandRolledKind::Checkbox)
        return isHandRolledCheckboxText(text) ? checkboxMarkerLength : 0;
    return 0;
}

HandRolledKind handRolledKind(const QTextBlock &block)
{
    const QVariant value = block.blockFormat().property(HandRolledKindProperty);
    if (!value.isValid())
        return HandRolledKind::None;
    const auto kind = static_cast<HandRolledKind>(value.toInt());
    if (kind == HandRolledKind::None)
        return HandRolledKind::None;
    // The property alone isn't authoritative: QTextCursor::insertBlock()
    // copies the CURRENT block's format — this property included — onto
    // any newly split block, even one that was never actually given real
    // marker text (e.g. a plain new paragraph split off from a list item
    // by some code path other than continueHandRolledList/exitHandRolledList,
    // which are the only two places that manage this property AND the
    // literal marker text together). Requiring the marker to actually be
    // there too is what stops such a block from being silently absorbed
    // into the list run it happens to sit next to (confirmed empirically:
    // without this check, splitting a block right after a list item
    // silently drags the new, unrelated block into the same list).
    return markerLength(kind, block.text()) > 0 ? kind : HandRolledKind::None;
}

QTextBlock runStart(QTextBlock block, HandRolledKind kind)
{
    while (block.previous().isValid() && handRolledKind(block.previous()) == kind)
        block = block.previous();
    return block;
}

QTextBlock runEnd(QTextBlock block, HandRolledKind kind)
{
    while (block.next().isValid() && handRolledKind(block.next()) == kind)
        block = block.next();
    return block;
}

// The number a numbered-list run currently starts at, read from its own
// first item's marker (defaulting to 1 if that's ever somehow missing).
// Irrelevant for checkbox lists (always 1, unused).
int runStartNumber(const QTextBlock &block, HandRolledKind kind)
{
    if (kind != HandRolledKind::Numbered)
        return 1;
    const QTextBlock first = runStart(block, kind);
    const auto match = numberedMarkerPattern().match(first.text());
    return match.hasMatch() ? match.captured(1).toInt() : 1;
}

// Re-derives every item's marker text (numbered lists only — checkbox
// markers keep whatever checked state each item already has, only their
// shared hanging indent below is ever recomputed) and applies a single
// hanging-indent width, sized to the run's own widest marker (never a
// fixed or document-wide amount — see toggleNumberedList's header comment),
// to every block in the run.
void renumberRun(const QTextBlock &anyBlockInRun, HandRolledKind kind, int startNumber)
{
    const QTextDocument *document = anyBlockInRun.document();
    if (!document || kind == HandRolledKind::None)
        return;

    const QTextBlock first = runStart(anyBlockInRun, kind);
    const QTextBlock last = runEnd(anyBlockInRun, kind);

    int itemCount = 0;
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        ++itemCount;
        if (b == last)
            break;
    }

    const QString widestMarker = kind == HandRolledKind::Numbered
        ? QString::number(startNumber + itemCount - 1) + QStringLiteral(". ")
        : uncheckedMarker();
    const QFontMetricsF metrics(anyBlockInRun.charFormat().font());
    const qreal markerWidth = metrics.horizontalAdvance(widestMarker);

    int number = startNumber;
    QTextBlock block = first;
    while (block.isValid()) {
        const QTextBlock next = (block == last) ? QTextBlock() : block.next();

        if (kind == HandRolledKind::Numbered) {
            const QString text = block.text();
            const QString newMarker = QString::number(number) + QStringLiteral(". ");
            ++number;
            const int oldLength = markerLength(kind, text);
            if (text.left(oldLength) != newMarker) {
                QTextCursor editCursor(block);
                editCursor.setPosition(block.position());
                editCursor.setPosition(block.position() + oldLength, QTextCursor::KeepAnchor);
                editCursor.insertText(newMarker);
            }
        }

        QTextBlockFormat format = block.blockFormat();
        format.setLeftMargin(markerWidth);
        format.setTextIndent(-markerWidth);
        QTextCursor formatCursor(block);
        formatCursor.mergeBlockFormat(format);

        if (block == last)
            break;
        block = next;
    }
}

// Strips the current block's own hand-rolled marker and tag, reverting it
// to a plain paragraph, and renumbers whatever remains of the list on
// either side (the "before" remainder keeps its own original start number;
// the "after" remainder — only possible when removing from the middle of a
// list — restarts at 1, the same simplification a real QTextList would
// need if a mid-list block were ever detached from it independently).
void detachFromHandRolledList(QTextCursor cursor)
{
    const QTextBlock block = cursor.block();
    const HandRolledKind kind = handRolledKind(block);
    if (kind == HandRolledKind::None)
        return;

    const QTextBlock prev = block.previous();
    const QTextBlock next = block.next();
    const int length = markerLength(kind, block.text());

    QTextCursor stripCursor(block);
    stripCursor.setPosition(block.position());
    stripCursor.setPosition(block.position() + length, QTextCursor::KeepAnchor);
    stripCursor.removeSelectedText();

    QTextBlockFormat format = stripCursor.blockFormat();
    format.clearProperty(HandRolledKindProperty);
    format.setLeftMargin(0);
    format.setTextIndent(0);
    stripCursor.mergeBlockFormat(format);

    if (prev.isValid() && handRolledKind(prev) == kind)
        renumberRun(prev, kind, runStartNumber(prev, kind));
    if (next.isValid() && handRolledKind(next) == kind)
        renumberRun(next, kind, 1);
}

// Shared toggle logic for numbered and checkbox lists: off if the cursor is
// already on an item of the requested kind; converting in place (stripping
// whatever hand-rolled marker or QTextList membership is already there)
// otherwise. Joins an adjacent existing run of the same kind rather than
// always starting a fresh one, mirroring toggleBulletList's own
// convert-in-place behavior for a toolbar click mid-list.
void toggleHandRolled(QTextCursor cursor, HandRolledKind targetKind, int startNumber)
{
    const QTextBlock block = cursor.block();
    const HandRolledKind existing = handRolledKind(block);

    if (existing == targetKind) {
        detachFromHandRolledList(cursor);
        return;
    }

    if (existing != HandRolledKind::None) {
        detachFromHandRolledList(cursor);
    } else if (QTextList *list = cursor.currentList()) {
        QTextBlockFormat blockFormat = cursor.blockFormat();
        blockFormat.setIndent(0);
        cursor.mergeBlockFormat(blockFormat);
        list->remove(cursor.block());
    }

    int effectiveStart = startNumber;
    const QTextBlock prev = block.previous();
    if (targetKind == HandRolledKind::Numbered && prev.isValid() && handRolledKind(prev) == HandRolledKind::Numbered)
        effectiveStart = runStartNumber(prev, HandRolledKind::Numbered);

    // Tagging BEFORE inserting the marker text (not after) matters: this
    // insertText() below mutates the live document the QML editor is bound
    // to, which synchronously re-enters DocumentHandler::
    // tryAutoConvertListShorthand (TextArea.onTextChanged) before this very
    // call even returns (confirmed empirically — the nested call runs
    // fully inside this insertText() frame, not after it). If the tag
    // weren't already in place by then, that nested call would see
    // list-like text with no tag yet, fail its own already-a-list check,
    // and reprocess/re-strip/re-insert the very same marker all over again
    // — a genuine unbounded recursion (confirmed: a real stack overflow,
    // not just a cosmetic one-bounce reentry) rather than the single
    // harmless extra pass every other shorthand conversion relies on.
    setHandRolledKind(QTextCursor(block), targetKind);
    QTextCursor insertCursor(block);
    insertCursor.setPosition(block.position());
    insertCursor.insertText(targetKind == HandRolledKind::Numbered
        ? QString::number(effectiveStart) + QStringLiteral(". ")
        : uncheckedMarker());

    renumberRun(block, targetKind, effectiveStart);
}

}

void toggleBold(QTextCursor cursor)
{
    const bool isBold = cursor.charFormat().fontWeight() >= QFont::Bold;
    QTextCharFormat format;
    format.setFontWeight(isBold ? QFont::Normal : QFont::Bold);
    cursor.mergeCharFormat(format);
}

void toggleItalic(QTextCursor cursor)
{
    const bool isItalic = cursor.charFormat().fontItalic();
    QTextCharFormat format;
    format.setFontItalic(!isItalic);
    cursor.mergeCharFormat(format);
}

void toggleStrikethrough(QTextCursor cursor)
{
    const bool isStruck = cursor.charFormat().fontStrikeOut();
    QTextCharFormat format;
    format.setFontStrikeOut(!isStruck);
    cursor.mergeCharFormat(format);
}

void toggleBulletList(QTextCursor cursor)
{
    // Converting from a hand-rolled numbered/checkbox item: strip its
    // marker/tag first (renumbering any surviving neighbors), then fall
    // through to the ordinary QTextList path below.
    if (handRolledKind(cursor.block()) != HandRolledKind::None)
        detachFromHandRolledList(cursor);

    if (QTextList *list = cursor.currentList()) {
        // Only ListDisc ever reaches here now — numbered lists are
        // hand-rolled (see toggleNumberedList) and already handled above.
        QTextBlockFormat blockFormat = cursor.blockFormat();
        blockFormat.setIndent(0);
        cursor.mergeBlockFormat(blockFormat);
        list->remove(cursor.block());
    } else {
        tightenListIndent(cursor);
        QTextListFormat format;
        format.setStyle(QTextListFormat::ListDisc);
        format.setIndent(1);
        cursor.createList(format);
    }
}

void toggleNumberedList(QTextCursor cursor, int startNumber)
{
    toggleHandRolled(cursor, HandRolledKind::Numbered, startNumber);
}

void toggleCheckboxList(QTextCursor cursor)
{
    toggleHandRolled(cursor, HandRolledKind::Checkbox, 1);
}

bool toggleCheckboxChecked(QTextCursor cursor)
{
    const QTextBlock block = cursor.block();
    if (handRolledKind(block) != HandRolledKind::Checkbox)
        return false;

    const bool checked = isCheckedMarkerText(block.text());
    QTextCursor editCursor(block);
    editCursor.setPosition(block.position());
    editCursor.setPosition(block.position() + checkboxMarkerLength, QTextCursor::KeepAnchor);
    editCursor.insertText(checked ? uncheckedMarker() : checkedMarker());
    return true;
}

bool tryToggleCheckboxAtPosition(QTextCursor cursor)
{
    const QTextBlock block = cursor.block();
    if (handRolledKind(block) != HandRolledKind::Checkbox)
        return false;
    const int offsetInBlock = cursor.position() - block.position();
    if (offsetInBlock < 0 || offsetInBlock > checkboxMarkerLength)
        return false;
    return toggleCheckboxChecked(cursor);
}

bool continueHandRolledList(QTextCursor cursor)
{
    const HandRolledKind kind = handRolledKind(cursor.block());
    if (kind == HandRolledKind::None)
        return false;

    // insertBlock() with no arguments copies the CURRENT block's format
    // (including HandRolledKindProperty and the hanging-indent margins)
    // onto the new one, splitting text at the cursor same as any plain
    // Enter — but the marker itself is literal TEXT, not part of that
    // format, so the new block starts with none at all (empty if Enter was
    // pressed at the item's end, or "whatever came after the cursor" if
    // pressed mid-item) until inserted below.
    cursor.insertBlock();
    const QTextBlock newBlock = cursor.block();

    QTextCursor insertCursor(newBlock);
    insertCursor.setPosition(newBlock.position());
    insertCursor.insertText(kind == HandRolledKind::Numbered ? QStringLiteral("0. ") : uncheckedMarker());

    renumberRun(newBlock, kind, runStartNumber(newBlock, kind));
    return true;
}

void exitHandRolledList(QTextCursor cursor)
{
    detachFromHandRolledList(cursor);
}

bool isHandRolledListItemEmpty(QTextCursor cursor)
{
    const QTextBlock block = cursor.block();
    const HandRolledKind kind = handRolledKind(block);
    if (kind == HandRolledKind::None)
        return false;
    return block.text().length() == markerLength(kind, block.text());
}

void setHeadingLevel(QTextCursor cursor, int level)
{
    QTextBlockFormat blockFormat = cursor.blockFormat();
    blockFormat.setHeadingLevel(level);
    cursor.mergeBlockFormat(blockFormat);

    // Qt's default stylesheet renders heading blocks bold/larger based on
    // headingLevel alone; no extra char-format changes needed for display,
    // and toMarkdown() serializes headingLevel back to "#"..."######" runs.
}

void insertTable(QTextCursor cursor)
{
    // If the cursor is sitting on an already-empty line (the common case:
    // the user pressed Enter to get a fresh line, then inserted a table),
    // QTextCursor::insertTable() does NOT consume that empty block — it
    // inserts the table's frame right after it, leaving the empty line
    // behind as an orphan blank paragraph between the preceding text and
    // the table. Collapsing it first (merging back into the previous
    // block, exactly like the cursor would sit if the user had inserted
    // the table without pressing Enter at all) avoids that phantom gap;
    // inserting at the true end of a non-empty block never has this
    // problem, which is why only "text, Enter, then table" showed it.
    if (cursor.atBlockStart() && cursor.atBlockEnd())
        cursor.deletePreviousChar();

    // Qt's markdown writer always renders a QTextTable's first row as the
    // GFM header row (with the "---" delimiter line right after it) — there
    // is no separate "is this the header" flag to set, so placeholder text
    // in row 0 is what marks it as a header once round-tripped through
    // toMarkdown().
    QTextTableFormat format;
    format.setBorder(1);
    format.setCellPadding(4);
    QTextTable *table = cursor.insertTable(2, 2, format);
    table->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("Header 1"));
    table->cellAt(0, 1).firstCursorPosition().insertText(QStringLiteral("Header 2"));
}

void insertTableRow(QTextCursor cursor)
{
    QTextTable *table = cursor.currentTable();
    if (!table)
        return;
    const int row = table->cellAt(cursor).row();
    // insertRows(pos, ...) inserts BEFORE pos, so row + 1 lands the new row
    // directly after the one the cursor is in.
    table->insertRows(row + 1, 1);
}

void deleteTableRow(QTextCursor cursor)
{
    QTextTable *table = cursor.currentTable();
    if (!table)
        return;
    if (table->rows() <= 1) {
        deleteTable(cursor);
        return;
    }
    const int row = table->cellAt(cursor).row();
    table->removeRows(row, 1);
}

void insertTableColumn(QTextCursor cursor)
{
    QTextTable *table = cursor.currentTable();
    if (!table)
        return;
    const int column = table->cellAt(cursor).column();
    table->insertColumns(column + 1, 1);
}

void deleteTableColumn(QTextCursor cursor)
{
    QTextTable *table = cursor.currentTable();
    if (!table)
        return;
    if (table->columns() <= 1) {
        deleteTable(cursor);
        return;
    }
    const int column = table->cellAt(cursor).column();
    table->removeColumns(column, 1);
}

void deleteTable(QTextCursor cursor)
{
    QTextTable *table = cursor.currentTable();
    if (!table)
        return;

    // A QTextFrame (QTextTable's base) is delimited in the document's
    // underlying character stream by one hidden "frame start" character
    // right before firstPosition() and one "frame end" character right
    // after lastPosition() — selecting exactly that span and removing it
    // deletes the frame itself, not just the rows/cells inside it (which
    // is all removeRows()/removeColumns() alone would ever manage, since
    // QTextTable refuses to go below 1x1).
    QTextCursor removeCursor(table->document());
    removeCursor.setPosition(table->firstPosition() - 1);
    removeCursor.setPosition(table->lastPosition() + 1, QTextCursor::KeepAnchor);
    removeCursor.removeSelectedText();
}

bool isBold(QTextCursor cursor)
{
    return cursor.charFormat().fontWeight() >= QFont::Bold;
}

bool isItalic(QTextCursor cursor)
{
    return cursor.charFormat().fontItalic();
}

bool isStrikethrough(QTextCursor cursor)
{
    return cursor.charFormat().fontStrikeOut();
}

int headingLevel(QTextCursor cursor)
{
    return cursor.blockFormat().headingLevel();
}

bool isBulletList(QTextCursor cursor)
{
    QTextList *list = cursor.currentList();
    return list && list->format().style() == QTextListFormat::ListDisc;
}

bool isNumberedList(QTextCursor cursor)
{
    return handRolledKind(cursor.block()) == HandRolledKind::Numbered;
}

bool isCheckboxList(QTextCursor cursor)
{
    return handRolledKind(cursor.block()) == HandRolledKind::Checkbox;
}

bool isCheckboxChecked(QTextCursor cursor)
{
    const QTextBlock block = cursor.block();
    if (handRolledKind(block) != HandRolledKind::Checkbox)
        return false;
    return isCheckedMarkerText(block.text());
}

bool isInTable(QTextCursor cursor)
{
    return cursor.currentTable() != nullptr;
}

bool isHandRolledListBlock(const QTextBlock &block)
{
    return handRolledKind(block) != HandRolledKind::None;
}

void convertNativeListsToHandRolled(QTextDocument *document)
{
    if (!document)
        return;

    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        QTextList *list = block.textList();
        if (!list)
            continue;

        const QTextListFormat::Style style = list->format().style();
        const QTextBlockFormat::MarkerType marker = block.blockFormat().marker();
        if (style == QTextListFormat::ListDecimal) {
            // Qt's own parser fully consumes "N. " into the QTextList
            // structure — nothing of the marker survives in block.text() —
            // so the real displayed number has to be read back from the
            // list itself before detaching.
            const int number = list->format().start() + list->itemNumber(block);
            list->remove(block);

            // Tag before inserting the marker text — see toggleHandRolled's
            // own comment for why that order matters (harmless here, since
            // this runs on a throwaway scratch QTextDocument with no QML
            // editor bound to it to reenter, but kept consistent anyway).
            setHandRolledKind(QTextCursor(block), HandRolledKind::Numbered);
            QTextCursor insertCursor(block);
            insertCursor.setPosition(block.position());
            insertCursor.insertText(QString::number(number) + QStringLiteral(". "));
            renumberRun(block, HandRolledKind::Numbered, runStartNumber(block, HandRolledKind::Numbered));
        } else if (style == QTextListFormat::ListDisc && marker != QTextBlockFormat::MarkerType::NoMarker) {
            // Qt 6.4+ actually does have native GFM task-list *parsing*
            // support after all (contrary to this project's own earlier
            // research) — it fully consumes "[ ]"/"[x]" into this
            // QTextBlockFormat::marker() property, leaving nothing of it in
            // block.text() either, exactly like the numbered case above.
            // Its WRITER, however, only ever emits "- [ ] "/"- [x] " syntax
            // for a block that's STILL part of a real QTextList (confirmed
            // empirically: setting marker() alone, with no list, writes a
            // plain paragraph, the marker silently ignored) — since this
            // app's hand-rolled representation deliberately drops QTextList
            // membership (see toggleNumberedList's own comment for why),
            // the literal "- [ ] "/"- [x] " text still has to be restored
            // by hand here, same as the numbered case restores "N. ".
            const bool checked = marker == QTextBlockFormat::MarkerType::Checked;
            list->remove(block);
            setHandRolledKind(QTextCursor(block), HandRolledKind::Checkbox);
            QTextCursor insertCursor(block);
            insertCursor.setPosition(block.position());
            insertCursor.insertText(checked ? checkedMarker() : uncheckedMarker());
            renumberRun(block, HandRolledKind::Checkbox, 1);
        }
    }
}

}
