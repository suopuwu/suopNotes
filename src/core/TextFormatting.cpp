#include "TextFormatting.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QRegularExpression>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>
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
// real, selectable text — a genuine checkbox glyph (☐/☑, feature request)
// rather than literal "- [ ] "/"- [x] " GFM task-list syntax, which read as
// raw markdown rather than an actual checkbox. Unlike the numbered marker
// below, this is NOT valid markdown verbatim, so it needs an explicit
// translation back to real GFM syntax on save — see
// MarkdownSerialization's own use of handRolledBlockMarkdownText — the
// inverse of convertNativeListsToHandRolled's own checkbox branch, which
// converts the other way on load.
const QString &uncheckedMarker()
{
    static const QString marker = QStringLiteral("☐ "); // "☐ "
    return marker;
}
const QString &checkedMarker()
{
    static const QString marker = QStringLiteral("☑ "); // "☑ "
    return marker;
}
constexpr int checkboxMarkerLength = 2; // both markers above are the same length

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
    return text.startsWith(checkedMarker());
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

// The raw, unvalidated property alone — unlike handRolledKind(), does NOT
// require the block to actually still carry valid marker text. Meant only
// for detecting the specific staleness maintainHandRolledListNumbering
// cleans up: a block that WAS a genuine hand-rolled item (this property set)
// but no longer qualifies as one (handRolledKind() now reports None), whose
// leftover hanging-indent format needs clearing.
HandRolledKind rawHandRolledKind(const QTextBlock &block)
{
    const QVariant value = block.blockFormat().property(HandRolledKindProperty);
    return value.isValid() ? static_cast<HandRolledKind>(value.toInt()) : HandRolledKind::None;
}

// The bold/size-adjustment char format a block at the given heading level
// should have — shared by setHeadingLevel (which applies it explicitly) and
// maintainHeadingCharFormatAfterMerge (which only re-applies it where a
// block-merge left it stale — see that function's own comment). See
// setHeadingLevel's own comment for why FontSizeAdjustment (not an absolute
// point size) is what encodes the size half of this.
QTextCharFormat headingCharFormat(int level)
{
    QTextCharFormat format;
    format.setFontWeight(level > 0 ? QFont::Bold : QFont::Normal);
    int adjustment = 0;
    if (level == 1)
        adjustment = 3;
    else if (level == 2)
        adjustment = 2;
    format.setProperty(QTextFormat::FontSizeAdjustment, adjustment);
    return format;
}

// Whether an existing char format already carries the same bold/size a
// heading's char format would apply — the only two properties either of
// them ever touches, so this is a complete equivalence check for their
// purposes (an unrelated property like italic is free to differ either way
// without counting as a mismatch here).
bool matchesHeadingCharFormat(const QTextCharFormat &format, const QTextCharFormat &expected)
{
    return (format.fontWeight() >= QFont::Bold) == (expected.fontWeight() >= QFont::Bold)
        && format.property(QTextFormat::FontSizeAdjustment).toInt()
        == expected.property(QTextFormat::FontSizeAdjustment).toInt();
}

// Whether every fragment inside [rangeStart, rangeEnd) of `block` already
// matches `expected` — the no-op fast path maintainHeadingCharFormatAfterMerge
// checks before actually merging anything, so an ordinary merge of two
// same-level blocks (the overwhelmingly common case: joining two plain
// paragraphs with Backspace) never touches the undo stack at all.
bool rangeMatchesHeadingCharFormat(const QTextBlock &block, int rangeStart, int rangeEnd, const QTextCharFormat &expected)
{
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (fragment.position() + fragment.length() <= rangeStart)
            continue;
        if (fragment.position() >= rangeEnd)
            break;
        if (!matchesHeadingCharFormat(fragment.charFormat(), expected))
            return false;
    }
    return true;
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

