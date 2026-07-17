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
    // The one child this filter is for — the editor TextArea. childMouseEventFilter
    // fires for EVERY descendant's mouse events, not just the editor's: the overlays
    // NoteWindow.qml layers over the editor (the "Select image…" placeholder button,
    // in particular) are descendants too. Without knowing which item is the editor,
    // this filter treated a release on that button as a below-content drag-release
    // and consumed it (a Button has no contentHeight, so the "is this within real
    // content?" guard read 0 and always failed) — so the button's own click never
    // fired (bug found live: "clicking it does nothing"). Only events headed to
    // exactly this item get the drag-selection reprojection; everything else passes
    // straight through untouched.
    Q_PROPERTY(QQuickItem *editorItem READ editorItem WRITE setEditorItem NOTIFY editorItemChanged)
public:
    explicit EditorDragFilter(QQuickItem *parent = nullptr);

    QQuickItem *editorItem() const { return m_editorItem; }
    void setEditorItem(QQuickItem *item);

signals:
    void editorItemChanged();

protected:
    bool childMouseEventFilter(QQuickItem *item, QEvent *event) override;

private:
    QQuickItem *m_editorItem = nullptr;
};
