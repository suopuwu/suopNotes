import QtQuick

// The TickTick list's name, as a fixed title over the task note (feature
// request: "title text as the first line that just says the name of the list
// being used … unselectable").
//
// Chrome, NOT a line of the document — same reasoning as the "synced N ago"
// footer, and here it is load-bearing twice over. A plain Text cannot be
// selected or edited, which is the requirement; and line 0 of this note is
// already spoken for by the compose slot (TickTickTaskList::kComposeIndex),
// whose index every diff, every dispatch and NoteWindow's own
// listLabelStartAt(0) calls depend on. A title line in the text would shift
// all of that by one.
//
// Fixed rather than scrolling with the text: which list you are looking at
// should not scroll away, and it costs nothing to pin — editorFlick simply
// starts below it (NoteWindow reads this component's implicitHeight through
// its Loader).
//
// Empty until the first pull of a fresh install resolves the default list
// (TickTickSync::selectedProjectName). The reserved height does not depend
// on the text, so that momentary blank does not shove the note's content
// around when the name arrives.
//
// Owned by the module: this file only exists in builds that compile TickTick
// (see the guarded qt_target_qml_sources in src/CMakeLists.txt), and
// NoteWindow.qml reaches it through a Loader by relative URL whose `active`
// already implies the module is present.
Text {
    id: title
    objectName: "tickTickListTitle"

    // The NoteWindow hosting this title, set by the Loader that creates it.
    property var noteWindow: null

    visible: noteWindow !== null && noteWindow.isTickTickTaskNote
    text: (noteWindow !== null && noteWindow.isTickTickTaskNote && tickTick !== null)
        ? tickTick.sync.selectedProjectName
        : ""
    elide: Text.ElideRight
    font.bold: true
    font.pixelSize: noteWindow !== null ? noteWindow.noteAppearance.fontSize : 12
    font.family: (noteWindow !== null && noteWindow.noteAppearance.fontFamily.length > 0)
        ? noteWindow.noteAppearance.fontFamily : Qt.application.font.family
    // Slightly recessive against the tasks themselves — it labels the note,
    // it is not a task. Same 0.65 alpha the sync footer uses.
    color: noteWindow !== null
        ? Qt.rgba(noteWindow.noteAppearance.fontColor.r,
                  noteWindow.noteAppearance.fontColor.g,
                  noteWindow.noteAppearance.fontColor.b, 0.65)
        : "transparent"
}
