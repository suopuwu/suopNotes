#include "BlurRegion.h"

#include <QPainterPath>
#include <algorithm>

QRegion roundedBlurRegion(qreal width, qreal height, qreal radius)
{
    if (width <= 0 || height <= 0)
        return QRegion();

    const qreal clampedRadius = std::clamp(radius, 0.0, std::min(width, height) / 2.0);

    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, width, height), clampedRadius, clampedRadius);

    return QRegion(path.toFillPolygon().toPolygon());
}
