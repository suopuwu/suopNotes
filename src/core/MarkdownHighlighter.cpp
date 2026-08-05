#include "MarkdownHighlighter.h"

#include "MarkdownImage.h"
#include "MarkdownLink.h"
#include "MarkdownList.h"
#include "MarkdownTable.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>

namespace
{

// Markdown punctuation the user literally typed ("**", "#", "`") as it looks
// on the line the cursor is on: visible, but dimmed so the prose still reads
// as the foreground. A mid-gray is used rather than an alpha blend of the text
// color because notes can carry an arbitrary user-chosen background, and
// mid-gray stays legible on both light and dark ones.
QTextCharFormat revealedMarkerFormat()
{
    QTextCharFormat format;
    format.setForeground(QColor(0x88, 0x88, 0x88));
    return format;
}

// The zero-width hide: the same punctuation on every line the cursor is NOT
// on. Transparent so no glyph paints, and a hair-thin font so the characters
// claim no horizontal advance either — the text closes up over them exactly as
// if they had been parsed away, while the buffer still holds every byte the
// user typed.
//
// The near-zero point size IS the mechanism, and it was picked by measurement,
// not taste. Qt exposes no "hidden" char-format property, so the three
// plausible candidates were rendered side by side in the running app:
// a 0% PercentageSpacing letter spacing left each glyph's full advance behind
// (invisible, but the text still had holes in it), and a large negative
// AbsoluteSpacing collapsed the entire rest of the line on top of itself. Only
// shrinking the font actually removes the advance. It costs no line height
// because a line's height comes from its tallest glyph, and every character
// that is not a marker keeps its real size.
//
// Deliberately REPLACES rather than merges into the format underneath it (see
// applyMarkerFormat in highlightBlock): a heading's own FontSizeAdjustment
// covers its "#" characters too, and leaving that adjustment in place
// alongside a 0.01pt size would be two font-size properties fighting over the
// same characters. Invisible text needs no size but its own.
//
// NoFontMerging is not an optimization detail — without it this format froze
// the whole UI for ~5.8 SECONDS the first time it was ever applied (measured;
// it was the entire cause of a reported "adding the first checkbox list or
// table locks up the app" freeze, and of nothing else). 0.01pt rounds to a
// pixel size of 0, at which the font engine can produce no glyph for any
// character — so Qt concludes every character is *missing* and walks the whole
// fallback chain, loading every font family installed on the system looking for
// one that can render it. None can, at size 0. The result is then cached in
// QFontCache, which is why only the first checkbox/table in a session paid it
// and every one after was instant. NoFontMerging tells Qt not to go looking:
// this text is transparent AND zero-width, so there is no glyph to fall back
// *for*. Same 0.000 advance, 5.8s -> ~40ms. Keep this and setFontPointSize
// together; the size is what makes the fallback walk happen.
QTextCharFormat hiddenMarkerFormat()
{
    QTextCharFormat format;
    format.setForeground(QColor(0, 0, 0, 0));
    format.setFontPointSize(0.01);
    format.setFontStyleStrategy(QFont::NoFontMerging);
    return format;
}

// How wide each drawn control's reserved slot is, as a percentage of the
// reserved character's own natural advance, lives with the marker definition in
// MarkdownList (kBulletSlotPercent / kCheckboxSlotPercent, applied here via
// slotPercentForKind). Without widening at all, the column is only as wide as
// whatever "-" or "[" happens to measure in the user's font (~6px at the default
// size) — narrower than the control drawn into it, so the bullet/checkbox
// overflowed on both sides: off the editor's left edge, where it was visibly
// clipped in half, and rightwards into its own label, leaving no gap (both
// confirmed on screen). A checkbox gets the wider slot because it is DRAWN
// bigger than a bullet; that value is shared with NoteWindow.qml's checkboxScale
// (which draws the control) and TextFormatting (which reserves the matching
// hanging indent) — one tuning decision three renderers must agree on.

// For punctuation that is replaced by a drawn glyph rather than removed: the
// "-" of a bullet and the "- [ ]" of a checkbox. Transparent, so the literal
// characters don't paint — but at full size, so they keep reserving exactly
// the horizontal space they always did. NoteWindow.qml then draws a real
// bullet/checkbox into that reserved slot (see DocumentHandler::
// listMarkerRanges), which is why this must NOT use hiddenMarkerFormat above:
// collapse the slot to zero width and the drawn glyph would land on top of the
// item's own text.
QTextCharFormat reservedMarkerFormat(qreal slotPercent)
{
    QTextCharFormat format;
    format.setForeground(QColor(0, 0, 0, 0));
    // Percentage rather than an absolute pixel pad so the column tracks the
    // note's font size, which is user-configurable per note. It's applied to
    // the reserved character only, so it widens the marker's column without
    // touching the spacing of any real text.
    format.setFontLetterSpacingType(QFont::PercentageSpacing);
    format.setFontLetterSpacing(slotPercent);
    return format;
}

// The one real space between a bullet/checkbox marker and its item's text,
// widened so the drawn control isn't flush against its own label.
//
// A marker's whole visible column is only about two characters wide once the
// rest of it has collapsed, and the control drawn into it takes up nearly all
// of that — leaving a checkbox touching the first letter of its label
// (confirmed on screen). Stretching this one space is what buys the gap back.
// The character stays a perfectly ordinary, visible, selectable space; only
// its advance changes, and only for a space that is part of a list marker.
QTextCharFormat markerGapFormat()
{
    QTextCharFormat format;
    format.setFontLetterSpacingType(QFont::PercentageSpacing);
    format.setFontLetterSpacing(200.0);
    return format;
}

// Mirrors TextFormatting::headingCharFormat exactly (bold + a relative
// FontSizeAdjustment of 3 for H1, 2 for H2, 0 below) so the plain-text editor
// renders headings at the same weight/size the structural QTextDocument model
// did before this rewrite — a visual no-op for the user. FontSizeAdjustment is
// relative, so it survives the user changing the note's base font size, which
// an absolute point size would not.
QTextCharFormat headingContentFormat(int level)
{
    QTextCharFormat format;
    format.setFontWeight(QFont::Bold);
    int adjustment = 0;
    if (level == 1)
        adjustment = 3;
    else if (level == 2)
        adjustment = 2;
    format.setProperty(QTextFormat::FontSizeAdjustment, adjustment);
    return format;
}

QTextCharFormat inlineCodeFormat()
{
    QTextCharFormat format;
    format.setFontFamilies({QStringLiteral("monospace")});
    format.setBackground(QColor(0x88, 0x88, 0x88, 0x30));
    return format;
}

// Reserves a transparent band for a whole-line image, so NoteWindow.qml can
// draw into the vertical space the collapsed "![alt](src)" text leaves. A large
// font point size is what actually reserves the room — a line's height is its
// tallest glyph — while the transparent foreground keeps the literal characters
// invisible. The same large point size also gives them a huge horizontal
// advance, but the editor pins its contentWidth to the viewport (see
// NoteWindow.qml's editorFlick), so that overflow is simply clipped and costs
// no horizontal scroll. Unlike hiddenMarkerFormat this size is nowhere near 0,
// so it triggers none of that format's font-fallback freeze — there is a real
// glyph box at this size, just a transparent one.
//
// A real image reserves a TALL band to draw the picture into; an image whose
// src is still empty ("![]()", e.g. straight from the toolbar's insert button)
// reserves only a SHORT strip — just enough for its "Select image…" button, so
// an unfilled placeholder isn't a big blank square (feature request/bugfix).
constexpr qreal kImageBandPointSize = 150.0;
constexpr qreal kImagePlaceholderPointSize = 30.0;
QTextCharFormat imageBandFormat(qreal pointSize)
{
    QTextCharFormat format;
    format.setForeground(QColor(0, 0, 0, 0));
    format.setFontPointSize(pointSize);
    return format;
}

// The find/replace panel's "here's everywhere else your term appears" wash, and
// the stronger version marking the one match the panel's own "3 of 7" counter
// is pointing at.
//
// Amber, and TRANSLUCENT, for the same reason revealedMarkerFormat picks a flat
// mid-gray: a note's background is whatever color the user chose, so a solid
// highlight would be unreadable against some of them. Alpha-blending over the
// real background keeps the text legible on any of them, and amber stays
// distinct from the editor's own selection color (derived from the note's
// highlightColor), which paints OVER the current match whenever the editor has
// a live selection — so the two must not be mistakable for each other.
//
// Deliberately hardcoded rather than a Settings knob, unlike the spellcheck
// underline's color/width: nobody asked to theme this, and adding a setting
// means a settings-dialog row and a migration. Both formats are only a
// background, so everything the markdown pass already applied (bold, heading
// size, monospace) still shows through the merge in applySearchHighlight().
QTextCharFormat searchMatchFormat()
{
    QTextCharFormat format;
    format.setBackground(QColor(0xFF, 0xB3, 0x00, 0x45));
    return format;
}

QTextCharFormat currentSearchMatchFormat()
{
    QTextCharFormat format;
    format.setBackground(QColor(0xFF, 0xB3, 0x00, 0x99));
    return format;
}

// "# " through "###### " — GFM requires whitespace after the hashes, so "#tag"
// is deliberately not a heading.
const QRegularExpression &headingPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^(#{1,6})(\\s+)"));
    return pattern;
}

// An inline span like **bold** or `code`: `pattern` matches the whole span
// including its delimiters, `markerLength` is how many characters each side's
// delimiter occupies, and `contentFormat` is merged over the text between them.
struct InlineRule
{
    const QRegularExpression &pattern;
    int markerLength;
    QTextCharFormat contentFormat;
    // Code spans mask their entire contents so markdown punctuation inside
    // them stays literal ("`**not bold**`"). Emphasis spans mask only their
    // delimiters, leaving the text between them open to further rules so
    // genuine nesting still renders ("**bold *and italic* **").
    bool masksContent;
};

const QRegularExpression &codeSpanPattern()
{
    static const QRegularExpression pattern(QStringLiteral("`([^`\\n]+)`"));
    return pattern;
}

const QRegularExpression &strikethroughPattern()
{
    static const QRegularExpression pattern(QStringLiteral("~~(.+?)~~"));
    return pattern;
}

const QRegularExpression &boldAsteriskPattern()
{
    static const QRegularExpression pattern(QStringLiteral("\\*\\*(.+?)\\*\\*"));
    return pattern;
}

const QRegularExpression &boldUnderscorePattern()
{
    static const QRegularExpression pattern(QStringLiteral("__(.+?)__"));
    return pattern;
}

const QRegularExpression &italicAsteriskPattern()
{
    static const QRegularExpression pattern(QStringLiteral("\\*([^*\\n]+)\\*"));
    return pattern;
}

const QRegularExpression &italicUnderscorePattern()
{
    static const QRegularExpression pattern(QStringLiteral("_([^_\\n]+)_"));
    return pattern;
}

QTextCharFormat boldFormat()
{
    QTextCharFormat format;
    format.setFontWeight(QFont::Bold);
    return format;
}

QTextCharFormat italicFormat()
{
    QTextCharFormat format;
    format.setFontItalic(true);
    return format;
}

QTextCharFormat strikethroughFormat()
{
    QTextCharFormat format;
    format.setFontStrikeOut(true);
    return format;
}

// The accent styling for a link — a mid-blue underline, matching the HUD/toolbar
// accent so links read as the app's one "interactive" color. Which part of the
// link wears it depends on whether the link is revealed (see applyLinks): at
// rest the collapsed link shows only its display text, so the display text is
// the accent link; once the cursor enters the link and the raw "[...](url)" is
// revealed, the accent moves to the URL — the real target being edited — and
// the display text goes dim.
QTextCharFormat linkTextFormat()
{
    QTextCharFormat format;
    format.setForeground(QColor(0x4d, 0xa6, 0xff));
    format.setFontUnderline(true);
    return format;
}

// Precedence matters and is load-bearing:
//  - code first, so its contents are masked from every emphasis rule;
//  - bold before italic, since "**x**" would otherwise be read by the italic
//    rule as "*" + "x*" ... — the bold rule masking its own "**" delimiters is
//    what makes the italic rule skip it.
const QList<InlineRule> &inlineRules()
{
    static const QList<InlineRule> rules = {
        {codeSpanPattern(), 1, inlineCodeFormat(), true},
        {strikethroughPattern(), 2, strikethroughFormat(), false},
        {boldAsteriskPattern(), 2, boldFormat(), false},
        {boldUnderscorePattern(), 2, boldFormat(), false},
        {italicAsteriskPattern(), 1, italicFormat(), false},
        {italicUnderscorePattern(), 1, italicFormat(), false},
    };
    return rules;
}

}

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(static_cast<QObject *>(nullptr))
{
    // See the header comment on this constructor: intentionally NOT using
    // QSyntaxHighlighter(document), which would make `document` this object's
    // QObject parent and race DocumentHandler's unique_ptr ownership of it.
    setDocument(document);
}

