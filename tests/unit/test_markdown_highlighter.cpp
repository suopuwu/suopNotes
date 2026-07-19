#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include "core/MarkdownHighlighter.h"

// These tests document the one guarantee that motivates the whole plain-text
// rewrite: the document holds the *literal markdown the user typed* — "**bold**"
// is really eight characters of buffer, not a bold run — and MarkdownHighlighter
// is what makes that literal text *look* formatted. Every assertion below reads
// styling back out of the block's layout (where a QSyntaxHighlighter's setFormat
// lands) while the buffer text itself stays untouched and byte-exact.
//
// The highlighter gives markdown punctuation one of three treatments, and most
// of what follows is about which punctuation gets which:
//
//   collapsed — invisible AND zero-width, so the text closes up over it as if
//               it had been parsed away. Emphasis and heading markers, on every
//               line except the one the cursor is on.
//   revealed  — visible but dimmed. The same markers, on the cursor's own line,
//               so they can be seen and edited (Obsidian-style live preview).
//   reserved  — invisible but still full-width, holding a slot open for
//               NoteWindow.qml to draw a real bullet/checkbox into.

namespace
{

// QSyntaxHighlighter::setFormat() results land in the block's QTextLayout
// format ranges — a separate mechanism from the document's own
// QTextCursor::charFormat(). This walks those ranges to find whichever one (if
// any) covers `position`, mirroring how Qt composites them at paint time.
// Mirrors the identical helper in test_spell_check_highlighter.cpp; not shared
// from there because it lives in that file's anonymous namespace and this repo
// has no shared-test-helper convention.
QTextCharFormat formatAt(const QTextBlock &block, int position)
{
    const QList<QTextLayout::FormatRange> ranges = block.layout()->formats();
    for (const QTextLayout::FormatRange &range : ranges) {
        if (position >= range.start && position < range.start + range.length)
            return range.format;
    }
    return QTextCharFormat();
}

// Attaches a highlighter and runs one synchronous pass. QSyntaxHighlighter's
// highlight-on-attach is queued through the event loop, which these tests never
// pump — rehighlight() runs the same pass inline, exactly as
// test_spell_check_highlighter.cpp does for the same reason.
void highlightNow(MarkdownHighlighter &highlighter)
{
    highlighter.rehighlight();
}

const QColor markerGray(0x88, 0x88, 0x88);

bool isBoldAt(const QTextBlock &block, int position)
{
    return formatAt(block, position).fontWeight() >= QFont::Bold;
}

bool isItalicAt(const QTextBlock &block, int position)
{
    return formatAt(block, position).fontItalic();
}

// Visible, but greyed out: how a marker looks on the cursor's own line.
bool isRevealedMarkerAt(const QTextBlock &block, int position)
{
    return formatAt(block, position).foreground().color() == markerGray;
}

// A link's visible text: the app's accent blue, underlined. Read as an exact
// colour here (unlike the search wash) because the link colour is a fixed,
// deliberate choice, not a strength-of-highlight tweak.
const QColor linkBlue(0x4d, 0xa6, 0xff);
bool isLinkTextAt(const QTextBlock &block, int position)
{
    const QTextCharFormat format = formatAt(block, position);
    return format.foreground().color() == linkBlue && format.fontUnderline();
}

// A link's display text: dim grey (the same grey a revealed marker uses) and
// NOT underlined. The display text is prose the reader sees but is not the link
// target, so it is de-emphasized rather than styled as the link — the reverse of
// the URL, which carries the accent link styling.
bool isDimTextAt(const QTextBlock &block, int position)
{
    const QTextCharFormat format = formatAt(block, position);
    return format.foreground().color() == markerGray && !format.fontUnderline();
}

// A whole-line image's reserved band: transparent, and blown up to a large
// point size — that large size is what actually holds the vertical room open
// for NoteWindow.qml to draw the picture into. The distinction from a collapsed
// marker (also transparent, but shrunk to near-zero) is exactly that size.
bool isImageBandAt(const QTextBlock &block, int position)
{
    const QTextCharFormat format = formatAt(block, position);
    return format.foreground().color().alpha() == 0 && format.fontPointSize() > 50.0;
}

// The reserved (transparent) point size at a position, or 0 if it isn't a
// reserved band — used to compare the tall picture band against the short
// empty-src placeholder strip.
qreal reservedPointSizeAt(const QTextBlock &block, int position)
{
    const QTextCharFormat format = formatAt(block, position);
    if (format.foreground().color().alpha() != 0)
        return 0.0;
    return format.fontPointSize();
}

// Gone from the rendered line entirely: transparent AND shrunk to a hair-thin
// font, which is what actually removes its horizontal advance (a transparent
// colour alone would leave a hole in the text).
bool isCollapsedMarkerAt(const QTextBlock &block, int position)
{
    const QTextCharFormat format = formatAt(block, position);
    const qreal pointSize = format.fontPointSize();
    return format.foreground().color().alpha() == 0 && pointSize > 0.0 && pointSize < 1.0;
}

// Invisible but still occupying its space, so a real bullet/checkbox can be
// drawn into the slot it holds open. The absence of a shrunk point size is the
// whole distinction from isCollapsedMarkerAt above.
bool isReservedMarkerAt(const QTextBlock &block, int position)
{
    const QTextCharFormat format = formatAt(block, position);
    return format.foreground().color().alpha() == 0 && format.fontPointSize() == 0.0;
}

// Whether a collapsed marker opts out of Qt's automatic font fallback.
// Performance, not appearance — see the freeze scenario below.
bool hasFontFallbackDisabledAt(const QTextBlock &block, int position)
{
    return (formatAt(block, position).fontStyleStrategy() & QFont::NoFontMerging) != 0;
}

// How wide a reserved slot is, as a percentage of the reserved character's own
// natural advance — the knob that decides how much room the drawn control gets.
qreal slotPercentAt(const QTextBlock &block, int position)
{
    const QTextCharFormat format = formatAt(block, position);
    if (format.fontLetterSpacingType() != QFont::PercentageSpacing)
        return 0.0;
    return format.fontLetterSpacing();
}

// How many extra pixels of advance a character has been given. This is how a
// table cell is padded out to its column's width: the cell's last character is
// widened, so the columns line up without a single byte of the buffer moving.
qreal extraWidthAt(const QTextBlock &block, int position)
{
    const QTextCharFormat format = formatAt(block, position);
    if (format.fontLetterSpacingType() != QFont::AbsoluteSpacing)
        return 0.0;
    return format.fontLetterSpacing();
}

// The find/replace match wash. Read as "has any background at all" plus its
// alpha, rather than against an exact QColor: the two match highlights differ
// only in strength, and pinning the literal ARGB here would turn a palette
// tweak into a test failure for no behavioral reason.
//
// Inline code carries a background of its own, so this cannot tell a code span
// apart from a match — the scenarios below only ask it about plain or
// emphasized text, never a code span.
int searchHighlightAlphaAt(const QTextBlock &block, int position)
{
    const QBrush background = formatAt(block, position).background();
    if (background.style() == Qt::NoBrush)
        return 0;
    return background.color().alpha();
}

bool isSearchHighlightedAt(const QTextBlock &block, int position)
{
    return searchHighlightAlphaAt(block, position) > 0;
}

}

