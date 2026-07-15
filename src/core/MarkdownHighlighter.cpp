#include "MarkdownHighlighter.h"

#include <QColor>
#include <QFont>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QTextDocument>

namespace
{

// Markdown punctuation (the "**", "#", "- ", "`" a user literally typed) stays
// visible but dimmed, rather than being hidden. Obsidian/CodeMirror collapse
// markers to zero width outside the cursor's line, but Qt's rich text has no
// zero-width-hide primitive short of a custom QAbstractTextDocumentLayout —
// dimming is the closest faithful approximation and is an accepted, explicit
// non-goal of this rewrite (see .task/plan.md Decisions). A mid-gray is used
// rather than an alpha blend of the text color because notes can carry an
// arbitrary user-chosen background, and mid-gray stays legible on both light
// and dark ones.
QTextCharFormat markerFormat()
{
    QTextCharFormat format;
    format.setForeground(QColor(0x88, 0x88, 0x88));
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

// "- [ ] " / "- [x] ". Must be tested BEFORE bulletPattern(), which would
// otherwise claim the leading "- " and leave the "[ ]" unstyled.
const QRegularExpression &checkboxPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^(\\s*)([-*+]\\s\\[[ xX]\\])(\\s+)"));
    return pattern;
}

const QRegularExpression &bulletPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^(\\s*)([-*+])(\\s+)"));
    return pattern;
}

const QRegularExpression &numberedPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^(\\s*)(\\d+\\.)(\\s+)"));
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

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    if (text.isEmpty())
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
        mergeFormat(heading.capturedStart(1), heading.capturedLength(1) + heading.capturedLength(2),
                    markerFormat());
        contentStart = heading.capturedEnd(2);
    } else if (const QRegularExpressionMatch checkbox = checkboxPattern().match(text);
               checkbox.hasMatch()) {
        // The buffer holds literal "- [ ]"/"- [x]" text and this dims it in
        // place: the character the user typed is the character that gets
        // saved. The pre-rewrite editor stored Unicode ☐/☑ glyphs in the
        // document and only translated them to GFM at save time — exactly the
        // stored-vs-saved split this rewrite exists to remove.
        mergeFormat(checkbox.capturedStart(2), checkbox.capturedLength(2), markerFormat());
        contentStart = checkbox.capturedEnd(3);
    } else if (const QRegularExpressionMatch bullet = bulletPattern().match(text);
               bullet.hasMatch()) {
        mergeFormat(bullet.capturedStart(2), bullet.capturedLength(2), markerFormat());
        contentStart = bullet.capturedEnd(3);
    } else if (const QRegularExpressionMatch numbered = numberedPattern().match(text);
               numbered.hasMatch()) {
        mergeFormat(numbered.capturedStart(2), numbered.capturedLength(2), markerFormat());
        contentStart = numbered.capturedEnd(3);
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
            mergeFormat(match.capturedStart(), rule.markerLength, markerFormat());
            mergeFormat(match.capturedEnd() - rule.markerLength, rule.markerLength, markerFormat());

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