int MarkdownHighlighter::cursorBlockNumber() const
{
    return m_cursorBlockNumber;
}

void MarkdownHighlighter::setCursorBlockNumber(int blockNumber)
{
    if (blockNumber == m_cursorBlockNumber)
        return;

    const int previous = m_cursorBlockNumber;
    m_cursorBlockNumber = blockNumber;

    if (!document())
        return;

    // Only these two lines can look different now: the one losing the cursor
    // (markers collapse) and the one gaining it (markers appear). Everything
    // else already renders correctly, so a full rehighlight() would be pure
    // waste on every arrow key.
    for (const int number : {previous, blockNumber}) {
        if (number < 0)
            continue;
        const QTextBlock block = document()->findBlockByNumber(number);
        if (block.isValid())
            rehighlightBlock(block);
    }
}

void MarkdownHighlighter::setCursorPosition(int position)
{
    const int previousPosition = m_cursorPosition;
    m_cursorPosition = position;

    if (!document())
        return;

    const int blockNumber = position < 0
        ? -1
        : document()->findBlock(qBound(0, position, document()->characterCount() - 1)).blockNumber();

    if (blockNumber != m_cursorBlockNumber) {
        // The coarse case: setCursorBlockNumber's own two-block repaint below
        // already covers whatever link/image touch state changed too — both
        // the line being left and the line being entered get repainted
        // regardless of why.
        setCursorBlockNumber(blockNumber);
        return;
    }

    // Same line as before, so the block-number guard above didn't fire — but a
    // link's or image's specific touch boundary on THIS line can still have
    // shifted (e.g. the cursor moved from touching a link to a few characters
    // past it, still the same line). Skipped for a position that didn't
    // actually move, and for any block that couldn't possibly contain a link
    // or image, so ordinary prose pays nothing extra for routine cursor moves.
    if (position == previousPosition || blockNumber < 0)
        return;
    const QTextBlock block = document()->findBlockByNumber(blockNumber);
    if (block.isValid()
        && (MarkdownLink::pattern().match(block.text()).hasMatch()
            || MarkdownImage::parse(block.text()).isValid))
        rehighlightBlock(block);
}