SCENARIO("Bold text renders bold while its ** markers vanish from the rendered line", "[markdown_highlighter]")
{
    GIVEN("a document whose literal text contains **bold**")
    {
        QTextDocument doc;
        doc.setPlainText("say **bold** here");

        WHEN("a MarkdownHighlighter is attached and the cursor is elsewhere")
        {
            MarkdownHighlighter highlighter(&doc);
            highlightNow(highlighter);
            const QTextBlock block = doc.begin();

            THEN("the buffer still holds every character the user typed, asterisks included")
            {
                CHECK(doc.toPlainText() == QStringLiteral("say **bold** here"));
            }

            THEN("the word between the markers is painted bold")
            {
                for (int i = 6; i < 10; ++i)
                    CHECK(isBoldAt(block, i));
            }

            THEN("the ** markers collapse to nothing, so the line reads as rendered text")
            {
                CHECK(isCollapsedMarkerAt(block, 4));
                CHECK(isCollapsedMarkerAt(block, 5));
                CHECK(isCollapsedMarkerAt(block, 10));
                CHECK(isCollapsedMarkerAt(block, 11));
            }

            THEN("surrounding plain text is left unstyled")
            {
                CHECK_FALSE(isBoldAt(block, 0));
                CHECK_FALSE(isBoldAt(block, 14));
            }
        }
    }
}

SCENARIO("The cursor's own line reveals its markdown punctuation", "[markdown_highlighter]")
{
    GIVEN("two identical lines of bold text")
    {
        QTextDocument doc;
        doc.setPlainText("**one**\n**two**");

        WHEN("the cursor is reported to be on the first line")
        {
            MarkdownHighlighter highlighter(&doc);
            highlighter.setCursorBlockNumber(0);
            highlightNow(highlighter);

            const QTextBlock cursorLine = doc.findBlockByNumber(0);
            const QTextBlock otherLine = doc.findBlockByNumber(1);

            THEN("that line's markers become visible, dimmed, so they can be edited")
            {
                CHECK(isRevealedMarkerAt(cursorLine, 0));
                CHECK(isRevealedMarkerAt(cursorLine, 1));
            }

            THEN("the identical line without the cursor keeps its markers collapsed")
            {
                CHECK(isCollapsedMarkerAt(otherLine, 0));
                CHECK(isCollapsedMarkerAt(otherLine, 1));
            }

            THEN("revealing punctuation does not stop the text itself rendering bold")
            {
                CHECK(isBoldAt(cursorLine, 2));
                CHECK(isBoldAt(otherLine, 2));
            }
        }

        WHEN("the cursor then moves to the second line")
        {
            MarkdownHighlighter highlighter(&doc);
            highlighter.setCursorBlockNumber(0);
            highlightNow(highlighter);
            // No rehighlight() here: setCursorBlockNumber must restyle the two
            // affected lines by itself, since in the app it is called from a
            // cursor move with no edit to trigger anything else.
            highlighter.setCursorBlockNumber(1);

            THEN("the markers follow it — the line it left collapses, the line it entered reveals")
            {
                CHECK(isCollapsedMarkerAt(doc.findBlockByNumber(0), 0));
                CHECK(isRevealedMarkerAt(doc.findBlockByNumber(1), 0));
            }
        }

        WHEN("the cursor is reported as being nowhere at all")
        {
            MarkdownHighlighter highlighter(&doc);
            highlighter.setCursorBlockNumber(0);
            highlightNow(highlighter);
            highlighter.setCursorBlockNumber(-1);

            THEN("every marker in the document collapses — an untouched note reads as prose")
            {
                CHECK(isCollapsedMarkerAt(doc.findBlockByNumber(0), 0));
                CHECK(isCollapsedMarkerAt(doc.findBlockByNumber(1), 0));
            }
        }
    }
}

SCENARIO("Italic, strikethrough, and inline code each render from their literal markers", "[markdown_highlighter]")
{
    GIVEN("a document using *italic*, ~~struck~~, and `code`")
    {
        QTextDocument doc;
        doc.setPlainText("*it* ~~no~~ `x`");

        WHEN("a MarkdownHighlighter is attached")
        {
            MarkdownHighlighter highlighter(&doc);
            highlightNow(highlighter);
            const QTextBlock block = doc.begin();

            THEN("the text between single asterisks is italic")
            {
                CHECK(isItalicAt(block, 1));
                CHECK(isItalicAt(block, 2));
            }

            THEN("the text between double tildes is struck out")
            {
                CHECK(formatAt(block, 7).fontStrikeOut());
                CHECK(formatAt(block, 8).fontStrikeOut());
            }

            THEN("the text between backticks is monospaced")
            {
                CHECK(formatAt(block, 13).fontFamilies().toStringList().contains(QStringLiteral("monospace")));
            }
        }
    }
}

