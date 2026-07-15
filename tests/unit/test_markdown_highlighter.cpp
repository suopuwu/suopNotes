#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QFont>
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

bool isDimmedMarkerAt(const QTextBlock &block, int position)
{
    return formatAt(block, position).foreground().color() == markerGray;
}

}

SCENARIO("Bold text renders bold while its ** markers stay in the buffer, dimmed", "[markdown_highlighter]")
{
    GIVEN("a document whose literal text contains **bold**")
    {
        QTextDocument doc;
        doc.setPlainText("say **bold** here");

        WHEN("a MarkdownHighlighter is attached")
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

            THEN("the ** markers are dimmed rather than hidden")
            {
                CHECK(isDimmedMarkerAt(block, 4));
                CHECK(isDimmedMarkerAt(block, 5));
                CHECK(isDimmedMarkerAt(block, 10));
                CHECK(isDimmedMarkerAt(block, 11));
            }

            THEN("surrounding plain text is left unstyled")
            {
                CHECK_FALSE(isBoldAt(block, 0));
                CHECK_FALSE(isBoldAt(block, 14));
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

            THEN("the # markers stay in the buffer, dimmed")
            {
                CHECK(doc.toPlainText().startsWith(QStringLiteral("# One")));
                CHECK(isDimmedMarkerAt(h1, 0));
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

SCENARIO("List markers are dimmed and never parsed as emphasis", "[markdown_highlighter]")
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

            THEN("the leading asterisks are dimmed markers")
            {
                CHECK(isDimmedMarkerAt(first, 0));
            }

            THEN("the two lines' markers do not pair up into an italic span")
            {
                CHECK_FALSE(isItalicAt(first, 2));
                CHECK_FALSE(isItalicAt(doc.findBlockByNumber(1), 2));
            }
        }
    }
}

SCENARIO("Numbered and checkbox list markers are dimmed as literal text", "[markdown_highlighter]")
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

            THEN("the numbered marker is dimmed")
            {
                CHECK(isDimmedMarkerAt(doc.findBlockByNumber(0), 0));
            }

            THEN("both checkbox markers are dimmed across their whole bracket span")
            {
                const QTextBlock unchecked = doc.findBlockByNumber(1);
                for (int i = 0; i < 5; ++i)
                    CHECK(isDimmedMarkerAt(unchecked, i));

                const QTextBlock checked = doc.findBlockByNumber(2);
                for (int i = 0; i < 5; ++i)
                    CHECK(isDimmedMarkerAt(checked, i));
            }

            THEN("the item text after a checkbox marker is left unstyled")
            {
                CHECK_FALSE(isDimmedMarkerAt(doc.findBlockByNumber(1), 6));
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

            THEN("the markers that created it are dimmed")
            {
                CHECK(isDimmedMarkerAt(doc.begin(), 0));
            }
        }
    }
}
