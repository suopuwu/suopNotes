#pragma once

#include <QRegion>

// Pure geometry: the rounded-rect region KWindowEffects::enableBlurBehind
// needs, in window-relative coordinates. Kept separate from BlurEffect (the
// actual KWindowEffects call) so this math is testable without a window,
// compositor, or even KWindowSystem being present at all.
QRegion roundedBlurRegion(qreal width, qreal height, qreal radius);
