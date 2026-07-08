#include "TrayIconComposer.h"

#include <QPainter>
#include <algorithm>

QPixmap composeTrayIcon(const QPixmap &base, int count)
{
    if (count <= 0)
        return base;

    if (base.isNull())
        return base;

    QPixmap result(base.size());
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    painter.drawPixmap(0, 0, base);

    // Circle diameter ~55% of the icon's smaller dimension, positioned so it
    // overlaps the bottom-right corner (matches the base icon's own corner,
    // not tucked fully inside it — the usual tray-badge look).
    const qreal diameter = std::min(base.width(), base.height()) * 0.55;
    const QRectF badgeRect(base.width() - diameter, base.height() - diameter, diameter, diameter);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#e05555"));
    painter.drawEllipse(badgeRect);

    const QString text = count > 99 ? QStringLiteral("99+") : QString::number(count);

    QFont font = painter.font();
    font.setBold(true);
    // Shrink the font as the text grows ("99+" is 3 chars) so it still fits
    // inside the circle rather than overflowing it.
    const qreal pixelSize = std::max(1.0, diameter / (text.length() <= 2 ? 2.0 : 2.6));
    font.setPixelSize(static_cast<int>(pixelSize));
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(badgeRect, Qt::AlignCenter, text);

    painter.end();
    return result;
}
