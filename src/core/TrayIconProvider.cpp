#include "TrayIconProvider.h"
#include "TrayIconComposer.h"

#include <QIcon>
#include <QPainter>

TrayIconProvider::TrayIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

QPixmap TrayIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    // QString::toInt() already returns 0 (with ok=false, which we ignore) on
    // anything unparsable, so this can never crash on a malformed id.
    const int count = id.toInt();

    const QSize targetSize = requestedSize.isValid() ? requestedSize : QSize(64, 64);

    QPixmap base = QIcon::fromTheme(QStringLiteral("accessories-text-editor")).pixmap(targetSize);
    if (base.isNull()) {
        // Theme icon missing on this system (minimal installs, some CI/test
        // environments) — QIcon::fromTheme() returns a null pixmap rather
        // than asserting, so fall back to a plain drawn base to guarantee
        // requestPixmap() never returns a null/invalid pixmap.
        base = QPixmap(targetSize);
        base.fill(Qt::transparent);

        QPainter painter(&base);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#555555"));
        painter.drawEllipse(base.rect().adjusted(2, 2, -2, -2));
    }

    const QPixmap composed = composeTrayIcon(base, count);

    if (size)
        *size = composed.size();

    return composed;
}
