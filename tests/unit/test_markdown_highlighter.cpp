#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QTextBlock>
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
                // disappear entirely, leaving a control exactly one column
                // wide — the same width a bullet gets, so mixed lists line up.
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

            THEN("undoing walks the document all the way back to where it started")
            {
                while (doc.isUndoAvailable())
                    doc.undo();
                CHECK(doc.toPlainText() == QStringLiteral("start\nsecond"));
            }
        }
    }
}
