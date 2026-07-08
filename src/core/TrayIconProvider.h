#pragma once

#include <QQuickImageProvider>

// Registered in main.cpp via engine.addImageProvider("notecount", new
// TrayIconProvider). QML binds icon.source to "image://notecount/" + count
// so that every open/close of a note re-requests a new URL, forcing Qt to
// call requestPixmap() again — Qt.labs.platform.SystemTrayIcon has no live
// badge API of its own, so this URL-change trick is what drives updates.
class TrayIconProvider : public QQuickImageProvider
{
public:
    TrayIconProvider();

    // `id` is expected to simply be the count as a string (e.g. the "3" in
    // "image://notecount/3") — any other content is treated as unparsable
    // and falls back to a count of 0 rather than crashing.
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};
