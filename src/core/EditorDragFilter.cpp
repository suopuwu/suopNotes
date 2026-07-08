#include "EditorDragFilter.h"

#include <QMouseEvent>

EditorDragFilter::EditorDragFilter(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFiltersChildMouseEvents(true);
}

bool EditorDragFilter::childMouseEventFilter(QQuickItem *item, QEvent *event)
{
    if (event->type() != QEvent::MouseMove && event->type() != QEvent::MouseButtonRelease)
        return false;

    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    // A plain hover move (no button down) isn't part of a selection drag —
    // let it through untouched. Releases are always relevant here since the
    // button that ends the drag is, by definition, no longer in buttons().
    if (event->type() == QEvent::MouseMove && !(mouseEvent->buttons() & Qt::LeftButton))
        return false;

    const qreal contentHeight = item->property("contentHeight").toReal();
    const QPointF localPos = item->mapFromScene(mouseEvent->scenePosition());
    if (localPos.y() < contentHeight)
        return false; // within real content — Qt's own drag-selection handles this fine

    int newPosition = 0;
    QMetaObject::invokeMethod(item, "positionAt", Q_RETURN_ARG(int, newPosition),
                              Q_ARG(qreal, localPos.x()), Q_ARG(qreal, contentHeight - 1));

    // Whichever end of the current selection isn't the active/moving end
    // (cursorPosition) is the drag's anchor — the fixed point the selection
    // is being extended from.
    const int cursorPosition = item->property("cursorPosition").toInt();
    const int selectionStart = item->property("selectionStart").toInt();
    const int selectionEnd = item->property("selectionEnd").toInt();
    const int anchor = (cursorPosition == selectionEnd) ? selectionStart : selectionEnd;

    QMetaObject::invokeMethod(item, "select", Q_ARG(int, anchor), Q_ARG(int, newPosition));

    return true; // consume — TextEdit's own (frozen) handling never sees this one
}