const QList<TextSearch::Match> &MarkdownHighlighter::searchMatches() const
{
    return m_searchMatches;
}

int MarkdownHighlighter::currentSearchMatch() const
{
    return m_currentSearchMatch;
}

void MarkdownHighlighter::setSearchMatches(const QList<TextSearch::Match> &matches, int currentIndex)
{
    if (matches == m_searchMatches && currentIndex == m_currentSearchMatch)
        return;

    const QList<TextSearch::Match> previous = m_searchMatches;
    m_searchMatches = matches;
    m_currentSearchMatch = currentIndex;

    if (!document())
        return;

    // The union of the blocks the OLD matches occupied (their highlight has to
    // come off) and the ones the NEW matches occupy (theirs has to go on).
    // Everything else on screen already renders correctly.
    //
    // The old list is included even when only currentIndex moved, since the
    // match losing "current" has to drop back to the weaker highlight — the two
    // lists are identical then, so this collects each match's block once and
    // repaints exactly the lines that can look different.
    QSet<int> blockNumbers;
    collectBlockNumbers(previous, blockNumbers);
    collectBlockNumbers(matches, blockNumbers);
    for (const int number : blockNumbers) {
        const QTextBlock block = document()->findBlockByNumber(number);
        if (block.isValid())
            rehighlightBlock(block);
    }
}