SCENARIO("A [text](url) link shows its display text as the link at rest, and flips the accent to the URL once revealed",
         "[markdown_highlighter]")
{
    // Buffer:  s  e  e     [  t  h  e     s  i  t  e  ]  (  h  t  t  p  s  :  /  / ...
    // Index:   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 ...
    // "the site" is the display text (5..12), "[" at 4, "]" at 13, "(" at 14,
    // the url "https://example.com" runs 15..33, ")" at 34. Position 20 ("/")
    // sits inside the url.
    //
    // At rest a link is collapsed to just its display text, so the display text
    // IS the link: accent blue + underline, the one thing the reader sees and
    // clicks. Only once the cursor enters the link — revealing the raw
    // "[...](url)" for editing — does the styling flip: the display text goes
    // dim and the URL, now visible, takes the accent, because at that point the
    // URL is the real target being edited.
    GIVEN("a document with a link embedded in a sentence")
    {
        QTextDocument doc;
        doc.setPlainText("see [the site](https://example.com) now");

        WHEN("a MarkdownHighlighter is attached with the cursor elsewhere")
        {
            MarkdownHighlighter highlighter(&doc);
            highlighter.setCursorBlockNumber(-1);
            highlightNow(highlighter);
            const QTextBlock block = doc.begin();

            THEN("the buffer still holds the literal markdown, brackets and all")
            {
                CHECK(doc.toPlainText() == QStringLiteral("see [the site](https://example.com) now"));
            }

            THEN("the display text is the link — accent blue and underlined")
            {
                CHECK(isLinkTextAt(block, 5));
                CHECK(isLinkTextAt(block, 12));
            }

            THEN("the [ ]( url ) punctuation AND the url collapse out of the rendered line")
            {
                CHECK(isCollapsedMarkerAt(block, 4));  // "["
                CHECK(isCollapsedMarkerAt(block, 13)); // "]"
                CHECK(isCollapsedMarkerAt(block, 14)); // "("
                CHECK(isCollapsedMarkerAt(block, 20)); // inside the url
                CHECK(isCollapsedMarkerAt(block, 34)); // ")"
            }

            THEN("plain text on either side of the link is left unstyled")
            {
                CHECK_FALSE(isLinkTextAt(block, 0));
                CHECK_FALSE(isLinkTextAt(block, 37));
            }
        }

        WHEN("the cursor is actually touching the link")
        {
            // setCursorPosition, NOT setCursorBlockNumber: links reveal based
            // on the exact cursor position touching their own span, not merely
            // being on the same block/line — see MarkdownHighlighter::
            // setCursorPosition's own comment. Position 6 sits inside "the
            // site", well within the link's [4..36) span.
            MarkdownHighlighter highlighter(&doc);
            highlighter.setCursorPosition(6);
            highlightNow(highlighter);
            const QTextBlock block = doc.begin();

            THEN("the punctuation reveals, dimmed, so the url can be seen and edited")
            {
                CHECK(isRevealedMarkerAt(block, 4));  // "["
                CHECK(isRevealedMarkerAt(block, 14)); // "("
            }

            THEN("the accent flips to the url — now the blue, underlined link")
            {
                CHECK(isLinkTextAt(block, 20)); // "/" inside the url
            }

            THEN("the display text goes dim, no longer styled as the link")
            {
                CHECK(isDimTextAt(block, 5));
                CHECK_FALSE(isLinkTextAt(block, 5));
            }
        }

        // Regression test (bug found live): a link used to stay expanded to
        // its raw "[text](url)" as long as the cursor was ANYWHERE on the same
        // line — several words past it, or on the trailing space auto-linking
        // leaves behind after converting a typed URL — because reveal was
        // block-level, not span-level. It must collapse the instant the cursor
        // leaves the link's own span, even while still on the same line.
        WHEN("the cursor is on the same line but has moved past the link — e.g. into the space after it")
        {
            MarkdownHighlighter highlighter(&doc);
            // Position 37 is inside "now", well past the link's [4..36) span,
            // on the same line/block.
            highlighter.setCursorPosition(37);
            highlightNow(highlighter);
            const QTextBlock block = doc.begin();

            THEN("the [ ]( url ) punctuation AND the url collapse again — being on the same line is not enough")
            {
                CHECK(isCollapsedMarkerAt(block, 4));  // "["
                CHECK(isCollapsedMarkerAt(block, 14)); // "("
                CHECK(isCollapsedMarkerAt(block, 20)); // inside the url
            }

            THEN("the display text is back to being the link — accent blue and underlined")
            {
                CHECK(isLinkTextAt(block, 5));
            }
        }
    }
}

SCENARIO("A revealed link collapses back to its at-rest styling when the cursor leaves entirely — e.g. the window loses focus",
         "[markdown_highlighter]")
{
    // The editor pushes cursorPosition -1 to the highlighter whenever the note
    // should show as finished prose rather than raw markdown: a never-edited
    // note, and — the case this documents — a note whose window is no longer the
    // app's focused one (NoteWindow.qml's syncMarkerReveal gates on
    // appHasFocus). So a link left expanded under the cursor must NOT stay
    // expanded once focus goes to another window: it collapses like any at-rest
    // link, its display text back to the accent link and its url hidden again.
    GIVEN("a link that is revealed because the cursor is sitting inside it")
    {
        QTextDocument doc;
        doc.setPlainText("see [the site](https://example.com) now");

        MarkdownHighlighter highlighter(&doc);
        highlighter.setCursorPosition(6); // inside "the site"
        highlightNow(highlighter);
        const QTextBlock block = doc.begin();

        // Precondition: while touched, the accent is on the url and the display
        // text is dim (the revealed-state styling).
        REQUIRE(isLinkTextAt(block, 20));  // url is the link
        REQUIRE(isDimTextAt(block, 5));    // display text dimmed

        WHEN("the window loses focus, so the editor pushes cursorPosition -1")
        {
            highlighter.setCursorPosition(-1);
            highlightNow(highlighter);

            THEN("the display text is the link again — accent blue and underlined")
            {
                CHECK(isLinkTextAt(block, 5));
                CHECK(isLinkTextAt(block, 12));
            }

            THEN("the url and its punctuation collapse back out of the rendered line")
            {
                CHECK(isCollapsedMarkerAt(block, 14)); // "("
                CHECK(isCollapsedMarkerAt(block, 20)); // inside the url
            }
        }
    }
}

SCENARIO("An image's ![alt](src) is not mistaken for a link", "[markdown_highlighter]")
{
    GIVEN("a document whose line is an image, not a link")
    {
        QTextDocument doc;
        doc.setPlainText("![a picture](img/x.png)");

        WHEN("a MarkdownHighlighter is attached")
        {
            MarkdownHighlighter highlighter(&doc);
            highlighter.setCursorBlockNumber(-1);
            highlightNow(highlighter);
            const QTextBlock block = doc.begin();

            THEN("the alt text is NOT styled as a link — the ! prefix marks it an image")
            {
                CHECK_FALSE(isLinkTextAt(block, 2)); // 'a' of "a picture"
            }
        }
    }
}

SCENARIO("A whole-line image collapses to a tall reserved band off the cursor line", "[markdown_highlighter]")
{
    GIVEN("a line that is nothing but an image")
    {
        QTextDocument doc;
        doc.setPlainText("![a cat](img/cat.png)");

        WHEN("a MarkdownHighlighter is attached with the cursor elsewhere")
        {
            MarkdownHighlighter highlighter(&doc);
            highlighter.setCursorBlockNumber(-1);
            highlightNow(highlighter);
            const QTextBlock block = doc.begin();

            THEN("the buffer still holds the literal image markdown")
            {
                CHECK(doc.toPlainText() == QStringLiteral("![a cat](img/cat.png)"));
            }

            THEN("the line becomes a transparent, tall reserved band for the picture")
            {
                CHECK(isImageBandAt(block, 0));
                CHECK(isImageBandAt(block, 10));
            }
        }

        WHEN("the cursor is actually touching the image")
        {
            // setCursorPosition, NOT setCursorBlockNumber: like links, images
            // reveal based on the exact cursor position touching their own
            // span, not merely being on the same line — see
            // MarkdownHighlighter::setCursorPosition's own comment. Position
            // 10 sits inside "img/cat.png".
            MarkdownHighlighter highlighter(&doc);
            highlighter.setCursorPosition(10);
            highlightNow(highlighter);
            const QTextBlock block = doc.begin();

            THEN("the raw markdown shows at normal size so the src can be edited")
            {
                CHECK_FALSE(isImageBandAt(block, 0));
            }
        }

        // Regression test (bug found live): after inserting a fresh image
        // placeholder, the cursor used to land exactly at the end of its own
        // line — "touching" it by the old, block-level definition — which kept
        // the raw "![]()" text showing instead of the picture/placeholder
        // overlay ("the image placeholder is now invisible"). Being on the
        // same line is not enough; only the image's own span counts.
        WHEN("the cursor is on the image's line but past its own span — e.g. on a trailing blank")
        {
            QTextDocument trailing;
            trailing.setPlainText(QStringLiteral("![a cat](img/cat.png) "));
            MarkdownHighlighter highlighter(&trailing);
            // The image's own span is [0..21) (position 21, right after ")",
            // is its inclusive touching boundary); position 22 is past the
            // trailing space that follows it — same line, not touching.
            highlighter.setCursorPosition(22);
            highlightNow(highlighter);
            const QTextBlock block = trailing.begin();

            THEN("the image still collapses to its reserved band")
            {
                CHECK(isImageBandAt(block, 0));
            }
        }
    }
}

