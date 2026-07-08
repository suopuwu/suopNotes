#pragma once

#include <QObject>

class QQuickWindow;

// Thin QML-invokable wrapper around KWindowEffects::enableBlurBehind.
// A no-op when KWindowSystem isn't available (non-Linux, or the dev package
// missing) so QML never needs to branch on platform.
class BlurEffect : public QObject
{
    Q_OBJECT
public:
    explicit BlurEffect(QObject *parent = nullptr);

    Q_INVOKABLE void applyTo(QQuickWindow *window, bool enabled, qreal radius);

signals:
    // Emitted at the end of every applyTo() call, regardless of platform —
    // lets tests verify the QML wiring (calls on load/resize/settings
    // change) without needing a real compositor to observe blur visually.
    void appliedTo(QQuickWindow *window, bool enabled, qreal radius);
};