void MarkdownHighlighter::setFadedLine(int position, qreal opacity, const QColor &baseTextColor)
{
    if (position == m_fadedLinePosition && qFuzzyCompare(opacity, m_fadeOpacity)
        && baseTextColor == m_fadeBaseColor) {
        return;
    }

    const int previousPosition = m_fadedLinePosition;
    m_fadedLinePosition = position;
    m_fadeOpacity = qBound(0.0, opacity, 1.0);
    m_fadeBaseColor = baseTextColor;

    if (!document())
        return;

    // The old line has to repaint too — that is how a cleared fade puts the
    // text back — and it is usually the same block, which the QSet collapses
    // to a single repaint.
    QSet<int> blockNumbers;
    for (const int candidate : {previousPosition, position}) {
        if (candidate < 0)
            continue;
        const QTextBlock block = document()->findBlock(candidate);
        if (block.isValid())
            blockNumbers.insert(block.blockNumber());
    }
    for (const int number : blockNumbers) {
        const QTextBlock block = document()->findBlockByNumber(number);
        if (block.isValid())
            rehighlightBlock(block);
    }
}

void MarkdownHighlighter::applyLineFade(const QString &text)
{
    if (m_fadedLinePosition < 0 || m_fadeOpacity >= 1.0)
        return;

    const int blockStart = currentBlock().position();
    if (m_fadedLinePosition < blockStart || m_fadedLinePosition > blockStart + text.size())
        return;

    for (int offset = 0; offset < text.size(); ++offset) {
        QTextCharFormat composed = format(offset);
        // No explicit foreground means "whatever the TextArea paints text in",
        // which only the caller knows — see setFadedLine's own comment.
        QColor color = composed.foreground().style() == Qt::NoBrush
            ? m_fadeBaseColor
            : composed.foreground().color();
        color.setAlphaF(color.alphaF() * m_fadeOpacity);
        composed.setForeground(color);
        setFormat(offset, 1, composed);
    }
}

void MarkdownHighlighter::collectBlockNumbers(const QList<TextSearch::Match> &matches,
                                              QSet<int> &out) const
{
    for (const TextSearch::Match &match : matches) {
        // Every block the match touches, not just the one it starts in: a regex
        // match can span lines. `position() <= match.end` can also pick up the
        // block just past a match that ends exactly on a block boundary, which
        // costs one extra rehighlight of a block whose local range is empty —
        // cheaper than the arithmetic to exclude it.
        for (QTextBlock block = document()->findBlock(match.start);
             block.isValid() && block.position() <= match.end;
             block = block.next()) {
            out.insert(block.blockNumber());
        }
    }
}

