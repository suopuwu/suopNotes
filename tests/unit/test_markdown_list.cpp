#include <catch2/catch_test_macros.hpp>

#include "core/MarkdownList.h"

// These tests document what counts as a list marker now that the text is the
// only truth. The pre-rewrite editor tagged each block with a custom format
// property and had to keep that tag in sync with the text; here a line either
// starts with a marker or it doesn't, and there is no second source of truth to
// drift from.

SCENARIO("A bullet line is recognised in any of GFM's spellings", "[markdown_list]")
{
    THEN("-, * and + all start a bullet")
    {
        for (const QString &line : {QStringLiteral("- item"), QStringLiteral("* item"),
                                    QStringLiteral("+ item")}) {
            const MarkdownList::Marker marker = MarkdownList::parse(line);
            CHECK(marker.kind == MarkdownList::Kind::Bullet);
            CHECK(marker.length == 2);
        }
    }

    THEN("the marker length covers the trailing space, so the item's text starts after it")
    {
        const QString line = QStringLiteral("- item");
        const MarkdownList::Marker marker = MarkdownList::parse(line);
        CHECK(line.mid(marker.length) == QStringLiteral("item"));
    }

    THEN("a bare dash with no space is not a list — it could be a minus sign or a rule")
    {
        CHECK(MarkdownList::parse(QStringLiteral("-item")).kind == MarkdownList::Kind::None);
        CHECK(MarkdownList::parse(QStringLiteral("---")).kind == MarkdownList::Kind::None);
    }

    THEN("ordinary prose is not a list")
    {
        CHECK_FALSE(MarkdownList::parse(QStringLiteral("just text")).isValid());
    }
}

SCENARIO("A numbered line reports its own number", "[markdown_list]")
{
    THEN("the number is read from the text, not tracked separately")
    {
        const MarkdownList::Marker marker = MarkdownList::parse(QStringLiteral("3. third"));
        CHECK(marker.kind == MarkdownList::Kind::Numbered);
        CHECK(marker.number == 3);
        CHECK(marker.length == 3);
    }

    THEN("multi-digit numbers report a longer marker")
    {
        const MarkdownList::Marker marker = MarkdownList::parse(QStringLiteral("12. twelfth"));
        CHECK(marker.number == 12);
        CHECK(marker.length == 4);
    }

    THEN("a number without the dot-space is not a list")
    {
        CHECK_FALSE(MarkdownList::parse(QStringLiteral("1984 was a year")).isValid());
        CHECK_FALSE(MarkdownList::parse(QStringLiteral("1.no space")).isValid());
    }
}

SCENARIO("A checkbox line is recognised before it can be mistaken for a bullet", "[markdown_list]")
{
    GIVEN("an unchecked checkbox, which also starts with '- '")
    {
        const MarkdownList::Marker marker = MarkdownList::parse(QStringLiteral("- [ ] milk"));

        THEN("it reports as a checkbox, not a bullet")
        {
            CHECK(marker.kind == MarkdownList::Kind::Checkbox);
            CHECK_FALSE(marker.checked);
            CHECK(marker.length == 6);
        }
    }

    GIVEN("a checked checkbox")
    {
        THEN("both the lowercase and uppercase GFM spellings read as checked")
        {
            CHECK(MarkdownList::parse(QStringLiteral("- [x] milk")).checked);
            CHECK(MarkdownList::parse(QStringLiteral("- [X] milk")).checked);
        }
    }

    THEN("the item's text starts right after the marker")
    {
        const QString line = QStringLiteral("- [x] milk");
        const MarkdownList::Marker marker = MarkdownList::parse(line);
        CHECK(line.mid(marker.length) == QStringLiteral("milk"));
    }

    THEN("a malformed checkbox falls back to being an ordinary bullet")
    {
        // "- [] x" has no space between the brackets, so it isn't GFM's
        // checkbox syntax — but it is still a perfectly good bullet.
        CHECK(MarkdownList::parse(QStringLiteral("- [] x")).kind == MarkdownList::Kind::Bullet);
    }
}

SCENARIO("Markers are written in one canonical spelling", "[markdown_list]")
{
    THEN("bullets are written with a dash")
    {
        CHECK(MarkdownList::bulletMarker() == QStringLiteral("- "));
    }

    THEN("checkboxes are written as literal GFM, never as a ballot-box glyph")
    {
        CHECK(MarkdownList::checkboxMarker(false) == QStringLiteral("- [ ] "));
        CHECK(MarkdownList::checkboxMarker(true) == QStringLiteral("- [x] "));
    }

    THEN("numbered markers carry their number")
    {
        CHECK(MarkdownList::numberedMarker(7) == QStringLiteral("7. "));
    }

    THEN("everything written round-trips back through the parser")
    {
        CHECK(MarkdownList::parse(MarkdownList::bulletMarker() + QStringLiteral("x")).kind
              == MarkdownList::Kind::Bullet);
        CHECK(MarkdownList::parse(MarkdownList::numberedMarker(7) + QStringLiteral("x")).number == 7);
        CHECK(MarkdownList::parse(MarkdownList::checkboxMarker(true) + QStringLiteral("x")).checked);
        CHECK_FALSE(MarkdownList::parse(MarkdownList::checkboxMarker(false) + QStringLiteral("x")).checked);
    }
}

SCENARIO("The reserved drawn-control slot width is shared across every renderer", "[markdown_list]")
{
    // MarkdownHighlighter draws a marker's control into this slot, NoteWindow.qml
    // sizes the control to it, and TextFormatting reserves a matching hanging
    // indent from it. Keeping the one number here — rather than a private copy in
    // each — is what stops the three from drifting apart, which last showed up as
    // wrapped checkbox lines misaligned under their own drawn box.
    GIVEN("the two marker kinds that are replaced by a drawn control")
    {
        THEN("a checkbox reserves a wider slot than a bullet, because it is drawn bigger")
        {
            CHECK(MarkdownList::slotPercentForKind(MarkdownList::Kind::Checkbox)
                  == MarkdownList::kCheckboxSlotPercent);
            CHECK(MarkdownList::slotPercentForKind(MarkdownList::Kind::Bullet)
                  == MarkdownList::kBulletSlotPercent);
            CHECK(MarkdownList::kCheckboxSlotPercent > MarkdownList::kBulletSlotPercent);
            CHECK(MarkdownList::kBulletSlotPercent > 100.0);
        }
    }

    GIVEN("the marker kinds that are left as ordinary text")
    {
        THEN("they reserve no drawn-control slot")
        {
            CHECK(MarkdownList::slotPercentForKind(MarkdownList::Kind::Numbered) == 0.0);
            CHECK(MarkdownList::slotPercentForKind(MarkdownList::Kind::None) == 0.0);
        }
    }
}
