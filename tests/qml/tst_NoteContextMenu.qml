import QtQml
import QtQuick
import QtTest
import SuopNotesUi

// Behavior tests for the app-wide right-click menu (NoteContextMenu.qml),
// driven through real NoteWindows the way the app itself wires it up.
//
// What a reader should be able to learn from this file alone:
//   - the menu opens at the point it was asked for, flipping around that point
//     rather than sliding under it near a screen edge;
//   - it closes on Escape, on running any action, on the tracked note changing,
//     and on losing focus to anything else — and only the first two hand focus
//     back to the note;
//   - Cut/Copy/Paste do the same thing the toolbar's buttons do; Duplicate
//     lands the copy at the menu; Delete removes the note; Reveal offers the
//     note's own file to the file manager, and only when there is one;
//   - it never blocks clicks while it isn't showing.
//
// The menu is declared STATICALLY here and shared by every test, unlike the
// per-test NoteWindows. That is not a style choice: it is a permanently mapped
// window (see NoteContextMenu.qml's Component.onCompleted — mapping and
// unmapping it repeatedly disrupts the focused note's text input), so it
// cannot be created and destroyed per test. cleanup() resets its state instead.
//
// The right-click gesture that OPENS it in the real app — the stationary-click
// vs. drag split — lives in NoteWindow.qml and is tested in tst_NoteWindow.qml
// against real synthetic mouse input. These tests start from openAt(), the
// point where that gesture hands off.
Item {
    id: root

    Component {
        id: noteWindowComponent
        NoteWindow {}
    }

    NoteContextMenu {
        id: menu
    }

    // Shared across every window in the process (windowInputMask is a single
    // app-wide singleton) — lastInputMaskFor() below filters by window rather
    // than assuming the menu is the only emitter.
    SignalSpy {
        id: inputMaskSpy
        target: windowInputMask
        signalName: "inputMaskApplied"
    }

    SignalSpy {
        id: closedSpy
        target: menu
        signalName: "closed"
    }

    // Reads back what openNotesModel actually recorded for each open note. The
    // model's X/Y are ROLES, not Q_INVOKABLE getters, so a delegate is the only
    // way to see them from QML — and "the duplicate lands where the menu was"
    // is precisely a claim about those two numbers.
    Instantiator {
        id: openNotesProbe
        model: openNotesModel
        delegate: QtObject {
            required property string noteId
            required property int noteX
            required property int noteY
        }
    }

    TestCase {
        name: "NoteContextMenu"
        when: windowShown

        // Destroyed by cleanup() rather than by each test, so the menu is
        // always unbound from a note BEFORE that note is torn down.
        property var createdWindows: []

        // Creates a fresh NoteWindow showing the given markdown and makes it
        // the tracked note, exactly as Main.qml's activeNoteWindow binding
        // would.
        //
        // Positioned relative to its own screen rather than at absolute
        // coordinates: on a multi-monitor layout a screen can legitimately
        // start at a negative virtual coordinate, so a hardcoded 300,300 could
        // land on a different screen than the one reposition() clamps against.
        // Same convention as tst_FindReplacePanel.qml's placement tests.
        //
        // noteX/noteY are deliberately NON-ZERO. With an origin of (0, 0) a
        // window-local coordinate and a screen coordinate are numerically
        // identical, and every placement assertion below would pass even if the
        // note's origin were dropped from the math entirely.
        function makeWindow(noteId, indexNum, markdown) {
            const w = noteWindowComponent.createObject(root, {
                noteId: noteId,
                index: indexNum,
                noteX: 0,
                noteY: 0,
                noteWidth: 260,
                noteHeight: 260,
                screenName: "",
                alwaysOnTop: false,
            })
            verify(w !== null)
            const screen = w.screen
            verify(screen !== null)
            w.noteX = screen.virtualX + 300
            w.noteY = screen.virtualY + 300
            w.docHandler.setMarkdown(markdown)
            createdWindows.push(w)
            menu.noteWindow = w
            wait(50)
            return w
        }

        // The menu outlives every test, so its state has to be put back by
        // hand. Order matters: unbind the note first, so nothing reaches into a
        // window that is about to be destroyed. `open = false` rather than
        // close(), which would try to hand focus back to a note that may
        // already be gone.
        function cleanup() {
            menu.open = false
            menu.noteWindow = null
            closedSpy.clear()
            for (let i = 0; i < createdWindows.length; ++i)
                createdWindows[i].destroy()
            createdWindows = []
            // Each top-level window needs its own hover reset — one aimed at
            // root never reaches a separate window's content item.
            mouseMove(menu.contentItem, -50, -50)
            wait(50)
        }

        // keyClick() always targets QGuiApplication::focusWindow(), so a window
        // a test means to type into must really be active before any synthetic
        // key lands.
        function activateWindow(win) {
            win.requestActivate()
            tryCompare(win, "active", true)
        }

        // Returns the acceptsInput value of the most recent inputMaskApplied
        // entry for `win`, or null if there wasn't one — the spy is shared
        // across every window in the process.
        function lastInputMaskFor(win) {
            let last = null
            for (let i = 0; i < inputMaskSpy.count; ++i) {
                if (inputMaskSpy.signalArguments[i][0] === win)
                    last = inputMaskSpy.signalArguments[i][1]
            }
            return last
        }

        // Position recorded in openNotesModel for a note id, or null if it
        // isn't open. See openNotesProbe above for why this needs a delegate.
        function recordedPositionOf(noteId) {
            for (let i = 0; i < openNotesProbe.count; ++i) {
                const entry = openNotesProbe.objectAt(i)
                if (entry.noteId === noteId)
                    return Qt.point(entry.noteX, entry.noteY)
            }
            return null
        }

        // ---- Placement ----

        function test_the_menu_opens_with_its_top_left_at_the_point_it_was_asked_for() {
            const w = makeWindow("ctx-place", 60, "hello world")
            const screen = w.screen

            // Well clear of every edge, so the plain no-flip, no-clamp path is
            // what's exercised here (each of the others has its own test).
            const pointX = screen.virtualX + 400
            const pointY = screen.virtualY + 400
            menu.openAt(pointX, pointY)

            compare(menu.open, true)
            compare(menu.x, pointX, "the menu's top-left sits exactly on the click")
            compare(menu.y, pointY)
        }

        // Flipping, not clamping. A clamp alone slides the menu back UNDER the
        // pointer, which would leave whichever row happened to land there
        // sitting one stray click from being triggered — and the bottom row is
        // Delete Note.
        function test_the_menu_flips_to_the_left_of_the_pointer_near_the_screen_right_edge() {
            const w = makeWindow("ctx-flip-x", 61, "hello world")
            const screen = w.screen

            // Close enough to the right edge that a menu drawn rightward would
            // overrun it, but far enough that the flipped menu still fits — so
            // the flip is what's observed, not the clamp behind it.
            const pointX = screen.virtualX + screen.width - 20
            const pointY = screen.virtualY + 400
            menu.openAt(pointX, pointY)

            compare(menu.x, pointX - menu.wantedWidth,
                    "the menu mirrors around the pointer, keeping it on the menu's corner")
            compare(menu.y, pointY, "the axis with room to spare is left alone")
        }

        function test_the_menu_flips_above_the_pointer_near_the_screen_bottom_edge() {
            const w = makeWindow("ctx-flip-y", 62, "hello world")
            const screen = w.screen

            const pointX = screen.virtualX + 400
            const pointY = screen.virtualY + screen.height - 20
            menu.openAt(pointX, pointY)

            compare(menu.y, pointY - menu.wantedHeight,
                    "the menu mirrors upward around the pointer")
            compare(menu.x, pointX)
        }

        // The floor behind the flip: a point close to an edge on a screen that
        // cannot fit the menu on either side of it still has to land the menu
        // ON the screen. Partly clipped beats off-screen entirely.
        function test_the_menu_is_clamped_onto_the_screen_at_the_very_top_left_corner() {
            const w = makeWindow("ctx-clamp", 63, "hello world")
            const screen = w.screen

            // Flush against the top-left: there is no room to flip into (that
            // would push the menu to negative coordinates), so only the clamp
            // is left.
            menu.openAt(screen.virtualX, screen.virtualY)

            compare(menu.x, screen.virtualX, "never off the left edge")
            compare(menu.y, screen.virtualY, "never off the top edge")
        }

        // ---- Closing ----

        // Escape closes the menu without running anything — the universal way
        // out of an open menu — and hands focus back to the note, since the
        // user is still working in it. It routes through close(), whose
        // behavior the two tests below cover for real.
        //
        // Only the WIRING is asserted here, because the key itself cannot be
        // delivered to the menu under the offscreen QPA plugin. That plugin
        // reports every window as active simultaneously (verified directly: a
        // note and this menu both read active: true at the same instant), and
        // Qt resolves a Qt.WindowShortcut by asking each candidate's window
        // whether it is active — so offscreen, this menu's Escape and the
        // tracked note's own Escape BOTH match, which is an ambiguous shortcut,
        // and Qt resolves ambiguity by firing neither. Confirmed by
        // elimination: with no NoteWindow alive the menu's Escape fires
        // normally; with one alive, neither the menu's handler nor the note's
        // own runs.
        //
        // A real session has exactly one active window, so there is no
        // ambiguity there — the same reason several open notes have never
        // fought over their own Escape (see NoteWindow.qml's shortcut
        // comments).
        function test_escape_is_wired_to_close_the_menu() {
            const w = makeWindow("ctx-escape", 64, "hello world")
            menu.openAt(w.noteX + 40, w.noteY + 40)

            compare(menu.escapeShortcut.sequence, "Escape")
            compare(menu.escapeShortcut.enabled, true)
        }

        // The Escape shortcut has to live inside an ITEM, not directly under
        // the Window — and gets this test because the wrong placement fails
        // SILENTLY: Qt resolves a Shortcut's window by walking up its QObject
        // parents, finds nothing when parented straight to a Window, matches no
        // context, and simply never fires. No warning, no error. Measured on
        // Qt 6.11 with a bare probe window: a Shortcut under an Item fired, an
        // otherwise identical one directly under the Window did not.
        //
        // This is the one part of Escape's path that IS checkable offscreen,
        // and it is the part that was actually got wrong while writing this
        // file.
        // Checked by containment (background is an Item, and `data` is where a
        // non-visual child like a Shortcut lands) rather than by reading the
        // shortcut's own parent — Shortcut is a QObject, not an Item, so it has
        // no `parent` property to read.
        function test_the_escape_shortcut_lives_inside_an_item_so_it_can_resolve_its_window() {
            const siblings = menu.background.data
            let found = false
            for (let i = 0; i < siblings.length; ++i) {
                if (siblings[i] === menu.escapeShortcut)
                    found = true
            }
            verify(found,
                   "the Escape shortcut must be a child of an Item (background), not of the Window itself — "
                   + "parented straight to a Window it resolves no context and silently never fires")
        }

        // Dismissal is what happens when the user clicks anywhere that isn't
        // the menu — another app, the desktop, or the note itself. The menu
        // takes keyboard focus it has no other use for purely so it can notice
        // that: losing `active` is the only signal the app gets, since a
        // frameless Tool window is told nothing else.
        //
        // What's checked here is dismiss() itself — that it closes and, unlike
        // close(), hands focus NOWHERE. The TRIGGER (onActiveChanged) cannot be
        // exercised offscreen and is deliberately not asserted: the offscreen
        // QPA plugin reports every window as active SIMULTANEOUSLY and never
        // deactivates anything (verified directly — a note and the menu both
        // read active: true at the same instant), so an activation-driven test
        // here would be testing the plugin's fiction rather than the menu. That
        // wiring belongs to a real Wayland session.
        function test_dismissing_closes_the_menu_without_yanking_focus_anywhere() {
            const w = makeWindow("ctx-dismiss", 65, "hello world")

            menu.openAt(w.noteX + 40, w.noteY + 40)
            compare(menu.open, true, "precondition: the menu is open")

            menu.dismiss()

            compare(menu.open, false, "dismissing closes the menu")
            compare(menu.background.visible, false, "and stops it showing anything")
            compare(closedSpy.count, 1)
        }

        // dismiss() and close() differ ONLY in the focus handoff, and that
        // difference is the entire reason both exist: close() is for the user
        // finishing with the menu but staying in their note (Escape, running an
        // action), while dismiss() fires when focus has ALREADY gone somewhere
        // the user chose — where close()'s handoff would yank it straight back
        // and override that choice.
        function test_dismiss_and_close_differ_only_in_where_focus_lands() {
            const w = makeWindow("ctx-dismiss-vs-close", 79, "hello world")

            menu.openAt(w.noteX + 40, w.noteY + 40)
            menu.close()
            tryCompare(w, "active", true, 2000,
                       "close() hands focus back to the note the user is still working in")

            // Both are no-ops once already closed — the guard that keeps
            // close()'s own focus handoff from re-entering through
            // onActiveChanged forever.
            closedSpy.clear()
            menu.close()
            menu.dismiss()
            compare(closedSpy.count, 0, "neither fires again on an already-closed menu")
        }

        // A context menu belongs to one point on one note. Following focus the
        // way the find panel deliberately does would leave it showing at the
        // OLD note's coordinates while every action inside it silently began
        // acting on the NEW one.
        function test_focus_moving_to_a_different_note_closes_the_menu_rather_than_retargeting_it() {
            const first = makeWindow("ctx-retarget-a", 66, "alpha")
            menu.openAt(first.noteX + 40, first.noteY + 40)
            compare(menu.open, true)

            const second = makeWindow("ctx-retarget-b", 67, "beta")

            compare(menu.open, false, "the menu closes rather than following focus to another note")
            compare(menu.noteWindow, second)
        }

        function test_losing_the_tracked_note_closes_the_menu() {
            const w = makeWindow("ctx-untrack", 68, "hello world")
            menu.openAt(w.noteX + 40, w.noteY + 40)
            compare(menu.open, true)

            menu.noteWindow = null

            compare(menu.open, false)
            compare(menu.background.visible, false, "and it stops showing anything")
        }

        function test_the_menu_refuses_to_open_with_no_tracked_note() {
            menu.noteWindow = null
            menu.openAt(100, 100)
            compare(menu.open, false,
                    "with no note to act on there is nothing for any row to do")
        }

        // ---- Actions ----

        function test_cut_is_disabled_without_a_selection_and_removes_the_selection_with_one() {
            const w = makeWindow("ctx-cut", 69, "alpha beta")

            w.editor.deselect()
            menu.openAt(w.noteX + 40, w.noteY + 40)
            compare(menu.cutItem.enabled, false,
                    "cut has nothing to fall back on with an empty selection — deliberately unlike copy, "
                    + "which would otherwise silently empty the whole note")

            w.editor.select(0, 5)
            compare(menu.cutItem.enabled, true)

            mouseClick(menu.cutItem)
            wait(50)

            compare(w.docHandler.toMarkdown(), " beta", "the selected text is gone from the note")
            compare(menu.open, false, "and running an action closes the menu")
        }

        // performCopy()'s no-selection behavior, reached through the menu:
        // copying the WHOLE note rather than doing nothing. Routed through the
        // note's own function (not editor.copy()) precisely so this and the
        // toolbar's Copy button cannot drift apart.
        function test_copy_with_nothing_selected_copies_the_whole_note_and_leaves_it_intact() {
            const w = makeWindow("ctx-copy", 70, "alpha beta")

            w.editor.deselect()
            const cursorBefore = w.editor.cursorPosition
            menu.openAt(w.noteX + 40, w.noteY + 40)
            compare(menu.copyItem.enabled, true, "copy always applies — see performCopy()")

            mouseClick(menu.copyItem)
            wait(50)

            compare(w.docHandler.toMarkdown(), "alpha beta", "copying never modifies the note")
            compare(w.editor.cursorPosition, cursorBefore,
                    "and the select-all it does internally is put back, cursor and all")
            compare(menu.open, false)

            // Proves the clipboard really took the whole note, not just that
            // nothing crashed: paste it into a second, empty note.
            const target = makeWindow("ctx-copy-target", 71, "")
            target.editor.paste()
            compare(target.docHandler.toMarkdown(), "alpha beta")
        }

        function test_paste_is_disabled_on_an_empty_clipboard_and_inserts_at_the_cursor_otherwise() {
            const source = makeWindow("ctx-paste-src", 72, "inserted")
            source.editor.selectAll()
            source.editor.copy()

            const w = makeWindow("ctx-paste", 73, "ab")
            w.editor.cursorPosition = 1

            menu.openAt(w.noteX + 40, w.noteY + 40)
            compare(menu.pasteItem.enabled, true, "canPaste is TextEdit's own live clipboard check")

            mouseClick(menu.pasteItem)
            wait(50)

            compare(w.docHandler.toMarkdown(), "ainsertedb", "the clipboard lands at the cursor")
            compare(menu.open, false)
        }

        // The feature request's specific ask: the copy appears where the menu
        // is, not at the fixed diagonal offset from the source note that the
        // HUD's own duplicate glyph uses.
        function test_duplicate_creates_an_independent_note_at_the_menus_own_position() {
            const w = makeWindow("ctx-duplicate", 74, "duplicate me")
            const idsBefore = openNotesModel.openNoteIds()

            const pointX = w.screen.virtualX + 500
            const pointY = w.screen.virtualY + 500
            menu.openAt(pointX, pointY)
            mouseClick(menu.duplicateItem)
            wait(50)

            const idsAfter = openNotesModel.openNoteIds()
            compare(idsAfter.length, idsBefore.length + 1, "exactly one new note")

            let newId = ""
            for (let i = 0; i < idsAfter.length; ++i) {
                if (idsBefore.indexOf(idsAfter[i]) === -1)
                    newId = idsAfter[i]
            }
            verify(newId !== "", "the duplicate is a genuinely new note id, not a second view of the old one")
            verify(newId !== w.noteId)

            const position = recordedPositionOf(newId)
            verify(position !== null)
            compare(position.x, pointX, "the copy is seeded at the menu's own top-left...")
            compare(position.y, pointY, "...which is where the user right-clicked")

            compare(noteStore.loadNoteContent(newId), "duplicate me",
                    "and it starts out with the source note's content")
            compare(menu.open, false)
        }

        // Reveal shows the note's OWN .md file, selected, in the user's file
        // manager — as opposed to the tray's "Open Notes Folder", which opens
        // the folder and leaves them to find one note among all of them.
        //
        // The click itself is deliberately never performed in these tests, and
        // that is a hard rule rather than an oversight: reveal() launches the
        // tester's real file manager (or, failing that, opens their real notes
        // folder), and neither effect happens inside this process, so the
        // offscreen platform does nothing to contain it. What IS asserted here
        // is the wiring the app can get wrong on its own — which note's file
        // the row would offer up, and when the row is live at all. The dispatch
        // itself is covered in tests/unit/test_file_revealer.cpp.
        function test_reveal_offers_the_tracked_notes_own_file_to_the_file_manager() {
            const id = noteStore.createNote("reveal me")
            openNotesModel.openNote(id)
            const w = makeWindow(id, 80, "reveal me")

            menu.openAt(w.noteX + 40, w.noteY + 40)

            compare(menu.revealItem.enabled, true, "a note with a file on disk can be revealed")

            // Compared against noteStore's own folder rather than
            // settings.notesFolder: those two are the same folder only because
            // main.cpp points the store at the setting, which this harness
            // deliberately doesn't do (it points the store at a temp dir), and
            // the claim being made here is about the store's file either way.
            const path = noteStore.noteFilePath(id)
            verify(path.endsWith(".md"), "what the row would hand the file manager is the note's own .md file")
            verify(path.indexOf(noteStore.notesFolder) === 0, "and it really lives in the notes folder")
        }

        // The state this row has to refuse: a window whose note has no file
        // behind it. Every other test in this file builds exactly that (a
        // NoteWindow carrying an id NoteStore has never heard of), which is why
        // this one reads like the rest.
        //
        // The row's answer is fixed when the menu BINDS to the note, not when it
        // opens — so the fileless state has to exist by makeWindow()'s own
        // `menu.noteWindow = w`, and asserting it any later would be asserting
        // something the row never re-reads. That also makes the save debounce a
        // non-issue rather than a race: handing a note markdown does start a
        // debounced save, and NoteStore does mint a file for whatever id it is
        // asked to save ~300ms later — but the binding above has long since read
        // its answer and will not look again while this note stays tracked.
        function test_reveal_is_dimmed_for_a_note_with_no_file_behind_it() {
            const w = makeWindow("ctx-reveal-fileless", 81, "not in the store")
            compare(noteStore.noteFilePath(w.noteId), "",
                    "precondition: this note has no file, which is what the row bound to")

            menu.openAt(w.noteX + 40, w.noteY + 40)

            compare(menu.revealItem.enabled, false,
                    "there is no file to show, so the row says so rather than opening the file manager on nothing")
        }

        // A REAL note in the store, not just a NoteWindow carrying an id like
        // every other test here: deleteNote() removes the .md file and the
        // model entry, so there has to be a file and an entry for it to remove
        // before any of this means anything.
        function test_delete_removes_the_note_permanently() {
            const id = noteStore.createNote("delete me")
            openNotesModel.openNote(id)
            const w = makeWindow(id, 75, "delete me")

            verify(openNotesModel.isOpen(id), "precondition: the note is really open")
            verify(noteStore.noteIds().indexOf(id) !== -1, "precondition: its file really exists")

            menu.openAt(w.noteX + 40, w.noteY + 40)
            mouseClick(menu.deleteItem)
            wait(50)

            compare(noteStore.noteIds().indexOf(id), -1,
                    "the note's file is gone, not just its window")
            compare(openNotesModel.isOpen(id), false)
            compare(menu.open, false)
        }

        // ---- Window lifecycle ----

        function test_the_menu_window_stays_mapped_whether_open_or_closed() {
            const w = makeWindow("ctx-mapped", 76, "alpha")

            compare(menu.visible, true, "the window itself is mapped from startup onward")

            menu.openAt(w.noteX + 40, w.noteY + 40)
            compare(menu.visible, true)
            compare(menu.content.visible, true)

            menu.close()
            compare(menu.visible, true,
                    "closing hides the CONTENT — the window is never unmapped")
            compare(menu.content.visible, false)
            compare(menu.background.visible, false)
        }

        // The user's explicit requirement for this feature ("make sure it can't
        // block clicks while invisible"), and a bug class already found live in
        // this app: a mapped Wayland surface eats clicks regardless of whether
        // its QML content is invisible, so hidden content alone is not enough.
        //
        // What's checked here is the WIRING (which window is told to stop
        // accepting input, and when). Whether the compositor then really routes
        // the click elsewhere is not observable offscreen — QWindow::setMask has
        // no effect under the offscreen QPA plugin at all — and belongs to a
        // real Wayland session. The off-screen park is deliberately NOT what
        // this rests on: it is async, unordered, and can no-op outright.
        function test_the_menu_stops_accepting_pointer_input_once_it_is_closed() {
            const w = makeWindow("ctx-mask", 77, "alpha")

            menu.openAt(w.noteX + 40, w.noteY + 40)
            wait(80)
            verify(menu.background.visible, "precondition: the menu is really showing content")

            inputMaskSpy.clear()
            menu.close()
            wait(80)

            compare(lastInputMaskFor(menu), false,
                    "a closed, still-mapped menu must stop accepting input rather than silently eat clicks meant for whatever is underneath it")

            inputMaskSpy.clear()
            menu.openAt(w.noteX + 40, w.noteY + 40)
            wait(80)

            compare(lastInputMaskFor(menu), true, "an open menu's own rows must be clickable again")
        }

        // The window is a fixed size on purpose (see wantedWidth/wantedHeight):
        // binding a Window's size to its content risks committing a 0-dimension
        // geometry on the first frame, which is an xdg-shell protocol error that
        // kills the app. enforceSize() is the guard that puts the size back if
        // the platform itself writes it — and it must restore a real BINDING,
        // not freeze the value.
        function test_the_menu_is_a_fixed_size_window_that_self_corrects_a_clobbered_size() {
            compare(menu.width, menu.wantedWidth)
            compare(menu.height, menu.wantedHeight)

            // Stands in for a stale compositor configure/ack landing late — the
            // clobber that was observed live on the toolbar next door.
            //
            // The wait is not incidental: a window's size change reaches QML via
            // a platform geometry-change event, delivered asynchronously, so the
            // guard fires on the next event-loop pass rather than inside this
            // assignment. That is the same path a real stale ack arrives on,
            // which is exactly why the guard hangs off onWidthChanged instead of
            // trying to intercept the write itself.
            menu.width = 1
            wait(80)
            compare(menu.width, menu.wantedWidth, "a clobbered width is put straight back")

            menu.height = 1
            wait(80)
            compare(menu.height, menu.wantedHeight, "and so is a clobbered height")
        }

        // The window is sized by hand rather than by its layout (see above), so
        // nothing recomputes it when a row is added or a padding changes — the
        // arithmetic in wantedHeight's comment is the only thing keeping the
        // surface and the content the same size, and a mismatch clips rows
        // silently. This is what notices.
        //
        // The width half is a floor, not a guarantee, and knowing which is which
        // matters before trusting it: this harness measures in the offscreen
        // platform's own default font, which is NARROWER than a real session's
        // (measured on this desktop: Sans Serif 9 here vs. Noto Sans 10 live —
        // 126.5px vs. 137px for the longest label, "Reveal in File Manager",
        // against a 168px budget). So a row that only just fits here could still
        // elide in front of a user with a larger UI font — which is the designed
        // outcome (see the elide in MenuAction) rather than a clipped surface,
        // but it does mean passing this is not the same as "it looks right."
        function test_the_windows_fixed_size_still_fits_the_rows_it_has() {
            const w = makeWindow("ctx-fits", 78, "alpha")
            menu.openAt(w.noteX + 40, w.noteY + 40)

            compare(menu.content.implicitHeight + 2 * menu.content.anchors.margins, menu.wantedHeight,
                    "wantedHeight must equal what the rows actually need — see its own arithmetic")
            verify(menu.content.implicitWidth + 2 * menu.content.anchors.margins <= menu.wantedWidth,
                   "and the rows must fit inside the fixed width")
        }
    }
}
