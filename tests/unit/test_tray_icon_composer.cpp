#include <catch2/catch_test_macros.hpp>

#include "core/TrayIconComposer.h"

#include <QColor>
#include <QImage>

namespace
{
// Sampling via QImage rather than QPixmap directly since QPixmap has no
// portable pixel-read API of its own (backing store is platform-specific);
// toImage() is the documented way to inspect pixel contents.
QColor pixelAt(const QPixmap &pixmap, int x, int y)
{
    return pixmap.toImage().pixelColor(x, y);
}
}

SCENARIO("composeTrayIcon leaves the base icon untouched when there is nothing to badge", "[tray_icon_composer]")
{
    GIVEN("a plain solid-color 64x64 base icon")
    {
        QPixmap base(64, 64);
        base.fill(Qt::blue);

        WHEN("count is 0")
        {
            const QPixmap result = composeTrayIcon(base, 0);

            THEN("the result is the same size as the base")
            {
                CHECK(result.size() == base.size());
            }

            THEN("the badge region (bottom-right) is still the plain base color")
            {
                CHECK(pixelAt(result, 46, 46) == QColor(Qt::blue));
            }
        }

        WHEN("count is negative")
        {
            const QPixmap result = composeTrayIcon(base, -5);

            THEN("the result is the same size as the base")
            {
                CHECK(result.size() == base.size());
            }

            THEN("the badge region (bottom-right) is still the plain base color")
            {
                CHECK(pixelAt(result, 46, 46) == QColor(Qt::blue));
            }
        }
    }
}

SCENARIO("composeTrayIcon paints a badge onto the bottom-right corner when count > 0", "[tray_icon_composer]")
{
    GIVEN("a plain solid-color 64x64 base icon and a positive count")
    {
        QPixmap base(64, 64);
        base.fill(Qt::blue);

        const QPixmap result = composeTrayIcon(base, 3);

        THEN("the result is the same overall size as the base")
        {
            CHECK(result.size() == base.size());
        }

        THEN("the center of the expected badge circle is no longer the base color")
        {
            // Diameter is ~55% of 64 (~35.2), positioned flush with the
            // bottom-right corner, so its center sits near (46, 46).
            CHECK(pixelAt(result, 46, 46) != QColor(Qt::blue));
        }

        THEN("a pixel far from the badge (top-left corner) is still the base color")
        {
            CHECK(pixelAt(result, 2, 2) == QColor(Qt::blue));
        }
    }
}

SCENARIO("composeTrayIcon handles a large count without crashing", "[tray_icon_composer]")
{
    GIVEN("a count above the 99+ clamp threshold")
    {
        QPixmap base(64, 64);
        base.fill(Qt::blue);

        const QPixmap result = composeTrayIcon(base, 150);

        THEN("it still produces a valid, non-null pixmap of the same size as the base")
        {
            CHECK_FALSE(result.isNull());
            CHECK(result.size() == base.size());
        }
    }
}

SCENARIO("composeTrayIcon handles a degenerate base pixmap without crashing", "[tray_icon_composer]")
{
    GIVEN("a null base pixmap (default-constructed)")
    {
        QPixmap base;

        THEN("it returns without crashing, and the result is still null")
        {
            const QPixmap result = composeTrayIcon(base, 3);
            CHECK(result.isNull());
        }
    }

    GIVEN("an explicitly 0x0 base pixmap")
    {
        QPixmap base(0, 0);

        THEN("it returns without crashing, and the result is still null")
        {
            const QPixmap result = composeTrayIcon(base, 3);
            CHECK(result.isNull());
        }
    }
}
