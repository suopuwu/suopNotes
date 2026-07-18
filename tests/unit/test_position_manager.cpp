#include <catch2/catch_test_macros.hpp>

#include <QGuiApplication>
#include <QRect>
#include <QScreen>

#include "core/PositionManager.h"

// PositionManager is mostly a KWin dispatcher whose effects (rule writes,
// moveWindow) happen off-process and are covered where that logic lives
// (test_kwin_rule_writer.cpp). offscreenPoint() is the one piece of pure,
// in-process math it owns: the point every frameless chrome window parks its
// content at to hide it, centralized here so those windows stop hand-copying
// the screen-scanning that computes it.
SCENARIO("offscreenPoint returns a point past every connected screen", "[position_manager]")
{
    GIVEN("a PositionManager and the current set of screens")
    {
        PositionManager manager;
        const QPointF point = manager.offscreenPoint();

        THEN("it lies beyond the right and bottom edge of every screen")
        {
            // Whatever the monitor arrangement, the parked window must be able
            // to sit here without any part of it landing on a real screen — so
            // the point has to clear every screen's own geometry.
            const QList<QScreen *> screens = QGuiApplication::screens();
            REQUIRE_FALSE(screens.isEmpty());
            for (const QScreen *screen : screens) {
                const QRect geometry = screen->geometry();
                CHECK(point.x() > geometry.x() + geometry.width());
                CHECK(point.y() > geometry.y() + geometry.height());
                CHECK_FALSE(geometry.contains(point.toPoint()));
            }
        }

        THEN("it clears the desktop by a wide margin, not just by a pixel")
        {
            // The 1000px cushion exists so a window of any ordinary size parked
            // with its top-left here is still wholly off every screen.
            qreal rightmost = 0;
            qreal bottommost = 0;
            for (const QScreen *screen : QGuiApplication::screens()) {
                const QRect geometry = screen->geometry();
                rightmost = qMax(rightmost, static_cast<qreal>(geometry.x() + geometry.width()));
                bottommost = qMax(bottommost, static_cast<qreal>(geometry.y() + geometry.height()));
            }
            CHECK(point.x() >= rightmost + 1000);
            CHECK(point.y() >= bottommost + 1000);
        }

        THEN("it is stable across calls")
        {
            const QPointF again = manager.offscreenPoint();
            CHECK(again == point);
        }
    }
}
