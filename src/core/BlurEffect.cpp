#include "BlurEffect.h"
#include "BlurRegion.h"

#include <QQuickWindow>

#ifdef SUOPNOTES_HAVE_KWINDOWSYSTEM
#include <KWindowEffects>
#endif

BlurEffect::BlurEffect(QObject *parent)
    : QObject(parent)
{
}

void BlurEffect::applyTo(QQuickWindow *window, bool enabled, qreal radius)
{
    if (!window)
        return;

#ifdef SUOPNOTES_HAVE_KWINDOWSYSTEM
    if (enabled) {
        const QRegion region = roundedBlurRegion(window->width(), window->height(), radius);
        KWindowEffects::enableBlurBehind(window, true, region);
    } else {
        KWindowEffects::enableBlurBehind(window, false);
    }
#endif

    emit appliedTo(window, enabled, radius);
}