// Walks outward from an already-valid `kind` block in one direction,
// extending over every immediately-adjacent block that's EITHER already
// properly tagged as `kind` (the ordinary case) OR still looks like a
// genuine `kind` marker in its own literal TEXT despite having lost its
// HandRolledKindProperty tag entirely — e.g. Qt's own multi-block
// paste/merge machinery synthesizing a fresh block format that doesn't carry
// custom properties over (reported live: several consecutive items in a
// pasted-over list "losing their special properties" at once, each only
// reflowing again once individually re-edited). Retags each such block as it
// reclaims it, so a single edit anywhere in a still-intact part of the run
// heals every adjacent lost item in one pass — including ones separated from
// the edit by other, already-valid items — not just the block the cursor
// happens to be sitting in. The two cases are interleaved in the SAME walk
// (not a separate pass over each) so a healed block never blocks the walk
// from reaching a still-valid block further out. Deliberately only ever
// extends from an already-recognized block — never bare isolated text
// elsewhere in the document — so an ordinary paragraph that just happens to
// start with "3. " on its own is never mistaken for a list item.
//
// `healedCount`, if non-null, is incremented once per block healed — the
// BACKWARD walk's caller needs this to shift its own startNumber back (see
// renumberRun), since every healed predecessor slots in BEFORE whatever
// number was computed from the pre-healing block, and would otherwise
// collide with the numbers already assigned to the run it just joined.
QTextBlock walkRunHealing(QTextBlock block, HandRolledKind kind, bool forward, int *healedCount)
{
    for (;;) {
        const QTextBlock candidate = forward ? block.next() : block.previous();
        if (!candidate.isValid())
            break;
        if (handRolledKind(candidate) == kind) {
            block = candidate;
            continue;
        }
        if (rawHandRolledKind(candidate) == HandRolledKind::None && markerLength(kind, candidate.text()) > 0) {
            setHandRolledKind(QTextCursor(candidate), kind);
            if (healedCount)
                ++(*healedCount);
            block = candidate;
            continue;
        }
        break;
    }
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

    int healedBefore = 0;
    const QTextBlock first = walkRunHealing(anyBlockInRun, kind, /*forward=*/false, &healedBefore);
    const QTextBlock last = walkRunHealing(anyBlockInRun, kind, /*forward=*/true, nullptr);
    // Healed predecessors slot in BEFORE whatever number the caller already
    // decided `anyBlockInRun` (pre-healing) should start at — see
    // walkRunHealing's own comment for why the caller's startNumber has to
    // shift back to make room for them, rather than colliding with it.
    startNumber -= healedBefore;

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

        // Bugfix: a narrower marker (e.g. "9. ", 3 characters) than the
        // run's widest (e.g. "10. ", 4 characters) used to hang at the SAME
        // textIndent as every other item — meaning its own first line of
        // TEXT (right after its own, narrower marker) landed measurably
        // further left than every wrapped continuation line and every other
        // item's own first line, all of which sit at the run's widest
        // width (confirmed empirically — a real misalignment, not
        // cosmetic). leftMargin stays constant across the whole run (it's
        // what keeps wrapped continuation lines lined up with each other
        // regardless of which item they belong to), but textIndent is now
        // sized to THIS block's own marker width, not the run's shared one:
        // that pulls a narrower marker's first line further left by exactly
        // the difference, reserving the same blank space to ITS left that
        // the wider marker already occupies, so the actual text after every
        // marker — first line or wrapped — starts at the same spot.
        const qreal ownMarkerWidth = kind == HandRolledKind::Numbered
            ? metrics.horizontalAdvance(block.text().left(markerLength(kind, block.text())))
            : markerWidth;

        // Bugfix: this used to merge unconditionally, even when the block's
        // margin/indent already matched — renumberRun reruns reactively on
        // EVERY keystroke typed anywhere inside a numbered list (see
        // DocumentHandler::maintainListNumbering, called from onTextChanged
        // in NoteWindow.qml), so a bare majority of those calls have
        // nothing to actually fix. Each still-unconditional merge became
        // its own undo-stack entry, so simply typing inside a list item
        // flooded the undo history with a wall of no-op format merges —
        // exactly what made Ctrl+Z feel like it was "cycling" instead of
        // stepping back through real edits. Skipping the merge when
        // nothing would change makes reactive maintenance passes a true
        // no-op — nothing to undo — rather than an invisible-but-real edit.
        const QTextBlockFormat existingFormat = block.blockFormat();
        if (existingFormat.leftMargin() != markerWidth || existingFormat.textIndent() != -ownMarkerWidth) {
            QTextBlockFormat format = existingFormat;
            format.setLeftMargin(markerWidth);
            format.setTextIndent(-ownMarkerWidth);
            QTextCursor formatCursor(block);
            formatCursor.mergeBlockFormat(format);
        }

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

// --- Inline emphasis, as literal markdown markers -----------------------
//
// Emphasis used to be a QTextCharFormat merge over the selection. Under the
// plain-text buffer there is no character formatting to carry it: bold IS the
// two asterisks, and the highlighter re-derives the look from them. So every
// toggle below is a text edit, and every query below is a text scan.

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

void removeMarkersOutside(QTextCursor cursor, const QString &marker)
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
}

void removeMarkersInside(QTextCursor cursor, const QString &marker)
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
}

