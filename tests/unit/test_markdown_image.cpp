#include <catch2/catch_test_macros.hpp>

#include "core/MarkdownImage.h"

// These document what counts as a whole-line image now that MarkdownHighlighter
// (which reserves the band) and DocumentHandler (which reports where to draw
// into it) share this one parser instead of each keeping a private copy of the
// regex.

SCENARIO("A whole-line image is recognised and split into span, alt and src", "[markdown_image]")
{
    GIVEN("a bare image line")
    {
        const MarkdownImage::Match m = MarkdownImage::parse(QStringLiteral("![a cat](cat.png)"));

        THEN("it is valid and its span covers the whole '![...](...)'")
        {
            CHECK(m.isValid);
            CHECK(m.spanStart == 0);
            CHECK(m.spanLength == QStringLiteral("![a cat](cat.png)").length());
        }

        THEN("alt and src are captured")
        {
            CHECK(m.alt == "a cat");
            CHECK(m.src == "cat.png");
        }
    }

    GIVEN("an image padded with leading and trailing whitespace")
    {
        const MarkdownImage::Match m = MarkdownImage::parse(QStringLiteral("   ![x](y.png)   "));

        THEN("the span EXCLUDES the surrounding whitespace")
        {
            // The trailing-space-counts-as-touching bug is exactly what this
            // guards: the span starts at the '!' and ends at the ')'.
            CHECK(m.isValid);
            CHECK(m.spanStart == 3);
            CHECK(m.spanLength == QStringLiteral("![x](y.png)").length());
        }
    }

    GIVEN("an empty-src placeholder, '![]()' still being filled in")
    {
        const MarkdownImage::Match m = MarkdownImage::parse(QStringLiteral("![]()"));

        THEN("it is still a valid image with empty alt and src")
        {
            CHECK(m.isValid);
            CHECK(m.alt.isEmpty());
            CHECK(m.src.isEmpty());
        }
    }
}

SCENARIO("A line that is not a whole-line image is rejected", "[markdown_image]")
{
    GIVEN("a line with text around the image")
    {
        THEN("it is not a whole-line image")
        {
            CHECK_FALSE(MarkdownImage::parse(QStringLiteral("look ![x](y.png) here")).isValid);
        }
    }

    GIVEN("a link, not an image (no leading '!')")
    {
        THEN("it is not matched as an image")
        {
            CHECK_FALSE(MarkdownImage::parse(QStringLiteral("[text](url)")).isValid);
        }
    }

    GIVEN("ordinary prose")
    {
        THEN("it is not matched")
        {
            CHECK_FALSE(MarkdownImage::parse(QStringLiteral("just some words")).isValid);
        }
    }
}
