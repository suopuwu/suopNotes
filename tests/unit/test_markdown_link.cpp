#include <catch2/catch_test_macros.hpp>

#include "core/MarkdownLink.h"

// These document link recognition now that MarkdownHighlighter (which styles
// links) and DocumentHandler (which resolves the url under the cursor) share
// this one definition — the negative-lookbehind-on-'!' that tells a link apart
// from an image lives here only.

SCENARIO("urlAt returns the url only when the position is on a link's visible text",
         "[markdown_link]")
{
    GIVEN("a line with one link, 'see [the site](https://example.com) now'")
    {
        const QString line = QStringLiteral("see [the site](https://example.com) now");
        const int textStart = line.indexOf("the site");

        THEN("a position inside the visible text resolves to the url")
        {
            CHECK(MarkdownLink::urlAt(line, textStart) == "https://example.com");
            CHECK(MarkdownLink::urlAt(line, textStart + 3) == "https://example.com");
        }

        THEN("a position in the surrounding prose is not on a link")
        {
            CHECK(MarkdownLink::urlAt(line, 0).isEmpty());
            CHECK(MarkdownLink::urlAt(line, line.length() - 1).isEmpty());
        }

        THEN("a position in the collapsed '](url)' punctuation is not a hit target")
        {
            const int urlPos = line.indexOf("https");
            CHECK(MarkdownLink::urlAt(line, urlPos).isEmpty());
        }
    }

    GIVEN("an image, not a link")
    {
        const QString line = QStringLiteral("![alt](pic.png)");

        THEN("the negative lookbehind on '!' keeps it from matching as a link")
        {
            const int altPos = line.indexOf("alt");
            CHECK(MarkdownLink::urlAt(line, altPos).isEmpty());
        }
    }

    GIVEN("a half-typed link, '[text]()' with an empty url")
    {
        const QString line = QStringLiteral("[text]()");

        THEN("it is still a link; its (empty) url resolves to an empty string")
        {
            // Empty is both "not on a link" and "an empty url" here, so assert
            // via the pattern that it DID match the visible text at all.
            CHECK(MarkdownLink::pattern().match(line).hasMatch());
            CHECK(MarkdownLink::urlAt(line, line.indexOf("text")).isEmpty());
        }
    }
}
