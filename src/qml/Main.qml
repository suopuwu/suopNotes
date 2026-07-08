import QtQml
import QtQuick
import Qt.labs.platform as Platform

// No main window: the app is just floating note windows, one per entry in
// openNotesModel, plus the system tray icon that acts as the app's hub
// (notes carry no taskbar presence of their own — req. 11). This root
// Item is never shown itself.
//
// Instantiator, not Repeater: Repeater's itemAt()/visual bookkeeping only
// works for Item delegates. NoteWindow is a top-level Window (non-visual as
// far as any parent Item is concerned), which is exactly Instantiator's
// documented use case.
Item {
    id: root

    Instantiator {
        id: noteInstantiator
        model: openNotesModel
        delegate: NoteWindow {}
    }

    SettingsDialog {
        id: settingsWindow
    }

    // Invariant: every .md file in the notes folder is always shown as an
    // open note, and no open note outlives its file — deleting a note (the
    // HUD's close button) removes the file, so there's no other way for
    // these two lists to drift apart than something changing the folder
    // itself (an external editor/sync tool adding or removing a .md file
    // outside the app). Reconciles both directions in one pass: open
    // anything on disk that isn't open yet, close anything open whose file
    // is gone.
    function reconcileNotesWithDisk() {
        const fileIds = noteStore.noteIds()
        for (let i = 0; i < fileIds.length; ++i) {
            if (!openNotesModel.isOpen(fileIds[i]))
                openNotesModel.openNote(fileIds[i])
        }

        const openIds = openNotesModel.openNoteIds()
        for (let i = 0; i < openIds.length; ++i) {
            if (fileIds.indexOf(openIds[i]) === -1) {
                // Mirrors NoteWindow.deleteNote()'s cleanup: an externally
                // removed .md file leaves the same orphaned NoteOverride_<id>
                // settings group behind if we only close the session state.
                settings.clearNoteOverride(openIds[i])
                openNotesModel.closeNote(openIds[i])
            }
        }
    }

    Connections {
        target: noteStore
        function onNoteIdsChanged() { root.reconcileNotesWithDisk() }
    }

    Component.onCompleted: root.reconcileNotesWithDisk()

    // Reveals every currently-open note (req. 3) — raises + shows each live
    // NoteWindow. Instantiator.objectAt(i), not Repeater, is the documented
    // way to reach non-visual/Window delegates (see NoteWindow's own
    // architecture notes) — count/objectAt already proven to work this way
    // elsewhere in this app.
    function revealAllNotes() {
        for (let i = 0; i < noteInstantiator.count; ++i) {
            const note = noteInstantiator.objectAt(i)
            note.show()
            note.raise()
            note.requestActivate()
        }
    }

    function createNewNote() {
        const id = noteStore.createNote()
        openNotesModel.openNote(id)
    }

    // Qt.labs.platform's SystemTrayIcon.DoubleClick reason is unreachable
    // on Linux: Plasma's tray talks to the app over the freedesktop
    // StatusNotifierItem D-Bus interface, whose only click-related methods
    // are Activate/SecondaryActivate/ContextMenu — there's no double-click
    // concept in that protocol, so Qt's DBus tray backend can never emit
    // it. Detect it ourselves by timing consecutive Trigger events: a
    // second Trigger within trayDoubleClickTimer's window cancels the
    // pending single-click action and creates a note instead. This is a
    // one-shot timer restarted by real click events, not a poll (see
    // CLAUDE.md).
    property double lastTrayTriggerTime: 0

    Timer {
        id: trayDoubleClickTimer
        interval: 400
        repeat: false
        onTriggered: root.revealAllNotes()
    }

    Platform.SystemTrayIcon {
        visible: true
        // icon.source (not icon.name) once there's at least one open note,
        // so the badge-composited image provider actually gets used —
        // re-requesting a fresh URL every time the count changes is what
        // forces TrayIconProvider::requestPixmap to run again (Qt.labs
        // .platform.SystemTrayIcon has no live badge API of its own).
        icon.name: openNotesModel.rowCount === 0 ? "accessories-text-editor" : ""
        icon.source: openNotesModel.rowCount === 0 ? "" : "image://notecount/" + openNotesModel.rowCount
        tooltip: "suopNotes"

        menu: Platform.Menu {
            Platform.MenuItem {
                text: "New Note"
                onTriggered: root.createNewNote()
            }
            Platform.MenuItem {
                text: "Settings…"
                onTriggered: settingsWindow.show()
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: "Open Notes Folder"
                onTriggered: Qt.openUrlExternally("file://" + settings.notesFolder)
            }
            Platform.MenuSeparator {}
            Platform.MenuItem {
                text: "Quit"
                onTriggered: Qt.quit()
            }
        }

        onActivated: (reason) => {
            // Left-click (or its touch equivalent) reveals every open note
            // (req. 3) — the fewest-clicks path to "where did my notes go,"
            // matching req. 8's fewest-clicks intent. Double-click creates a
            // new note; see trayDoubleClickTimer above for why this can't
            // rely on the DoubleClick reason. Right-click reaches the full
            // menu via the platform's own default handling.
            if (reason === Platform.SystemTrayIcon.Trigger) {
                const now = Date.now()
                if (now - root.lastTrayTriggerTime < trayDoubleClickTimer.interval) {
                    trayDoubleClickTimer.stop()
                    root.lastTrayTriggerTime = 0
                    root.createNewNote()
                } else {
                    root.lastTrayTriggerTime = now
                    trayDoubleClickTimer.restart()
                }
            }
        }
    }
}