SCENARIO("An empty-src image reserves only a short strip, not the tall picture band", "[markdown_highlighter]")
{
    GIVEN("a filled image and an empty placeholder image")
    {
        QTextDocument filled;
        filled.setPlainText("![c](img/c.png)");
        QTextDocument empty;
        empty.setPlainText("![]()");

        WHEN("both are highlighted with the cursor elsewhere")
        {
            MarkdownHighlighter filledHl(&filled);
            filledHl.setCursorBlockNumber(-1);
            highlightNow(filledHl);
            MarkdownHighlighter emptyHl(&empty);
            emptyHl.setCursorBlockNumber(-1);
            highlightNow(emptyHl);

            const qreal filledBand = reservedPointSizeAt(filled.begin(), 0);
            const qreal emptyBand = reservedPointSizeAt(empty.begin(), 0);

            THEN("both reserve a transparent band, but the empty one is much shorter")
            {
                CHECK(filledBand > 0.0);
                CHECK(emptyBand > 0.0);
                CHECK(emptyBand < filledBand);
                // The filled band is the tall picture band; the empty one is
                // just a button-height strip.
                CHECK(isImageBandAt(filled.begin(), 0));
                CHECK_FALSE(isImageBandAt(empty.begin(), 0));
            }
        }
    }
}

SCENARIO("An image sharing its line with other text is not turned into a band", "[markdown_highlighter]")
{
    GIVEN("an image embedded mid-sentence rather than alone on its line")
    {
        QTextDocument doc;
        doc.setPlainText("see ![x](img/x.png) here");

        WHEN("a MarkdownHighlighter is attached")
        {
            MarkdownHighlighter highlighter(&doc);
            highlighter.setCursorBlockNumber(-1);
            highlightNow(highlighter);
            const QTextBlock block = doc.begin();

            THEN("nothing on the line is reserved as an image band — only whole-line images render")
            {
                CHECK_FALSE(isImageBandAt(block, 0));
                CHECK_FALSE(isImageBandAt(block, 6));
            }
        }
    }
}

SCENARIO("A single asterisk pair inside a bold span is not mistaken for italic", "[markdown_highlighter]")
{
    GIVEN("a document containing **bold**")
    {
        QTextDocument doc;
        doc.setPlainText("**bold**");

        WHEN("a MarkdownHighlighter is attached")
        {
            MarkdownHighlighter highlighter(&doc);
            highlightNow(highlighter);
            const QTextBlock block = doc.begin();

            THEN("the word renders bold and NOT italic — the italic rule must not claim the inner *")
            {
                for (int i = 2; i < 6; ++i) {
                    CHECK(isBoldAt(block, i));
                    CHECK_FALSE(isItalicAt(block, i));
                }
            }
        }
    }
}

SCENARIO("Emphasis nested inside bold renders both, rather than one clobbering the other", "[markdown_highlighter]")
{
    GIVEN("a document containing **bold *and italic* done**")
    {
        QTextDocument doc;
        doc.setPlainText("**bold *and italic* done**");

        WHEN("a MarkdownHighlighter is attached")
        {
            MarkdownHighlighter highlighter(&doc);
            highlightNow(highlighter);
            const QTextBlock block = doc.begin();
            const int italicStart = block.text().indexOf(QStringLiteral("and italic"));

            THEN("the nested span is bold AND italic at once")
            {
                CHECK(isBoldAt(block, italicStart));
                CHECK(isItalicAt(block, italicStart));
            }

            THEN("the surrounding bold text stays bold but not italic")
            {
                CHECK(isBoldAt(block, 2));
                CHECK_FALSE(isItalicAt(block, 2));
            }
        }
    }
}

SCENARIO("Markdown punctuation inside an inline code span stays literal", "[markdown_highlighter]")
{
    GIVEN("a document containing `**not bold**` inside backticks")
    {
        QTextDocument doc;
        doc.setPlainText("`**not bold**`");

        WHEN("a MarkdownHighlighter is attached")
        {
            MarkdownHighlighter highlighter(&doc);
            highlightNow(highlighter);
            const QTextBlock block = doc.begin();

            THEN("nothing inside the code span is rendered bold")
            {
                for (int i = 1; i < 13; ++i)
                    CHECK_FALSE(isBoldAt(block, i));
            }
        }
    }
}

SCENARIO("Headings render bold and larger, sized by their level", "[markdown_highlighter]")
{
    GIVEN("a document with an H1, an H2, and an H3")
    {
        QTextDocument doc;
        doc.setPlainText("# One\n## Two\n### Three");

        WHEN("a MarkdownHighlighter is attached")
        {
            MarkdownHighlighter highlighter(&doc);
            highlightNow(highlighter);

            const QTextBlock h1 = doc.findBlockByNumber(0);
            const QTextBlock h2 = doc.findBlockByNumber(1);
            const QTextBlock h3 = doc.findBlockByNumber(2);

            THEN("heading text is bold at every level")
            {
                CHECK(isBoldAt(h1, 2));
                CHECK(isBoldAt(h2, 3));
                CHECK(isBoldAt(h3, 4));
            }

            THEN("H1 and H2 get the same relative size bump the pre-rewrite editor applied, H3 none")
            {
                CHECK(formatAt(h1, 2).property(QTextFormat::FontSizeAdjustment).toInt() == 3);
                CHECK(formatAt(h2, 3).property(QTextFormat::FontSizeAdjustment).toInt() == 2);
                CHECK(formatAt(h3, 4).property(QTextFormat::FontSizeAdjustment).toInt() == 0);
            }

            THEN("the # markers stay in the buffer but collapse out of the rendered line")
            {
                CHECK(doc.toPlainText().startsWith(QStringLiteral("# One")));
                CHECK(isCollapsedMarkerAt(h1, 0));
                // The space after the hash goes too, so the heading's text
                // starts at the left margin rather than indented by its own
                // syntax.
                CHECK(isCollapsedMarkerAt(h1, 1));
            }
        }
    }
}