bool MarkdownHighlighter::highlightImageLine(const QString &text)
{
    const MarkdownImage::Match image = MarkdownImage::parse(text);
    if (!image.isValid)
        return false;

    // Position-based touch test (NOT block-level revealMarkers — see
    // m_cursorPosition's own doc comment): only while the cursor is actually
    // within the image's own "![alt](src)" span does the raw markdown stay at
    // normal size for editing. Any surrounding whitespace on the line (leading
    // indentation, or trailing padding) does NOT count as touching — the span
    // (excluding that whitespace) is what buys that here.
    const int absoluteStart = currentBlock().position() + image.spanStart;
    const int absoluteEnd = absoluteStart + image.spanLength;
    const bool touching = m_cursorPosition >= absoluteStart && m_cursorPosition <= absoluteEnd;

    // Off the touched span, reserve a band: a tall one for a real image to draw
    // into, or just a short strip for an empty src's "Select image…" button (so
    // an unfilled placeholder isn't a big blank square).
    if (!touching) {
        const qreal pointSize =
            image.src.isEmpty() ? kImagePlaceholderPointSize : kImageBandPointSize;
        setFormat(0, text.size(), imageBandFormat(pointSize));
    }
    return true;
}

bool MarkdownHighlighter::highlightTableRow(const QString &text)
{
    const MarkdownTable::Region region =
        MarkdownTable::regionAt(document(), currentBlock().blockNumber());
    // A line of prose that merely happens to start with "|", or a header row
    // whose delimiter row hasn't been typed yet. Not a table — render it as
    // ordinary text, pipes and all, so a half-typed table stays visible while
    // it's being written.
    if (!region.isValid())
        return false;

    const int delimiterBlockNumber = region.firstBlockNumber + 1;
    if (currentBlock().blockNumber() == delimiterBlockNumber) {
        // "| --- | --- |" carries no information a drawn grid doesn't already
        // show, so the whole row collapses. Its line still exists, roughly a
        // pixel tall (line height comes from the tallest glyph on it, and every
        // glyph here is now hair-thin) — which conveniently leaves exactly the
        // sliver of space NoteWindow.qml draws the header rule into.
        setFormat(0, text.size(), hiddenMarkerFormat());
        return true;
    }

    // Measured in the document's default font, which is the note's own font.
    // This measurement is only exact because a table row is masked from every
    // inline rule below — no cell can contain bold or collapsed markers, so
    // what gets measured here is precisely what gets drawn. Allowing emphasis
    // inside cells would mean measuring one font and rendering another, and the
    // columns would drift.
    const QFontMetricsF metrics(document()->defaultFont());

    // Every column's target width: the widest that column's cell is in any row.
    // Recomputed per row rather than cached — a table is a handful of short
    // lines, and a cache would need invalidating on every keystroke in any cell
    // of the region.
    QList<qreal> columnWidths(region.columnCount, 0.0);
    for (int number = region.firstBlockNumber; number <= region.lastBlockNumber; ++number) {
        if (number == delimiterBlockNumber)
            continue; // collapsed, so it must not stretch any column
        const QTextBlock row = document()->findBlockByNumber(number);
        if (!row.isValid())
            continue;
        const QString rowText = row.text();
        const QList<QPair<int, int>> spans = MarkdownTable::cellSpans(rowText);
        for (int column = 0; column < spans.size() && column < columnWidths.size(); ++column) {
            const QString cell = rowText.mid(spans.at(column).first,
                                             spans.at(column).second - spans.at(column).first);
            columnWidths[column] = qMax(columnWidths.at(column), metrics.horizontalAdvance(cell));
        }
    }

    // Pad this row's cells out to those widths by widening ONE character each:
    // the cell's last, which is its own trailing padding space. Absolute
    // spacing (a pixel amount) rather than percentage, because the shortfall
    // being corrected is a pixel amount.
    const QList<QPair<int, int>> spans = MarkdownTable::cellSpans(text);
    for (int column = 0; column < spans.size() && column < columnWidths.size(); ++column) {
        const int start = spans.at(column).first;
        const int end = spans.at(column).second;
        if (end <= start)
            continue;
        const QString cell = text.mid(start, end - start);
        const qreal shortfall = columnWidths.at(column) - metrics.horizontalAdvance(cell);
        if (shortfall <= 0.0)
            continue;
        QTextCharFormat stretch;
        stretch.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        stretch.setFontLetterSpacing(shortfall);
        QTextCharFormat composed = format(end - 1);
        composed.merge(stretch);
        setFormat(end - 1, 1, composed);
    }

    // The pipes go last: they're the column boundaries the grid is drawn at,
    // and collapsing them to zero width is what makes each boundary a single
    // exact x rather than a visible character the grid line would sit beside.
    for (const int pipe : MarkdownTable::pipePositions(text))
        setFormat(pipe, 1, hiddenMarkerFormat());

    return true;
}

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    // Code mode turns the markdown pass off (see setMarkdownDecorationEnabled)
    // — but ONLY that pass. Find/replace's match highlight below is not
    // markdown decoration and has nothing to do with the mode: searching a
    // code note is exactly as useful as searching a prose one, and skipping
    // the whole of highlightBlock() here would silently break it.
    if (m_markdownDecorationEnabled)
        highlightMarkdown(text);

    // Last, and outside highlightMarkdown()'s own early returns: the search
    // highlight has to survive being merged over the markdown formatting rather
    // than the other way round, and it applies to empty and table lines too.
    applySearchHighlight(text);

    // Later still: a line on its way out fades everything above, the search
    // highlight included, so a match on a completing task doesn't stay bright
    // amber while its text disappears.
    applyLineFade(text);
}

