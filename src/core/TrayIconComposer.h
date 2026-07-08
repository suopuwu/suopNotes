#pragma once

#include <QPixmap>

// Composites a small numeric badge onto `base` (bottom-right corner) showing
// `count`. Pure pixmap math — no QQuickImageProvider, no GUI surface running
// — so it's testable under QT_QPA_PLATFORM=offscreen with no display, same
// spirit as BlurRegion.h being split out of the GUI-touching BlurEffect.
//
// count <= 0 returns `base` unchanged: an idle tray with zero open notes
// shouldn't show a "0". Counts above 99 clamp their displayed text to "99+"
// so the badge text never overflows its own circle. A null/degenerate
// `base` is returned as-is rather than risk painting onto an invalid target.
QPixmap composeTrayIcon(const QPixmap &base, int count);
