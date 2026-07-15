#include "WindowInputMask.h"

#include <QQuickWindow>
#include <QRegion>

namespace {

// The "blocks all input" mask. Deliberately a 1x1 rect at (-1,-1) — OUTSIDE
// the surface — rather than the empty QRegion one would reach for first.
//
// An empty QRegion does the exact OPPOSITE of what's wanted here:
// QWindow::setMask() documents an empty region as "clears the mask," and
// Qt's Wayland backend implements that as wl_surface.set_input_region(nil),
// whose protocol meaning is "the whole surface accepts input." Confirmed at
// the protocol level (WAYLAND_DEBUG=1 against a real headless kwin_wayland,
// Qt 6.11): setMask(QRegion()) emits set_input_region(nil), while
// setMask(QRegion(-1,-1,1,1)) emits create_region + region.add(-1,-1,1,1) +
// set_input_region(that region). A region that lies entirely off the surface
// is what actually yields "no pixel of this window is clickable"; the
// compositor intersects it with the surface bounds, so nothing is left.
//
// QRegion(0,0,0,0) is NOT a substitute: a zero-sized rect makes an EMPTY
// QRegion, so it takes the clears-the-mask path above. (Confirmed in the
// same trace: it emitted no protocol traffic at all, since Qt short-circuits
// when the new mask equals the current one — both being empty.)
const QRegion &blockAllInputMask()
{
    static const QRegion mask(-1, -1, 1, 1);
    return mask;
}

}

WindowInputMask::WindowInputMask(QObject *parent)
    : QObject(parent)
{
}

void WindowInputMask::setAcceptsInput(QQuickWindow *window, bool acceptsInput)
{
    if (!window)
        return;

    window->setMask(acceptsInput ? QRegion() : blockAllInputMask());

    // Required, not belt-and-braces: wl_surface.set_input_region is
    // DOUBLE-BUFFERED Wayland state — it does nothing until the surface's
    // next wl_surface.commit — and Qt's Wayland backend does not commit on
    // its own after setMask(). Confirmed at the protocol level (same
    // WAYLAND_DEBUG trace as above): the set_input_region request went out
    // and then NO commit followed it, leaving the new region pending
    // indefinitely on an otherwise-idle surface. That is exactly this app's
    // case — these windows are permanently mapped and spend most of their
    // life not repainting at all, so without forcing a frame here the mask
    // could sit unapplied for an unbounded time (i.e. silently no-op, which
    // is the whole bug this exists to fix). requestUpdate() was verified in
    // that same trace to produce an attach+commit pair, which flushes it.
    window->requestUpdate();

    emit inputMaskApplied(window, acceptsInput);
}