void MarkdownHighlighter::setMarkdownDecorationEnabled(bool enabled)
{
    if (m_markdownDecorationEnabled == enabled)
        return;
    m_markdownDecorationEnabled = enabled;
    // Whole-document, unlike setCursorBlockNumber's two-block repaint: this
    // changes how EVERY line renders, and it is a mode flip a user does by
    // hand once in a while, not something on any hot path.
    rehighlight();
}

bool MarkdownHighlighter::markdownDecorationEnabled() const
{
    return m_markdownDecorationEnabled;
}

void MarkdownHighlighter::applySearchHighlight(const QString &text)
{
    if (m_searchMatches.isEmpty())
        return;

    // Matches carry ABSOLUTE document positions; setFormat() wants offsets
    // within this block. Clipping each match to the block's own span is what
    // renders a match spanning several lines as a highlight on each of them —
    // every block it crosses paints its own slice.
    const int blockStart = currentBlock().position();
    const int blockEnd = blockStart + text.size();

    for (int i = 0; i < m_searchMatches.size(); ++i) {
        const TextSearch::Match &match = m_searchMatches.at(i);
        const int start = qMax(match.start, blockStart);
        const int end = qMin(match.end, blockEnd);
        // No overlap with this block, or a zero-length match (a regex like
        // "x*" can produce one — TextSearch::allMatches reports them). Either
        // way there is no character here to paint.
        if (start >= end)
            continue;

        const QTextCharFormat highlight =
            i == m_currentSearchMatch ? currentSearchMatchFormat() : searchMatchFormat();
        // Merged char by char against what this same pass has already set, for
        // exactly the reason highlightMarkdown()'s own mergeFormat lambda
        // documents: setFormat() REPLACES the format over its range, so
        // applying this wholesale would strip the bold/heading/monospace off
        // any match that happens to land on decorated text.
        for (int position = start; position < end; ++position) {
            const int offset = position - blockStart;
            QTextCharFormat composed = format(offset);
            composed.merge(highlight);
            setFormat(offset, 1, composed);
        }
    }
}

