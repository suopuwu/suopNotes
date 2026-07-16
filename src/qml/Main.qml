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

    // The note the pointer is currently over, or null — the scope the windowed
    // ribbon (NoteHudWindow below) binds to. Deliberately SEPARATE from
    // activeNoteWindow: the ribbon is a hover affordance (it appears on the
    // note you're pointing at, focused or not), matching the inline ribbon it
    // replaces, whereas the toolbar/find/menu are focus-gated. Kept alive
    // across the small gap between the note and the ribbon window (which sits
    // just outside the note) by hoveredNoteClearDebounce below.
    property var hoveredNoteWindow: null

    // Debounces clearing hoveredNoteWindow, restarted whenever the tracked note
    // OR the ribbon loses hover (a one-shot, event-driven timer — never a
    // recurring poll, see CLAUDE.md). Re-checks at fire time rather than just
    // delaying: moving the pointer from the note onto the ribbon un-hovers the
    // note for an instant, and this is what stops the ribbon vanishing out from
    // under the pointer mid-reach. 150ms is comfortably longer than that gap
    // crossing while still feeling immediate on leave.
    Timer {
        id: hoveredNoteClearDebounce
        interval: 150
        repeat: false
        onTriggered: {
            if (root.hoveredNoteWindow
                && (root.hoveredNoteWindow.hovered || noteHudWindow.hovered))
                return
            root.hoveredNoteWindow = null
        }
    }

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
    // This version doesn't fight the OS for focus at all: the chrome is
    // considered "in use" — and stays visible — for as long as EITHER the
    // tracked note OR one of the chrome windows itself is the active
    // window. Only when focus moves to something that's none of them
    // (another note, another app, the desktop) does it actually clear. A
    // click anywhere in the toolbar (a button, the grip, dead space) is
    // then just a normal click in a normal window — nothing to race,
    // nothing to swallow.
    //
    // findReplacePanel is in here for a sharper reason than the toolbar is.
    // The toolbar only holds focus for the instant of a click, so it would
    // mostly survive without this; the find panel holds focus for as long
    // as the user is typing a search term. Without its term here, clicking
    // into the find field deactivates the note, the debounce below fires
    // 250ms later, finds nothing "active", and clears activeNoteWindow —
    // pulling `noteWindow` out from under the very panel the user is typing
    // into, which blanks it and parks it off-screen mid-keystroke. It would
    // fail every time, not intermittently.
    //
    // noteContextMenu is in here for the same sharper reason the find panel
    // is, and one more of its own: it takes focus the moment it opens (that is
    // how it notices a click landing anywhere else — see its onActiveChanged),
    // which deactivates the note by construction, every single time. Without
    // its term here the debounce below would clear activeNoteWindow 250ms
    // later and blank the menu while it is sitting open under the user's
    // pointer. It would fail every time, not intermittently.
    function noteOrChromeActive() {
        return (root.activeNoteWindow && root.activeNoteWindow.active)
            || selectionToolbar.active
            || findReplacePanel.active
            || noteContextMenu.active
            || noteHudWindow.active
    }

    // A brief, spurious activation blip on the tracked note or the chrome
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
    function noteOrChromeHovered() {
        return (root.activeNoteWindow && root.activeNoteWindow.hovered)
            || selectionToolbar.hovered
            || findReplacePanel.hovered
            || noteContextMenu.hovered
            || noteHudWindow.hovered
    }

    // Sees exactly one grace retry per deactivation episode, so the bound
    // on "how long the toolbar can possibly outlive real focus" stays
    // small and predictable rather than open-ended.
    property bool activeNoteWindowClearGraceUsed: false

    // Debounces clearing activeNoteWindow to null, restarted by every real
    // "the tracked note just deactivated" event (never runs on a schedule
    // — see CLAUDE.md on polling timers) and re-checked (not just delayed)
    // at fire time via noteOrChromeActive() above, so a still-focused
    // toolbar or find panel never gets cleared out from under itself.
    Timer {
        id: activeNoteWindowClearDebounce
        interval: 250
        repeat: false
        onTriggered: {
            if (root.noteOrChromeActive()) {
                root.activeNoteWindowClearGraceUsed = false
                return
            }
            // One bounded extra grace period if the pointer is still
            // resting on the note/toolbar, in case a brief activation blip
            // is still resolving — never more than one, so genuinely
            // switching focus to a different app entirely still clears
            // this promptly (within 2 * interval) even if the mouse never
            // moved off the note meanwhile.
            if (!root.activeNoteWindowClearGraceUsed && root.noteOrChromeHovered()) {
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

            // Hover tracking for the windowed ribbon (see hoveredNoteWindow
            // above) — independent of focus, so the ribbon appears on whichever
            // note the pointer is over. Clearing is debounced so the pointer
            // moving off the note toward the ribbon window (which sits just
            // outside it) doesn't drop tracking mid-reach.
            onHoveredChanged: {
                if (hovered) {
                    hoveredNoteClearDebounce.stop()
                    root.hoveredNoteWindow = thisNoteWindow
                } else if (root.hoveredNoteWindow === thisNoteWindow) {
                    hoveredNoteClearDebounce.restart()
                }
            }

            // A note can't reach the app-wide find panel itself, so it asks
            // and this is where the ask is joined up (see NoteWindow's own
            // findRequested comment). Guarded on being the tracked note:
            // Ctrl+F is a per-note Qt.WindowShortcut, so only the focused
            // note can fire it — but the tracking clears on a 250ms
            // debounce, and an unfocused note driving the shared panel at
            // whatever the tail of that window allows would be a bug, not a
            // feature.
            onFindRequested: (withReplace) => {
                if (root.activeNoteWindow !== thisNoteWindow)
                    return
                if (withReplace)
                    findReplacePanel.openReplace()
                else
                    findReplacePanel.openFind()
            }
            onFindCloseRequested: {
                if (root.activeNoteWindow === thisNoteWindow)
                    findReplacePanel.close()
            }

            // Same one-way ask as findRequested above — a note can't reach the
            // app-wide context menu itself. Guarded on being the tracked note
            // for a sharper reason than the find panel's, though: the menu
            // binds to root.activeNoteWindow, so an untracked note opening it
            // would put a menu on screen at THIS note's coordinates while
            // every action in it ran against a different note entirely.
            onContextMenuRequested: (screenX, screenY) => {
                if (root.activeNoteWindow !== thisNoteWindow)
                    return
                noteContextMenu.openAt(screenX, screenY)
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

    // The other app-wide chrome singleton, same shape as the toolbar above
    // and for the same reason (see FindReplacePanel.qml's header): a note
    // enforces no minimum size, so this card could not live inside one.
    FindReplacePanel {
        id: findReplacePanel
        noteWindow: root.activeNoteWindow

        // Same re-arm as the toolbar's: the Timer only ever checks at the
        // moment it fires and never reschedules itself, so a panel losing
        // focus to something that ISN'T the tracked note has to restart it
        // or the note stays tracked forever.
        onActiveChanged: {
            if (!active && root.activeNoteWindow && !root.activeNoteWindow.active)
                activeNoteWindowClearDebounce.restart()
        }
    }

    // The third app-wide chrome singleton, same shape and same reason as the
    // two above (see NoteContextMenu.qml's header): a right-click near a
    // note's own corner puts most of this menu outside the note, which a small
    // note could not render at all.
    NoteContextMenu {
        id: noteContextMenu
        noteWindow: root.activeNoteWindow

        // Same re-arm as the toolbar's and the panel's: the Timer only ever
        // checks at the moment it fires and never reschedules itself, so chrome
        // losing focus to something that ISN'T the tracked note has to restart
        // it or the note stays tracked forever.
        onActiveChanged: {
            if (!active && root.activeNoteWindow && !root.activeNoteWindow.active)
                activeNoteWindowClearDebounce.restart()
        }
    }

    // App-wide windowed note ribbon (Settings.hudAsWindow) — the same
    // singleton-owned-by-Main shape as the toolbar/find/menu above, but bound
    // to the HOVERED note rather than the active one (see hoveredNoteWindow).
    // When hudAsWindow is off it simply never gets a note (NoteWindow renders
    // its own inline ribbon instead) and stays parked.
    NoteHudWindow {
        id: noteHudWindow
        noteWindow: root.hoveredNoteWindow

        // The ribbon itself just lost hover — re-check whether the note under
        // it should still be tracked (the Timer only checks at fire time and
        // never reschedules itself, same re-arm the chrome windows above do
        // for the active-note debounce).
        onHoveredChanged: {
            if (!hovered && root.hoveredNoteWindow && !root.hoveredNoteWindow.hovered)
                hoveredNoteClearDebounce.restart()
        }

        // Search is the one ribbon action that can't act on the note directly:
        // the find panel is bound to the ACTIVE note, so make the hovered note
        // active first (updates findReplacePanel.noteWindow synchronously via
        // its binding), give it real OS focus, then open the panel against it.
        onFindRequested: (note) => {
            if (!note)
                return
            root.activeNoteWindow = note
            note.requestActivate()
            findReplacePanel.openFind()
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
            // `visible` on a Qt.labs.platform Menu means "shown as a popup
            // right now", not "this menu exists" — and it defaults to true,
            // so at component completion Qt realizes the QMenu that KDE's
            // platform theme backs this with, parentless, and Wayland
            // refuses to grab a popup with no transientParent ("Failed to
            // create grabbing popup"). The QMenu isn't ours to give a parent
            // to. Harmless to leave false: the tray menu reaches Plasma over
            // DBusMenu, not this local popup, and nothing here ever calls
            // open()/popup() — right-click is served by the platform's own
            // default handling (see onActivated below).
            visible: false

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
            // matching req. 8's fewest-clicks intent. Middle-click creates a
            // new note: Plasma's tray talks to the app over the freedesktop
            // StatusNotifierItem D-Bus interface, whose only click-related
            // methods are Activate/SecondaryActivate/ContextMenu — Plasma
            // sends SecondaryActivate on middle-click, which Qt's DBus tray
            // backend surfaces here as MiddleClick. Right-click reaches the
            // full menu via the platform's own default handling.
            if (reason === Platform.SystemTrayIcon.Trigger)
                root.revealAllNotes()
            else if (reason === Platform.SystemTrayIcon.MiddleClick)
                root.createNewNote()
        }
    }
}