SCENARIO("A hash with no following space is not a heading", "[markdown_highlighter]")
{
    GIVEN("a document containing a #hashtag")
    {
        QTextDocument doc;
        doc.setPlainText("#hashtag");

        WHEN("a MarkdownHighlighter is attached")
        {
            MarkdownHighlighter highlighter(&doc);
            highlightNow(highlighter);

            THEN("it is left as ordinary text, per GFM's whitespace requirement")
            {
                CHECK_FALSE(isBoldAt(doc.begin(), 1));
            }
        }
    }
}

SCENARIO("A bullet's marker holds its space open for a drawn bullet", "[markdown_highlighter]")
{
    GIVEN("a bullet list written with an asterisk marker")
    {
        QTextDocument doc;
        doc.setPlainText("* first\n* second");

        WHEN("a MarkdownHighlighter is attached")
        {
            MarkdownHighlighter highlighter(&doc);
            highlightNow(highlighter);
            const QTextBlock first = doc.findBlockByNumber(0);

            THEN("the marker itself is invisible but still occupies its slot")
            {
                // Reserved, NOT collapsed: NoteWindow.qml draws the real bullet
                // into exactly this character's rect, and a zero-width slot
                // would put that bullet on top of the item's own text.
                CHECK(isReservedMarkerAt(first, 0));
                CHECK_FALSE(isCollapsedMarkerAt(first, 0));
            }

            THEN("the two lines' markers do not pair up into an italic span")
            {
                CHECK_FALSE(isItalicAt(first, 2));
                CHECK_FALSE(isItalicAt(doc.findBlockByNumber(1), 2));
            }
        }
    }
}

SCENARIO("A checkbox collapses to a single drawn control, and a numbered marker stays text", "[markdown_highlighter]")
{
    GIVEN("a numbered item and both checkbox states, written in literal GFM")
    {
        QTextDocument doc;
        doc.setPlainText("1. one\n- [ ] todo\n- [x] done");

        WHEN("a MarkdownHighlighter is attached")
        {
            MarkdownHighlighter highlighter(&doc);
            highlightNow(highlighter);

            THEN("the buffer holds literal GFM checkbox syntax, not ☐/☑ glyphs")
            {
                CHECK(doc.toPlainText().contains(QStringLiteral("- [ ] todo")));
                CHECK(doc.toPlainText().contains(QStringLiteral("- [x] done")));
            }

            THEN("the numbered marker is left as ordinary, text-coloured content")
            {
                // A number is not punctuation to read past — it's what a
                // rendered list would show anyway, so it is neither dimmed,
                // collapsed, nor replaced by anything drawn.
                const QTextBlock numbered = doc.findBlockByNumber(0);
                CHECK_FALSE(isRevealedMarkerAt(numbered, 0));
                CHECK_FALSE(isCollapsedMarkerAt(numbered, 0));
                CHECK_FALSE(isReservedMarkerAt(numbered, 0));
            }

            THEN("a checkbox keeps ONE slot open and collapses the rest of its syntax")
            {
                // "- [ ] todo": the "[" at index 2 is the reserved slot the
                // drawn checkbox lands in; "- " before it and " ]" after it
                // disappear entirely, leaving a control exactly one column wide.
                for (const int blockNumber : {1, 2}) {
                    const QTextBlock item = doc.findBlockByNumber(blockNumber);
                    CHECK(isCollapsedMarkerAt(item, 0));
                    CHECK(isCollapsedMarkerAt(item, 1));
                    CHECK(isReservedMarkerAt(item, 2));
                    CHECK(isCollapsedMarkerAt(item, 3));
                    CHECK(isCollapsedMarkerAt(item, 4));
                }
            }

            THEN("the item text after a checkbox marker is left unstyled")
            {
                CHECK_FALSE(isRevealedMarkerAt(doc.findBlockByNumber(1), 6));
            }

            THEN("a checkbox's slot is wider than a bullet's, because its control is bigger")
            {
                // Not an accident of which character each one reserves, and not
                // free to drift: NoteWindow.qml draws the checkbox at a larger
                // fraction of the line height than the bullet's dot (feature
                // request: "the display for checkboxes should be a little
                // bigger"), and centres it in exactly this slot. The slot starts
                // at the editor's left edge, so a control wider than its slot
                // does not overhang — its left half is clipped off. This is the
                // C++ half of that pairing; NoteWindow.qml's checkboxSize is the
                // other, and it clamps to the slot so the two can never disagree
                // badly enough to draw half a checkbox.
                QTextDocument mixed;
                mixed.setPlainText("- bullet\n- [ ] todo");
                MarkdownHighlighter mixedHighlighter(&mixed);
                highlightNow(mixedHighlighter);

                const qreal bulletSlot = slotPercentAt(mixed.findBlockByNumber(0), 0);
                const qreal checkboxSlot = slotPercentAt(mixed.findBlockByNumber(1), 2);
                CHECK(bulletSlot > 100.0);
                CHECK(checkboxSlot > bulletSlot);
            }
        }
    }
}

SCENARIO("Collapsing a marker never sends Qt hunting through every font on the system",
         "[markdown_highlighter]")
{
    // A performance guarantee, pinned by a test because the cost was invisible
    // in the output and enormous in practice: this exact format froze the whole
    // UI for ~5.8 SECONDS the first time it was applied in a session, which is
    // what a user saw as "the first time you add a checkbox list or table, the
    // notes ui is frozen" (a checkbox and a table's delimiter row are the first
    // two things most notes collapse). A collapsed marker's point size rounds to
    // a pixel size of zero, at which no font can produce a glyph — so Qt decided
    // every character was missing and walked the entire fallback chain, loading
    // every font family installed, before giving up. QFontCache then held the
    // answer, which is why only the FIRST one in a session was slow and every
    // one after it was instant.
    //
    // Nothing about the rendered result depends on this flag; the marker is
    // transparent and zero-width either way. Delete it and the tests still all
    // pass — they just take six seconds longer, and so does the app.
    GIVEN("markers that get collapsed: a checkbox's syntax and a table's delimiter row")
    {
        QTextDocument doc;
        doc.setPlainText("- [ ] todo\n\n| a | b |\n| --- | --- |\n| 1 | 2 |");

        WHEN("a MarkdownHighlighter is attached")
        {
            MarkdownHighlighter highlighter(&doc);
            highlightNow(highlighter);

            THEN("the collapsed characters ask Qt not to look for a fallback font")
            {
                const QTextBlock checkbox = doc.findBlockByNumber(0);
                REQUIRE(isCollapsedMarkerAt(checkbox, 0));
                CHECK(hasFontFallbackDisabledAt(checkbox, 0));
                CHECK(hasFontFallbackDisabledAt(checkbox, 3));

                const QTextBlock delimiter = doc.findBlockByNumber(3);
                REQUIRE(isCollapsedMarkerAt(delimiter, 0));
                CHECK(hasFontFallbackDisabledAt(delimiter, 0));
            }

            THEN("the slot held open for a drawn control still keeps its real size")
            {
                // The opt-out belongs to collapsed text only. A reserved slot is
                // invisible but full-width, so its character is still measured
                // — and still resolves through the ordinary font path.
                CHECK_FALSE(hasFontFallbackDisabledAt(doc.findBlockByNumber(0), 2));
            }
        }
    }
}