void toggleInlineMarker(QTextCursor cursor, const QString &marker)
{
    if (!cursor.document())
        return;

    if (isWrappedOutside(cursor, marker)) {
        removeMarkersOutside(cursor, marker);
        return;
    }
    if (isWrappedInside(cursor, marker)) {
        removeMarkersInside(cursor, marker);
        return;
    }

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
}

}

void toggleBold(QTextCursor cursor)
{
    toggleInlineMarker(cursor, boldMarker());
}

void toggleItalic(QTextCursor cursor)
{
    toggleInlineMarker(cursor, italicMarker());
}

void toggleStrikethrough(QTextCursor cursor)
{
    toggleInlineMarker(cursor, strikethroughMarker());
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
    const QTextBlock originalBlock = cursor.block();
    const HandRolledKind kind = handRolledKind(originalBlock);
    if (kind == HandRolledKind::None)
        return false;

    // Pressing Enter AT OR BEFORE the marker itself (not just after it, in
    // the item's real text) must not split the marker away from its own
    // item — insertBlock() below splits at the cursor's literal position,
    // so with no clamp, a cursor sitting inside/before the marker would
    // carry that whole marker into the new block, which then got ANOTHER
    // fresh one inserted in front of it (confirmed empirically: pressing
    // Enter at the very start of "1. some text" produced "0. 1. some
    // text" — the "1. " travelled into the new block whole, then this
    // function inserted "0. " ahead of it, none the wiser). Clamping the
    // effective split point to just past the marker makes Enter anywhere
    // at or before it behave the same as pressing it right after —
    // inserting a new blank item, leaving the original text untouched.
    const int markerEnd = originalBlock.position() + markerLength(kind, originalBlock.text());
    if (cursor.position() < markerEnd)
        cursor.setPosition(markerEnd);

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

void maintainHandRolledListNumbering(QTextCursor cursor)
{
    const QTextBlock block = cursor.block();
    const HandRolledKind kind = handRolledKind(block);

    if (kind == HandRolledKind::None) {
        // The block's HandRolledKindProperty tag can outlive its own marker
        // text through perfectly ordinary editing that never goes through
        // detachFromHandRolledList/exitHandRolledList at all — e.g. clicking
        // at the very start of a list item and typing plain prose there,
        // which pushes the marker away from position 0 and breaks
        // numberedMarkerPattern's own "^" requirement (handRolledKind()
        // above already accounts for this). Left alone, though, the block's
        // hanging-indent format (leftMargin/textIndent, sized for the
        // marker that's no longer actually there) survives right along with
        // the stale tag, silently indenting every WRAPPED continuation line
        // of what is now just an ordinary paragraph, forever (confirmed
        // empirically — a real bug, reported live). Only the first line
        // looks fine, since textIndent cancels leftMargin there.
        if (rawHandRolledKind(block) != HandRolledKind::None) {
            QTextBlockFormat format = block.blockFormat();
            format.setProperty(HandRolledKindProperty, static_cast<int>(HandRolledKind::None));
            format.setLeftMargin(0);
            format.setTextIndent(0);
            cursor.mergeBlockFormat(format);
        }
        return;
    }

    if (kind != HandRolledKind::Numbered)
        return; // checkbox markers never need renumbering, only their (constant) shared width
    renumberRun(block, kind, runStartNumber(block, kind));
}

void maintainHeadingCharFormatAfterMerge(QTextCursor cursor)
{
    const QTextBlock block = cursor.block();
    const int level = block.blockFormat().headingLevel();
    const QTextCharFormat expected = headingCharFormat(level);

    const int blockEnd = block.position() + block.length() - 1; // -1: exclude the block separator itself
    const int rangeStart = cursor.position();

    // The merge point sits at (or past) the block's own end — nothing of
    // either side's text actually landed in this block after `rangeStart`
    // (e.g. an empty line got backspaced away entirely). The block itself
    // can still be a genuinely empty, freshly-surviving heading/paragraph
    // whose own (invisible until typed-into) blockCharFormat() is stale —
    // same bug, same fix, as setHeadingLevel's own empty-block bugfix above.
    if (rangeStart >= blockEnd) {
        if (block.length() <= 1 && !matchesHeadingCharFormat(block.charFormat(), expected))
            QTextCursor(block).mergeBlockCharFormat(expected);
        return;
    }

    // No-op fast path (see rangeMatchesHeadingCharFormat's own comment):
    // merging two blocks that already agree on heading level — by far the
    // most common case, e.g. joining two plain paragraphs with Backspace —
    // needs nothing fixed and must not create an undo-stack entry for doing
    // nothing.
    if (rangeMatchesHeadingCharFormat(block, rangeStart, blockEnd, expected))
        return;

    QTextCursor formatCursor(cursor.document());
    formatCursor.setPosition(rangeStart);
    formatCursor.setPosition(blockEnd, QTextCursor::KeepAnchor);
    formatCursor.mergeCharFormat(expected);
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
    // format over the selection, then re-merge it as a *block* char format so
    // an empty line wouldn't keep a stale one, and maintainHeadingCharFormatAfterMerge
    // existed purely to repair the desync when two blocks merged. The
    // highlighter re-derives all of it from the "#" characters instead.
    const int rangeStart = cursor.selectionStart();
    const int rangeEnd = cursor.selectionEnd();
    const QTextBlock firstBlock = document->findBlock(rangeStart);
    // rangeEnd - 1 when there's a real selection: a selection ending exactly at
    // the start of the next block (i.e. it only swallowed the previous block's
    // trailing newline) shouldn't pull that next, unselected block in too.
    const QTextBlock lastBlock =
        document->findBlock(rangeEnd > rangeStart ? rangeEnd - 1 : rangeEnd);

    QTextCursor edit(document);
    edit.beginEditBlock();
    // Bottom-up: rewriting a block's prefix shifts the positions of every block
    // after it, but never of those before it, so walking backwards keeps each
    // remaining block's cached position valid without re-finding it.
    QList<QTextBlock> blocks;
    for (QTextBlock b = firstBlock; b.isValid(); b = b.next()) {
        blocks.append(b);
        if (b.blockNumber() == lastBlock.blockNumber())
            break;
    }
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
        if (edit.selectedText() == replacement)
            continue;
        edit.insertText(replacement); // replaces the selection
    }
    edit.endEditBlock();
}

