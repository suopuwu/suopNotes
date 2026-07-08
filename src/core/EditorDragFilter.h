#pragma once

#include <QQuickItem>

// Works around a real Qt/TextEdit limitation (confirmed live, on Qt
// 6.11.1): once a mouse-drag text selection started over the editor's real
// content crosses below its last line — into the dead space that makes the
// editable box visually fill the whole note, see NoteWindow.qml's
// `editor.height` binding — QQuickTextEdit's own internal drag-selection
// handling silently stops updating the selection altogether. It doesn't
// even fall back to jumping to the document's end (that's a separate,
// already-worked-around quirk that only affects a bare positionAt() call);
// it just freezes wherever the drag last validly was. Meanwhile the
// existing below-content MouseArea (a sibling of `editor`, handling plain
// clicks there) never sees a single event during such a drag, because
// `editor` already holds the exclusive mouse grab for the whole gesture.
//
// Placed as `editor`'s direct parent in NoteWindow.qml (between it and
// editorFlick), this uses childMouseEventFilter — the same ancestor
// interception hook Flickable itself relies on to arbitrate its children's
// mouse events — to catch drag moves/releases that fall below the editor's
// real content and manually extend the selection to the nearest character
// on the last line (the same positionAt() reprojection the below-content
// click handler already uses), instead of leaving the selection stuck.
class EditorDragFilter : public QQuickItem
{
    Q_OBJECT
public:
    explicit EditorDragFilter(QQuickItem *parent = nullptr);

protected:
    bool childMouseEventFilter(QQuickItem *item, QEvent *event) override;
};