SCENARIO("Editing a block restyles it without any timer or polling", "[markdown_highlighter]")
{
    GIVEN("an attached highlighter over plain text with no markdown in it")
    {
        QTextDocument doc;
        // QTextDocument creates its document layout lazily, and
        // QTextDocumentPrivate::finishEdit() only emits contentsChange when one
        // exists — so on a bare document an edit fires *no* signal and a
        // QSyntaxHighlighter silently never runs. A real QML TextArea always
        // has a layout, so this line is the test harness catching up to the
        // app, not a workaround. Without it this scenario would pass only under
        // an explicit rehighlight() and would prove nothing about event-driven
        // restyling, which is the entire point of it.
        doc.documentLayout();
        doc.setPlainText("plain");
        MarkdownHighlighter highlighter(&doc);
        highlightNow(highlighter);
        REQUIRE_FALSE(isBoldAt(doc.begin(), 0));

        WHEN("bold markers are typed around the word, as a cursor insertion")
        {
            // No rehighlight() call here on purpose. QSyntaxHighlighter
            // rehighlights off QTextDocument::contentsChange, which a cursor
            // edit emits synchronously — the same path a real keystroke in the
            // editor takes. That these assertions pass with no manual trigger
            // and no timer anywhere is precisely the repo's no-polling
            // guarantee (CLAUDE.md) holding for the new highlighter: styling is
            // driven by real edit events, so an idle note costs nothing.
            QTextCursor cursor(&doc);
            cursor.insertText(QStringLiteral("**"));
            cursor.movePosition(QTextCursor::EndOfBlock);
            cursor.insertText(QStringLiteral("**"));
            REQUIRE(doc.toPlainText() == QStringLiteral("**plain**"));

            THEN("the newly-bold span is styled immediately, driven only by the edit event")
            {
                CHECK(isBoldAt(doc.begin(), 2));
            }

            THEN("the markers that created it collapse out of the rendered line")
            {
                CHECK(isCollapsedMarkerAt(doc.begin(), 0));
            }
        }
    }
}

SCENARIO("A pipe table renders as a grid without rewriting a byte of it", "[markdown_highlighter]")
{
    GIVEN("a ragged table — its cells are not padded to a common width in the file")
    {
        //  0123456789...
        // "| Name | Status |"   " Name " is narrower than " alpha "
        // "| --- | --- |"
        // "| alpha | done |"    " done "  is narrower than " Status "
        const QString source = QStringLiteral("| Name | Status |\n| --- | --- |\n| alpha | done |");
        QTextDocument doc;
        doc.setPlainText(source);

        WHEN("a MarkdownHighlighter is attached")
        {
            MarkdownHighlighter highlighter(&doc);
            highlightNow(highlighter);

            const QTextBlock header = doc.findBlockByNumber(0);
            const QTextBlock delimiter = doc.findBlockByNumber(1);
            const QTextBlock row = doc.findBlockByNumber(2);

            THEN("the file's own text is completely untouched — alignment is a rendering effect only")
            {
                CHECK(doc.toPlainText() == source);
            }

            THEN("every pipe collapses, so each column boundary is one exact x for the grid to be drawn at")
            {
                CHECK(isCollapsedMarkerAt(header, 0));
                CHECK(isCollapsedMarkerAt(header, 7));
                CHECK(isCollapsedMarkerAt(header, 16));
            }

            THEN("the whole |---|---| row collapses — a drawn header rule replaces it")
            {
                for (int i = 0; i < delimiter.text().size(); ++i)
                    CHECK(isCollapsedMarkerAt(delimiter, i));
            }

            THEN("every cell in a column ends up rendered at exactly the same width — which IS the alignment")
            {
                // Asserted as a width equality rather than "this specific cell
                // gets padded", because which cell is the widest depends on the
                // font: " Name " actually out-measures " alpha " in the default
                // font (a capital N and an m beat five narrow lowercase
                // letters), so hard-coding a winner tests the test's font, not
                // the highlighter.
                const QFontMetricsF metrics(doc.defaultFont());
                const auto renderedWidth = [&](const QTextBlock &block, int start, int end) {
                    return metrics.horizontalAdvance(block.text().mid(start, end - start))
                        + extraWidthAt(block, end - 1);
                };

                // Column 0: " Name " against " alpha ".
                CHECK(renderedWidth(header, 1, 7) == Catch::Approx(renderedWidth(row, 1, 8)));
                // Column 1: " Status " against " done ".
                CHECK(renderedWidth(header, 8, 16) == Catch::Approx(renderedWidth(row, 9, 15)));
            }

            THEN("cells are only ever widened, never squeezed")
            {
                CHECK(extraWidthAt(header, 6) >= 0.0);
                CHECK(extraWidthAt(row, 7) >= 0.0);
                CHECK(extraWidthAt(header, 15) >= 0.0);
                CHECK(extraWidthAt(row, 14) >= 0.0);
            }
        }
    }
}

SCENARIO("A line that merely starts with a pipe is not treated as a table", "[markdown_highlighter]")
{
    GIVEN("prose beginning with a pipe, and no delimiter row under it")
    {
        QTextDocument doc;
        doc.setPlainText("| this is just prose");

        WHEN("a MarkdownHighlighter is attached")
        {
            MarkdownHighlighter highlighter(&doc);
            highlightNow(highlighter);

            THEN("its pipe stays visible — GFM needs a delimiter row, and a table half-typed must not vanish")
            {
                CHECK_FALSE(isCollapsedMarkerAt(doc.begin(), 0));
            }
        }
    }
}