void insertTabSpaces(QTextCursor cursor)
{
    cursor.insertText(QStringLiteral("    "));
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

QString handRolledBlockMarkdownText(const QTextBlock &block)
{
    if (handRolledKind(block) == HandRolledKind::Checkbox) {
        const QString text = block.text();
        const bool checked = isCheckedMarkerText(text);
        return (checked ? QStringLiteral("- [x] ") : QStringLiteral("- [ ] "))
            + text.mid(checkboxMarkerLength);
    }
    return block.text();
}

QRectF checkboxMarkerRect(QTextCursor cursor)
{
    const QTextBlock block = cursor.block();
    if (handRolledKind(block) != HandRolledKind::Checkbox)
        return QRectF();
    const int offsetInBlock = cursor.position() - block.position();
    if (offsetInBlock < 0 || offsetInBlock > checkboxMarkerLength)
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

    // The marker is always on the block's own first visual line — a
    // checkbox item's marker never wraps, however long its label text
    // runs on afterward.
    const QTextLine line = layout->lineAt(0);
    const qreal startX = line.cursorToX(0);
    const qreal endX = line.cursorToX(checkboxMarkerLength);
    return QRectF(blockRect.left() + qMin(startX, endX), blockRect.top() + line.y(),
                  qAbs(endX - startX), line.height());
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
            // list->remove() detaches list MEMBERSHIP but does not reset
            // the block's own indent() LEVEL (a separate, additive property
            // — Qt sets indent()==1 on every real list item it parses) —
            // left alone, that stacked on top of the leftMargin/textIndent
            // renumberRun sets below, producing extra unwanted left margin
            // on every note that had been through a save/reload cycle
            // (confirmed empirically; toggleBulletList's own bullet-removal
            // path already clears this for the same reason).
            {
                QTextBlockFormat format = block.blockFormat();
                format.setIndent(0);
                QTextCursor(block).mergeBlockFormat(format);
            }

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
            // membership (see toggleNumberedList's own comment for why), a
            // marker has to be restored by hand here, same as the numbered
            // case restores "N. " — but a real "☐"/"☑" checkbox glyph
            // (feature request) rather than literal "- [ ] "/"- [x] "
            // syntax; handRolledBlockMarkdownText translates it back to
            // that syntax on save.
            const bool checked = marker == QTextBlockFormat::MarkerType::Checked;
            list->remove(block);
            {
                QTextBlockFormat format = block.blockFormat();
                format.setIndent(0);
                QTextCursor(block).mergeBlockFormat(format);
            }
            setHandRolledKind(QTextCursor(block), HandRolledKind::Checkbox);
            QTextCursor insertCursor(block);
            insertCursor.setPosition(block.position());
            insertCursor.insertText(checked ? checkedMarker() : uncheckedMarker());
            renumberRun(block, HandRolledKind::Checkbox, 1);
        }
    }
}

}
