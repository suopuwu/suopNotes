#include <catch2/catch_test_macros.hpp>

#include "core/BlurRegion.h"

SCENARIO("roundedBlurRegion describes a window-relative rounded-rect region", "[blur_region]")
{
    GIVEN("a typical note window size and radius")
    {
        const QRegion region = roundedBlurRegion(260, 260, 12);

        THEN("it is not empty")
        {
            CHECK_FALSE(region.isEmpty());
        }

        THEN("its bounding rect matches the window exactly (origin at 0,0)")
        {
            const QRect bounds = region.boundingRect();
            CHECK(bounds.x() == 0);
            CHECK(bounds.y() == 0);
            CHECK(bounds.width() == 260);
            CHECK(bounds.height() == 260);
        }

        THEN("the extreme corner pixel is excluded (rounded away), unlike a plain rectangle")
        {
            CHECK_FALSE(region.contains(QPoint(0, 0)));
            CHECK_FALSE(region.contains(QPoint(259, 0)));
            CHECK_FALSE(region.contains(QPoint(0, 259)));
            CHECK_FALSE(region.contains(QPoint(259, 259)));
        }

        THEN("the center and edge midpoints are included")
        {
            CHECK(region.contains(QPoint(130, 130)));
            CHECK(region.contains(QPoint(130, 2)));
            CHECK(region.contains(QPoint(2, 130)));
        }
    }
}

SCENARIO("roundedBlurRegion handles degenerate input without crashing", "[blur_region]")
{
    GIVEN("zero or negative dimensions")
    {
        THEN("zero width/height produces an empty region")
        {
            CHECK(roundedBlurRegion(0, 0, 12).isEmpty());
            CHECK(roundedBlurRegion(-10, 100, 12).isEmpty());
        }
    }

    GIVEN("a radius larger than half the smallest dimension")
    {
        THEN("it is clamped rather than producing an invalid/inverted shape")
        {
            const QRegion region = roundedBlurRegion(40, 40, 1000);
            CHECK_FALSE(region.isEmpty());
            const QRect bounds = region.boundingRect();
            CHECK(bounds.width() == 40);
            CHECK(bounds.height() == 40);
        }
    }
}

SCENARIO("roundedBlurRegion with zero radius is (close to) the full rectangle", "[blur_region]")
{
    GIVEN("radius 0")
    {
        const QRegion region = roundedBlurRegion(100, 100, 0);

        THEN("all four corners are included")
        {
            CHECK(region.contains(QPoint(0, 0)));
            CHECK(region.contains(QPoint(99, 0)));
            CHECK(region.contains(QPoint(0, 99)));
            CHECK(region.contains(QPoint(99, 99)));
        }
    }
}