SCENARIO("Undo survives the restyling that every cursor move triggers", "[markdown_highlighter]")
{
    GIVEN("an attached highlighter over a document with an undo history")
    {
        QTextDocument doc;
        doc.documentLayout();
        // Seed the text with undo off, mirroring DocumentHandler::setMarkdown,
        // so the baseline itself is not an undoable step.
        doc.setUndoRedoEnabled(false);
        doc.setPlainText("start\nsecond");
        doc.setUndoRedoEnabled(true);

        MarkdownHighlighter highlighter(&doc);
        highlighter.setCursorBlockNumber(0);
        highlightNow(highlighter);

        WHEN("edits are interleaved with the cursor moving between lines")
        {
            // This is the scenario that matters. Revealing markers means
            // rehighlighting a block on every cursor move — and an earlier
            // attempt at hanging indents proved that touching a BLOCK format
            // from highlightBlock() live-locks undo outright (ten undos in a
            // row reverted nothing). This highlighter only ever sets CHARACTER
            // formats, which is what keeps undo intact; the assertion below is
            // what would catch a regression back to that class of bug.
            QTextCursor cursor(&doc);
            for (int i = 0; i < 5; ++i) {
                cursor.movePosition(QTextCursor::End);
                cursor.insertText(QStringLiteral(" *x%1*").arg(i));
                highlighter.setCursorBlockNumber(1);
                highlighter.setCursorBlockNumber(0);
            }
            REQUIRE(doc.toPlainText() != QStringLiteral("start\nsecond"));

            THEN("undoing walks the document all the way back to where it started, in a finite number of steps")
            {
                // Bounded deliberately. The bug this guards against is a
                // LIVE-LOCK — undo reverting a highlighter-applied format,
                // which retriggers the highlighter, which reapplies it — so an
                // unbounded `while (doc.isUndoAvailable())` here would not fail
                // on a regression, it would hang the test run forever (this
                // cost 426 minutes at 99% CPU once already). A cap turns that
                // same regression into an ordinary red test.
                int undos = 0;
                while (doc.isUndoAvailable() && undos < 100) {
                    doc.undo();
                    ++undos;
                }
                CHECK(undos < 100);
                CHECK(doc.toPlainText() == QStringLiteral("start\nsecond"));
            }
        }
    }
}

SCENARIO("Highlighting never writes a block format", "[markdown_highlighter]")
{
    // Regression for the root cause of the live-lock in the scenario above,
    // asserted directly rather than only through its symptom.
    //
    // A QTextBlockFormat is undoable document state, and the highlighter re-runs
    // on every contentsChange — including the format-only ones a block-format
    // write itself produces. So a block format written from highlightBlock() is
    // reapplied every time the user undoes it: Ctrl+Z live-locks and never
    // reaches the text (measured at 200+ undos with the typed text never
    // reverting). It is reachable in production through a font change, which
    // calls rehighlight() with undo enabled and no user edit to attach to.
    //
    // Hanging indent — the one feature that wants a block format — is therefore
    // applied from the list EDIT operations and the load path instead (see
    // TextFormatting), never from here. The highlighter's own output goes to the
    // block's LAYOUT, which is not undoable, which is exactly why it is safe.
    GIVEN("markdown exercising every rule the highlighter has: lists, headings and emphasis")
    {
        QTextDocument doc;
        doc.documentLayout();
        doc.setPlainText("# heading\n- bullet\n- [ ] checkbox\n1. numbered\n**bold** and *italic*");

        WHEN("a highlighter styles all of it")
        {
            MarkdownHighlighter highlighter(&doc);
            highlighter.setCursorBlockNumber(0);
            highlightNow(highlighter);

            THEN("every block's format is left at its defaults — no indent, no margins")
            {
                for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
                    const QTextBlockFormat format = b.blockFormat();
                    CHECK(format.leftMargin() == 0);
                    CHECK(format.textIndent() == 0);
                }
            }
        }
    }
}

// --- Find/replace match highlighting -------------------------------------
// Why these live in the MARKDOWN highlighter's tests: two QSyntaxHighlighters
// on one document erase each other's formats rather than composing, so the
// find/replace panel's match highlighting has to be applied by the same class
// that paints markdown, or one of the two would win and the other would vanish.
// See MarkdownHighlighter's header.
//
// Note what none of these scenarios do: rehighlight() after setSearchMatches().
// That is the point — setSearchMatches() repaints the affected lines itself, so
// the highlight appears the moment the panel pushes it, with no edit and no
// document-wide pass to trigger it.

SCENARIO("Every occurrence of the search term is highlighted at once", "[markdown_highlighter]")
{
    GIVEN("a line containing the same word twice, already styled")
    {
        QTextDocument doc;
        doc.setPlainText("find me and find me again");
        MarkdownHighlighter highlighter(&doc);
        highlightNow(highlighter);

        WHEN("the find panel pushes both matches, with the second one current")
        {
            highlighter.setSearchMatches({{0, 4}, {12, 16}}, 1);
            const QTextBlock block = doc.begin();

            THEN("both occurrences are highlighted, not only the current one")
            {
                for (int i = 0; i < 4; ++i)
                    CHECK(isSearchHighlightedAt(block, i));
                for (int i = 12; i < 16; ++i)
                    CHECK(isSearchHighlightedAt(block, i));
            }

            THEN("the current match is highlighted more strongly than the rest")
            {
                CHECK(searchHighlightAlphaAt(block, 12) > searchHighlightAlphaAt(block, 0));
            }

            THEN("the text between and around the matches is left alone")
            {
                CHECK_FALSE(isSearchHighlightedAt(block, 4));
                CHECK_FALSE(isSearchHighlightedAt(block, 8));
                CHECK_FALSE(isSearchHighlightedAt(block, 24));
            }
        }

        WHEN("the user steps to the next match")
        {
            highlighter.setSearchMatches({{0, 4}, {12, 16}}, 1);
            highlighter.setSearchMatches({{0, 4}, {12, 16}}, 0);
            const QTextBlock block = doc.begin();

            THEN("the strong highlight moves with it, and the one it left drops back")
            {
                CHECK(searchHighlightAlphaAt(block, 0) > searchHighlightAlphaAt(block, 12));
            }
        }

        WHEN("no match is current (the term matches nothing the panel is pointing at)")
        {
            highlighter.setSearchMatches({{0, 4}, {12, 16}}, -1);
            const QTextBlock block = doc.begin();

            THEN("every match still gets the plain highlight")
            {
                CHECK(isSearchHighlightedAt(block, 0));
                CHECK(isSearchHighlightedAt(block, 12));
                CHECK(searchHighlightAlphaAt(block, 0) == searchHighlightAlphaAt(block, 12));
            }
        }
    }
}

SCENARIO("Closing the find panel takes every match highlight back off", "[markdown_highlighter]")
{
    GIVEN("a document with matches currently highlighted")
    {
        QTextDocument doc;
        doc.setPlainText("find me and find me again");
        MarkdownHighlighter highlighter(&doc);
        highlightNow(highlighter);
        highlighter.setSearchMatches({{0, 4}, {12, 16}}, 0);
        REQUIRE(isSearchHighlightedAt(doc.begin(), 0));

        WHEN("the panel clears them")
        {
            highlighter.setSearchMatches({}, -1);
            const QTextBlock block = doc.begin();

            THEN("nothing is left highlighted")
            {
                CHECK_FALSE(isSearchHighlightedAt(block, 0));
                CHECK_FALSE(isSearchHighlightedAt(block, 12));
            }
        }
    }
}

