#pragma once

#include <QObject>

class QQuickWindow;

// Thin QML-invokable wrapper around QWindow::setMask(), used to make a
// permanently-mapped window stop accepting pointer input without unmapping
// it — see setAcceptsInput()'s own comment, and SelectionToolbar.qml's, for
// why that combination is needed at all.
//
// Deliberately its own object rather than another method on PositionManager:
// that class's whole documented job is KWin rule/position application via
// out-of-process qdbus6 calls on background threads, and an input region is
// none of those things (it's synchronous, client-side, and never talks to
// KWin at all). Mirrors BlurEffect, the existing precedent for "a tiny
// QML-facing wrapper around one window-level Qt call."
class WindowInputMask : public QObject
{
    Q_OBJECT
public:
    explicit WindowInputMask(QObject *parent = nullptr);

    // acceptsInput=false makes the window's whole surface transparent to
    // pointer input; true restores normal, full-surface input.
    Q_INVOKABLE void setAcceptsInput(QQuickWindow *window, bool acceptsInput);

signals:
    // Emitted at the end of every setAcceptsInput() call, regardless of
    // platform — lets tests verify the QML wiring (which windows get masked
    // when) without a real compositor, since the mask itself has no
    // observable effect under the offscreen QPA plugin and QWindow::mask()
    // isn't exposed to QML at all.
    void inputMaskApplied(QQuickWindow *window, bool acceptsInput);
};
