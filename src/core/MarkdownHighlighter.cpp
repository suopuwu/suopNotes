#include "MarkdownHighlighter.h"

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
QTextCharFormat hiddenMarkerFormat()
{
    QTextCharFormat format;
    format.setForeground(QColor(0, 0, 0, 0));
    format.setFontPointSize(0.01);
    return format;
}

// For punctuation that is replaced by a drawn glyph rather than removed: the
// "-" of a bullet and the "- [ ]" of a checkbox. Transparent, so the literal
// characters don't paint — but at full size, so they keep reserving exactly
// the horizontal space they always did. NoteWindow.qml then draws a real
// bullet/checkbox into that reserved slot (see DocumentHandler::
// listMarkerRanges), which is why this must NOT use hiddenMarkerFormat above:
// collapse the slot to zero width and the drawn glyph would land on top of the
// item's own text.
QTextCharFormat reservedMarkerFormat()
{
    QTextCharFormat format;
    format.setForeground(QColor(0, 0, 0, 0));
    // Widen the slot to twice the character's natural advance. Without this
    // the column is only as wide as whatever "-" or "[" happens to measure in
    // the user's font (~6px at the default size) — narrower than the control
    // drawn into it, so the bullet/checkbox overflowed on both sides: off the
    // editor's left edge, where it was visibly clipped in half, and rightwards
    // into its own label, leaving no gap (both confirmed on screen).
    //
    // Percentage rather than an absolute pixel pad so the column tracks the
    // note's font size, which is user-configurable per note. It's applied to
    // the reserved character only, so it widens the marker's column without
    // touching the spacing of any real text.
    format.setFontLetterSpacingType(QFont::PercentageSpacing);
    format.setFontLetterSpacing(200.0);
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
            mergeFormat(marker.glyphStart, marker.glyphLength, reservedMarkerFormat());
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
    for (const InlineRule &rule : inlineRules()) {
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
    }
}