SCENARIO("A highlighted match keeps the markdown styling underneath it", "[markdown_highlighter]")
{
    GIVEN("a bold word and a heading, both containing the search term")
    {
        QTextDocument doc;
        doc.setPlainText("say **find** here\n# find this");
        MarkdownHighlighter highlighter(&doc);
        highlightNow(highlighter);

        WHEN("the term is highlighted in both places")
        {
            // "find" inside the bold span, and "find" in the heading text.
            highlighter.setSearchMatches({{6, 10}, {20, 24}}, 0);
            const QTextBlock boldBlock = doc.begin();
            const QTextBlock headingBlock = doc.findBlockByNumber(1);

            THEN("the bold word is highlighted AND still bold")
            {
                CHECK(isSearchHighlightedAt(boldBlock, 6));
                CHECK(isBoldAt(boldBlock, 6));
            }

            THEN("the heading is highlighted AND still sized as a heading")
            {
                CHECK(isSearchHighlightedAt(headingBlock, 2));
                CHECK(formatAt(headingBlock, 2).intProperty(QTextFormat::FontSizeAdjustment) == 3);
            }
        }
    }
}

SCENARIO("A match spanning more than one line is highlighted on each of them", "[markdown_highlighter]")
{
    GIVEN("a two-line document")
    {
        QTextDocument doc;
        doc.setPlainText("alpha\nbeta");
        MarkdownHighlighter highlighter(&doc);
        highlightNow(highlighter);

        WHEN("a match runs from inside the first line into the second (a regex can)")
        {
            // "ha\nbe" — starts mid-"alpha", ends mid-"beta".
            highlighter.setSearchMatches({{3, 8}}, 0);
            const QTextBlock first = doc.begin();
            const QTextBlock second = doc.findBlockByNumber(1);

            THEN("the part on the first line is highlighted")
            {
                CHECK_FALSE(isSearchHighlightedAt(first, 2));
                CHECK(isSearchHighlightedAt(first, 3));
                CHECK(isSearchHighlightedAt(first, 4));
            }

            THEN("so is the part on the second, and no further")
            {
                CHECK(isSearchHighlightedAt(second, 0));
                CHECK(isSearchHighlightedAt(second, 1));
                CHECK_FALSE(isSearchHighlightedAt(second, 2));
            }
        }
    }
}

SCENARIO("A match inside a table cell is highlighted like any other", "[markdown_highlighter]")
{
    GIVEN("a pipe table — a line the markdown pass styles whole and returns early from")
    {
        QTextDocument doc;
        doc.documentLayout();
        doc.setPlainText("| a | find |\n|---|---|\n| b | c |");
        MarkdownHighlighter highlighter(&doc);
        highlightNow(highlighter);

        WHEN("the find panel pushes a match sitting in one of its cells")
        {
            highlighter.setSearchMatches({{6, 10}}, 0);
            const QTextBlock headerRow = doc.begin();

            THEN("the cell's text is highlighted, table row or not")
            {
                for (int i = 6; i < 10; ++i)
                    CHECK(isSearchHighlightedAt(headerRow, i));
            }

            THEN("the rest of the row is not")
            {
                CHECK_FALSE(isSearchHighlightedAt(headerRow, 2));
            }
        }
    }
}

// Code mode (feature request): a code note renders its buffer literally — in
// code, "**" is an operator and "#" starts a comment, so live-preview
// collapsing them hides real syntax. These document the gate that turns the
// markdown pass off, and — the part with the sharp edge — exactly what the
// gate does NOT cover.
SCENARIO("The markdown pass can be switched off, leaving the text rendered literally",
         "[markdown_highlighter]")
{
    GIVEN("a document with bold text and a heading")
    {
        QTextDocument doc;
        doc.setPlainText("say **bold** here\n# a heading");
        MarkdownHighlighter highlighter(&doc);
        highlightNow(highlighter);

        THEN("markdown decoration is on by default — an ordinary note is a markdown note")
        {
            CHECK(highlighter.markdownDecorationEnabled());
        }

        THEN("the markers collapse and the word renders bold, as normal")
        {
            const QTextBlock block = doc.begin();
            CHECK(isCollapsedMarkerAt(block, 4));
            CHECK(isBoldAt(block, 6));
        }

        WHEN("markdown decoration is switched off")
        {
            highlighter.setMarkdownDecorationEnabled(false);

            THEN("it reports itself as off")
            {
                CHECK_FALSE(highlighter.markdownDecorationEnabled());
            }

            THEN("the asterisks are no longer collapsed — every character is shown as typed")
            {
                const QTextBlock block = doc.begin();
                CHECK_FALSE(isCollapsedMarkerAt(block, 4));
                CHECK_FALSE(isCollapsedMarkerAt(block, 5));
            }

            THEN("the word between them is no longer styled bold")
            {
                CHECK_FALSE(isBoldAt(doc.begin(), 6));
            }

            THEN("the heading is no longer enlarged")
            {
                const QTextBlock headingBlock = doc.findBlockByNumber(1);
                CHECK(formatAt(headingBlock, 2).intProperty(QTextFormat::FontSizeAdjustment) == 0);
            }

            THEN("the buffer text itself is untouched — this only ever changed styling")
            {
                CHECK(doc.toPlainText() == "say **bold** here\n# a heading");
            }
        }

        WHEN("markdown decoration is switched off and then back on")
        {
            highlighter.setMarkdownDecorationEnabled(false);
            highlighter.setMarkdownDecorationEnabled(true);

            THEN("the formatting comes back without needing an edit to trigger it")
            {
                const QTextBlock block = doc.begin();
                CHECK(isCollapsedMarkerAt(block, 4));
                CHECK(isBoldAt(block, 6));
            }
        }
    }
}

SCENARIO("Find/replace highlighting keeps working with the markdown pass off",
         "[markdown_highlighter]")
{
    GIVEN("a code note — markdown decoration off — containing a search term twice")
    {
        QTextDocument doc;
        doc.setPlainText("int find = 1;\n// find it");
        MarkdownHighlighter highlighter(&doc);
        highlighter.setMarkdownDecorationEnabled(false);
        highlightNow(highlighter);

        WHEN("both occurrences are highlighted")
        {
            highlighter.setSearchMatches({{4, 8}, {17, 21}}, 0);

            THEN("both are still highlighted — searching a code note is exactly as useful")
            {
                // The gate covers the markdown pass alone. Skipping the whole
                // of highlightBlock() instead would silently break find in
                // every code note, which is why this is asserted separately
                // from the markdown assertions above.
                CHECK(isSearchHighlightedAt(doc.begin(), 4));
                CHECK(isSearchHighlightedAt(doc.findBlockByNumber(1), 3));
            }
        }
    }
}