void MarkdownHighlighter::highlightMarkdown(const QString &text)
{
    if (text.isEmpty())
        return;

    // Tables are handled whole-line and return early: their cells are
    // deliberately masked from every emphasis rule below. That's what keeps
    // column measurement exact (see highlightTableRow), at the cost of not
    // rendering bold inside a cell.
    if (MarkdownTable::isTableRow(text) && highlightTableRow(text))
        return;

    // Whether this is the line the cursor is on, and so whether its markdown
    // punctuation shows. Read once per block rather than per marker.
    const bool revealMarkers = currentBlock().blockNumber() == m_cursorBlockNumber;

    // A whole-line image collapses to a reserved band (while not touched) that
    // QML draws the picture into, or stays raw editable text (while touched) —
    // either way it's not a heading/list/emphasis context, so this returns
    // early like the table row above. Position-based, not revealMarkers (see
    // highlightImageLine's own comment).
    if (highlightImageLine(text))
        return;

    // QSyntaxHighlighter::setFormat() *replaces* the format over its range
    // rather than merging into it, so applying italic over an already-bold
    // span would silently drop the bold. Composing against format(i) — which
    // reads back what this same pass has set so far — is what lets rules
    // layer instead of clobber.
    const auto mergeFormat = [this](int start, int length, const QTextCharFormat &format) {
        for (int i = start; i < start + length; ++i) {
            QTextCharFormat composed = this->format(i);
            composed.merge(format);
            setFormat(i, 1, composed);
        }
    };

    // Characters already claimed by an earlier, higher-precedence rule. Later
    // rules refuse any match overlapping one, which is what keeps "**bold**"
    // from also reading as italic and "`**x**`" from reading as bold at all.
    QList<bool> masked(text.size(), false);
    const auto overlapsMask = [&masked](int start, int length) {
        for (int i = start; i < start + length; ++i) {
            if (masked[i])
                return true;
        }
        return false;
    };
    const auto mask = [&masked](int start, int length) {
        for (int i = start; i < start + length; ++i)
            masked[i] = true;
    };

    // Collapsible punctuation: "**", "*", "~~", "`", and a heading's "#"s.
    // Revealed (and merged, so it keeps the heading size around it and the
    // line height stays uniform) on the cursor's own line; replaced outright
    // by the zero-width format everywhere else — see hiddenMarkerFormat on why
    // hiding must replace rather than merge.
    const auto applyMarkerFormat = [&](int start, int length) {
        if (revealMarkers)
            mergeFormat(start, length, revealedMarkerFormat());
        else
            setFormat(start, length, hiddenMarkerFormat());
    };

    // --- Block-level prefix ---------------------------------------------
    // Inline scanning starts after any list marker, so a bullet's own "*"
    // can never be mistaken for an italic delimiter.
    int contentStart = 0;

    if (const QRegularExpressionMatch heading = headingPattern().match(text); heading.hasMatch()) {
        const int level = heading.capturedLength(1);
        // The heading format covers the "#" markers too, so the whole line
        // shares one size and the line height stays uniform; the marker's
        // dimmed foreground is then merged on top of it.
        mergeFormat(0, text.size(), headingContentFormat(level));
        applyMarkerFormat(heading.capturedStart(1),
                          heading.capturedLength(1) + heading.capturedLength(2));
        contentStart = heading.capturedEnd(2);
    } else {
        // List markers come from MarkdownList rather than this file's own
        // regexes. That module exists precisely so the places that must agree
        // on where a marker ends actually do (see its header) — and they did
        // not: its patterns match "- x" only at the very start of a line,
        // while the private copies that used to live here also allowed leading
        // indentation. That split was survivable while the disagreement only
        // changed a marker's *color*; it stops being survivable now that the
        // highlighter makes the marker invisible and QML draws the replacement
        // glyph from MarkdownList's own answer. An indented "  - x" would go
        // transparent here and get no bullet drawn over there — a marker that
        // silently disappeared.
        const MarkdownList::Marker marker = MarkdownList::parse(text);
        switch (marker.kind) {
        case MarkdownList::Kind::Checkbox:
        case MarkdownList::Kind::Bullet:
            // The buffer holds literal "-"/"- [ ]" text; this hides those
            // characters but keeps their slot, and NoteWindow.qml draws a real
            // bullet/checkbox into it. What the user typed is still exactly
            // what gets saved — the pre-rewrite editor instead stored Unicode
            // •/☐/☑ glyphs in the document and translated them to GFM only at
            // save time, the stored-vs-saved split this rewrite removes.
            //
            // Unlike emphasis, these do not follow the cursor: a checkbox
            // stays a checkbox on the line you're editing, because it's a
            // control you click, not syntax you read past.
            //
            // Everything in the marker collapses except two things: the one
            // reserved slot, and the space that ends it (marker.length - 1),
            // which stays real and separates the drawn control from the item's
            // text. For a bullet there's nothing to collapse — its slot IS its
            // whole marker. For a checkbox this is what turns six characters
            // of "- [x] " into a single checkbox-wide column followed by one
            // ordinary space.
            setFormat(0, marker.glyphStart, hiddenMarkerFormat());
            mergeFormat(marker.glyphStart, marker.glyphLength,
                        reservedMarkerFormat(MarkdownList::slotPercentForKind(marker.kind)));
            setFormat(marker.glyphStart + marker.glyphLength,
                      marker.length - 1 - marker.glyphStart - marker.glyphLength,
                      hiddenMarkerFormat());
            mergeFormat(marker.length - 1, 1, markerGapFormat());
            contentStart = marker.length;
            break;
        case MarkdownList::Kind::Numbered:
            // Deliberately left unformatted, so "1." renders in the ordinary
            // text color. A numbered list's marker is not punctuation to read
            // past: the number is content — it's what a rendered list would
            // show anyway — and there's no better glyph to swap it for.
            // Dimming it, as every marker once was, made real content look
            // like syntax.
            contentStart = marker.length;
            break;
        case MarkdownList::Kind::None:
            break;
        }
    }

    if (contentStart > 0)
        mask(0, contentStart);

    // --- Inline spans ----------------------------------------------------
    // One rule's worth of scanning, factored out so links can be slotted into
    // the middle of the precedence order below (after code, before emphasis)
    // rather than tacked on at either end.
    const auto applyRule = [&](const InlineRule &rule) {
        // Deliberately a manual scan rather than globalMatch(): when a match is
        // rejected for overlapping the mask, the search has to resume just
        // *inside* it, not past its end. globalMatch would skip the remainder
        // wholesale — in "**bold *and italic* done**" the italic rule first
        // matches the masked "*bold *", and resuming past that match's end
        // would step over the real "*and italic*" and leave it unstyled.
        int searchFrom = contentStart;
        while (searchFrom < text.size()) {
            const QRegularExpressionMatch match = rule.pattern.match(text, searchFrom);
            if (!match.hasMatch())
                break;
            if (overlapsMask(match.capturedStart(), match.capturedLength())) {
                searchFrom = match.capturedStart() + 1;
                continue;
            }

            mergeFormat(match.capturedStart(1), match.capturedLength(1), rule.contentFormat);
            applyMarkerFormat(match.capturedStart(), rule.markerLength);
            applyMarkerFormat(match.capturedEnd() - rule.markerLength, rule.markerLength);

            if (rule.masksContent)
                mask(match.capturedStart(), match.capturedLength());
            else {
                mask(match.capturedStart(), rule.markerLength);
                mask(match.capturedEnd() - rule.markerLength, rule.markerLength);
            }

            searchFrom = match.capturedEnd();
        }
    };

    // [text](url): at rest the link is collapsed to just its display text, which
    // wears the accent link styling (blue + underline). Once the cursor enters
    // the link the raw "[...](url)" reveals: the display text goes dim and the
    // accent moves to the URL, the real target being edited. The display text
    // stays visible everywhere;
    // the surrounding "[" and "](url)" collapse EXCEPT while the cursor is
    // actually touching this specific link, exactly like emphasis
    // delimiters — except position-based, not block-level (bug found live:
    // with block-level reveal, "[the site](url) and more prose" stayed fully
    // expanded as long as the cursor was anywhere on that line — several words
    // later, or on the trailing space auto-linking leaves behind — rather than
    // only while the cursor was actually on the link). The whole span is masked
    // so the emphasis rules below don't re-parse the link text or its url. url
    // may be empty ("[]()" mid-edit) — still a valid, styled link.
    const auto applyLinks = [&]() {
        int searchFrom = contentStart;
        while (searchFrom < text.size()) {
            const QRegularExpressionMatch match = MarkdownLink::pattern().match(text, searchFrom);
            if (!match.hasMatch())
                break;
            if (overlapsMask(match.capturedStart(), match.capturedLength())) {
                searchFrom = match.capturedStart() + 1;
                continue;
            }

            const int textStart = match.capturedStart(1);
            const int textLen = match.capturedLength(1);

            // Absolute document position, compared against THIS match's own
            // span (not the whole line) — see the comment above.
            const int absoluteMatchStart = currentBlock().position() + match.capturedStart();
            const int absoluteMatchEnd = currentBlock().position() + match.capturedEnd();
            const bool linkTouched =
                m_cursorPosition >= absoluteMatchStart && m_cursorPosition <= absoluteMatchEnd;

            // The display text is what a collapsed link shows, so at rest it IS
            // the link: accent blue + underline. Only once the cursor enters the
            // link (revealing the raw "[...](url)") does the styling flip — the
            // display text goes dim and the URL, now visible, takes the accent
            // instead, since the URL is the real target being edited.
            mergeFormat(textStart, textLen, linkTouched ? revealedMarkerFormat() : linkTextFormat());

            const auto applyLinkMarkerFormat = [&](int start, int length) {
                if (linkTouched)
                    mergeFormat(start, length, revealedMarkerFormat());
                else
                    setFormat(start, length, hiddenMarkerFormat());
            };
            // Leading "[" ...
            applyLinkMarkerFormat(match.capturedStart(), textStart - match.capturedStart());
            // ... the "](" and ")" punctuation reveal/collapse like any marker,
            // but the URL between them (capture 2) is the real link target: it
            // takes the accent link styling when the link is revealed, and still
            // collapses with the surrounding punctuation when it isn't touched.
            const int urlStart = match.capturedStart(2);
            const int urlLen = match.capturedLength(2);
            applyLinkMarkerFormat(textStart + textLen, urlStart - (textStart + textLen)); // "]("
            if (linkTouched)
                mergeFormat(urlStart, urlLen, linkTextFormat());
            else
                setFormat(urlStart, urlLen, hiddenMarkerFormat());
            applyLinkMarkerFormat(urlStart + urlLen, match.capturedEnd() - (urlStart + urlLen)); // ")"
            mask(match.capturedStart(), match.capturedLength());
            searchFrom = match.capturedEnd();
        }
    };

    // Precedence: code first (so a link or emphasis inside `backticks` stays
    // literal, because code masks its whole contents), then links (so their
    // text isn't re-read as emphasis), then the emphasis rules.
    const QList<InlineRule> &rules = inlineRules();
    if (!rules.isEmpty())
        applyRule(rules.first()); // the code-span rule
    applyLinks();
    for (int i = 1; i < rules.size(); ++i)
        applyRule(rules.at(i));
}
