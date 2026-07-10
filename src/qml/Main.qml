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

    // The note currently holding OS-level focus, or null when none is —
    // the single source of truth SelectionToolbar (the app-wide floating
    // formatting toolbar) binds to below. Each delegate's own
    // activeChanged is the one signal that can actually tell us this;
    // there's no equivalent "any window somewhere became active" signal
    // at the Instantiator/model level.
    property var activeNoteWindow: null

    // Third design of this focus-tracking logic (see git history for the
    // two earlier attempts) — both of those tried to force OS focus back
    // to the note the instant the toolbar took it, and both failed for
    // the same underlying reason: this window MUST be allowed to actually
    // hold focus for its own buttons to receive clicks at all (an earlier
    // Qt.WindowDoesNotAcceptFocus attempt silently swallowed clicks
    // outright on this compositor), but re-activating the note the moment
    // the toolbar's `active` turns true fires on mouse-*press*, before
    // onClicked ever runs on release — yanking focus away mid-click and
    // cancelling the button's own press/release gesture, breaking every
    // button rather than just the untracked ones.
    //
    // This version doesn't fight the OS for focus at all: the toolbar is
    // considered "in use" — and stays visible — for as long as EITHER the
    // tracked note OR the toolbar itself is the active window. Only when
    // focus moves to something that's neither (another note, another app,
    // the desktop) does it actually clear. A click anywhere in the
    // toolbar (a button, the grip, dead space) is then just a normal
    // click in a normal window — nothing to race, nothing to swallow.
    function noteOrToolbarActive() {
        return (root.activeNoteWindow && root.activeNoteWindow.active) || selectionToolbar.active
    }

    // A brief, spurious activation blip on the tracked note or the toolbar
    // — neither window's fault, just a transient compositor-level hiccup —
    // could otherwise let the debounce below race past the moment a real
    // focus hand-off (e.g. clicking a toolbar button) resolves, hiding the
    // toolbar despite the user still visibly working in/around the note.
    // `hovered` (plain mouse presence, unrelated to and far less prone to
    // blip than window activation) is used as a one-time grace signal for
    // exactly that: see activeNoteWindowClearDebounce below. Deliberately
    // NOT used as a standalone, indefinite "still in use" signal — that
    // would leave the toolbar stuck open forever once the user genuinely
    // switched focus to a different app entirely, if their mouse simply
    // never happened to move off the note afterward.
    function noteOrToolbarHovered() {
        return (root.activeNoteWindow && root.activeNoteWindow.hovered) || selectionToolbar.hovered
    }

    // Sees exactly one grace retry per deactivation episode, so the bound
    // on "how long the toolbar can possibly outlive real focus" stays
    // small and predictable rather than open-ended.
    property bool activeNoteWindowClearGraceUsed: false

    // Debounces clearing activeNoteWindow to null, restarted by every real
    // "the tracked note just deactivated" event (never runs on a schedule
    // — see CLAUDE.md on polling timers) and re-checked (not just delayed)
    // at fire time via noteOrToolbarActive() above, so a still-focused
    // toolbar never gets cleared out from under itself.
    Timer {
        id: activeNoteWindowClearDebounce
        interval: 250
        repeat: false
        onTriggered: {
            if (root.noteOrToolbarActive()) {
                root.activeNoteWindowClearGraceUsed = false
                return
            }
            // One bounded extra grace period if the pointer is still
            // resting on the note/toolbar, in case a brief activation blip
            // is still resolving — never more than one, so genuinely
            // switching focus to a different app entirely still clears
            // this promptly (within 2 * interval) even if the mouse never
            // moved off the note meanwhile.
            if (!root.activeNoteWindowClearGraceUsed && root.noteOrToolbarHovered()) {
                root.activeNoteWindowClearGraceUsed = true
                activeNoteWindowClearDebounce.restart()
                return
            }
            root.activeNoteWindowClearGraceUsed = false
            root.activeNoteWindow = null
        }
    }

    Instantiator {
        id: noteInstantiator
        model: openNotesModel
        delegate: NoteWindow {
            id: thisNoteWindow
            onActiveChanged: {
                if (active) {
                    activeNoteWindowClearDebounce.stop()
                    root.activeNoteWindowClearGraceUsed = false
                    root.activeNoteWindow = thisNoteWindow
                } else if (root.activeNoteWindow === thisNoteWindow) {
                    activeNoteWindowClearDebounce.restart()
                }
            }
        }
    }

    // Single, app-wide instance (feature request) — see SelectionToolbar.qml's
    // own header comment for why exactly one shared window, rather than one
    // per note, is what makes "follows whichever note is focused" possible.
    SelectionToolbar {
        id: selectionToolbar
        noteWindow: root.activeNoteWindow

        // The toolbar itself just lost focus — if the tracked note didn't
        // pick it back up either (the user clicked away to something
        // else entirely, not back into their note), re-arm the debounce
        // so the bar still hides shortly after, rather than staying stuck
        // forever (the Timer above only ever checks at the moment it
        // fires; it never reschedules itself on its own).
        onActiveChanged: {
            if (!active && root.activeNoteWindow && !root.activeNoteWindow.active)
                activeNoteWindowClearDebounce.restart()
        }
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
