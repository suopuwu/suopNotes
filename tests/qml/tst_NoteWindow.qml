import QtQml
import QtQuick
import QtQuick.Templates
import QtTest
import SuopNotesUi

Item {
    id: root

    ListModel {
        id: testModel
        ListElement { noteId: "note-a"; noteX: 0; noteY: 0; noteWidth: 260; noteHeight: 260; screenName: ""; alwaysOnTop: false }
        ListElement { noteId: "note-b"; noteX: 0; noteY: 0; noteWidth: 260; noteHeight: 260; screenName: ""; alwaysOnTop: false }
        ListElement { noteId: "note-c"; noteX: 0; noteY: 0; noteWidth: 260; noteHeight: 260; screenName: ""; alwaysOnTop: false }
    }

    Instantiator {
        id: instantiator
        model: testModel
        delegate: NoteWindow {}
    }

    NoteWindow {
        id: soloWindow
        noteId: "solo-blur-test"
        index: 50
        noteX: 0
        noteY: 0
        noteWidth: 260
        noteHeight: 260
        screenName: ""
        alwaysOnTop: false
    }

    // A dedicated instance for the delete test, kept separate from
    // soloWindow (reused across the blur tests — deleting it would break
    // those) and from instantiator's testModel (a plain ListModel, not the
    // real openNotesModel deleteNote() actually calls into). Not inside an
    // Instantiator bound to openNotesModel, so calling deleteNote() on it
    // doesn't destroy this object — it can still be inspected afterward.
    NoteWindow {
        id: deleteTestWindow
        noteId: "delete-test-note"
        index: 51
        noteX: 0
        noteY: 0
        noteWidth: 260
        noteHeight: 260
        screenName: ""
        alwaysOnTop: false
    }

    // A note whose origin is deliberately NON-ZERO, unlike the Instantiator's
    // notes above (which have no persisted position at all). The
    // screen-coordinate tests below need it: with an origin of (0, 0) a
    // window-local coordinate and a screen coordinate are numerically
    // identical, so a test using those notes would pass even if the origin
    // were dropped from the math entirely.
    NoteWindow {
        id: contextMenuTestWindow
        noteId: "context-menu-test-note"
        index: 52
        noteX: 300
        noteY: 400
        noteWidth: 260
        noteHeight: 260
        screenName: ""
        alwaysOnTop: false
    }

    // A note dedicated to the code-mode tests at the end of this file. Kept
    // apart from the Instantiator's notes and from soloWindow: code mode is a
    // per-note override persisted against a note's OWN id, so the tests need
    // one note they can turn into a code note (swapping its font, adding a
    // gutter, shifting its editor) without changing what any other test in
    // this file sees.
    NoteWindow {
        id: codeModeTestWindow
        noteId: "code-mode-test-note"
        index: 53
        noteX: 0
        noteY: 0
        noteWidth: 260
        noteHeight: 260
        screenName: ""
        alwaysOnTop: false
    }

    // The one aggregated TickTick task note — the only note in the app that
    // refreshes itself. Its id comes from the module rather than being typed
    // out here, exactly as NoteWindow's own isTickTickTaskNote asks for it, so
    // this stays a real task note even if the id scheme changes. Falls back to
    // an id that is deliberately NOT a task note when the module is not built,
    // which the tests below skip on anyway.
    NoteWindow {
        id: taskNoteTestWindow
        noteId: tickTick !== null ? tickTick.sync.taskNoteId() : "ticktick-module-not-built"
        index: 54
        noteX: 0
        noteY: 0
        noteWidth: 260
        noteHeight: 260
        screenName: ""
        alwaysOnTop: false
    }

    SignalSpy {
        id: blurSpy
        target: blurEffect
        signalName: "appliedTo"
    }

    // Targeted per-test (the notes come from an Instantiator, so there's no
    // one instance to bind to at load time).
    SignalSpy {
        id: contextMenuSpy
        signalName: "contextMenuRequested"
    }

    // Deferred (not instantiated until createObject() is called) so each
    // test that uses it gets a fresh NoteWindow, unlike soloWindow/
    // deleteTestWindow above which are created once at file load.
    Component {
        id: restoreOrderTestComponent
        NoteWindow {}
    }

    TestCase {
        name: "NoteWindow"
        when: windowShown

        // This Qt version's QML TestCase has no built-in "type this whole
        // string" helper (only keyClick(), one key/char at a time) — a
        // plain per-character loop over keyClick() is the simplest way to
        // simulate real typing for the auto-list-shorthand/list-continuation
        // tests below.
        function typeText(text) {
            for (let i = 0; i < text.length; ++i)
                keyClick(text[i])
        }

        function test_instantiator_creates_one_window_per_model_entry() {
            compare(instantiator.count, 3)
        }

        function test_each_window_gets_the_matching_note_id() {
            compare(instantiator.objectAt(0).noteId, "note-a")
            compare(instantiator.objectAt(1).noteId, "note-b")
            compare(instantiator.objectAt(2).noteId, "note-c")
        }

        function test_windows_are_frameless() {
            for (let i = 0; i < instantiator.count; ++i)
                verify((instantiator.objectAt(i).flags & Qt.FramelessWindowHint) !== 0)
        }

        function test_windows_use_tool_flag_to_hide_from_taskbar() {
            for (let i = 0; i < instantiator.count; ++i)
                verify((instantiator.objectAt(i).flags & Qt.Tool) !== 0)
        }

        // Main.qml's toolbar-persistence fix (feature request: the toolbar
        // used to disappear after a while even though the note was still
        // focused) relies on this `hovered` alias as a mouse-presence signal
        // independent of window activation — verify it actually tracks the
        // pointer.
        function test_hovered_tracks_whether_the_pointer_is_over_the_note() {
            const win = instantiator.objectAt(0)

            mouseMove(win.contentItem, win.width / 2, win.height / 2)
            tryCompare(win, "hovered", true)

            mouseMove(win.contentItem, -50, -50)
            tryCompare(win, "hovered", false)
        }

        // Regression test: restoring a note's already-persisted width/height
        // in Component.onCompleted used to fire persistSize() on the very
        // assignments that were just restoring known-good values (each of
        // width/height is set imperatively, and each triggers
        // onWidthChanged/onHeightChanged). In the real app, driven by a live
        // Instantiator bound to openNotesModel, that write-back could
        // corrupt the OTHER dimension: the width assignment's persistSize()
        // call fires before the height assignment has run, so it captures
        // the window's height still at its 260 default and writes that to
        // Settings — which round-trips synchronously through
        // openNotesModel's dataChanged back into this exact delegate's own
        // live noteHeight property, before the next line ever reads it.
        // Not practical to reproduce that full loop here without a live
        // Instantiator bound to openNotesModel (this component is created
        // standalone), but the fix's actual guarantee is directly
        // testable: restoring already-known geometry must never write
        // anything to Settings at all — a note that was never explicitly
        // opened must not become "open" just because a window showing its
        // last-known geometry was constructed.
        function test_restoring_persisted_geometry_applies_it_without_writing_to_settings() {
            const w = restoreOrderTestComponent.createObject(root, {
                noteId: "restore-order-test",
                index: 70,
                noteX: 0,
                noteY: 0,
                noteWidth: 300,
                noteHeight: 400,
                screenName: "",
                alwaysOnTop: false,
            })
            verify(w !== null)

            compare(w.width, 300)
            compare(w.height, 400)
            verify(!openNotesModel.isOpen("restore-order-test"),
                   "restoring already-known geometry must not implicitly open/insert a Settings entry")

            w.destroy()
        }

        // Middle is included alongside right (feature request: middle-click
        // autoscroll) — left stays excluded so normal editing clicks keep
        // falling through to the editor underneath, except transiently
        // while an autoscroll is armed (see mouseDragArea's own comment).
        function test_mouse_drag_is_scoped_to_right_and_middle_button_only() {
            const area = instantiator.objectAt(0).mouseDragArea
            compare(area.acceptedButtons, Qt.RightButton | Qt.MiddleButton)
        }

        function test_touch_drag_accepts_touch_input() {
            const handler = instantiator.objectAt(0).touchDragHandler
            compare(handler.acceptedDevices, PointerDevice.TouchScreen)
        }

        function test_editor_right_click_menu_is_disabled_so_drag_owns_the_button() {
            const editor = instantiator.objectAt(0).editor
            compare(editor.ContextMenu.menu, null)
        }

        // The structural tests above only check the area's config, not
        // whether a real right-button press over the editor (where the user
        // actually grabs a note) ever reaches it — simulates the real
        // gesture end-to-end instead of requiring a human at a Wayland
        // session (which blocks synthetic input injection).
        function test_right_click_over_editor_reaches_the_drag_area() {
            const win = instantiator.objectAt(0)
            const area = win.mouseDragArea
            const editor = win.editor

            const cx = editor.width / 2
            const cy = editor.height / 2

            mousePress(editor, cx, cy, Qt.RightButton)
            verify(area.pressed, "the drag area, not TextInput underneath, should own a right-button press over the editor")
            mouseRelease(editor, cx, cy, Qt.RightButton)
        }

        // The right button does two jobs (feature request), told apart by
        // MOVEMENT alone: a right-drag moves the note, a stationary
        // right-click opens the context menu. The next few tests pin down
        // that split from both sides — it is the whole reason
        // startSystemMove() stays deferred behind a threshold rather than
        // firing on press.
        function test_right_click_drag_past_threshold_starts_a_move_and_asks_for_no_menu() {
            const win = instantiator.objectAt(0)
            const area = win.mouseDragArea
            const editor = win.editor
            const cx = editor.width / 2
            const cy = editor.height / 2

            contextMenuSpy.target = win
            contextMenuSpy.clear()

            mouseDrag(editor, cx, cy, 20, 20, Qt.RightButton, Qt.NoModifier, 50)

            verify(area.rightDragStarted,
                   "moving past the threshold while right-button is held should start a drag")
            compare(contextMenuSpy.count, 0,
                    "a right-click that became a drag must not ALSO open the menu on release")
        }

        function test_stationary_right_click_asks_for_the_context_menu() {
            const win = instantiator.objectAt(0)
            const area = win.mouseDragArea
            const editor = win.editor
            const cx = editor.width / 2
            const cy = editor.height / 2

            contextMenuSpy.target = win
            contextMenuSpy.clear()

            mousePress(editor, cx, cy, Qt.RightButton)
            mouseRelease(editor, cx, cy, Qt.RightButton)

            compare(contextMenuSpy.count, 1,
                    "a right-click that never moved is a click, and asks for the menu")
            verify(!area.rightDragStarted, "and it must not have moved the note")
        }

        // Movement UNDER the threshold is still a click — a plain right-click
        // carries a pixel or two of hand tremor, and treating that as a drag
        // would make the menu feel like it randomly refused to open.
        function test_right_click_that_barely_moves_is_still_a_menu_click() {
            const win = instantiator.objectAt(0)
            const area = win.mouseDragArea
            const editor = win.editor
            const cx = editor.width / 2
            const cy = editor.height / 2

            contextMenuSpy.target = win
            contextMenuSpy.clear()

            // 2px, inside the 4px threshold.
            mousePress(editor, cx, cy, Qt.RightButton)
            mouseMove(editor, cx + 2, cy + 2)
            mouseRelease(editor, cx + 2, cy + 2, Qt.RightButton)

            verify(!area.rightDragStarted, "a tremor is not a drag")
            compare(contextMenuSpy.count, 1, "so it still opens the menu")
        }

        // The menu point is reported in SCREEN coordinates, resolved through
        // the note's own model-backed origin — NOT through mapToGlobal(),
        // which reads a Window position Wayland never updates after a
        // compositor-driven drag. See NoteWindow.noteOrigin().
        //
        // Uses contextMenuTestWindow, whose origin is deliberately non-zero:
        // with an origin of (0, 0) a window coordinate and a screen coordinate
        // are numerically identical, and this test would pass even if the
        // origin were dropped from the math entirely.
        function test_the_context_menu_point_is_the_click_in_screen_coordinates() {
            const win = contextMenuTestWindow
            const editor = win.editor
            const cx = editor.width / 2
            const cy = editor.height / 2

            contextMenuSpy.target = win
            contextMenuSpy.clear()

            mousePress(editor, cx, cy, Qt.RightButton)
            mouseRelease(editor, cx, cy, Qt.RightButton)

            compare(contextMenuSpy.count, 1)

            // mapToItem(null) is the click in the note's own WINDOW
            // coordinates; the note's origin is what turns that into a point
            // on the screen.
            const inWindow = editor.mapToItem(null, cx, cy)
            const origin = win.noteOrigin()
            compare(origin.x, 300, "precondition: this note's origin really is offset")
            compare(origin.y, 400)

            compare(contextMenuSpy.signalArguments[0][0], Math.round(origin.x + inWindow.x))
            compare(contextMenuSpy.signalArguments[0][1], Math.round(origin.y + inWindow.y))
        }

        // Chrome's convention (which this app's autoscroll follows) is that
        // ANY button cancels an armed autoscroll rather than also performing
        // that button's normal action — so the right-click that cancels must
        // not leave a context menu behind. This is the case that
        // rightClickCandidate exists for: rightDragStarted alone still reads
        // false here, from whatever gesture came before.
        function test_right_click_that_cancels_an_armed_autoscroll_asks_for_no_menu() {
            const win = instantiator.objectAt(0)
            const area = win.mouseDragArea
            const editor = win.editor
            const cx = editor.width / 2
            const cy = editor.height / 2

            contextMenuSpy.target = win
            contextMenuSpy.clear()

            mouseClick(editor, cx, cy, Qt.MiddleButton)
            verify(area.middleScrollArmed, "precondition: middle-click arms autoscroll")

            mousePress(editor, cx, cy, Qt.RightButton)
            mouseRelease(editor, cx, cy, Qt.RightButton)

            verify(!area.middleScrollArmed, "the right-click disarms the autoscroll")
            compare(contextMenuSpy.count, 0,
                    "and that click is spent on the cancel — it must not also open the menu")
        }

        // duplicateNote() (the HUD's glyph) and duplicateNoteAt() (the context
        // menu's row) are the same operation with a different position, which
        // is why the position is a parameter rather than baked in.
        function test_duplicate_at_an_explicit_position_seeds_the_copy_there() {
            const win = contextMenuTestWindow
            win.docHandler.setMarkdown("duplicate me")
            const idsBefore = openNotesModel.openNoteIds()

            win.duplicateNoteAt(640, 480)

            const idsAfter = openNotesModel.openNoteIds()
            compare(idsAfter.length, idsBefore.length + 1)

            let newId = ""
            for (let i = 0; i < idsAfter.length; ++i) {
                if (idsBefore.indexOf(idsAfter[i]) === -1)
                    newId = idsAfter[i]
            }
            verify(newId !== "")
            compare(noteStore.loadNoteContent(newId), "duplicate me",
                    "the copy starts out with the source note's content")

            openNotesModel.closeNote(newId)
            noteStore.deleteNote(newId)
        }

        // An external change to a note's file (a sync tool, another editor) is
        // pulled back into the open note by editor.loadFromDisk() — the reload
        // the noteStore.onNoteContentChangedExternally handler drives. Here the
        // reload mechanism itself is exercised directly (the file is changed,
        // then reloaded) for determinism; that it fires only when there are no
        // unsaved local edits is guarded by NoteStore::hasUnsavedChanges, which
        // is unit-tested separately.
        function test_an_external_file_change_can_be_reloaded_into_the_open_note() {
            const id = noteStore.createNote("original text")
            const win = restoreOrderTestComponent.createObject(root, {
                noteId: id,
                index: 91,
                noteX: 0,
                noteY: 0,
                noteWidth: 260,
                noteHeight: 260,
                screenName: "",
                alwaysOnTop: false,
            })
            verify(win !== null)
            tryCompare(win.editor, "ready", true)
            compare(win.docHandler.toMarkdown(), "original text",
                    "the window loads its file's content on open")

            // The file changes on disk underneath the open note.
            noteStore.saveNoteContent(id, "changed on disk")
            wait(500) // past NoteStore's 300ms save debounce, so the write lands
            verify(!noteStore.hasUnsavedChanges(id))

            win.editor.loadFromDisk(false)
            compare(win.docHandler.toMarkdown(), "changed on disk",
                    "reloading pulls the new on-disk content into the editor")

            noteStore.deleteNote(id)
            win.destroy()
        }

        // The bug this guards: the TickTick task note reloads itself after
        // nearly every user action (a commit, the hourly pull, a pull fired
        // by window activation or a click in the note body), and that reload
        // used to go through docHandler.setMarkdown() — which clears Qt's
        // native undo stack by design (see DocumentHandler.h's doc comment
        // on setMarkdown). That made Ctrl+Z do nothing in that note almost
        // all the time. loadFromDisk(false) — what
        // onNoteContentChangedExternally's handler calls — now routes
        // through replaceAllTextPreservingUndo() instead, which records the
        // reload as one undo step rather than wiping history.
        function test_reloading_the_task_note_after_a_commit_leaves_the_users_undo_history_intact() {
            const id = noteStore.createNote("original text")
            const win = restoreOrderTestComponent.createObject(root, {
                noteId: id,
                index: 92,
                noteX: 0,
                noteY: 0,
                noteWidth: 260,
                noteHeight: 260,
                screenName: "",
                alwaysOnTop: false,
            })
            verify(win !== null)
            tryCompare(win.editor, "ready", true)

            // An edit the user makes before the reload — the thing that must
            // survive it.
            win.requestActivate()
            tryCompare(win, "active", true)
            win.editor.forceActiveFocus()
            tryCompare(win.editor, "activeFocus", true)
            win.editor.cursorPosition = win.editor.length
            typeText(" committed task")
            verify(win.editor.canUndo, "precondition: typing should leave undo history")
            const afterTyping = win.docHandler.toMarkdown()

            // The file changes on disk underneath the open note — a commit or
            // a pull, simulated here the same way
            // test_an_external_file_change_can_be_reloaded_into_the_open_note
            // above does — and the note reloads without resetting its
            // cursor, exactly what onNoteContentChangedExternally's handler
            // does.
            noteStore.saveNoteContent(id, "synced from elsewhere")
            wait(500) // past NoteStore's 300ms save debounce, so the write lands
            verify(!noteStore.hasUnsavedChanges(id))

            win.editor.loadFromDisk(false)
            compare(win.docHandler.toMarkdown(), "synced from elsewhere",
                    "reload still pulls in the new on-disk content")

            verify(win.editor.canUndo,
                   "the pre-reload edit must still be undoable, not wiped by the reload")

            // First undo steps back through the reload itself, landing on
            // exactly what was in the editor right before it fired — undo
            // doing SOMETHING, not nothing.
            win.editor.undo()
            compare(win.docHandler.toMarkdown(), afterTyping,
                    "undo should step back through the reload rather than doing nothing")

            // Second undo reaches back through the user's own pre-reload
            // edit — the history this bug used to wipe out entirely.
            win.editor.undo()
            compare(win.docHandler.toMarkdown(), "original text",
                    "undo should keep walking back through the committed edit made before the reload")

            noteStore.deleteNote(id)
            win.destroy()
        }

        // revealInFileManager() shows this note's own .md file, selected, in the
        // user's file manager — what the context menu's Reveal row calls.
        //
        // A note with NO file behind it is the only input this can be called
        // with here, and that is the point rather than a limitation: it is the
        // one path that stops before the D-Bus call, so it exercises the whole
        // QML->C++ seam (the function resolves, fileRevealer is reachable from
        // QML, reveal() is invokable) while launching nothing. Every other input
        // would open the tester's real file manager, which no offscreen platform
        // contains — that half is verified in tests/unit/test_file_revealer.cpp
        // and against a live session by hand.
        //
        // A note of its OWN, never typed into and never handed any markdown,
        // rather than one of the shared windows above — and that is load-bearing
        // rather than tidiness. Handing a note content emits contentEdited, which
        // NoteWindow answers with a debounced saveNoteContent(), and NoteStore
        // mints a filename for whatever id it is asked to save — so any shared
        // note here has a real file behind it the moment some earlier test's
        // debounce lands, and "has no file" would depend on test order and a
        // 300ms race. An untouched note has no file, deterministically.
        function test_revealing_a_note_with_no_file_behind_it_asks_for_nothing() {
            const win = restoreOrderTestComponent.createObject(root, {
                noteId: "reveal-seam-test",
                index: 90,
                noteX: 0,
                noteY: 0,
                noteWidth: 260,
                noteHeight: 260,
                screenName: "",
                alwaysOnTop: false,
            })
            verify(win !== null)

            // Past NoteStore's own 300ms save debounce: proves this note has no
            // file because nothing ever wrote one, not because we asked too soon.
            wait(500)
            compare(noteStore.noteFilePath(win.noteId), "",
                    "precondition: this note has no file on disk to show")

            // Returns false rather than throwing: proof the call really reached
            // FileRevealer and was declined there, not that it silently failed
            // to resolve.
            compare(fileRevealer.reveal(noteStore.noteFilePath(win.noteId)), false,
                    "a note with no file reveals nothing, rather than opening the file manager on a missing path")

            // The real call the menu row makes, end to end. Nothing is asserted
            // afterward because there is nothing to assert: it must simply not
            // throw, and must not launch anything.
            win.revealInFileManager()

            win.destroy()
        }

        // linkUrlAt is the hit-test behind middle-click-to-open and the hover
        // tooltip (feature request: clickable links). It reports a url only for
        // a position inside a link's VISIBLE text, and never for an image's
        // ![alt](src) (the "!" marks it an image, rendered as a block instead).
        function test_linkUrlAt_reports_the_url_only_inside_the_visible_link_text() {
            const win = contextMenuTestWindow
            win.docHandler.setMarkdown("see [the site](https://example.com) now")

            compare(win.docHandler.linkUrlAt(6), "https://example.com",
                    "a position inside the link text resolves to the link's url")
            compare(win.docHandler.linkUrlAt(0), "",
                    "a position in ordinary prose before the link resolves to no url")
            compare(win.docHandler.linkUrlAt(37), "",
                    "a position in ordinary prose after the link resolves to no url")

            win.docHandler.setMarkdown("![alt](img/x.png)")
            compare(win.docHandler.linkUrlAt(3), "",
                    "an image's alt text is not a link, so it reports no url")

            win.docHandler.setMarkdown("")
        }

        // Typing a bare URL then a space rewrites it into [url](url) markdown
        // (feature request), adding https:// to the target when the typed text
        // had no scheme — so a scheme-less "example.com" still becomes a working
        // link. tryAutoLinkUrl keys off the just-typed trailing space.
        function test_typing_a_url_then_space_converts_it_to_a_markdown_link() {
            const win = contextMenuTestWindow

            win.docHandler.setMarkdown("visit example.com ")
            win.docHandler.tryAutoLinkUrl(win.editor.length)
            compare(win.editor.text, "visit [example.com](https://example.com) ",
                    "a scheme-less domain gains https:// in the link target, text unchanged")

            win.docHandler.setMarkdown("see https://foo.io ")
            win.docHandler.tryAutoLinkUrl(win.editor.length)
            compare(win.editor.text, "see [https://foo.io](https://foo.io) ",
                    "an already-schemed URL keeps its scheme")

            win.docHandler.setMarkdown("at www.bar.com ")
            win.docHandler.tryAutoLinkUrl(win.editor.length)
            compare(win.editor.text, "at [www.bar.com](https://www.bar.com) ",
                    "a www. host is linked with an added scheme")

            win.docHandler.setMarkdown("")
        }

        // Auto-linking must not fire on things that merely contain a dot: an
        // ordinary word, a note filename (.md), or an already-linked URL.
        function test_auto_link_leaves_non_urls_and_existing_links_alone() {
            const win = contextMenuTestWindow

            win.docHandler.setMarkdown("edit readme.md ")
            win.docHandler.tryAutoLinkUrl(win.editor.length)
            compare(win.editor.text, "edit readme.md ", "a .md filename is not a link")

            win.docHandler.setMarkdown("say hello ")
            win.docHandler.tryAutoLinkUrl(win.editor.length)
            compare(win.editor.text, "say hello ", "a plain word is not a link")

            win.docHandler.setMarkdown("[example.com](https://example.com) ")
            win.docHandler.tryAutoLinkUrl(win.editor.length)
            compare(win.editor.text, "[example.com](https://example.com) ",
                    "an already-markdown link is not converted again")

            win.docHandler.setMarkdown("")
        }

        // The toolbar's Insert Image button drops an empty "![]()" on its own
        // line, which imageRanges then reports so NoteWindow can draw a Browse
        // placeholder for it.
        function test_insertImage_adds_an_empty_image_that_imageRanges_reports() {
            const win = contextMenuTestWindow
            win.docHandler.setMarkdown("hello")

            win.docHandler.insertImage(win.editor.length)

            verify(win.editor.text.indexOf("![]()") !== -1,
                   "an empty image placeholder is inserted into the buffer")
            const ranges = win.docHandler.imageRanges
            compare(ranges.length, 1, "the empty image is reported as one image range")
            compare(ranges[0].src, "", "with an empty src, so QML draws a Browse placeholder")

            win.docHandler.setMarkdown("")
        }

        // After the Browse dialog picks a file (copied in via importImage), the
        // chosen path is written into the empty image's src, leaving the alt
        // text alone.
        function test_setImageSourceAt_fills_in_an_empty_images_src() {
            const win = contextMenuTestWindow
            win.docHandler.setMarkdown("![alt]()")

            win.docHandler.setImageSourceAt(0, "img/cat.png")

            compare(win.editor.text, "![alt](img/cat.png)",
                    "the src is filled in and the alt text preserved")
            const ranges = win.docHandler.imageRanges
            compare(ranges.length, 1)
            compare(ranges[0].src, "img/cat.png")
            compare(ranges[0].alt, "alt")

            win.docHandler.setMarkdown("")
        }

        // Only a whole-line image is reported (and so rendered) — an image
        // sharing its line with other text is left as raw markdown.
        function test_imageRanges_reports_only_whole_line_images() {
            const win = contextMenuTestWindow
            win.docHandler.setMarkdown("see ![x](img/x.png) inline")
            compare(win.docHandler.imageRanges.length, 0,
                    "an inline image mid-sentence is not reported as a block image")
            win.docHandler.setMarkdown("")
        }

        // Regression: the empty-src placeholder ("Select image…") rendered
        // NOTHING on the real compositor (bug found live) because a bare QQC2
        // Basic Button uses an IconLabel contentItem, which silently paints
        // no content in this app — the exact failure SelectionToolbar.qml's
        // ToolbarButtonLabel and FindReplacePanel.qml's PanelButton already
        // work around. The fix spells out an explicit Text contentItem. This
        // guards against a regression back to the default IconLabel: the
        // offscreen harness can't reproduce the blank painting itself (a bare
        // Button still reports visible/enabled fine here), so the structural
        // assertion — placeholder contentItem is a Text showing the button's
        // label — is what keeps the fix from being silently undone.
        function test_empty_image_placeholder_uses_a_visible_text_label() {
            const win = contextMenuTestWindow
            win.docHandler.setMarkdown("![]()")
            // The overlay's ListModel is synced from imageRanges via a signal
            // handler, and the Repeater builds its delegate a frame later.
            // (No message arg — tryCompare's 4th positional is the timeout.)
            tryCompare(win.imageRepeater, "count", 1)

            const delegate = win.imageRepeater.itemAt(0)
            verify(delegate, "the delegate exists")
            compare(delegate.src, "",
                    "precondition: this delegate is the empty-src placeholder")
            const button = delegate.placeholderButton
            verify(button, "the delegate exposes its placeholder button")
            compare(button.text, "Select image…",
                    "the placeholder carries the Browse prompt as its label")

            // The load-bearing part of the fix: the contentItem is an explicit
            // Text carrying the label, NOT Basic's default IconLabel (which
            // rendered nothing on the real compositor). This is asserted
            // structurally rather than by pixels because the offscreen harness
            // can't reproduce the blank painting — but it IS what fails if
            // someone reverts to a bare Button and lets the default IconLabel
            // back in.
            const content = button.contentItem
            verify(content, "the button has an explicit contentItem")
            verify(content instanceof Text,
                   "which is a plain Text, not the Basic style's blank IconLabel")
            compare(content.text, "Select image…",
                    "and it actually carries the visible label")

            // Second regression, same button (also found live): with a custom
            // contentItem AND background both overridden, the Button's implicit
            // sizing collapsed to width 0 — which both clipped the centre-aligned
            // label off its own left edge ("Select image…" showed as "…t image…")
            // and left no hit area to click. The fix sizes it explicitly from the
            // content, so the button must be at least as wide as its label plus
            // its own horizontal padding.
            verify(button.width >= button.implicitContentWidth + button.leftPadding + button.rightPadding,
                   "the placeholder is sized to fit its label, not collapsed to zero width")
            verify(button.width > 0, "and so has a real, clickable hit area")

            win.docHandler.setMarkdown("")
        }

        // The placeholder button lives inside the image overlay, which is a
        // descendant of the editor's EditorDragFilter (an ancestor mouse-event
        // filter for drag-selection). Found live ("clicking it does nothing"):
        // that filter consumed the button's MouseButtonRelease — it fires for
        // EVERY descendant, and a Button has no contentHeight, so its
        // "is this a below-content drag?" guard read 0 and always matched — so
        // the click never completed. The fix scopes the filter to the editor
        // item alone. This drives a real click through the same event path and
        // checks the button's onClicked handler actually ran (it records the
        // target position into imageBrowseTarget before opening the file dialog).
        function test_clicking_empty_image_placeholder_runs_its_action() {
            const win = contextMenuTestWindow
            // A second line so the cursor can rest OFF the image's own line —
            // on the image line the raw "![]()" is revealed for editing and the
            // placeholder is intentionally hidden.
            win.docHandler.setMarkdown("![]()\n\nafter")
            win.editor.cursorPosition = win.editor.length
            tryCompare(win.imageRepeater, "count", 1)

            const delegate = win.imageRepeater.itemAt(0)
            verify(delegate, "the delegate exists")
            const button = delegate.placeholderButton
            // The delegate only reveals the button once its reserved band has a
            // real height (a frame after the edit settles) — wait for that so
            // the click lands on a visible, sized target.
            tryVerify(() => button.visible && button.width > 0 && button.height > 0)

            compare(win.imageBrowseTarget, -1,
                    "precondition: nothing is being browsed for yet")
            mouseClick(button)
            // If the release still got eaten (or the button had no hit area),
            // onClicked never runs and this stays -1. On success it holds the
            // clicked image's start position, ready for the file dialog.
            tryCompare(win, "imageBrowseTarget", delegate.start)

            // Reset: dismiss the file dialog the click opened and clear state so
            // the shared window is clean for the next test.
            win.imageFileDialog.close()
            win.imageBrowseTarget = -1
            win.docHandler.setMarkdown("")
        }

        // The placeholder is a clickable control, so the pointer should say so
        // (a pointing hand) and the button should visibly light up on hover —
        // like every other clickable in the note. Because mouseDragArea sits on
        // top of the editor and swallows hover, that state is pointer-driven
        // from mouseDragArea (see hoveredImagePlaceholderStart), exactly as the
        // checkbox highlight and link tooltip are. This checks that moving the
        // pointer onto the button flags it (which is what turns on both the
        // hover styling and the PointingHandCursor) and moving off clears it.
        function test_hovering_empty_image_placeholder_flags_it_for_cursor_and_highlight() {
            const win = contextMenuTestWindow
            win.docHandler.setMarkdown("![]()\n\nafter")
            win.editor.cursorPosition = win.editor.length
            tryCompare(win.imageRepeater, "count", 1)

            const delegate = win.imageRepeater.itemAt(0)
            verify(delegate, "the delegate exists")
            const button = delegate.placeholderButton
            tryVerify(() => button.visible && button.width > 0 && button.height > 0)

            compare(win.hoveredImagePlaceholderStart, -1,
                    "precondition: nothing is hovered yet")
            verify(!button.placeholderHovered)

            // Onto the button: mouseDragArea's hover handler maps the pointer
            // into editor space and recognises the placeholder under it.
            mouseMove(button, button.width / 2, button.height / 2)
            tryCompare(win, "hoveredImagePlaceholderStart", delegate.start)
            verify(button.placeholderHovered,
                   "the button reads itself as hovered, so its styling firms up")

            // Off the button but still over the editor (the empty area well below
            // the image): the flag clears, so the cursor drops back to the I-beam.
            mouseMove(win.editor, button.width / 2, win.editor.height - 5)
            tryCompare(win, "hoveredImagePlaceholderStart", -1)
            verify(!button.placeholderHovered)

            win.docHandler.setMarkdown("")
        }

        // noteOrigin() is what every screen-coordinate consumer in this file
        // goes through, so its fallback rule is worth stating outright: a note
        // that has never been positioned has no model-backed origin to read,
        // and its own Window x/y is the only record of where it sits.
        function test_note_origin_falls_back_to_the_window_position_for_a_never_positioned_note() {
            const win = instantiator.objectAt(0)
            compare(win.noteX, 0, "precondition: this note has no persisted position")
            compare(win.noteY, 0)

            const origin = win.noteOrigin()
            compare(origin.x, win.x)
            compare(origin.y, win.y)
        }

        // Middle-mouse autoscroll (feature request, Chrome-style): a
        // middle-click arms it; moving the pointer afterward — with no
        // button held — scrolls the note continuously in that direction.
        // Middle-mouse autoscroll (feature request, Chrome-style): a
        // middle-click arms it; moving the pointer afterward — with no
        // button held — scrolls the note continuously in that direction.
        function test_middle_click_arms_autoscroll_and_movement_scrolls_the_note() {
            const win = instantiator.objectAt(0)
            const area = win.mouseDragArea
            const flick = win.editorFlick
            const editor = win.editor

            let many = ""
            for (let i = 0; i < 60; ++i)
                many += "line " + i + "\n\n"
            win.docHandler.setMarkdown(many)
            wait(50)
            // setMarkdown() here (unlike a fresh note's initial load) does
            // NOT go through Component.onCompleted's own top-scroll fix
            // (req. 10) — that only runs once, when the editor is first
            // created — so this editor, being reused across the whole test
            // file, is left scrolled to the bottom by the same
            // cursor-rides-to-the-end mechanism that fix addresses. Reset
            // it explicitly so a small, fixed y coordinate below reliably
            // lands within the actual visible (and thus hit-testable)
            // window area, instead of somewhere off-screen above a
            // bottom-scrolled viewport.
            editor.cursorPosition = 0
            wait(50)
            verify(flick.contentHeight > flick.height, "precondition: content must overflow to scroll at all")
            compare(flick.contentY, 0, "precondition: the viewport must actually be scrolled to the top")

            const cx = editor.width / 2
            // Clear of the hover HUD, which occupies the top-right corner and
            // deliberately swallows every button press over itself. It appears
            // as soon as the pointer enters the note, and at this 260px test
            // width its icon row reaches past the horizontal centre — so a
            // press up at y=20 lands on the HUD, not the editor, and autoscroll
            // never arms. Chosen against the HUD's HEIGHT (26 + 4 margin, fixed)
            // rather than dodging its width, which grows with each icon added.
            const cy = 40

            mouseClick(editor, cx, cy, Qt.MiddleButton)
            verify(area.middleScrollArmed, "a middle-click should arm autoscroll")

            const before = flick.contentY
            mouseMove(editor, cx, cy + 60)
            wait(200) // let a few of the timer's 16ms ticks actually run

            verify(flick.contentY > before,
                   "moving the pointer below the origin should scroll the note downward")

            // A further click anywhere disarms it again. Targets `area`
            // itself (fixed window coordinates), not `editor` — the
            // autoscroll that just happened moved editorFlick's contentY,
            // which shifts where editor's OWN local (cx, cy) now maps to
            // on screen; area's local coordinate space doesn't move.
            mouseClick(area, cx, cy, Qt.LeftButton)
            verify(!area.middleScrollArmed, "a subsequent click should disarm autoscroll")

            win.docHandler.setMarkdown("hi")
            wait(50)
        }

        // Chrome shows a distinct scroll-mode cursor while autoscroll is
        // armed (feature request).
        function test_middle_click_autoscroll_shows_a_scroll_cursor_while_armed() {
            const win = instantiator.objectAt(0)
            const area = win.mouseDragArea
            const flick = win.editorFlick
            const editor = win.editor

            let many = ""
            for (let i = 0; i < 60; ++i)
                many += "line " + i + "\n\n"
            win.docHandler.setMarkdown(many)
            wait(50)
            editor.cursorPosition = 0
            wait(50)

            const cx = editor.width / 2
            // Clear of the hover HUD, which occupies the top-right corner and
            // deliberately swallows every button press over itself. It appears
            // as soon as the pointer enters the note, and at this 260px test
            // width its icon row reaches past the horizontal centre — so a
            // press up at y=20 lands on the HUD, not the editor, and autoscroll
            // never arms. Chosen against the HUD's HEIGHT (26 + 4 margin, fixed)
            // rather than dodging its width, which grows with each icon added.
            const cy = 40

            verify(area.cursorShape !== Qt.SizeAllCursor, "precondition: not the scroll cursor before arming")

            mouseClick(editor, cx, cy, Qt.MiddleButton)
            compare(area.cursorShape, Qt.SizeAllCursor,
                    "armed but still at the origin (nothing to scroll yet) should show the neutral four-way cursor")

            // Qt has no distinct single-direction up/down cursor pair, only
            // SizeVerCursor (bidirectional vertical) — the best available
            // "makes sense for the direction" indicator without shipping
            // custom cursor bitmaps, and correct either way since this app
            // only ever scrolls vertically.
            mouseMove(editor, cx, cy + 60) // past the deadzone -> actually scrolling
            wait(50)
            compare(area.cursorShape, Qt.SizeVerCursor,
                    "once actually scrolling, the cursor should switch to the vertical-scroll shape")

            mouseClick(area, cx, cy, Qt.LeftButton) // disarm
            verify(area.cursorShape !== Qt.SizeAllCursor && area.cursorShape !== Qt.SizeVerCursor,
                   "the scroll cursor should go away once disarmed")

            win.docHandler.setMarkdown("hi")
            wait(50)
        }

        // Feature request: besides click-to-arm/click-to-disarm, pressing
        // and HOLDING the middle button while dragging should also scroll,
        // stopping the instant the button is released — no second click
        // needed for that gesture specifically.
        function test_holding_middle_button_and_dragging_scrolls_and_stops_on_release() {
            const win = instantiator.objectAt(0)
            const area = win.mouseDragArea
            const flick = win.editorFlick
            const editor = win.editor

            let many = ""
            for (let i = 0; i < 60; ++i)
                many += "line " + i + "\n\n"
            win.docHandler.setMarkdown(many)
            wait(50)
            editor.cursorPosition = 0
            wait(50)
            verify(flick.contentHeight > flick.height, "precondition: content must overflow to scroll at all")

            const cx = editor.width / 2
            // Clear of the hover HUD, which occupies the top-right corner and
            // deliberately swallows every button press over itself. It appears
            // as soon as the pointer enters the note, and at this 260px test
            // width its icon row reaches past the horizontal centre — so a
            // press up at y=20 lands on the HUD, not the editor, and autoscroll
            // never arms. Chosen against the HUD's HEIGHT (26 + 4 margin, fixed)
            // rather than dodging its width, which grows with each icon added.
            const cy = 40

            mousePress(editor, cx, cy, Qt.MiddleButton)
            verify(area.middleScrollArmed, "pressing (not yet releasing) the middle button should arm autoscroll")

            const before = flick.contentY
            mouseMove(editor, cx, cy + 60) // dragged while still held
            wait(200) // let a few of the timer's 16ms ticks actually run
            verify(flick.contentY > before, "dragging while held should scroll, same as the click-then-move gesture")

            mouseRelease(area, cx, cy + 60, Qt.MiddleButton)
            verify(!area.middleScrollArmed, "releasing after an actual drag should stop the scroll immediately")

            win.docHandler.setMarkdown("hi")
            wait(50)
        }

        // Guards against the fix above regressing normal editing: the drag
        // area now sits on top of the whole note, so a left-button click
        // must still fall through to whatever's underneath (the editor,
        // and by the same mechanism the toolbar) instead of being
        // swallowed by the drag area.
        function test_left_click_still_reaches_editor_through_drag_area() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            editor.text = "hello world"
            mouseClick(editor, editor.width / 2, editor.height / 2, Qt.LeftButton)
            verify(editor.cursorPosition > 0, "a left click over the editor should still move the text cursor")
        }

        // Guards against the fix below being too aggressive: a click that
        // actually lands on existing text (near its start, well left of
        // where "hello world" ends) must still place the cursor there, not
        // jump it to the end of the text — i.e. the new below-content
        // handler must not be swallowing clicks that land on real content.
        function test_click_on_existing_text_places_cursor_near_click_not_at_end() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            editor.text = "hello world"
            mouseClick(editor, 2, editor.height / 2, Qt.LeftButton)
            verify(editor.cursorPosition < editor.text.length,
                   "a click near the start of existing text should not jump the cursor to the end")
        }

        // The requirement: clicking anywhere below the last line of text
        // should put the cursor on the last line, at the character nearest
        // the click horizontally — not unconditionally at the very end of
        // the document. A click near the left edge, below the last line,
        // should land near the *start* of that line rather than jumping
        // past all of it to the document's true end.
        function test_click_below_last_line_near_left_edge_lands_near_start_of_last_line() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            const flick = win.editorFlick

            // A plain `editor.text = "a\nb"` assignment doesn't produce two
            // separate rendered lines here — MarkdownText mode treats a lone
            // "\n" as a CommonMark soft break, which folds into a space on
            // reserialization ("a b"), not a real line break. Loading
            // through docHandler.setMarkdown() with a genuine blank-line
            // paragraph break (the same load path NoteWindow itself uses,
            // and confirmed elsewhere to produce lineCount === 2) is what
            // actually renders two distinct lines to click between.
            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            const lastLineStart = editor.text.lastIndexOf("line two")
            mouseClick(flick, 2, flick.height - 5, Qt.LeftButton)

            verify(editor.cursorPosition >= lastLineStart,
                   "click below the last line should still land on that line, not an earlier one")
            verify(editor.cursorPosition < editor.length,
                   "a click near the left edge should land near the start of the last line, not jump to the document's end")
        }

        // The other half: a click below the last line but near the *right*
        // edge — past where the short last line's own text actually ends —
        // should still resolve to the end of that line, same as clicking
        // directly on the text there would.
        function test_click_below_last_line_near_right_edge_lands_at_end_of_last_line() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            const flick = win.editorFlick

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            mouseClick(flick, flick.width - 2, flick.height - 5, Qt.LeftButton)
            compare(editor.cursorPosition, editor.length)
        }

        // Double-clicking below the last line should select the word
        // nearest the click horizontally, same as double-clicking directly
        // on that line's real text would — not just place a single-point
        // caret. The fix tracks clicks manually (no native onDoubleClicked
        // involvement), so two mouseClick() calls back-to-back with no
        // intervening wait() are enough: their synchronous Date.now() deltas
        // are trivially under Qt.application.styleHints.mouseDoubleClickInterval.
        function test_double_click_below_last_line_selects_word_on_last_line() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            const flick = win.editorFlick

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            const lastLineStart = editor.text.lastIndexOf("line two")

            mouseClick(flick, 2, flick.height - 5, Qt.LeftButton)
            mouseClick(flick, 2, flick.height - 5, Qt.LeftButton)

            compare(editor.selectionStart, lastLineStart,
                    "double-click near the left edge below the last line should select the word starting there")
            compare(editor.text.substring(editor.selectionStart, editor.selectionEnd), "line",
                    "double-click should select the whole nearest word (\"line\"), not just a point")

            // belowContentArea's click-count state (pendingClickCount,
            // lastPressTime/X/Y) lives on the shared live window instance
            // used by every test function in this file. Without letting it
            // go stale here, a later, unrelated test that happens to press
            // at this same point below the last line within the OS
            // double-click interval would inherit this test's click count
            // instead of starting a fresh single click.
            wait(Qt.application.styleHints.mouseDoubleClickInterval + 100)
        }

        // Triple-clicking below the last line should select the whole line
        // (its full paragraph), same as triple-clicking directly on that
        // line's real text would.
        function test_triple_click_below_last_line_selects_whole_last_line() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            const flick = win.editorFlick

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            const lastLineStart = editor.text.lastIndexOf("line two")

            mouseClick(flick, 2, flick.height - 5, Qt.LeftButton)
            mouseClick(flick, 2, flick.height - 5, Qt.LeftButton)
            mouseClick(flick, 2, flick.height - 5, Qt.LeftButton)

            compare(editor.selectionStart, lastLineStart,
                    "triple-click below the last line should select from that line's start")
            compare(editor.selectionEnd, editor.length,
                    "triple-click below the last line should select through the document's end")
            compare(editor.text.substring(editor.selectionStart, editor.selectionEnd), "line two",
                    "triple-click should select the whole last line's text")

            // See the matching comment in the double-click test above: let
            // belowContentArea's click-count state go stale so it doesn't
            // leak into a later, unrelated test pressing at this same point.
            wait(Qt.application.styleHints.mouseDoubleClickInterval + 100)
        }

        // Pressing Up has nowhere to go once the cursor is already on the
        // editor's topmost line — rather than leaving it stuck mid-line, it
        // should jump to that line's start, the same place Home would land.
        function test_up_arrow_on_top_line_moves_cursor_to_start_of_line() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            editor.cursorPosition = editor.text.indexOf("one")
            win.requestActivate()
            // requestActivate() only *requests* OS-level window activation;
            // with several other NoteWindow instances having grabbed it in
            // earlier tests, the switch isn't synchronous — waiting for
            // `active` before focusing the editor is what makes keyClick()
            // (which always targets QGuiApplication::focusWindow(), not a
            // specific item's window) land on the right window.
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Up)

            compare(editor.cursorPosition, 0,
                    "Up on the first line should land at its start, not stay mid-line")
        }

        // The other half: Down on the editor's bottommost line should jump
        // to that line's end, the same place End would land, instead of
        // staying mid-line with nowhere lower to move the cursor into.
        function test_down_arrow_on_bottom_line_moves_cursor_to_end_of_line() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)


            editor.cursorPosition = editor.text.indexOf("two")
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Down)

            compare(editor.cursorPosition, editor.length,
                    "Down on the last line should land at its end, not stay mid-line")
        }

        // Regression guard for the two boundary tests above: Keys.onXxxPressed
        // handlers default event.accepted to true even when the handler body
        // never touches it, so an early version of the line-boundary jump
        // above swallowed every Up/Down press unconditionally — including
        // ordinary navigation between lines away from either boundary. Down
        // from the first (non-bottom) line must still move the cursor onto
        // the next line via TextEdit's own default handling, not get stuck.
        function test_down_arrow_not_on_bottom_line_moves_cursor_down_normally() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            const startPos = editor.text.indexOf("one")
            editor.cursorPosition = startPos
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Down)

            verify(editor.cursorPosition !== startPos,
                   "Down on a non-bottom line must not be swallowed — the cursor should move")
            verify(editor.cursorPosition < editor.length,
                   "Down from the first line should land on the next line, not jump straight to the document's end")
        }

        // The other half of the regression guard: Up from the last
        // (non-top) line must still move the cursor onto the line above.
        function test_up_arrow_not_on_top_line_moves_cursor_up_normally() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            const startPos = editor.text.indexOf("two")
            editor.cursorPosition = startPos
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Up)

            verify(editor.cursorPosition !== startPos,
                   "Up on a non-top line must not be swallowed — the cursor should move")
            verify(editor.cursorPosition > 0,
                   "Up from the last line should land on the line above, not jump straight to the document's start")
        }

        // Sharper than the two guards above, and a regression guard for a
        // reported bug: Up pressed on the FIRST line of a lower paragraph
        // used to land at that same paragraph's start and stay there, so no
        // amount of further Up presses could ever reach the paragraph above
        // — the cursor was simply stuck. Both guards above were satisfied by
        // that stuck position (it isn't the start position, and it isn't 0
        // either), so this one asserts on the thing that actually matters:
        // Up must leave the row it started on.
        function test_up_arrow_from_first_line_of_a_paragraph_lands_on_the_row_above() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            editor.cursorPosition = editor.text.indexOf("two")
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)
            const startRow = editor.positionToRectangle(editor.cursorPosition).y

            keyClick(Qt.Key_Up)

            verify(editor.positionToRectangle(editor.cursorPosition).y < startRow,
                   "Up from the first line of a paragraph should move the cursor onto the row above, not to the start of the row it was already on")
        }

        // Shift+Up on the topmost line should EXTEND the selection to that
        // line's start (where Shift+Home would take it) rather than throwing
        // the selection away — reported alongside the Shift+Down bug below.
        function test_shift_up_on_top_line_extends_selection_to_start_of_line() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            const startPos = editor.text.indexOf("one")
            editor.cursorPosition = startPos
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Up, Qt.ShiftModifier)

            compare(editor.selectionStart, 0,
                    "Shift+Up on the first line should extend the selection back to that line's start")
            compare(editor.selectionEnd, startPos,
                    "Shift+Up on the first line should leave the anchor where the cursor started")
        }

        // The other half, and the reported bug itself: Shift+Down on the
        // bottommost line used to CLEAR the selection and dump the cursor at
        // the document's end, because the boundary jump assigned
        // cursorPosition (which collapses any selection) with no regard for
        // the Shift modifier. It should extend to that line's end instead,
        // the same as Shift+End.
        function test_shift_down_on_bottom_line_extends_selection_to_end_of_line() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            const startPos = editor.text.indexOf("two")
            editor.cursorPosition = startPos
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Down, Qt.ShiftModifier)

            compare(editor.selectionStart, startPos,
                    "Shift+Down on the last line should leave the anchor where the cursor started")
            compare(editor.selectionEnd, editor.length,
                    "Shift+Down on the last line should extend the selection on to that line's end")
        }

        // Ctrl+Down jumps the caret to the END of the paragraph (one
        // hard-newline-delimited block) it currently sits in, not to the end
        // of the whole document. With the caret inside the middle block
        // "bravo", it should land at that block's end (offset 11), leaving the
        // final "charlie" block untouched.
        function test_ctrl_down_moves_caret_to_end_of_current_paragraph() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("alpha\nbravo\ncharlie")
            wait(50)

            editor.cursorPosition = editor.text.indexOf("bravo") + 2
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Down, Qt.ControlModifier)

            compare(editor.cursorPosition,
                    editor.text.indexOf("bravo") + "bravo".length,
                    "Ctrl+Down should land at the current paragraph's end, not the document end")
        }

        // Ctrl+Up is the mirror: from inside the middle block "bravo" the
        // caret jumps to that block's START (offset 6), not to the top of the
        // whole document.
        function test_ctrl_up_moves_caret_to_start_of_current_paragraph() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("alpha\nbravo\ncharlie")
            wait(50)

            editor.cursorPosition = editor.text.indexOf("bravo") + 2
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Up, Qt.ControlModifier)

            compare(editor.cursorPosition, editor.text.indexOf("bravo"),
                    "Ctrl+Up should land at the current paragraph's start, not the document start")
        }

        // Repeated presses walk paragraph by paragraph: when the caret is
        // ALREADY at a paragraph's start, Ctrl+Up carries on into the previous
        // paragraph and lands at ITS start. From the start of "bravo" that is
        // the start of "alpha" (offset 0).
        function test_ctrl_up_at_paragraph_start_jumps_to_previous_paragraph_start() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("alpha\nbravo\ncharlie")
            wait(50)

            editor.cursorPosition = editor.text.indexOf("bravo")
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Up, Qt.ControlModifier)

            compare(editor.cursorPosition, 0,
                    "Ctrl+Up at a paragraph's start should step to the previous paragraph's start")
        }

        // The Ctrl+Down mirror of the walk: from a paragraph's END the press
        // steps on to the next paragraph and lands at ITS end. From the end of
        // "bravo" (offset 11) that is the end of "charlie" (the document end).
        function test_ctrl_down_at_paragraph_end_jumps_to_next_paragraph_end() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("alpha\nbravo\ncharlie")
            wait(50)

            editor.cursorPosition = editor.text.indexOf("bravo") + "bravo".length
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Down, Qt.ControlModifier)

            compare(editor.cursorPosition, editor.length,
                    "Ctrl+Down at a paragraph's end should step to the next paragraph's end")
        }

        // Holding Shift with Ctrl+Down EXTENDS the selection to the paragraph
        // end rather than collapsing it, matching how plain Shift+Down already
        // behaves at line boundaries. From position 8 inside "bravo" the
        // selection should grow to the block end (offset 11) with the anchor
        // kept at 8.
        function test_ctrl_shift_down_extends_selection_to_end_of_current_paragraph() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("alpha\nbravo\ncharlie")
            wait(50)

            editor.cursorPosition = 8
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Down, Qt.ControlModifier | Qt.ShiftModifier)

            compare(editor.selectionStart, 8,
                    "Ctrl+Shift+Down should keep the anchor where the caret started")
            compare(editor.selectionEnd, 11,
                    "Ctrl+Shift+Down should extend the selection to the current paragraph's end")
        }

        // The cursor-line marker reveal (raw markdown shown for editing) is
        // gated on appHasFocus: it stays revealed only while the app is focused
        // on this note or the shared chrome acting for it, and collapses back to
        // finished prose the moment focus moves to another window. Main.qml owns
        // that decision (root.activeNoteWindow === thisNoteWindow) — untestable
        // here — but the gate must DEFAULT off, so a note that has merely been
        // constructed, or is not the focused one, never reveals. (The
        // reveal/collapse the gate drives is covered in the C++ highlighter
        // suite; see "A revealed link collapses back to its at-rest styling
        // when the cursor leaves entirely" in test_markdown_highlighter.cpp.)
        function test_marker_reveal_is_gated_off_by_default_until_the_app_focuses_the_note() {
            const win = instantiator.objectAt(0)

            compare(win.appHasFocus, false,
                    "a note must not consider itself app-focused until Main.qml says so, so a fresh or unfocused note shows as prose")
        }

        // The boundary jump must also respect a selection that's already
        // under way rather than only a fresh one: Shift+Down held from the
        // first line down past the last keeps one continuous selection
        // anchored where it began, growing to the document's end.
        function test_shift_down_from_top_line_through_bottom_line_keeps_one_growing_selection() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            const startPos = editor.text.indexOf("one")
            editor.cursorPosition = startPos
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            // Enough presses to walk off the bottom of any of the note's
            // rows, so the last of them lands on the boundary handler.
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            keyClick(Qt.Key_Down, Qt.ShiftModifier)

            compare(editor.selectionStart, startPos,
                    "walking Shift+Down off the bottom should still be anchored where the selection began")
            compare(editor.selectionEnd, editor.length,
                    "walking Shift+Down off the bottom should end up selecting through to the document's end")
        }

        // Regression guard: a bottom-of-document list item short enough to
        // fit on a single visual line used to send Down the WRONG way —
        // onto the end of the line above — instead of leaving the cursor
        // on its own (already-bottommost) line. Root cause was the
        // boundary check comparing cursorRectangle against contentHeight,
        // two values that can legitimately disagree by a pixel or more
        // for a list marker's line (see the fix's own comment in
        // NoteWindow.qml). Bullet, checkbox and numbered markers are each
        // exercised below since the marker-height mismatch that caused
        // this is marker-specific.
        function down_arrow_lands_at_end_of_short_bottom_list_item(markdown) {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown(markdown)
            wait(50)

            // Land mid-way through the last item's own line (not at its
            // very end already), the same way a user arrives there by
            // clicking or arrowing in — Home from the document's end,
            // then one character in.
            editor.cursorPosition = editor.length
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)
            keyClick(Qt.Key_Home)
            keyClick(Qt.Key_Right)

            keyClick(Qt.Key_Down)

            compare(editor.cursorPosition, editor.length,
                    "Down on a short bottom list item should land at its own end, not jump backward onto the line above")
        }
        function test_down_arrow_on_short_bottom_bullet_list_item_stays_at_its_own_end() {
            down_arrow_lands_at_end_of_short_bottom_list_item("- one\n- two\n")
        }
        function test_down_arrow_on_short_bottom_checkbox_list_item_stays_at_its_own_end() {
            down_arrow_lands_at_end_of_short_bottom_list_item("- [ ] one\n- [ ] two\n")
        }
        function test_down_arrow_on_short_bottom_numbered_list_item_stays_at_its_own_end() {
            down_arrow_lands_at_end_of_short_bottom_list_item("1. one\n2. two\n")
        }

        // Companion to the regression guard above: the bug never affected
        // a bottom list item long enough to wrap across multiple visual
        // lines, because only a marker's own (first) line can carry the
        // height mismatch — this pins that down as intended, working
        // behavior rather than an accident.
        function test_down_arrow_on_multiline_bottom_list_item_stays_at_its_own_end() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("- one\n- this list item is long enough that it should wrap across two visual lines in the note")
            wait(50)

            editor.cursorPosition = editor.length
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)
            keyClick(Qt.Key_Home)
            keyClick(Qt.Key_Right)

            keyClick(Qt.Key_Down)

            compare(editor.cursorPosition, editor.length,
                    "Down on the last visual line of a wrapped bottom list item should land at its own end")
        }

        // A drag-select (not just a click) that starts on the last real
        // line of text and is dragged down into the below-content fill
        // space should extend the selection to the nearest character on
        // that line — the same requirement as the click tests above, but
        // for an in-progress selection. Confirmed live (before the fix)
        // that Qt's own TextEdit drag-selection just freezes once the drag
        // crosses below the real content, instead of extending at all —
        // EditorDragFilter (wrapping `editor` in NoteWindow.qml) is what
        // makes this work. Near the left edge, the selection should stay
        // anchored close to the start of "line two", not overshoot all the
        // way to the document's true end (which sits one character past
        // "line two" itself, behind a trailing newline — see the click
        // tests' own use of this same distinction).
        function test_drag_select_below_last_line_near_left_edge_stays_on_last_line() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            const flick = win.editorFlick

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            const lastLineStart = editor.text.lastIndexOf("line two")
            editor.cursorPosition = lastLineStart
            const startY = editor.positionToRectangle(lastLineStart).y + editor.positionToRectangle(lastLineStart).height / 2

            mouseDrag(flick, 2, startY, 0, flick.height - 5 - startY, Qt.LeftButton, Qt.NoModifier, 50)

            verify(editor.selectionEnd < editor.length,
                   "dragging near the left edge below the last line should not overshoot to the document's true end")
            verify(editor.selectionEnd >= lastLineStart,
                   "the selection should extend to a character on the last line, not freeze at the drag's start")
        }

        // The other half: dragging down near the *right* edge, past where
        // the short last line's own text ends, should extend the selection
        // all the way to the end of that line — same resolution as a plain
        // click there.
        function test_drag_select_below_last_line_near_right_edge_selects_to_end_of_last_line() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            const flick = win.editorFlick

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            const lastLineStart = editor.text.lastIndexOf("line two")
            editor.cursorPosition = lastLineStart
            const startY = editor.positionToRectangle(lastLineStart).y + editor.positionToRectangle(lastLineStart).height / 2

            mouseDrag(flick, flick.width - 2, startY, 0, flick.height - 5 - startY, Qt.LeftButton, Qt.NoModifier, 50)

            compare(editor.selectionEnd, editor.length)
            compare(editor.selectionStart, lastLineStart)
        }

        // A drag that *starts* below the last line (not just one that
        // starts on real text and extends below it) should also produce a
        // real selection, not just move the cursor to wherever it's
        // released. Before this fix, the below-content area only supported
        // a plain click (onClicked, firing once on release) — a
        // press-drag-release gesture there just relocated the cursor with
        // no highlighted range at all, regardless of how far the drag
        // moved. Here the whole gesture stays below the content, dragging
        // rightward from near the start of "line two" to past its end.
        function test_drag_select_starting_below_last_line_selects_within_it() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            const flick = win.editorFlick

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            const lastLineStart = editor.text.lastIndexOf("line two")

            mouseDrag(flick, 2, flick.height - 5, flick.width - 4, 0, Qt.LeftButton, Qt.NoModifier, 50)

            verify(editor.selectionStart >= lastLineStart && editor.selectionStart < editor.length,
                   "the drag's anchor (pressed near the left edge) should land near the start of the last line")
            compare(editor.selectionEnd, editor.length)
        }

        // The other direction: a drag that starts below the content but is
        // dragged back UP into real text should extend the selection all
        // the way up there too — not freeze, and not stay confined to the
        // below-content strip it started in.
        function test_drag_select_starting_below_last_line_extends_up_into_real_text() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            const flick = win.editorFlick

            win.docHandler.setMarkdown("line one\n\nline two")
            wait(50)

            const lastLineStart = editor.text.lastIndexOf("line two")
            const firstLineRect = editor.positionToRectangle(0)
            const upwardDy = (firstLineRect.y + firstLineRect.height / 2) - (flick.height - 5)

            mouseDrag(flick, 2, flick.height - 5, 0, upwardDy, Qt.LeftButton, Qt.NoModifier, 50)

            verify(editor.selectionStart <= 1,
                   "dragging up into the first line should extend the selection there, not stay confined below the content")
            verify(editor.selectionEnd >= lastLineStart && editor.selectionEnd < editor.length,
                   "the drag's anchor (pressed near the left edge, below the last line) should land near its start")
        }

        // Regression guard for a real bug: TextArea's own implicitHeight
        // never reactively recomputes after docHandler.setMarkdown()
        // mutates the underlying QTextDocument directly (contentHeight
        // tracks correctly — verified separately — implicitHeight just
        // doesn't re-read it outside edits routed through TextArea's own
        // text-editing API). Since `editor` used to have no explicit height
        // binding (falling back to the broken implicitHeight), a freshly
        // loaded multi-line note would visually show only its first line,
        // with the rest clipped away inside editorFlick despite plenty of
        // room — confirmed live against the real app before the fix.
        function test_editor_grows_to_fit_multiline_content_loaded_via_setMarkdown() {
            const win = soloWindow
            const editor = win.editor
            const flick = win.editorFlick

            win.docHandler.setMarkdown("line one")
            wait(50)
            const singleLineHeight = editor.height

            // Enough paragraphs to guarantee the real content exceeds even
            // a generously-sized note's viewport, so growth is unambiguous
            // regardless of the test window's own height.
            let many = ""
            for (let i = 0; i < 40; ++i)
                many += "line " + i + "\n\n"
            win.docHandler.setMarkdown(many)
            wait(50)

            verify(editor.height > singleLineHeight,
                   "editor should grow to fit multi-paragraph content loaded via docHandler.setMarkdown")
            verify(editor.height > flick.height,
                   "long content should grow the editor past the viewport (so it scrolls) rather than being clipped")
            verify(editor.height >= editor.contentHeight,
                   "editor should always be at least as tall as its real content")
        }

        // The other half of the fix: even short/empty notes should have the
        // editable box fill the whole note, not just be as tall as their
        // (possibly empty) content — the dead space below short text used
        // to look and behave like it wasn't part of the note at all.
        function test_editor_fills_editorFlick_viewport_when_content_is_short() {
            const win = soloWindow
            const editor = win.editor
            const flick = win.editorFlick

            win.docHandler.setMarkdown("hi")
            wait(50)

            compare(editor.height, flick.height)
        }

        // Cheap regression guard: editorFlick must not re-clip its own
        // content at its 8px-inset bounds. mainFrame already renders its
        // whole subtree into one layer and masks it to its own rounded-rect
        // bounds at the real window edge (NoteWindow.qml:1025-1047), so
        // editorFlick's own clip was a second, tighter, redundant clip that
        // cut a partially-scrolled line off 8px before it needed to be — see
        // the comment above editorFlick's anchors for the full explanation.
        function test_editor_flick_does_not_clip_its_own_content() {
            const win = soloWindow
            compare(win.editorFlick.clip, false,
                    "editorFlick must not clip its own content — mainFrame's rounded-rect mask is the real boundary")
        }

        // The fix itself: a line that's only partially scrolled into view —
        // its bottom sitting a few pixels past editorFlick's own (inset)
        // bottom edge, but still within the 8px margin — must have geometry
        // that fits inside mainFrame's real bounds. mainFrame is the
        // boundary that actually matters (its rounded-rect mask already
        // clips everything rendered inside it at the true window edge), so
        // this confirms the extra 8px of margin space a partially-cut line
        // now renders into is still safely contained by mainFrame rather
        // than spilling past the window.
        function test_a_partially_scrolled_line_renders_past_editor_flicks_inset() {
            const win = soloWindow
            const editor = win.editor
            const flick = win.editorFlick
            const mainFrame = win.mainFrame

            let many = ""
            for (let i = 0; i < 40; ++i)
                many += "line " + i + "\n\n"
            win.docHandler.setMarkdown(many)
            wait(50)

            verify(editor.height > flick.height, "precondition: content must overflow to scroll at all")

            const lastLineRect = editor.positionToRectangle(editor.length)

            // Scroll so the last line's bottom sits only a few pixels past
            // editorFlick's own bottom edge — well within the 8px margin —
            // rather than fully in view or fully scrolled past.
            const overflowPastFlick = 3
            flick.contentY = lastLineRect.y + lastLineRect.height - flick.height - overflowPastFlick
            wait(50)

            verify(flick.contentY >= 0, "precondition: the scroll target must be a valid contentY")

            const lineBottomInMainFrame = editor.mapToItem(
                mainFrame, lastLineRect.x, lastLineRect.y + lastLineRect.height)
            const flickBottomInMainFrame = flick.mapToItem(mainFrame, 0, flick.height)

            verify(lineBottomInMainFrame.y > flickBottomInMainFrame.y,
                   "precondition: the line's bottom must actually extend past editorFlick's own (inset) bottom edge")
            verify(lineBottomInMainFrame.y <= mainFrame.height,
                   "a partially-scrolled line should still lie within mainFrame's real bounds, not just editorFlick's inset bounds")

            win.docHandler.setMarkdown("hi")
            wait(50)
        }

        // Feature request: a subtle scrollbar should only show up once the
        // note's content actually overflows the visible area — a short note
        // shouldn't show a scroll track it can't use.
        function test_scroll_indicator_hidden_for_short_content_visible_for_long_content() {
            const win = soloWindow

            win.docHandler.setMarkdown("hi")
            wait(50)
            verify(!win.scrollIndicator.visible,
                   "no scroll indicator when content fits without scrolling")

            let many = ""
            for (let i = 0; i < 40; ++i)
                many += "line " + i + "\n\n"
            win.docHandler.setMarkdown(many)
            wait(50)
            verify(win.scrollIndicator.visible,
                   "scroll indicator should appear once content overflows the viewport")

            win.docHandler.setMarkdown("hi")
            wait(50)
        }

        // Feature request: the scrollbar handle should follow the window's
        // own curvature, exactly like NoteHud's ribbon does — both bind
        // straight to mainFrame.radius rather than deriving their own shape.
        function test_scroll_handle_radius_matches_window_radius() {
            const win = soloWindow
            compare(win.scrollHandle.radius, win.mainFrame.radius)
        }

        // Feature request: dragging the handle should scroll the note's
        // content, not just reflect scroll position passively. Simulates a
        // real drag gesture (press, move, release) on the handle and checks
        // editorFlick.contentY actually moved. Distinct from dragging the
        // note's own TEXT content to scroll, which stays disabled — see
        // test_left_mouse_drag_over_the_editor_does_not_scroll below and
        // editorFlick.interactive's own comment — clarified live that only
        // THAT conflicts with text selection; the scrollbar handle itself
        // does not.
        function test_dragging_scroll_handle_scrolls_the_editor() {
            // Uses instantiator.objectAt(0), not soloWindow — matching the
            // convention every other synthetic-mouse-gesture test in this
            // file already follows (e.g.
            // test_right_click_over_editor_reaches_the_drag_area). soloWindow
            // is only ever driven via property/API calls (docHandler,
            // deleteNote), never mouse input.
            const win = instantiator.objectAt(0)
            const flick = win.editorFlick

            let many = ""
            for (let i = 0; i < 60; ++i)
                many += "line " + i + "\n\n"
            win.docHandler.setMarkdown(many)
            wait(50)

            verify(win.scrollIndicator.visible, "precondition: content must overflow for this test to be meaningful")
            // Starts mid-track, not at contentY 0 (feature request: text
            // fills the whole note, right up under noteHud's corner —
            // NoteWindow.qml's editorFlick comment — so a handle scrolled
            // all the way to the top can sit under the hover-revealed
            // pin/gear/close icons, an accepted, unrelated tradeoff this
            // test isn't about). Mid-track keeps the handle clear of that
            // corner regardless of hover state.
            const scrollRange = flick.contentHeight - flick.height
            flick.contentY = scrollRange / 2
            wait(20)

            // Fire the synthetic gesture against scrollIndicator (the
            // stable parent, anchored to editorFlick) rather than
            // scrollHandle itself: scrollHandle IS the drag target, so its
            // own position moves mid-gesture, and re-deriving each
            // mouseMove's scene position from a moving item's mapToScene()
            // produces the wrong delta. scrollIndicator's own geometry
            // never changes during the drag, so a fixed local point stays
            // valid for the whole press/move/release sequence.
            const handle = win.scrollHandle
            const startX = handle.width / 2
            const startY = handle.y + handle.height / 2
            const contentYBeforeDrag = flick.contentY

            // mouseDrag() (not raw mousePress/mouseMove/mouseRelease) is
            // required here — MouseArea.drag needs the drag threshold
            // crossed via an intermediate move step before it latches onto
            // drag.target at all; a single large mouseMove jump straight to
            // the destination never triggers that (confirmed live: drag.active
            // stayed false with a raw press+single-move+release sequence).
            mouseDrag(win.scrollIndicator, startX, startY, 0, 80, Qt.LeftButton)
            wait(50)

            verify(flick.contentY > contentYBeforeDrag, "dragging the scroll handle downward should scroll the note's content")

            win.docHandler.setMarkdown("hi")
            wait(50)
        }

        // The other half of the same bugfix: a left-mouse-drag over the
        // editor's own text must not scroll the note either — before this
        // fix, editorFlick's default `interactive: true` competed with (and
        // could win over) TextInput's own drag-text-selection, a known
        // Flickable-vs-TextInput arbitration race, so dragging to select
        // text could silently scroll instead. See editorFlick's own
        // `interactive: false` comment.
        function test_left_mouse_drag_over_the_editor_does_not_scroll() {
            const win = instantiator.objectAt(0)
            const flick = win.editorFlick
            const editor = win.editor

            let many = ""
            for (let i = 0; i < 60; ++i)
                many += "line " + i + "\n\n"
            win.docHandler.setMarkdown(many)
            wait(50)
            editor.cursorPosition = 0
            wait(50)

            verify(flick.contentHeight > flick.height, "precondition: content must overflow to scroll at all")
            compare(flick.contentY, 0, "precondition: the viewport must actually be scrolled to the top")

            // Bounded to flick's own (clipped, actually visible) height, not
            // editor.height — editor's real height spans its full virtual
            // content (far taller than the note's window), so a coordinate
            // near its bottom would land off-window and never reach a real
            // mouse event.
            mouseDrag(editor, editor.width / 2, 5, 0, flick.height - 10, Qt.LeftButton, Qt.NoModifier, 50)

            compare(flick.contentY, 0, "a plain left-drag over the text must not scroll the note")

            win.docHandler.setMarkdown("hi")
            wait(50)
        }

        // Feature request/bugfix: mouse-wheel scrolling must keep working
        // even though editorFlick.interactive is now false (see its own
        // comment) — Qt gates its own built-in wheel handling behind that
        // same flag, so wheelHandler drives contentY manually instead.
        function test_mouse_wheel_scrolls_the_editor() {
            const win = instantiator.objectAt(0)
            const flick = win.editorFlick
            const editor = win.editor

            let many = ""
            for (let i = 0; i < 60; ++i)
                many += "line " + i + "\n\n"
            win.docHandler.setMarkdown(many)
            wait(50)
            editor.cursorPosition = 0
            wait(50)

            verify(flick.contentHeight > flick.height, "precondition: content must overflow to scroll at all")
            compare(flick.contentY, 0, "precondition: the viewport must actually be scrolled to the top")

            mouseWheel(flick, flick.width / 2, flick.height / 2, 0, -240, Qt.NoButton, Qt.NoModifier, 0)
            wait(50)

            verify(flick.contentY > 0, "scrolling the wheel downward should move the viewport down")

            win.docHandler.setMarkdown("hi")
            wait(50)
        }

        // Feature request: the caret must never be able to drift off-screen —
        // neither by typing past the bottom of the visible area nor by
        // navigating there with arrow/Home/End keys. Drives it exactly the
        // way both of those actually move the cursor: reassigning
        // editor.cursorPosition (typing appends past the end; arrow keys
        // move it directly) — not literal key events, since what matters is
        // that the Flickable reacts to cursorRectangleChanged regardless of
        // what caused it to move.
        function test_editor_scrolls_to_keep_cursor_visible_when_it_moves_off_bottom() {
            const win = instantiator.objectAt(0)
            const flick = win.editorFlick
            const editor = win.editor

            let many = ""
            for (let i = 0; i < 60; ++i)
                many += "line " + i + "\n\n"
            win.docHandler.setMarkdown(many)
            wait(50)
            verify(flick.contentHeight > flick.height, "precondition: content must overflow for scrolling to mean anything")

            editor.cursorPosition = editor.text.length
            wait(50)

            const cursorBottom = editor.cursorRectangle.y + editor.cursorRectangle.height
            verify(flick.contentY > 0,
                   "moving the cursor to the end of long content should scroll the editor down")
            verify(cursorBottom - flick.contentY <= flick.height + 1,
                   "the cursor's line should end up within the visible viewport, not scrolled past it")

            win.docHandler.setMarkdown("hi")
            wait(50)
        }

        // Same guarantee in the other direction: scrolled down to the
        // bottom, moving the cursor back up (Home / arrow-up / click near
        // the top) must scroll the view back up to reveal it rather than
        // leaving it hidden above the current viewport.
        function test_editor_scrolls_to_keep_cursor_visible_when_it_moves_off_top() {
            const win = instantiator.objectAt(0)
            const flick = win.editorFlick
            const editor = win.editor

            let many = ""
            for (let i = 0; i < 60; ++i)
                many += "line " + i + "\n\n"
            win.docHandler.setMarkdown(many)
            wait(50)

            editor.cursorPosition = editor.text.length
            wait(50)
            verify(flick.contentY > 0, "precondition: cursor-at-end must have scrolled the view down")

            editor.cursorPosition = 0
            wait(50)

            verify(flick.contentY === 0,
                   "moving the cursor back to the very start should scroll the editor back to the top")

            win.docHandler.setMarkdown("hi")
            wait(50)
        }

        // Feature request: a note whose saved content overflows the visible
        // area used to always open scrolled to the very bottom (the load
        // path's own cursor rides to the end of the freshly-inserted text —
        // see NoteWindow.qml's Component.onCompleted comment). It should
        // open at the top instead. Needs a genuinely fresh window (not
        // instantiator.objectAt(0), whose Component.onCompleted already ran
        // against empty content before this test's content ever existed on
        // disk) so Component.onCompleted's own initial load is what's being
        // exercised, not a later setMarkdown() call.
        function test_note_with_overflowing_content_opens_scrolled_to_the_top_not_the_bottom() {
            let many = ""
            for (let i = 0; i < 60; ++i)
                many += "line " + i + "\n\n"
            noteStore.saveNoteContent("scroll-to-top-test", many)
            wait(350) // let NoteStore's ~300ms debounced write flush to disk

            const w = restoreOrderTestComponent.createObject(root, {
                noteId: "scroll-to-top-test",
                index: 71,
                noteX: 0,
                noteY: 0,
                noteWidth: 260,
                noteHeight: 260,
                screenName: "",
                alwaysOnTop: false,
            })
            verify(w !== null)
            wait(50)

            verify(w.editorFlick.contentHeight > w.editorFlick.height,
                   "precondition: content must overflow for a scroll position to mean anything")
            compare(w.editor.cursorPosition, 0,
                    "a freshly-loaded note's cursor must start at position 0, not ride to the end")
            compare(w.editorFlick.contentY, 0,
                    "a freshly-loaded overflowing note must open scrolled to the top, not the bottom")

            w.destroy()
        }

        // Regression guard for a reported bug: pressing Enter alone (no
        // other keystroke following it) used to not persist at all — the
        // save trigger used to guard on TextArea's own `text` property,
        // Qt's built-in MarkdownText serialization of the document, which
        // collapses blank lines/trailing newlines. A bare Enter changes the
        // document (a new empty block) without necessarily changing that
        // lossy string, so the guard mistook it for a no-op and skipped the
        // save — the edit only reached disk once some LATER, non-newline
        // keystroke changed the string too. Comparing what's on disk
        // against docHandler.toMarkdown() (the real, blank-line-preserving
        // serialization straight from the live document) directly checks
        // that the save reflects the newline itself, not just eventually
        // catches up once something else is typed.
        function test_pressing_enter_alone_saves_without_any_further_edit() {
            const w = restoreOrderTestComponent.createObject(root, {
                noteId: "newline-save-test",
                index: 72,
                noteX: 0,
                noteY: 0,
                noteWidth: 260,
                noteHeight: 260,
                screenName: "",
                alwaysOnTop: false,
            })
            verify(w !== null)
            wait(50)

            w.docHandler.setMarkdown("line one")
            wait(50)

            w.editor.cursorPosition = w.editor.length
            w.requestActivate()
            tryCompare(w, "active", true)
            w.editor.forceActiveFocus()
            tryCompare(w.editor, "activeFocus", true)

            keyClick(Qt.Key_Return)
            wait(350) // let NoteStore's ~300ms debounced write flush to disk

            compare(noteStore.loadNoteContent("newline-save-test"), w.docHandler.toMarkdown(),
                    "a bare Enter press must be persisted to disk on its own, without needing a follow-up edit")

            w.destroy()
        }

        function test_window_title_is_unique_per_note_for_kwin_rule_matching() {
            compare(instantiator.objectAt(0).title, "suopNotes-note-a")
            compare(instantiator.objectAt(1).title, "suopNotes-note-b")
            compare(instantiator.objectAt(2).title, "suopNotes-note-c")
        }

        function test_blur_is_applied_on_load() {
            let foundSoloWindow = false
            for (let i = 0; i < blurSpy.count; ++i) {
                if (blurSpy.signalArguments[i][0] === soloWindow)
                    foundSoloWindow = true
            }
            verify(foundSoloWindow)
        }

        function test_blur_is_reapplied_when_blurEnabled_changes() {
            blurSpy.clear()
            settings.blurEnabled = !settings.blurEnabled
            verify(blurSpy.count > 0)
            settings.blurEnabled = !settings.blurEnabled
        }

        function test_blur_is_reapplied_when_windowRadius_changes() {
            blurSpy.clear()
            const original = settings.windowRadius
            settings.windowRadius = original + 5
            verify(blurSpy.count > 0)
            settings.windowRadius = original
        }

        // Exercises the new HUD close button's full effect via
        // NoteWindow's own deleteNote() (real noteStore/openNotesModel/
        // settings — the same objects the app itself uses, per
        // tst_qml_main.cpp's Setup): file removed, no longer open, and any
        // per-note override cleared.
        function test_delete_note_removes_file_clears_override_and_closes_it() {
            const id = deleteTestWindow.noteId

            noteStore.saveNoteContent(id, "content to delete")
            // saveNoteContent's own debounced write doesn't call
            // refreshNoteIds() itself (only the async directory-watcher
            // does, on its own schedule) — loadNoteContent() reads straight
            // from disk synchronously, so it's a reliable precondition
            // check without depending on that watcher's timing.
            wait(350) // let NoteStore's ~300ms debounced write flush
            compare(noteStore.loadNoteContent(id), "content to delete",
                    "precondition: the file must actually exist on disk first")

            deleteTestWindow.noteAppearance.setWindowRadiusOverride(99)
            verify(deleteTestWindow.noteAppearance.hasWindowRadiusOverride)

            openNotesModel.openNote(id)
            verify(openNotesModel.isOpen(id))

            deleteTestWindow.deleteNote()

            // deleteNote() -> noteStore.deleteNote() calls refreshNoteIds()
            // synchronously itself, so this reflects the removal
            // immediately — no watcher-timing dependency here either.
            verify(noteStore.noteIds().indexOf(id) === -1, "the .md file must actually be removed from disk")
            verify(!openNotesModel.isOpen(id))
            verify(!deleteTestWindow.noteAppearance.hasWindowRadiusOverride,
                   "the per-note override must be cleared, not left orphaned for a future note that reuses this id")
        }

        // The app-wide floating formatting toolbar (SelectionToolbar.qml) is
        // no longer part of NoteWindow itself — it's a single shared
        // instance owned by Main.qml, bound to whichever note has focus.
        // See tst_SelectionToolbar.qml for its own behavioral tests.

        // Enter/Return inside a list item continues the list (DocumentHandler::
        // tryContinueList), the same "keep hitting Enter to add items"
        // behavior as Notion/Word/Google Docs, rather than breaking out to a
        // plain paragraph on every press.
        function test_pressing_enter_in_a_bullet_list_item_continues_the_list() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("- item one")
            wait(50)

            editor.cursorPosition = editor.length
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Return)
            typeText("item two")

            const md = win.docHandler.toMarkdown()
            verify(md.indexOf("item one") !== -1)
            verify(md.indexOf("item two") !== -1)
            verify(/^- item one\n- item two/m.test(md) || md.indexOf("- item one\n- item two") !== -1,
                   "expected a second bullet, not a plain paragraph:\n" + md)
        }

        // The other half: pressing Enter on an EMPTY list item (i.e. hitting
        // Enter twice in a row) is the standard escape hatch back to a plain
        // paragraph, rather than adding an empty bullet forever.
        function test_pressing_enter_on_an_empty_list_item_exits_the_list() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("- item one")
            wait(50)

            editor.cursorPosition = editor.length
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Return) // new, empty list item
            keyClick(Qt.Key_Return) // empty item -> exit the list
            typeText("plain paragraph")

            const md = win.docHandler.toMarkdown()
            verify(md.indexOf("- item one") !== -1)
            verify(md.indexOf("plain paragraph") !== -1)
            verify(md.indexOf("- plain paragraph") === -1,
                   "the second Enter on an empty item should exit the list, not add another bullet:\n" + md)
        }

        // Toggling a heading (feature request): clicking/shortcutting H1 on a
        // line that's already H1 should revert it to a plain paragraph,
        // matching every other toolbar toggle (Bold, bullet list, ...) —
        // DocumentHandler::setHeadingLevel.
        function test_setting_the_same_heading_level_twice_toggles_it_back_to_a_paragraph() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("some text")
            wait(50)
            editor.cursorPosition = 0

            win.docHandler.setHeadingLevel(editor.selectionStart, editor.selectionEnd, 1)
            compare(win.docHandler.headingLevel(editor.selectionStart, editor.selectionEnd), 1,
                    "first click should apply heading level 1")

            win.docHandler.setHeadingLevel(editor.selectionStart, editor.selectionEnd, 1)
            compare(win.docHandler.headingLevel(editor.selectionStart, editor.selectionEnd), 0,
                    "clicking H1 again on an already-H1 line should toggle it back to a paragraph")
        }

        // Switching directly between H1 and H2 (not via a toggle-off first)
        // should just switch the level, not require clearing first.
        function test_switching_from_h1_to_h2_applies_h2_without_needing_to_clear_first() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("some text")
            wait(50)
            editor.cursorPosition = 0

            win.docHandler.setHeadingLevel(editor.selectionStart, editor.selectionEnd, 1)
            win.docHandler.setHeadingLevel(editor.selectionStart, editor.selectionEnd, 2)
            compare(win.docHandler.headingLevel(editor.selectionStart, editor.selectionEnd), 2)
        }

        // Pressing Enter from within a heading line should NOT continue the
        // heading style onto the new line — a heading is a one-line label,
        // not a style that propagates — DocumentHandler::
        // tryResetHeadingOnReturn.
        function test_pressing_enter_in_a_heading_does_not_continue_the_heading_on_the_next_line() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("Title")
            wait(50)

            editor.cursorPosition = 0
            win.docHandler.setHeadingLevel(0, 0, 1)

            editor.cursorPosition = editor.length
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Return)
            typeText("plain body text")

            compare(win.docHandler.headingLevel(editor.selectionStart, editor.selectionEnd), 0,
                    "the new line after a heading must not still be a heading")
            const md = win.docHandler.toMarkdown()
            verify(md.indexOf("# Title") !== -1)
            verify(md.indexOf("plain body text") !== -1)
            verify(md.indexOf("# plain body text") === -1,
                   "the new line must not have been serialized as a heading:\n" + md)
        }

        // Regression, end-to-end, for the defect that got an earlier attempt
        // at list hanging-indent reverted: after a FONT CHANGE, Ctrl+Z was
        // permanently broken on that note.
        //
        // List indent is a QTextBlockFormat — undoable document state. A font
        // change resizes it (it is measured in the note's own font), so it is
        // the one thing that makes the indent the editor stores disagree with
        // the indent the code would compute. Anything that reacts to that
        // disagreement by rewriting the indent fights the user's undo: undo
        // reverts the write, the revert is itself a change, the change
        // retriggers the writer, forever — with the user's typed text never
        // reverting at all (measured at 200+ undos).
        //
        // This drives the real path: NoteWindow's onFontChanged ->
        // docHandler.rehighlightMarkdown(), then real Ctrl+Z key events.
        // BOUNDED on purpose — a regression here must fail this test, not hang
        // the test run forever, which is what an unbounded "undo until nothing
        // is left" loop would do.
        function test_undo_still_works_on_a_list_note_after_a_font_change() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            const original = "- first\n- second\n1. one\n2. two"

            win.docHandler.setMarkdown(original)
            wait(50)
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            editor.cursorPosition = editor.length
            typeText("XYZ")
            verify(win.docHandler.toMarkdown().indexOf("XYZ") !== -1,
                   "precondition: the typed text should be in the buffer")

            // The user changes this note's font size in its settings.
            win.noteAppearance.setFontSizeOverride(win.noteAppearance.fontSize + 8)
            wait(50)

            // Undo until the stack is empty, capped. The cap is the assertion:
            // a live-lock keeps manufacturing new undo entries, so canUndo
            // never goes false no matter how many times Ctrl+Z is pressed.
            // Checking only "did the typed text come back out" would NOT catch
            // it — the first Ctrl+Z removes the text either way, and the
            // live-lock is in the undos AFTER that.
            let undos = 0
            while (undos < 40 && editor.canUndo) {
                keyClick(Qt.Key_Z, Qt.ControlModifier)
                ++undos
            }
            win.noteAppearance.clearFontSizeOverride()

            verify(!editor.canUndo,
                   "Ctrl+Z live-locked after a font change: still undoable after "
                   + undos + " undos, because something keeps re-adding undo entries")
            compare(win.docHandler.toMarkdown().trim(), original,
                    "undo should walk the buffer back to exactly what was loaded")
        }

        // Auto-list shorthand (feature request): typing "- " at the very
        // start of an otherwise-plain line converts it into a real bullet
        // list, consuming the typed marker — DocumentHandler::
        // tryAutoConvertListShorthand, wired off TextArea.onTextChanged.
        function test_typing_dash_space_at_line_start_converts_to_a_bullet_list() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("")
            wait(50)
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            typeText("- milk")

            compare(win.docHandler.toMarkdown().trim(), "- milk")
        }

        // Same shorthand, numbered variant: "1. " at the start of a line.
        function test_typing_one_dot_space_at_line_start_converts_to_a_numbered_list() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("")
            wait(50)
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            typeText("1. milk")

            const md = win.docHandler.toMarkdown().trim()
            verify(/^1\.\s+milk$/.test(md), "expected a decimal-prefixed list item, got: " + md)
        }

        // Regression: the shorthand used to always start the list at 1
        // regardless of what number was actually typed (createList() never
        // propagated it) — "5. " should start counting at 5, not silently
        // reset to 1.
        function test_typing_a_number_other_than_one_preserves_that_start_number() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("")
            wait(50)
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            typeText("5. milk")

            const md = win.docHandler.toMarkdown().trim()
            verify(/^5\.\s+milk$/.test(md), "expected the list to start at 5, got: " + md)
        }

        // Checkbox list shorthand (feature request): "- [ ] " at the start
        // of an otherwise-plain line converts it into a real checkbox item.
        function test_typing_dash_bracket_space_at_line_start_converts_to_a_checkbox_list() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("")
            wait(50)
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            typeText("- [ ] milk")

            compare(win.docHandler.toMarkdown().trim(), "- [ ] milk")
        }

        // Enter/Return inside a numbered list item continues the list —
        // same convention as bullet lists (see
        // test_pressing_enter_in_a_bullet_list_item_continues_the_list
        // above), now via DocumentHandler::continueHandRolledList instead
        // of a QTextList.
        function test_pressing_enter_in_a_numbered_list_item_continues_the_list() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("1. item one")
            wait(50)

            editor.cursorPosition = editor.length
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Return)
            typeText("item two")

            const md = win.docHandler.toMarkdown()
            verify(md.indexOf("1. item one") !== -1)
            verify(md.indexOf("2. item two") !== -1,
                   "expected the second item to be numbered 2, not a plain paragraph:\n" + md)
        }

        function test_pressing_enter_on_an_empty_numbered_list_item_exits_the_list() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("1. item one")
            wait(50)

            editor.cursorPosition = editor.length
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Return) // new, empty numbered item
            keyClick(Qt.Key_Return) // empty item -> exit the list
            typeText("plain paragraph")

            const md = win.docHandler.toMarkdown()
            verify(md.indexOf("1. item one") !== -1)
            verify(md.indexOf("plain paragraph") !== -1)
            verify(md.indexOf("2. plain paragraph") === -1,
                   "the second Enter on an empty item should exit the list, not add another item:\n" + md)
        }

        // Bugfix, two parts. (1) Continuing a numbered list reacts to a
        // single Enter keypress with several separate document edits
        // (insert the new block, tag it, insert its "2. " marker, then
        // re-margin every item in the run) — DocumentHandler::
        // tryContinueList now wraps all of that in one QTextDocument edit
        // block, so it lands on the undo stack as a single command. (2)
        // Typing "item two" afterward re-triggers TextFormatting::
        // renumberRun on every single keystroke (see DocumentHandler::
        // maintainListNumbering, called reactively from onTextChanged),
        // and it used to re-merge every block's margin/indent
        // unconditionally even when already correct — flooding the undo
        // stack with a real (if invisible) no-op edit per keystroke.
        // renumberRun now skips that merge when nothing would actually
        // change. Without both fixes, a single undo() right after typing
        // only unwound one of these many small edits, leaving the list
        // visibly untouched (the undone edit was an invisible margin
        // no-op) instead of cleanly restoring the pre-Enter text.
        function test_undoing_a_numbered_list_continuation_is_a_single_step() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("1. item one")
            wait(50)

            editor.cursorPosition = editor.length
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Return)
            typeText("item two")
            verify(win.docHandler.toMarkdown().indexOf("2. item two") !== -1,
                   "precondition: the list should have continued to a second item")

            verify(editor.canUndo, "precondition: there should be undo history after typing")
            editor.undo()

            const md = win.docHandler.toMarkdown().trim()
            compare(md, "1. item one",
                   "a single undo() after continuing the list should fully restore the " +
                   "pre-Enter text, not leave a partially-undone second item behind:\n" + md)
        }

        // Same two behaviors, checkbox variant.
        function test_pressing_enter_in_a_checkbox_list_item_continues_the_list() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("- [ ] item one")
            wait(50)

            editor.cursorPosition = editor.length
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Return)
            typeText("item two")

            const md = win.docHandler.toMarkdown()
            verify(md.indexOf("- [ ] item one") !== -1)
            verify(md.indexOf("- [ ] item two") !== -1,
                   "expected a second checkbox item, not a plain paragraph:\n" + md)
        }

        function test_pressing_enter_on_an_empty_checkbox_list_item_exits_the_list() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("- [ ] item one")
            wait(50)

            editor.cursorPosition = editor.length
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            keyClick(Qt.Key_Return) // new, empty checkbox item
            keyClick(Qt.Key_Return) // empty item -> exit the list
            typeText("plain paragraph")

            const md = win.docHandler.toMarkdown()
            verify(md.indexOf("- [ ] item one") !== -1)
            verify(md.indexOf("plain paragraph") !== -1)
            verify(md.indexOf("- [ ] plain paragraph") === -1,
                   "the second Enter on an empty item should exit the list, not add another item:\n" + md)
        }

        // Click-to-toggle (feature request): clicking directly on a
        // checkbox item's own glyph flips its checked state.
        function test_clicking_the_checkbox_glyph_toggles_its_checked_state() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("- [ ] milk")
            wait(50)

            // The displayed marker is "☐ " (2 characters — a real checkbox
            // glyph, feature request, not the raw "- [ ] " markdown loaded
            // above) — click at its very start, inside the glyph itself,
            // not the "milk" text after it.
            const rect = editor.positionToRectangle(0)
            mouseClick(editor, rect.x, rect.y + rect.height / 2, Qt.LeftButton)

            verify(win.docHandler.toMarkdown().indexOf("- [x] milk") !== -1,
                   "clicking the checkbox glyph should check it: " + win.docHandler.toMarkdown())
        }

        // Bug found live: "upon the creation of a new checkbox list, the text
        // cursor is inside of the checkbox, causing any content typed to break
        // it". A checkbox's "- [ ] " is invisible text under a drawn box, so
        // every position inside it renders at the box's own left or right edge
        // — measured on a fresh "- [ ] ", the box spans x=0..14.69 while its
        // label only starts at position 6, x=21.94. A click aimed at the box,
        // or at the gap beside it where an empty item's label looks like it
        // begins, therefore used to land the caret in the middle of the marker
        // text, and the next keystroke split it ("- [ b] "), destroying the
        // checkbox. Clicking there now puts the caret where the label starts.
        function test_clicking_a_checkbox_leaves_the_caret_at_its_label_not_inside_the_marker() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("- [ ] ")
            wait(50)
            win.requestActivate()
            tryCompare(win, "active", true)

            // Clicking the box itself. Every position this can resolve to (0-5,
            // all of which render at one edge of the box or the other) is also
            // inside click-to-toggle's own hit region, so for a checkbox the
            // snap and the toggle always come together — hence the checked box
            // asserted below. That is the pre-existing feature, not a side
            // effect of the snap: what changed is only where the caret lands.
            const box = editor.positionToRectangle(2)
            mouseClick(editor, box.x + 2, box.y + box.height / 2, Qt.LeftButton)
            wait(50)

            compare(editor.cursorPosition, 6,
                    "a click on the box should place the caret at the label, after '- [x] '")

            // The point of all of the above: typing extends the label instead
            // of splitting the marker, so the checkbox survives.
            keyClick(Qt.Key_B)
            verify(win.docHandler.toMarkdown().indexOf("- [x] b") !== -1,
                   "typing after clicking the box should extend the label, not split "
                   + "the marker: " + win.docHandler.toMarkdown())

            win.docHandler.setMarkdown("")
            wait(50)
        }

        // The other half of the reported flow: the toolbar's Checkbox List
        // button is in a separate window that takes focus, so the way back into
        // a brand-new, still-label-less checkbox is a click on the note — and
        // the only thing to aim at is the box and the blank line beside it.
        // A click out in that blank space must land ready to type: no toggle
        // (it is nowhere near the box), and the caret at the label, not
        // clamped back into the marker.
        function test_clicking_the_empty_space_beside_a_new_checkbox_is_ready_to_type() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("- [ ] ")
            wait(50)
            win.requestActivate()
            tryCompare(win, "active", true)

            const line = editor.positionToRectangle(6)
            mouseClick(editor, line.x + 60, line.y + line.height / 2, Qt.LeftButton)
            wait(50)

            compare(editor.cursorPosition, 6, "the caret should sit at the empty label")
            verify(win.docHandler.toMarkdown().indexOf("- [ ] ") !== -1,
                   "a click out in the blank space should not toggle the box: "
                   + win.docHandler.toMarkdown())

            keyClick(Qt.Key_B)
            verify(win.docHandler.toMarkdown().indexOf("- [ ] b") !== -1,
                   "typing should extend the label: " + win.docHandler.toMarkdown())

            win.docHandler.setMarkdown("")
            wait(50)
        }

        // The same snap for a bullet, whose "- " is invisible under a drawn dot
        // for the identical reason — and, unlike the two above, a numbered
        // item's "1. " is real visible text, so a click in it is left exactly
        // where the user put it (see TextFormatting::listLabelPosition).
        function test_clicking_a_bullets_dot_leaves_the_caret_at_its_label() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("- milk")
            wait(50)
            win.requestActivate()
            tryCompare(win, "active", true)

            const dot = editor.positionToRectangle(0)
            mouseClick(editor, dot.x, dot.y + dot.height / 2, Qt.LeftButton)
            wait(50)

            compare(editor.cursorPosition, 2,
                    "a click on the dot should place the caret at the label, after '- '")

            win.docHandler.setMarkdown("")
            wait(50)
        }

        // On-hover highlight (feature request: "real checkboxes with on
        // hover animations") — a small highlight (the same
        // InteractableHighlight every other bit of interactable chrome in
        // this app uses) appears exactly over the checkbox glyph while
        // hovering it, and disappears once the pointer moves away, so the
        // glyph reads as a real, clickable widget rather than plain text.
        function test_hovering_the_checkbox_glyph_shows_a_highlight() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("- [ ] milk")
            wait(50)
            win.requestActivate()
            tryCompare(win, "active", true)

            compare(win.checkboxHighlight.active, false, "precondition: no highlight before hovering")

            const rect = editor.positionToRectangle(0)
            mouseMove(editor, rect.x, rect.y + rect.height / 2)
            wait(20)

            compare(win.checkboxHighlight.active, true,
                    "hovering the checkbox glyph should show the highlight")

            mouseMove(editor, -50, -50)
            wait(20)

            compare(win.checkboxHighlight.active, false, "moving away should hide the highlight again")

            win.docHandler.setMarkdown("")
            wait(50)
        }

        // The pointer says a checkbox is clickable (feature request), using the
        // same hand cursor as every other clickable in the app. It is derived
        // from the highlight rather than hit-tested separately, so the two can
        // never disagree — wherever the box lights up, the hand shows.
        function test_hovering_the_checkbox_glyph_shows_a_pointing_hand_cursor() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("- [ ] milk")
            wait(50)
            win.requestActivate()
            tryCompare(win, "active", true)

            const rect = editor.positionToRectangle(0)
            mouseMove(editor, rect.x, rect.y + rect.height / 2)
            wait(20)
            compare(win.mouseDragArea.cursorShape, Qt.PointingHandCursor,
                    "the checkbox is a control you click, so the pointer should say so")

            // Out in the label text it is ordinary editable prose again.
            const labelRect = editor.positionToRectangle(9)
            mouseMove(editor, labelRect.x, labelRect.y + labelRect.height / 2)
            wait(20)
            compare(win.mouseDragArea.cursorShape, Qt.IBeamCursor,
                    "the item's own label text is still text, and gets the I-beam")

            win.docHandler.setMarkdown("")
            wait(50)
        }

        // Regression (reported as "adding a checkbox leaves a blue highlight
        // behind it"). TextEdit.positionAt() never fails — it CLAMPS, handing
        // back the nearest character position for a point nowhere near any
        // text. So resolving the pointer to a position and asking "is that
        // position inside a marker?" is not the same question as "is the
        // pointer on the checkbox?", and for a note whose last line is a
        // freshly-added, still-empty "- [ ] " the two answers disagreed: the
        // whole blank area below the text clamps onto that marker, so the
        // highlight lit up behind the checkbox and stayed lit while the pointer
        // sat somewhere else entirely — over a spot where clicking did nothing.
        function test_the_checkbox_highlight_ignores_a_pointer_that_is_not_on_the_glyph() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            // Exactly the state right after adding a checkbox at the end of a
            // note: a marker with no label typed yet, on the last line.
            win.docHandler.setMarkdown("hello\n- [ ] ")
            wait(50)

            const glyph = editor.positionToRectangle(6)
            verify(editor.height > glyph.y + glyph.height * 4,
                   "precondition: there is real blank space below the text to hover over")

            const below = win.checkboxRectUnderPointer(4, editor.height - 5)
            compare(below.width, 0,
                    "the blank space far below the text is not the checkbox")

            // mouseDragArea covers the HUD strip above the editor too, which
            // maps to a negative y here and clamps to position 0 — the first
            // line's start.
            const above = win.checkboxRectUnderPointer(4, -40)
            compare(above.width, 0, "the space above the editor is not the checkbox either")

            const beside = win.checkboxRectUnderPointer(editor.width - 5,
                                                        glyph.y + glyph.height / 2)
            compare(beside.width, 0, "the empty run of the line past the marker is not the checkbox")

            // ...and the glyph itself still highlights, which is the point.
            const onGlyph = win.checkboxRectUnderPointer(3, glyph.y + glyph.height / 2)
            verify(onGlyph.width > 0, "the checkbox glyph itself must still highlight")

            win.docHandler.setMarkdown("")
            wait(50)
        }

        // checkboxRectUnderPointer compares a pointer in the editor's own item
        // coordinates against a rect the document layout reports in document
        // coordinates, and simply assumes they are the same space. They are —
        // but only because nothing has given this TextArea any padding, which
        // is easy to change by accident and would silently offset every
        // checkbox hit test by that padding.
        function test_the_editor_has_no_padding_so_item_and_document_coordinates_agree() {
            const editor = instantiator.objectAt(0).editor
            compare(editor.leftPadding, 0)
            compare(editor.topPadding, 0)
        }

        // Selectable numbers (feature request): unlike Qt's own QTextList
        // markers (paint-time decoration with no character position, never
        // selectable), a hand-rolled numbered marker is literal text —
        // selecting from the very start of the line includes it.
        function test_numbered_list_marker_text_is_selectable() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("1. milk")
            wait(50)

            editor.select(0, 2) // "1."
            compare(editor.selectedText, "1.")
        }

        // Configurable keyboard shortcuts (feature request): Ctrl+B (the
        // documented default for settings.shortcutBold) must toggle bold the
        // same as clicking the toolbar's "B" button — proves the Shortcut
        // item in NoteWindow.qml is actually wired to Settings and to
        // docHandler, not just present.
        // Feature request: Tab inserts four spaces instead of TextArea's
        // own default of a literal tab character.
        function test_pressing_tab_inserts_four_spaces() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("hello")
            wait(50)

            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            editor.cursorPosition = 0
            keyClick(Qt.Key_Tab)

            // Checked via toMarkdown(), not editor.text: MarkdownText mode
            // regenerates editor.text through Qt's own markdown writer,
            // which (unrelated to this feature) can reformat pure leading
            // whitespace — toMarkdown() reads the underlying QTextDocument
            // directly, the same thing that actually gets saved.
            verify(win.docHandler.toMarkdown().startsWith("    "),
                   "Tab should insert four spaces, got: " + JSON.stringify(win.docHandler.toMarkdown()))

            win.docHandler.setMarkdown("")
            wait(50)
        }

        function test_ctrl_b_shortcut_toggles_bold_on_the_current_selection() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("bold me")
            wait(50)

            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            editor.select(0, "bold me".length)
            keyClick(Qt.Key_B, Qt.ControlModifier)

            verify(win.docHandler.toMarkdown().indexOf("**bold me**") !== -1,
                   "Ctrl+B should toggle bold the same as the toolbar button")
        }

        // Feature request: Ctrl+C routes through performCopy() (see
        // NoteWindow.qml) instead of TextArea's own built-in Copy handling,
        // so it gets the same small glass "Copied" confirmation the
        // toolbar's Copy button shows.
        function test_ctrl_c_copies_the_selection_and_shows_the_copy_toast() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("copy me")
            wait(50)

            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            editor.select(0, "copy me".length)
            // tryCompare, not compare: a previous test's toast may still be
            // mid fade-out (opacity animates over 150ms — see copyToast's
            // own Behavior).
            tryCompare(win.copyToast, "opacity", 0, 1000, "precondition: the copy toast starts hidden")

            keyClick(Qt.Key_C, Qt.ControlModifier)

            tryCompare(win.copyToast, "opacity", 1, 1000, "Ctrl+C should show the copy toast")

            editor.cursorPosition = editor.length
            editor.paste()
            verify(editor.text.indexOf("copy me") !== -1,
                   "Ctrl+C should have copied the selected text to the clipboard")

            win.docHandler.setMarkdown("")
            wait(50)
        }

        // Feature request: Copy — both Ctrl+C and the toolbar button, see
        // SelectionToolbar.qml — copies the whole note when nothing is
        // selected, rather than doing nothing, since a copy with no visible
        // change otherwise gives no indication anything happened.
        function test_ctrl_c_with_nothing_selected_copies_the_whole_note() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("copy the whole note")
            wait(50)

            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            editor.cursorPosition = 3
            editor.deselect()

            keyClick(Qt.Key_C, Qt.ControlModifier)

            compare(editor.selectionStart, editor.selectionEnd,
                    "no selection should be left visible after copying the whole note")
            compare(editor.cursorPosition, 3, "the cursor should land back where it was, not at the document's end")

            editor.cursorPosition = editor.length
            editor.paste()
            verify(editor.text.indexOf("copy the whole note") !== -1,
                   "copying with nothing selected should have copied the entire note's text")

            win.docHandler.setMarkdown("")
            wait(50)
        }

        // The toast is a brief confirmation, not a persistent indicator —
        // it should fade itself back out shortly after showing.
        function test_copy_toast_hides_itself_after_a_delay() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("copy me")
            wait(50)
            editor.select(0, "copy me".length)

            win.performCopy()
            tryCompare(win.copyToast, "opacity", 1, 1000, "precondition: the toast is showing right after a copy")

            wait(1200)
            compare(win.copyToast.opacity, 0, "the toast should have hidden itself again after its own delay")

            win.docHandler.setMarkdown("")
            wait(50)
        }

        // Starter table insertion (feature request): docHandler.insertTable()
        // is what both the toolbar's "▦" button and the Ctrl+Alt+T shortcut
        // call — this exercises it through NoteWindow's real, live
        // QQuickTextDocument (not just the standalone unit tests against a
        // bare QTextDocument).
        function test_insert_table_creates_a_table_with_a_header_row() {
            const win = instantiator.objectAt(0)

            win.docHandler.setMarkdown("")
            wait(50)

            win.docHandler.insertTable(0)

            const md = win.docHandler.toMarkdown()
            verify(md.indexOf("Header 1") !== -1)
            verify(md.indexOf("Header 2") !== -1)
        }

        // Table row/column editing (feature request): each of the five
        // functions below is what the popup toolbar's "+R"/"-R"/"+C"/"-C"/
        // "-▦" buttons call — exercised here through NoteWindow's real, live
        // QQuickTextDocument, same convention as the insert-table test above.
        // Position 1 (rather than 0) is used for every call below: it lands
        // inside "Header 1"'s own text, solidly inside the table's first
        // cell — position 0 sits right at the table frame's own boundary,
        // which currentTable() does not resolve as "inside" the table.
        function test_insert_table_row_adds_a_row() {
            const win = instantiator.objectAt(0)

            win.docHandler.setMarkdown("")
            wait(50)
            win.docHandler.insertTable(0)
            const rowsBefore = win.docHandler.toMarkdown().split("\n").filter(l => l.startsWith("|")).length

            win.docHandler.insertTableRow(1)

            const rowsAfter = win.docHandler.toMarkdown().split("\n").filter(l => l.startsWith("|")).length
            compare(rowsAfter, rowsBefore + 1, "inserting a row should add one more table row line")
        }

        function test_delete_table_row_removes_a_row_but_keeps_the_table() {
            const win = instantiator.objectAt(0)

            // Seeded directly as markdown, with real content in every row
            // (rather than built via insertTable()'s starter table, which
            // only has real text in row 0) — deleting row 0 below leaves an
            // otherwise entirely-empty-celled table, which trips an
            // unrelated, pre-existing edge case in this app's own
            // isBlankSpacerBlock() (see MarkdownSerialization's "A table
            // with an empty cell..." test): an all-blank table can get its
            // own delimiter line mistaken for spacer content. Real content
            // in the surviving row sidesteps that entirely, and is more
            // representative of an actual table anyway.
            win.docHandler.setMarkdown("| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |")
            wait(50)
            const rowsBefore = win.docHandler.toMarkdown().split("\n").filter(l => l.startsWith("|")).length

            // Position 1 sits inside row 0 (the "A"/"B" row itself), so
            // this actually deletes THAT row, not one of the data rows
            // beneath it — same as clicking "delete row" with the cursor
            // still sitting in a real word processor's header row.
            win.docHandler.deleteTableRow(1)

            const md = win.docHandler.toMarkdown()
            const lines = md.split("\n").filter(l => l.startsWith("|"))
            compare(lines.length, rowsBefore - 1, "deleting a row should remove one table row line")
            verify(md.indexOf("A") === -1, "the deleted row's own text should be gone")
            verify(md.indexOf("1") !== -1 && md.indexOf("3") !== -1, "the surviving rows' text should remain")
            // The GFM delimiter row is only "|-|-|" (compact form) once
            // there's no header text left wide enough to need padding —
            // not the literal "---" a hand-written table would use, but
            // still a delimiter line: every character in it is "|", "-",
            // or whitespace.
            verify(lines.some(l => /^\|[\s|-]+\|?$/.test(l)),
                   "a surviving table still has its GFM header delimiter line")
        }

        function test_delete_table_rows_down_to_the_last_one_removes_the_whole_table() {
            const win = instantiator.objectAt(0)

            win.docHandler.setMarkdown("")
            wait(50)
            win.docHandler.insertTable(0)

            // A starter table has exactly one data row beneath the header —
            // one deleteTableRow() call removes it, leaving no rows at all.
            win.docHandler.deleteTableRow(1)

            const md = win.docHandler.toMarkdown()
            verify(md.indexOf("Header 1") === -1, "the whole table should be gone, not left with zero rows")
        }

        function test_insert_table_column_adds_a_column() {
            const win = instantiator.objectAt(0)

            win.docHandler.setMarkdown("")
            wait(50)
            win.docHandler.insertTable(0)
            const headerLineBefore =
                win.docHandler.toMarkdown().split("\n").find(l => l.startsWith("|"))
            const pipesBefore = (headerLineBefore.match(/\|/g) || []).length

            win.docHandler.insertTableColumn(1)

            const headerLineAfter =
                win.docHandler.toMarkdown().split("\n").find(l => l.startsWith("|"))
            const pipesAfter = (headerLineAfter.match(/\|/g) || []).length
            compare(pipesAfter, pipesBefore + 1, "inserting a column should add one more pipe to the header row")
        }

        function test_delete_table_column_removes_a_column_but_keeps_the_table() {
            const win = instantiator.objectAt(0)

            win.docHandler.setMarkdown("")
            wait(50)
            win.docHandler.insertTable(0)

            win.docHandler.deleteTableColumn(1)

            const md = win.docHandler.toMarkdown()
            verify(md.indexOf("Header 1") === -1, "the deleted column's own header text should be gone")
            verify(md.indexOf("Header 2") !== -1, "the surviving column's header text should remain")
        }

        function test_delete_table_columns_down_to_the_last_one_removes_the_whole_table() {
            const win = instantiator.objectAt(0)

            win.docHandler.setMarkdown("")
            wait(50)
            win.docHandler.insertTable(0)

            // A starter table has exactly two columns — one
            // deleteTableColumn() call removes column 0 (position 1 sits in
            // "Header 1"), and the whole table only has one column left.
            win.docHandler.deleteTableColumn(1)
            win.docHandler.deleteTableColumn(1)

            const md = win.docHandler.toMarkdown()
            verify(md.indexOf("Header") === -1, "the whole table should be gone, not left with zero columns")
        }

        function test_delete_table_removes_the_whole_table() {
            const win = instantiator.objectAt(0)

            // Seeded directly as markdown with the table at the very start
            // (same "position 1 lands inside the first cell" setup as the
            // row/column tests above, proven reliable) rather than via
            // insertTable() partway through "intro\n" — a table's own frame
            // delimiters occupy real document positions that don't appear
            // in plain text at all, so deriving a position from
            // editor.text (tried initially, both via .length to place the
            // table and via .indexOf("Header 1") to find it afterward)
            // drifted out of range instead.
            win.docHandler.setMarkdown("| Header 1 | Header 2 |\n| --- | --- |\n|  |  |\n\nintro")
            wait(50)
            verify(win.docHandler.toMarkdown().indexOf("Header 1") !== -1, "precondition: the table exists")

            win.docHandler.deleteTable(1)

            const md = win.docHandler.toMarkdown()
            verify(md.indexOf("Header 1") === -1, "the table should be gone")
            verify(md.indexOf("intro") !== -1, "surrounding text should survive")
        }

        // docHandler.spellcheckEnabled is bound live to settings.spellcheckEnabled
        // (NoteWindow.qml's DocumentHandler block: "spellcheckEnabled:
        // settings.spellcheckEnabled") — toggling the global setting should
        // be reflected on the per-note document handler without any other
        // action. This is a genuine, dictionary-independent assertion about
        // the binding itself, unlike the tap-to-suggest tests below.
        function test_docHandler_spellcheckEnabled_tracks_the_global_setting() {
            const win = instantiator.objectAt(0)
            const original = settings.spellcheckEnabled

            settings.spellcheckEnabled = !original
            compare(win.docHandler.spellcheckEnabled, !original,
                    "docHandler.spellcheckEnabled should follow settings.spellcheckEnabled")

            settings.spellcheckEnabled = original
            compare(win.docHandler.spellcheckEnabled, original,
                    "docHandler.spellcheckEnabled should follow settings.spellcheckEnabled back")
        }

        function test_docHandler_spellcheckLanguage_and_userDictionaryWords_track_the_global_settings() {
            const win = instantiator.objectAt(0)
            const originalLanguage = settings.spellcheckLanguage

            settings.spellcheckLanguage = "en_GB"
            compare(win.docHandler.spellcheckLanguage, "en_GB",
                    "docHandler.spellcheckLanguage should follow settings.spellcheckLanguage")

            settings.spellcheckLanguage = originalLanguage
            compare(win.docHandler.spellcheckLanguage, originalLanguage,
                    "docHandler.spellcheckLanguage should follow settings.spellcheckLanguage back")

            verify(!settings.userDictionaryWords.includes("testword12345"),
                   "precondition: testword12345 should not already be in the user dictionary")

            settings.addUserDictionaryWord("testword12345")
            verify(win.docHandler.userDictionaryWords.includes("testword12345"),
                   "docHandler.userDictionaryWords should follow settings.userDictionaryWords after adding a word")

            settings.removeUserDictionaryWord("testword12345")
            verify(!settings.userDictionaryWords.includes("testword12345"),
                   "settings.userDictionaryWords should no longer contain testword12345 after removing it")
            verify(!win.docHandler.userDictionaryWords.includes("testword12345"),
                   "docHandler.userDictionaryWords should follow settings.userDictionaryWords after removing a word")
        }

        // Tap-to-suggest (feature request): tapping a misspelled word should
        // populate noteWindow.currentMisspelledWord/currentSpellingSuggestions/
        // misspelledWordStart/misspelledWordEnd, which SelectionToolbar's own
        // popup then reads. Tapping ordinary correctly-spelled text must leave
        // them empty. This dev/CI machine now has a REAL working dictionary
        // (Phase 2's bundled en_US fallback, fetched at CMake configure time),
        // so both the negative and positive cases can be genuinely exercised
        // end-to-end through the real TapHandler + DocumentHandler + backend
        // chain, via real mouseClicks — not just direct invokable calls.
        function test_tapping_ordinary_text_does_not_populate_suggestion_properties() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            // Starts empty/default before any tap has happened.
            compare(win.currentMisspelledWord, "", "no word should be selected via tap yet")
            compare(win.currentSpellingSuggestions.length, 0, "no suggestions should be pending yet")
            compare(win.misspelledWordStart, -1)
            compare(win.misspelledWordEnd, -1)

            win.docHandler.setMarkdown("hello world")
            wait(50)

            const rect = editor.positionToRectangle(2)
            mouseClick(editor, rect.x, rect.y + rect.height / 2, Qt.LeftButton)

            compare(win.currentMisspelledWord, "",
                    "tapping correctly-spelled text must not populate a suggestion")
            compare(win.currentSpellingSuggestions.length, 0)
        }

        function test_tapping_a_misspelled_word_selects_it_and_populates_suggestion_properties() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("teh normal")
            wait(50)

            // Tap into the middle of "teh" (position 1). try/finally so a
            // failed assertion still resets this shared window's state for
            // later tests (see test_editing_the_document_clears_any_pending_
            // spelling_suggestion's own comment for why that matters here).
            try {
                const rect = editor.positionToRectangle(1)
                mouseClick(editor, rect.x, rect.y + rect.height / 2, Qt.LeftButton)

                compare(win.currentMisspelledWord, "teh", "tapping a real misspelled word must select it")
                verify(win.currentSpellingSuggestions.indexOf("the") !== -1,
                       "\"the\" should be among the surfaced suggestions, got: " + JSON.stringify(win.currentSpellingSuggestions))
                // These two (not the editor's own live selection highlight,
                // see below) are what actually drives the popup: its position
                // (positionToRectangle(misspelledWordStart)) and its replace
                // action (replaceRange(misspelledWordStart, misspelledWordEnd,
                // ...)) both read these tracked offsets, not editor.selectedText.
                compare(win.misspelledWordStart, 0)
                compare(win.misspelledWordEnd, 3)
                // NOT asserted: editor.selectedText staying "teh" after the
                // click. editor.select(word.start, word.end) inside the
                // TapHandler does set it momentarily, but TextArea's own
                // default click-to-place-cursor handling for the SAME click
                // collapses it back to a plain cursor position immediately
                // after — a real, observed ordering quirk between the two
                // handlers sharing one click, but a cosmetic one: it doesn't
                // affect the popup's position or replace action above, which
                // is why it isn't asserted here.
            } finally {
                win.currentMisspelledWord = ""
                win.currentSpellingSuggestions = []
                win.misspelledWordStart = -1
                win.misspelledWordEnd = -1
            }
        }

        // Feature request: the spelling-suggestion popup is anchored to
        // one specific word's on-screen rect (see SelectionToolbar.qml's
        // own repositionSpellingSuggestionsPopup) — scrolling the note
        // enough that the flagged word itself scrolls out of the visible
        // viewport leaves that anchor pointing at text the user can no
        // longer see, so currentMisspelledWord should clear itself (same
        // property a normal click already uses to close the popup)
        // instead of leaving the popup pinned there.
        function test_scrolling_the_flagged_word_out_of_view_clears_the_misspelled_word() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            // "teh" at the very top, then enough filler lines below to
            // make the note's own content genuinely taller than its
            // fixture noteHeight (260px), so there's real scroll range.
            let markdown = "teh normal\n"
            for (let i = 0; i < 40; i++)
                markdown += "filler line " + i + "\n"
            win.docHandler.setMarkdown(markdown)
            // Reset defensively, not just in the finally block below: a
            // prior test in this shared window could have left contentY
            // non-zero, which would scroll "teh" itself out of view before
            // this test ever taps it.
            win.editorFlick.contentY = 0
            // Longer than the other tap-to-suggest tests' own wait(50):
            // the spellcheck highlighter's first pass takes longer over
            // this much more text, and isWordMisspelledAt() (which the
            // TapHandler checks) needs that pass to have actually reached
            // "teh" already.
            wait(200)

            try {
                const rect = editor.positionToRectangle(1)
                mouseClick(editor, rect.x, rect.y + rect.height / 2, Qt.LeftButton)
                compare(win.currentMisspelledWord, "teh", "precondition: tapping the word flags it")

                const scrollRange = win.editorFlick.contentHeight - win.editorFlick.height
                verify(scrollRange > 0, "precondition: the filler content is tall enough to actually scroll")
                win.editorFlick.contentY = scrollRange

                compare(win.currentMisspelledWord, "",
                        "scrolling the flagged word out of view should clear currentMisspelledWord")
            } finally {
                win.editorFlick.contentY = 0
                win.currentMisspelledWord = ""
                win.currentSpellingSuggestions = []
                win.misspelledWordStart = -1
                win.misspelledWordEnd = -1
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        // Bug found live: SelectionToolbar's spellingSuggestionsPopup.
        // wantsVisible depends only on currentMisspelledWord, so the
        // TapHandler above used to assign it FIRST — synchronously firing
        // that popup's reposition-and-show logic immediately, while
        // misspelledWordStart/End and currentSpellingSuggestions still held
        // stale values (or -1, on the very first tap of a session, which
        // also logged a live "QTextCursor::setPosition: Position '-1' out
        // of range" warning). The popup positioned itself from that
        // garbage data — often well outside the note's visible area — with
        // nothing left afterward to correct it once the real values landed
        // a few lines later, so it flashed briefly then immediately
        // "disappeared" to the wrong spot. currentMisspelledWord is now
        // assigned LAST specifically so every property a listener might
        // read alongside it is already correct by the time its own change
        // notification fires — verified directly here, not just via the
        // end state the test above already covers.
        function test_currentMisspelledWord_changes_last_after_its_own_dependencies_are_already_set() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("teh normal")
            wait(50)

            let capturedStart = null
            let capturedEnd = null
            let capturedSuggestions = null
            const onChanged = () => {
                capturedStart = win.misspelledWordStart
                capturedEnd = win.misspelledWordEnd
                capturedSuggestions = win.currentSpellingSuggestions
            }
            win.currentMisspelledWordChanged.connect(onChanged)

            try {
                const rect = editor.positionToRectangle(1)
                mouseClick(editor, rect.x, rect.y + rect.height / 2, Qt.LeftButton)

                compare(capturedStart, 0,
                        "misspelledWordStart must already be correct by the time currentMisspelledWord's own change notification fires")
                compare(capturedEnd, 3,
                        "misspelledWordEnd must already be correct by the time currentMisspelledWord's own change notification fires")
                verify(capturedSuggestions && capturedSuggestions.length > 0,
                       "currentSpellingSuggestions must already be populated by the time currentMisspelledWord's own change notification fires")
            } finally {
                win.currentMisspelledWordChanged.disconnect(onChanged)
                win.currentMisspelledWord = ""
                win.currentSpellingSuggestions = []
                win.misspelledWordStart = -1
                win.misspelledWordEnd = -1
            }
        }

        // Bug found live: the popup was unbounded — "teh" alone returns 10+
        // real dictionary suggestions, making the popup far taller than
        // useful. Tapping now truncates to settings.spellcheckMaxSuggestions
        // (default 3).
        function test_tapping_a_misspelled_word_truncates_suggestions_to_the_configured_max() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            const originalMax = settings.spellcheckMaxSuggestions

            try {
                win.docHandler.setMarkdown("teh normal")
                wait(50)
                verify(win.docHandler.suggestionsForWordAt(1).length > 3,
                       "this test only proves anything if the real dictionary returns MORE than 3 raw suggestions for \"teh\", got: "
                       + JSON.stringify(win.docHandler.suggestionsForWordAt(1)))

                settings.spellcheckMaxSuggestions = 2
                const rect = editor.positionToRectangle(1)
                mouseClick(editor, rect.x, rect.y + rect.height / 2, Qt.LeftButton)
                compare(win.currentSpellingSuggestions.length, 2,
                        "expected exactly 2 (the configured max), got: " + JSON.stringify(win.currentSpellingSuggestions))
            } finally {
                settings.spellcheckMaxSuggestions = originalMax
                win.currentMisspelledWord = ""
                win.currentSpellingSuggestions = []
                win.misspelledWordStart = -1
                win.misspelledWordEnd = -1
            }
        }

        // Defensive clear (feature request): any document edit invalidates a
        // previously-selected misspelled word's start/end offsets, so
        // onTextChanged unconditionally dismisses a shown suggestion the
        // instant the user keeps typing, regardless of whether the edit
        // actually touched the selected word.
        function test_editing_the_document_clears_any_pending_spelling_suggestion() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("hello world")
            wait(50)

            // Simulate a suggestion already being shown (as tap-to-suggest
            // would populate it, were a real misspelling flagged) without
            // depending on a real dictionary to get there. `win` is a
            // shared, reused window instance (instantiator.objectAt(0)) —
            // if the activation/focus dance below hits this sandbox's known
            // pre-existing window-activation flakiness (see other
            // tryCompare(..., "active", true) failures throughout this
            // file) and aborts the test early, these fields must still be
            // reset so the dirtied state doesn't leak into later tests that
            // also use objectAt(0) (e.g. the tapping test above expects
            // these to start empty). try/finally guarantees the reset runs
            // either way.
            win.currentMisspelledWord = "hello"
            win.currentSpellingSuggestions = ["hullo", "hallo"]
            win.misspelledWordStart = 0
            win.misspelledWordEnd = 5

            try {
                editor.cursorPosition = editor.length
                win.requestActivate()
                tryCompare(win, "active", true)
                editor.forceActiveFocus()
                tryCompare(editor, "activeFocus", true)
                typeText("!")

                compare(win.currentMisspelledWord, "",
                        "any text edit should dismiss a previously-shown suggestion")
            } finally {
                win.currentMisspelledWord = ""
                win.currentSpellingSuggestions = []
                win.misspelledWordStart = -1
                win.misspelledWordEnd = -1
            }
        }

        // Bug found live (2026-07-11): a genuinely misspelled word that
        // happens to have zero suggestions (too garbled for the dictionary
        // to find any close match) was indistinguishable from a correctly-
        // spelled word to the old suggestionsForWordAt()-only check in
        // NoteWindow.qml's TapHandler, so tapping it silently showed no
        // popup at all even though the word was still correctly
        // wavy-underlined. Fixed by adding DocumentHandler::
        // isWordMisspelledAt() and having the TapHandler check that instead
        // of suggestions.length. This dev environment now has a REAL working
        // spellcheck dictionary (the Phase 2 bundled en_US fallback, fetched
        // at CMake configure time), so this test uses genuine words rather
        // than a fake backend.
        function test_a_misspelled_word_with_no_suggestions_still_reports_misspelled() {
            const win = instantiator.objectAt(0)
            win.docHandler.setMarkdown("thisisdefinitelymisspelledxyz normal")
            wait(50)

            verify(win.docHandler.isWordMisspelledAt(5),
                   "a garbled non-word must be reported as misspelled")
            compare(win.docHandler.suggestionsForWordAt(5).length, 0,
                    "this specific garbled word is expected to have zero real suggestions")
        }

        function test_a_misspelled_word_with_real_suggestions_reports_them() {
            const win = instantiator.objectAt(0)
            win.docHandler.setMarkdown("teh normal")
            wait(50)

            verify(win.docHandler.isWordMisspelledAt(1), "\"teh\" must be reported as misspelled")
            const suggestions = win.docHandler.suggestionsForWordAt(1)
            verify(suggestions.indexOf("the") !== -1,
                   "\"the\" should be among the real dictionary's suggestions for \"teh\", got: " + JSON.stringify(suggestions))
        }

        // Feature request: precache each misspelled word's suggestions as
        // it settles (DocumentHandler::cachedSuggestionsFor, called from
        // computeMisspelledWordRanges()) rather than only computing them
        // the instant the user taps a word — so the suggestion popup
        // doesn't have to wait on a real dictionary lookup before it can
        // even start opening. Calling suggestionsForWordAt() twice exercises
        // both the precached (from the ambient misspelledWordRanges
        // recompute above) and the on-demand path, which must agree.
        function test_suggestionsForWordAt_matches_the_precached_suggestions_for_the_same_word() {
            const win = instantiator.objectAt(0)
            win.docHandler.setMarkdown("teh normal")
            wait(50)

            try {
                // Forces the precaching pass: reading misspelledWordRanges
                // walks every settled misspelled word and caches its
                // suggestions (see computeMisspelledWordRanges()'s own
                // comment).
                win.docHandler.misspelledWordRanges

                const suggestions = win.docHandler.suggestionsForWordAt(1)
                verify(suggestions.indexOf("the") !== -1,
                       "the on-demand lookup should return the same real suggestions the precaching pass already " +
                       "computed, got: " + JSON.stringify(suggestions))
            } finally {
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        // Guards cachedSuggestionsFor's own cache against going stale:
        // rehighlightSpellcheck() is the same explicit "what counts as
        // misspelled may have changed" signal that already forces the
        // per-block misspelled-word cache to recompute (see
        // invalidateAllMisspelledBlockCaches's own comment) — the
        // suggestions cache must be cleared alongside it, not just the
        // block cache, or a stale suggestion list could survive a
        // language/dictionary change.
        function test_suggestionsForWordAt_still_returns_correct_suggestions_after_rehighlightSpellcheck() {
            const win = instantiator.objectAt(0)
            win.docHandler.setMarkdown("teh normal")
            wait(50)

            try {
                const before = win.docHandler.suggestionsForWordAt(1)
                verify(before.indexOf("the") !== -1, "precondition: real suggestions before rehighlighting")

                win.docHandler.rehighlightSpellcheck()

                const after = win.docHandler.suggestionsForWordAt(1)
                verify(after.indexOf("the") !== -1,
                       "suggestions should still be correct after rehighlightSpellcheck — cache invalidation must " +
                       "not corrupt or lose them: " + JSON.stringify(after))
            } finally {
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        function test_a_correctly_spelled_word_is_not_misspelled() {
            const win = instantiator.objectAt(0)
            win.docHandler.setMarkdown("hello world")
            wait(50)

            verify(!win.docHandler.isWordMisspelledAt(1), "\"hello\" must not be reported as misspelled")
            compare(win.docHandler.suggestionsForWordAt(1).length, 0)
        }

        // Bug found live ("with the specific string 'x selected', selecting
        // the right side of the letter x doesn't trigger the spellchecker"):
        // NoteWindow.qml's tap-to-suggest TapHandler resolves a click to a
        // cursor position via editor.positionAt(), which for a click on the
        // RIGHT half of a glyph lands on the position immediately AFTER
        // that character. For a one-character word like "x" (range [0, 1)),
        // that's position 1 — DocumentHandler::wordAt() used to require
        // `position < end` (strictly less than), so position 1 fell just
        // outside its own word's range and wordAt() found nothing at all.
        // Reproduced here with "teh" instead of a bare single letter — this
        // dev/CI machine's bundled dictionary (see makeSpellCheckerBackend's
        // own comment on the Sonnet/Hunspell fallback) lists every single
        // letter as a valid word on its own (grep the fetched .dic file:
        // "x" is a real, unabbreviated entry), so it wouldn't actually
        // reproduce the live report here — but the underlying boundary
        // condition is identical: "teh"'s own right edge is position 3
        // (immediately after its last letter), the exact same kind of
        // dead zone every word has at the right half of its LAST
        // character; a one-letter word just means the dead zone covers
        // half of the entire word instead of a sliver of its last letter,
        // which is why that specific case is what got noticed live.
        function test_isWordMisspelledAt_detects_a_misspelled_word_at_its_own_right_edge() {
            const win = instantiator.objectAt(0)
            win.docHandler.setMarkdown("teh normal")
            wait(50)

            try {
                verify(win.docHandler.isWordMisspelledAt(0),
                       "\"teh\" must be reported as misspelled at its own start position")
                verify(win.docHandler.isWordMisspelledAt(3),
                       "\"teh\" must ALSO be reported as misspelled at position 3 (its own right edge, immediately " +
                       "after the last letter) — this is exactly where a click on the right half of a word's " +
                       "LAST character resolves to via editor.positionAt()")
                compare(win.docHandler.wordAt(3).text, "teh",
                        "wordAt(3) should still resolve to \"teh\" itself, not fail to find any word there")
            } finally {
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        // The visible red wavy underline itself is drawn by NoteWindow.qml's
        // misspelledWordOverlay (a Canvas-based decorative layer), not by
        // SpellCheckHighlighter's QTextCharFormat (QtQuick's TextArea does
        // not render underline decorations from highlighter-applied formats
        // at all — verified empirically via grabToImage + pixel inspection
        // during implementation, see DocumentHandler::misspelledWordRanges'
        // doc comment). What's verifiable here at the data level is that
        // misspelledWordRanges reports exactly the misspelled word's range —
        // the overlay's own Canvas painting isn't something worth asserting
        // pixel-for-pixel in an automated test (fragile, and this codebase
        // has no precedent for pixel-comparison tests elsewhere).
        function test_misspelledWordRanges_reports_exactly_the_misspelled_words_range() {
            const win = instantiator.objectAt(0)
            win.docHandler.setMarkdown("teh normal")
            wait(50)

            const ranges = win.docHandler.misspelledWordRanges
            compare(ranges.length, 1, "only \"teh\" should be flagged, not \"normal\": " + JSON.stringify(ranges))
            compare(ranges[0].start, 0)
            compare(ranges[0].end, 3)
        }

        // Bug found live ("the turn off spellcheck option doesn't seem to
        // actually do that"): switching spellcheck off used to leave every
        // squiggle on screen. SpellCheckHighlighter::setActive(false) was the
        // only thing being switched off, and QtQuick never paints a
        // highlighter's underline anyway — the visible underline is the overlay
        // driven by misspelledWordRanges, so THAT is what has to go empty. This
        // asserts at the data level for the reason the test above documents.
        function test_turning_spellcheck_off_clears_the_words_it_had_flagged() {
            const win = instantiator.objectAt(0)
            const originalSpellcheckEnabled = settings.spellcheckEnabled
            try {
                settings.spellcheckEnabled = true
                win.docHandler.setMarkdown("teh normal")
                wait(50)
                compare(win.docHandler.misspelledWordRanges.length, 1,
                        "precondition: \"teh\" is flagged while spellcheck is on")

                settings.spellcheckEnabled = false
                wait(50)

                compare(win.docHandler.spellcheckEnabled, false)
                compare(win.docHandler.misspelledWordRanges.length, 0,
                        "with spellcheck off there must be nothing left for the underline overlay " +
                        "to draw, and nothing left for tap-to-suggest to find")

                // And it comes back, rather than being lost until the next edit.
                settings.spellcheckEnabled = true
                wait(50)
                compare(win.docHandler.misspelledWordRanges.length, 1,
                        "turning it back on should re-flag text that is already there")
            } finally {
                settings.spellcheckEnabled = originalSpellcheckEnabled
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        // Bug found live ("spellcheck checks as you type, including the
        // word you're working on... partial in-progress words are flagged
        // as misspelled"): a word still being typed is, by definition, an
        // incomplete token — flagging it mid-keystroke made the underline
        // flicker on and off with every letter. An earlier attempt at this
        // excluded whatever word the CURSOR happened to be positioned
        // inside, but that reintroduced its own bug (see the next test's
        // own comment) — DocumentHandler now debounces by actual EDITS
        // instead: the word at the position of the most recent real text
        // edit is excluded from misspelledWordRanges until
        // spellcheckDebounceMs has passed with no further edit there (see
        // DocumentHandler::onContentsChange).
        function test_misspelledWordRanges_excludes_a_word_until_its_edit_debounce_expires() {
            const win = instantiator.objectAt(0)
            const originalDebounce = settings.spellcheckDebounceMs
            settings.spellcheckDebounceMs = 100 // short, so this test doesn't need to wait long
            // "normal x here": "x" is a one-character placeholder already
            // flanked by real spaces on both sides, so replacing JUST it
            // (not inserting fresh whitespace of its own) sidesteps a
            // confirmed-live TextArea/MarkdownText quirk where an insert()
            // call's own leading/trailing whitespace gets silently
            // stripped during markdown reserialization (the same class of
            // issue TextFormatting::insertTabSpaces' own comment documents
            // elsewhere).
            win.docHandler.setMarkdown("normal x here")
            wait(50)

            try {
                win.docHandler.replaceRange(7, 8, "teh") // replaces "x" with "teh"
                compare(win.docHandler.misspelledWordRanges.length, 0,
                        "a word within its debounce window (just edited) should not be flagged yet")

                wait(200) // past the 100ms debounce
                const ranges = win.docHandler.misspelledWordRanges
                compare(ranges.length, 1,
                        "once the debounce expires, the now-settled word should be flagged: " + JSON.stringify(ranges))
                compare(ranges[0].start, 7)
                compare(ranges[0].end, 10)
            } finally {
                settings.spellcheckDebounceMs = originalDebounce
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        // Bug found live ("the current word that has a popup currently
        // loses the underline when the popup is on. It shouldn't"): the
        // cursor/selection-position-based exclusion this replaced excluded
        // whatever word the cursor sat inside — tapping a misspelled word
        // calls editor.select(word.start, word.end), which lands the cursor
        // at the word's own end, indistinguishable BY POSITION ALONE from
        // still mid-typing it, so a tapped word's underline disappeared for
        // as long as its suggestion popup stayed open. Debouncing by actual
        // EDITS instead sidesteps this entirely: select() doesn't edit
        // anything, so it never starts (or extends) the debounce — a
        // selected word is simply never a candidate for exclusion.
        function test_misspelledWordRanges_never_excludes_a_merely_selected_word() {
            const win = instantiator.objectAt(0)
            win.docHandler.setMarkdown("teh normal")
            wait(50)

            try {
                win.editor.select(0, 3) // selects "teh" — no text edited
                compare(win.editor.cursorPosition, 3,
                        "precondition: a forward selection should land the cursor at its own end")

                const ranges = win.docHandler.misspelledWordRanges
                compare(ranges.length, 1,
                        "\"teh\" should stay flagged while it's selected (i.e. while its suggestion popup is " +
                        "open) — selecting it is not an edit, so it should never enter the debounce at all: " +
                        JSON.stringify(ranges))
                compare(ranges[0].start, 0)
                compare(ranges[0].end, 3)
            } finally {
                win.editor.select(0, 0)
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        // Guards the per-block cache DocumentHandler::computeMisspelledWordRanges()
        // now keeps (bug found live: "every letter typed seems to
        // recalculate the spellcheck... performance intensive" — fixed by
        // only re-running isMisspelled() against a block whose own TEXT
        // actually changed, via QTextBlock::revision()). Adding a word to
        // the user dictionary changes what isMisspelled() answers WITHOUT
        // touching the block's text at all, so its revision never moves —
        // this exercises the separate, explicit cache-invalidation path
        // (DocumentHandler::invalidateAllMisspelledBlockCaches(), called
        // from rehighlightSpellcheck()) that has to catch exactly this case.
        function test_misspelledWordRanges_recomputes_after_a_word_is_added_to_the_user_dictionary() {
            const win = instantiator.objectAt(0)
            win.docHandler.setMarkdown("someuniquetestword normal")
            wait(50)

            try {
                let ranges = win.docHandler.misspelledWordRanges
                compare(ranges.length, 1,
                        "precondition: the made-up word should be flagged misspelled: " + JSON.stringify(ranges))

                settings.addUserDictionaryWord("someuniquetestword")
                win.docHandler.rehighlightSpellcheck()

                ranges = win.docHandler.misspelledWordRanges
                compare(ranges.length, 0,
                        "adding the word to the user dictionary and rehighlighting should immediately clear its " +
                        "flagged range, even though the block's own text never changed: " + JSON.stringify(ranges))
            } finally {
                settings.removeUserDictionaryWord("someuniquetestword")
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        // Bug found live ("every letter typed seems to recalculate the
        // spellcheck, causing the underline to flicker" — reported again
        // even after excluding the in-progress word and caching per-block
        // dictionary lookups): docHandler.misspelledWordRanges reports
        // ABSOLUTE document positions, so editing anywhere before a
        // misspelled word genuinely shifts that word's own start/end —
        // correct, and true for every misspelled word after the edit
        // point, not just the one being typed. The underline overlay used
        // to feed that plain list straight to a Repeater's `model`, which
        // destroys and recreates EVERY delegate on any change at all —
        // so one keystroke near the top of a note with several typos
        // further down tore down and rebuilt all of their underlines at
        // once. misspelledWordOverlay now syncs a ListModel via targeted
        // set()/append()/remove() calls instead, so a delegate whose entry
        // only shifted position gets its properties updated in place and
        // is never destroyed — verified here directly via Repeater.itemAt()
        // returning the SAME object across the edit.
        function test_editing_earlier_text_shifts_underline_delegates_without_recreating_them() {
            const win = instantiator.objectAt(0)
            win.docHandler.setMarkdown("teh two normal words thisisdefinitelymisspelledxyz")
            wait(50)
            // setMarkdown() leaves editor.cursorPosition at the very end of
            // the freshly-inserted text (see NoteWindow.qml's own
            // Component.onCompleted comment on this exact Qt cursor
            // behavior) — which happens to sit right on top of the LAST
            // flagged word here, spuriously excluding it via
            // cursorPosition's own cursor-in-word check. Real NoteWindow
            // instances always reset cursorPosition back to 0 immediately
            // after their own (one and only) setMarkdown() call for this
            // reason; parking it inside "two" (a real, correctly-spelled
            // word, safely away from both flagged words) here reproduces
            // that same safety for a direct setMarkdown() call in a test.
            win.editor.cursorPosition = 6

            try {
                // Deep-copied, not a plain reference assignment: QML's
                // QVariantList-to-JS conversion for this property returned
                // a live view rather than a frozen snapshot in practice
                // (confirmed live: reading rangesBefore's own contents
                // AFTER the edit below showed the post-edit values) — this
                // captures the actual pre-edit values for later comparison.
                const rangesBefore = JSON.parse(JSON.stringify(win.docHandler.misspelledWordRanges))
                compare(rangesBefore.length, 2,
                        "precondition: \"teh\" and the garbled word should both be flagged, not the real words " +
                        "between them: " + JSON.stringify(rangesBefore))

                tryCompare(win.misspelledWordRepeater, "count", 2)
                const secondDelegateBefore = win.misspelledWordRepeater.itemAt(1)
                verify(secondDelegateBefore, "precondition: the second flagged word has its own delegate")

                // Inserting a character at the very start of the document
                // shifts every misspelled word's absolute start/end after
                // it — including the garbled word's — without changing how
                // many misspelled words there are.
                //
                // A SPACE, deliberately — this test is about a shift-only
                // edit, and a space is the only thing that shifts every
                // following word without altering any word itself. Inserting a
                // letter here instead merges into "teh" and makes it a
                // different word ("Xteh"), which the spellcheck debounce then
                // correctly withholds as "the word currently being typed" —
                // dropping it from the list and back again, destroying the very
                // delegates this test exists to prove get reused.
                win.editor.insert(0, " ")
                // Parked in "two" for the same reason as above: insert()
                // leaves the cursor just past the inserted text.
                win.editor.cursorPosition = 7
                wait(50)

                const rangesAfter = win.docHandler.misspelledWordRanges
                compare(rangesAfter.length, 2,
                        "the count of misspelled words should be unchanged by a shift-only edit: " +
                        JSON.stringify(rangesAfter))
                verify(rangesAfter[1].start > rangesBefore[1].start,
                       "precondition: the second word's own absolute position should have genuinely shifted")

                compare(win.misspelledWordRepeater.count, 2,
                        "the delegate count should stay the same after a shift-only edit")
                compare(win.misspelledWordRepeater.itemAt(1), secondDelegateBefore,
                        "the delegate for the second word should be REUSED (its properties updated in place), " +
                        "not destroyed and recreated, since only its position changed")
            } finally {
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        // Bug found live ("deleting text before a misspelled word makes
        // future misspelled words have an incorrect underline"): a pure
        // deletion (replaceRange(start, end, "") with nothing typed in its
        // place) whose removed range sits immediately before a misspelled
        // word collapses DocumentHandler's pendingEditPosition to land
        // exactly on that word's own (new, shifted) start — which used to
        // be treated as "the word is still being typed" (see
        // DocumentHandler::computeMisspelledWordRanges' pendingEdit
        // exclusion check), silently dropping the word from
        // misspelledWordRanges even though the deletion never touched any
        // of its own characters. "teh" here is deliberately short (any
        // real x position, never line-wrapped in this narrow test window,
        // unlike a long word that can wrap to a fresh line and paint at
        // x == 0 regardless of its document position — which would make an
        // x-position assertion meaningless).
        function test_deleting_earlier_text_moves_the_underline_delegate_to_its_new_position() {
            const win = instantiator.objectAt(0)
            win.docHandler.setMarkdown("hello teh normal words here")
            wait(50)
            // Parked in "normal", safely away from "teh".
            win.editor.cursorPosition = 12

            try {
                const rangesBefore = JSON.parse(JSON.stringify(win.docHandler.misspelledWordRanges))
                compare(rangesBefore.length, 1,
                        "precondition: only \"teh\" should be flagged, not \"hello\" or the real words " +
                        "around it: " + JSON.stringify(rangesBefore))
                compare(rangesBefore[0].start, 6)
                compare(rangesBefore[0].end, 9)

                tryCompare(win.misspelledWordRepeater, "count", 1)
                const delegate = win.misspelledWordRepeater.itemAt(0)
                verify(delegate, "precondition: the flagged word has its own delegate")
                const rectBefore = win.editor.positionToRectangle(6)
                tryCompare(delegate, "x", rectBefore.x)
                verify(rectBefore.x > 0, "precondition: \"teh\" doesn't start at the editor's own left edge")

                // Deletes "hello " (the first 6 characters), shifting "teh"
                // 6 positions earlier — a real "delete text before a
                // misspelled word" edit, going through the same
                // replaceRange() path the tap-to-suggest popup itself uses
                // to apply a replacement.
                win.docHandler.replaceRange(0, 6, "")
                win.editor.cursorPosition = 3
                wait(50)

                const rangesAfter = win.docHandler.misspelledWordRanges
                compare(rangesAfter.length, 1,
                        "\"teh\" must still be flagged after a deletion that never touched its own characters " +
                        "— it must not be mistaken for the word still being typed: " + JSON.stringify(rangesAfter))
                compare(rangesAfter[0].start, 0)
                compare(rangesAfter[0].end, 3)

                compare(win.misspelledWordRepeater.count, 1,
                        "the delegate count should stay the same after a shift-only deletion")
                compare(win.misspelledWordRepeater.itemAt(0), delegate,
                        "the delegate for \"teh\" should be REUSED, not destroyed and recreated")

                const rectAfter = win.editor.positionToRectangle(0)
                compare(rectAfter.x, 0, "precondition: \"teh\" now sits at the very start of the document")
                tryCompare(delegate, "x", rectAfter.x)
                verify(delegate.x < rectBefore.x,
                       "the reused delegate's own painted x position must move left to follow \"teh\", not " +
                       "stay stuck at its pre-deletion spot (got " + delegate.x + ", was " + rectBefore.x + ")")
            } finally {
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        // Bug found live ("adding text before a misspelled word breaks its
        // underline — oddly, underlines added after that break too"): the
        // mirror-image of the deletion bug above, but for INSERTION. Checks
        // both halves of the report by asserting every delegate paints at
        // its OWN currently-bound range's real position, regardless of
        // which physical delegate object ends up bound to which word —
        // syncMisspelledWordsModel() reuses delegates by INDEX, not by
        // word identity (see its own comment), so inserting a NEW
        // misspelled word ahead of "teh" actually repurposes what used to
        // be "teh"'s own delegate (now index 0) to display the new word
        // instead, and appends a FRESH delegate for "teh" at index 1 — the
        // reverse of what a naive reading of "teh's delegate should be
        // reused" would expect. What must hold regardless of that reuse
        // bookkeeping: whichever delegate ends up bound to which range,
        // its painted x must match that range's real on-screen position.
        function test_inserting_earlier_text_moves_the_underline_delegate_to_its_new_position() {
            const win = instantiator.objectAt(0)
            // "hi" is a deliberate placeholder, not the word under test: a
            // real, correctly-spelled word (not flagged itself) that sits
            // in the exact spot the new misspelled word will occupy, ALREADY
            // flanked by a real, pre-existing space before "teh" — see
            // test_misspelledWordRanges_excludes_a_word_until_its_edit_
            // debounce_expires' own comment for why this sidesteps a
            // confirmed-live TextArea/MarkdownText quirk where an insert()
            // call's own leading/trailing whitespace gets silently stripped
            // during markdown reserialization: replaceRange() below only
            // ever inserts "xyznotaword" itself (no whitespace of its own
            // at either end), never touching the separating space.
            win.docHandler.setMarkdown("hi teh normal words here")
            wait(50)
            // Parked in "normal", safely away from both "hi" and "teh".
            win.editor.cursorPosition = 9

            try {
                const rangesBefore = JSON.parse(JSON.stringify(win.docHandler.misspelledWordRanges))
                compare(rangesBefore.length, 1,
                        "precondition: only \"teh\" should be flagged, not the \"hi\" placeholder: " +
                        JSON.stringify(rangesBefore))
                compare(rangesBefore[0].start, 3)
                compare(rangesBefore[0].end, 6)

                tryCompare(win.misspelledWordRepeater, "count", 1)
                const rectBefore = win.editor.positionToRectangle(3)
                tryCompare(win.misspelledWordRepeater.itemAt(0), "x", rectBefore.x)

                // Replaces "hi" (positions 0-2) with "xyznotaword" (11
                // characters) — both a real "add text before a misspelled
                // word" edit (shifting "teh" 9 positions right) AND, since
                // "xyznotaword" is itself gibberish, a fresh word becoming
                // misspelled for the first time as a direct result of that
                // same edit — going through the same replaceRange() path
                // the tap-to-suggest popup itself uses.
                win.docHandler.replaceRange(0, 2, "xyznotaword")
                win.editor.cursorPosition = 20 // parked back in "normal"
                wait(600) // past the default 500ms edit-exclusion debounce for BOTH words

                const rangesAfter = win.docHandler.misspelledWordRanges
                compare(rangesAfter.length, 2,
                        "\"teh\" must still be flagged (shifted, not dropped) and \"xyznotaword\" must now " +
                        "ALSO be flagged: " + JSON.stringify(rangesAfter))
                tryCompare(win.misspelledWordRepeater, "count", 2)

                const tehRangeAfter = rangesAfter.find(r => r.start === rangesBefore[0].start + 9)
                verify(tehRangeAfter, "\"teh\" should have shifted right by exactly the 9-character difference " +
                       "between \"hi\" and \"xyznotaword\": " + JSON.stringify(rangesAfter))
                const xyzRange = rangesAfter.find(r => r.start !== tehRangeAfter.start)
                verify(xyzRange, "precondition: the other range belongs to the newly-flagged word")
                verify(xyzRange.start < tehRangeAfter.start,
                       "precondition: \"xyznotaword\" sorts before \"teh\" in document order")

                // Every currently-bound delegate, regardless of index, must
                // paint at ITS OWN range's real position — the actual claim
                // in the bug report ("underlines added after that break
                // too"), not just that some specific object got reused.
                for (let i = 0; i < win.misspelledWordRepeater.count; i++) {
                    const item = win.misspelledWordRepeater.itemAt(i)
                    const expectedRect = win.editor.positionToRectangle(item.start)
                    tryCompare(item, "x", expectedRect.x)
                }

                const tehDelegate = win.misspelledWordRepeater.itemAt(
                    win.misspelledWordRepeater.itemAt(0).start === xyzRange.start ? 1 : 0)
                verify(tehDelegate.x > rectBefore.x,
                       "\"teh\"'s own delegate must paint to the right of where \"teh\" used to sit, not stay " +
                       "stuck at its pre-insertion spot (got " + tehDelegate.x + ", was " + rectBefore.x + ")")
            } finally {
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        // Same bug report as the test above, but via REAL character-by-
        // character typing (keyClick(), through typeText() — the actual
        // Qt Quick TextEdit input pipeline) immediately in front of an
        // already-underlined word, rather than one bulk replaceRange()
        // call. Exercises the exact live sequence the report describes:
        // each keystroke fires its own onContentsChange/pendingEdit cycle
        // and its own wordRect-dependency changes, one at a time, rather
        // than a single settle-and-done edit.
        function test_typing_text_one_keystroke_at_a_time_before_a_misspelled_word_keeps_its_underline_correct() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            win.docHandler.setMarkdown("teh normal words here")
            wait(50)

            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)
            editor.cursorPosition = 0

            try {
                const rangesBefore = win.docHandler.misspelledWordRanges
                compare(rangesBefore.length, 1, "precondition: \"teh\" should be flagged: " + JSON.stringify(rangesBefore))
                tryCompare(win.misspelledWordRepeater, "count", 1)
                tryCompare(win.misspelledWordRepeater.itemAt(0), "x", win.editor.positionToRectangle(0).x)

                // Types "dog " one keystroke at a time, right in front of
                // "teh" — a real, correctly-spelled word (so it never gets
                // flagged itself) — the cursor stays glued to "teh"'s own
                // left edge throughout, exactly like a user typing a new
                // word right before an existing typo.
                typeText("dog ")
                wait(600) // past the default 500ms edit-exclusion debounce

                const rangesAfter = win.docHandler.misspelledWordRanges
                compare(rangesAfter.length, 1,
                        "\"teh\" should still be the only flagged word (\"xyz\" is a real word): " +
                        JSON.stringify(rangesAfter))
                compare(rangesAfter[0].start, 4, "\"teh\" should have shifted right by exactly the 4 typed characters")

                tryCompare(win.misspelledWordRepeater, "count", 1)
                const delegate = win.misspelledWordRepeater.itemAt(0)
                const expectedRect = win.editor.positionToRectangle(4)
                tryCompare(delegate, "x", expectedRect.x)
                verify(expectedRect.x > 0, "precondition: \"teh\" no longer sits at the document's own left edge")
            } finally {
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        // Every currently-flagged word (matched by TEXT, not by array
        // index — a word can freely change which index it sits at as
        // others are added/removed earlier in the document, see
        // syncMisspelledWordsModel's own comment) must have a delegate
        // painted at exactly its own real on-screen position. Shared by
        // the stress test below, run after every edit in its sequence.
        function verifyEveryMisspelledWordHasACorrectlyPositionedUnderline(win, expectedWords) {
            const text = win.editor.text
            const ranges = win.docHandler.misspelledWordRanges
            compare(ranges.length, expectedWords.length,
                    "expected exactly " + JSON.stringify(expectedWords) + " to be flagged, got: " +
                    JSON.stringify(ranges) + " (text: " + JSON.stringify(text) + ")")

            for (const word of expectedWords) {
                const idx = text.indexOf(word)
                verify(idx !== -1, "\"" + word + "\" should still be present in the text: " + JSON.stringify(text))
                const range = ranges.find(r => r.start === idx && r.end === idx + word.length)
                verify(range, "\"" + word + "\" (at " + idx + ") should be in misspelledWordRanges: " +
                       JSON.stringify(ranges))
            }

            tryCompare(win.misspelledWordRepeater, "count", ranges.length)
            for (let i = 0; i < win.misspelledWordRepeater.count; i++) {
                const item = win.misspelledWordRepeater.itemAt(i)
                const expectedRect = win.editor.positionToRectangle(item.start)
                tryCompare(item, "x", expectedRect.x, 2000)
                tryCompare(item, "y", expectedRect.y, 2000)
            }
        }

        // Bug reported live ("inconsistent — sometimes adding text fixes
        // missing underlines, sometimes it removes them, sometimes
        // deleting text still removes underlines... tested in large notes,
        // edits close or far from underlines"): a single two-word,
        // single-block edit (the tests above) isn't enough to catch this —
        // it needs multiple misspelled words scattered across SEVERAL
        // blocks, with a SEQUENCE of edits landing at varying distances
        // from each one, checking every underline's correctness after
        // EVERY edit (not just the last one), the same way a real editing
        // session would surface an intermittent failure.
        function test_a_sequence_of_edits_scattered_through_a_multiparagraph_note_never_breaks_an_underline() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            win.docHandler.setMarkdown(
                "teh first block here\n" +
                "a normal correct paragraph\n" +
                "and yett another paragraph\n" +
                "plus one moar here\n" +
                "final linne done\n")
            wait(50)
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            const words = ["teh", "yett", "moar", "linne"]

            try {
                wait(600) // clear the initial edit-exclusion debounce from setMarkdown() itself
                verifyEveryMisspelledWordHasACorrectlyPositionedUnderline(win, words)

                // 1) Edit directly adjacent to "teh" (the very start of the
                // document) — the closest possible edit to an underline.
                editor.cursorPosition = 0
                typeText("hi ")
                wait(600)
                verifyEveryMisspelledWordHasACorrectlyPositionedUnderline(win, words)

                // 2) Edit inside the untouched, all-correct second
                // paragraph — far from every misspelled word, and not
                // itself adjacent to any of them.
                const correctParaPos = win.editor.text.indexOf("a normal") + 2
                editor.cursorPosition = correctParaPos
                typeText("plain ")
                wait(600)
                verifyEveryMisspelledWordHasACorrectlyPositionedUnderline(win, words)

                // 3) Delete text immediately before "yett" (third
                // paragraph) — the deletion-before-a-misspelled-word case,
                // now inside a multi-paragraph document instead of a
                // single line.
                const andPos = win.editor.text.indexOf("and yett")
                win.docHandler.replaceRange(andPos, andPos + 4, "")
                wait(600)
                verifyEveryMisspelledWordHasACorrectlyPositionedUnderline(win, words)

                // 4) Edit directly adjacent to "moar" (fourth paragraph),
                // right after the previous paragraph's own edit — back-to-
                // back edits in different paragraphs, close together in
                // time.
                const moarPos = win.editor.text.indexOf("moar")
                editor.cursorPosition = moarPos
                typeText("x")
                wait(600)
                const shiftedWords = ["teh", "yett", "xmoar", "linne"]
                verifyEveryMisspelledWordHasACorrectlyPositionedUnderline(win, shiftedWords)

                // 5) Finally, an edit far from everything again (end of
                // the document, well past "linne") — the underlines from
                // every earlier step must all still be correct, not just
                // the most recently touched one.
                editor.cursorPosition = editor.length
                typeText(" done")
                wait(600)
                verifyEveryMisspelledWordHasACorrectlyPositionedUnderline(win, shiftedWords)
            } finally {
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        // Bug found live ("a flash — the line disappears for a tiny bit
        // before reappearing"): recomputing a delegate's x/y/width/height
        // the INSTANT start/end changes (the previous fix's own approach)
        // can observe Qt Quick's still-settling text layout and briefly
        // collapse to an invisible (width <= 1) state before a later,
        // debounce-triggered recompute corrects it — a real, visible
        // flicker even though the FINAL position always ends up right
        // (which is why the position-correctness tests above didn't catch
        // it: tryCompare only observes the eventual, settled value). This
        // asserts the delegate NEVER goes invisible mid-transition: right
        // after an edit, before the settle debounce has had any chance to
        // fire, it must still be showing its OLD (pre-edit) rect — not a
        // degenerate one — and only jump to the new, correct rect once
        // settled, never passing through "nothing visible" in between.
        function test_editing_before_a_misspelled_word_never_makes_its_underline_flash_invisible() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            // "hi" is a real, correctly-spelled placeholder — see
            // test_inserting_earlier_text_moves_the_underline_delegate_to_
            // its_new_position's own comment for why a placeholder that's
            // NOT the word under test, already separated from "teh" by a
            // real space, is used here rather than typing characters
            // directly adjacent to "teh" itself: doing THAT merges the
            // in-progress typed text and "teh" into one growing token
            // until the next space splits them apart again — correctly
            // excluding (and destroying/recreating the delegate for) that
            // MERGED word while it's still being typed, which is
            // intentional, unrelated behavior this test isn't about. The
            // edit below targets "xx " being inserted before "hi" instead
            // — nowhere near "teh" — so "teh" itself is never excluded,
            // never destroyed, just shifted: the exact scenario the flash
            // bug actually happened in.
            win.docHandler.setMarkdown("hi teh normal words here")
            wait(50)
            win.requestActivate()
            tryCompare(win, "active", true)
            editor.forceActiveFocus()
            tryCompare(editor, "activeFocus", true)

            try {
                tryCompare(win.misspelledWordRepeater, "count", 1)
                const delegate = win.misspelledWordRepeater.itemAt(0)
                const rectBefore = win.editor.positionToRectangle(3)
                tryCompare(delegate, "x", rectBefore.x)
                verify(delegate.visible, "precondition: \"teh\"'s underline is visible before any edit")
                const widthBefore = delegate.width
                const heightBefore = delegate.height

                // A single atomic edit — not typed keystroke by keystroke
                // — inserting "xx " right before "hi", shifting "teh" 3
                // positions right without ever exciting the pendingEdit
                // exclusion for "teh" itself (the edit lands nowhere near
                // it) and without merging into any other token. Right
                // after this, settleDebounce (32ms) hasn't fired yet, so
                // the delegate's x/width/height/visible must still reflect
                // "teh"'s OLD, pre-edit rect — not a stale-layout guess,
                // and not collapsed to invisible.
                win.docHandler.replaceRange(0, 0, "xx ")

                compare(win.misspelledWordRepeater.itemAt(0), delegate,
                        "the SAME delegate must still be bound to \"teh\" immediately after the edit — this " +
                        "edit never excludes or merges \"teh\", so nothing should have destroyed/recreated it")
                compare(delegate.x, rectBefore.x,
                        "the delegate must stay at its OLD position immediately after the edit, not jump to " +
                        "a wrong intermediate value before settling")
                compare(delegate.width, widthBefore,
                        "the delegate's width must stay unchanged immediately after the edit")
                compare(delegate.height, heightBefore,
                        "the delegate's height must stay unchanged immediately after the edit")
                verify(delegate.visible,
                       "the underline must stay visible through the entire transition — it must never " +
                       "flash invisible while waiting for the new position to settle")

                // Once settleDebounce fires (32ms), it should land at
                // "teh"'s real, new, shifted position — same final-state
                // check as the other tests above.
                wait(100)
                const rectAfter = win.editor.positionToRectangle(6)
                tryCompare(delegate, "x", rectAfter.x)
                verify(delegate.visible, "the underline should still be visible once settled")
                verify(delegate.x > rectBefore.x, "precondition: \"teh\" actually shifted right")
            } finally {
                win.docHandler.setMarkdown("")
                wait(50)
            }
        }

        // ------------------------------------------------------------------
        // Code mode (feature request)
        //
        // A code note renders as code: a monospaced font (resolved in
        // NoteAppearance::fontFamily) and a line-number gutter down its left
        // edge, with markdown styling, as-you-type auto-formatting and
        // spellcheck switched off. It is a PER-NOTE setting resolved against
        // a global default (settings.codeModeEnabled) that ships OFF, so
        // every test below restores both the note's own override and any
        // global setting it touched before it finishes — the Settings
        // instance is shared by every note here and by every other qml test
        // file in this process.
        // ------------------------------------------------------------------

        // The numbers the gutter is actually showing, in line order. The
        // Repeater's delegates are parented to the gutter itself (and the
        // Repeater, which has no `text`, sits among them), so this picks out
        // the delegates and sorts them by the line they point at rather than
        // trusting child order — nothing promises children are in index
        // order, and this test file should not start depending on it.
        function gutterNumbers(gutter) {
            const delegates = []
            for (let i = 0; i < gutter.children.length; ++i) {
                const child = gutter.children[i]
                if (child.text !== undefined)
                    delegates.push(child)
            }
            delegates.sort(function(a, b) { return Number(a.text) - Number(b.text) })
            return delegates.map(function(d) { return String(d.text) })
        }

        function test_a_note_is_not_in_code_mode_unless_something_turns_it_on() {
            compare(settings.codeModeEnabled, false,
                    "the global code-mode default ships off")
            compare(codeModeTestWindow.noteAppearance.hasCodeModeEnabledOverride, false,
                    "a note has no code-mode override of its own until the user sets one")
            compare(codeModeTestWindow.codeModeEnabled, false)
        }

        // The gutter must be entirely free for an ordinary note: it collapses
        // to zero width so every consumer of gutterWidth stays plain
        // arithmetic, and a note that isn't code lays out exactly as it did
        // before the gutter existed.
        function test_an_ordinary_note_has_an_invisible_zero_width_gutter_and_an_unshifted_editor() {
            const win = codeModeTestWindow
            compare(win.codeModeEnabled, false, "precondition: this note is not a code note")

            compare(win.lineNumberGutter.gutterWidth, 0)
            compare(win.lineNumberGutter.visible, false)
            // An ordinary note doesn't even derive its line starts: the
            // binding short-circuits before it reads editor.text, so it never
            // subscribes to the buffer and never scans it on a keystroke for
            // a gutter it isn't showing. The empty array here is what proves
            // that early return is still in place.
            compare(win.lineNumberGutter.lineStarts.length, 0)
            // The gutter reserves space by shifting the editor's own parent
            // (the EditorDragFilter it's wrapped in) rightward, so with no
            // gutter the editor must still sit at the flickable's content
            // origin and span its full width.
            compare(win.editor.parent.x, 0)
            compare(win.editor.width, win.editorFlick.width)
        }

        function test_turning_on_code_mode_shows_the_gutter_and_shifts_the_editor_right_by_its_width() {
            const win = codeModeTestWindow
            try {
                win.noteAppearance.setCodeModeEnabledOverride(true)
                tryCompare(win, "codeModeEnabled", true)

                verify(win.lineNumberGutter.gutterWidth > 0,
                       "the gutter must reserve real width once code mode is on")
                compare(win.lineNumberGutter.visible, true)
                compare(win.editor.parent.x, win.lineNumberGutter.gutterWidth,
                        "the editor should be pushed right by exactly the gutter's width — the gutter " +
                        "reserves space beside the text, it does not overlay it")
                compare(win.editor.width, win.editorFlick.width - win.lineNumberGutter.gutterWidth,
                        "the editor should give up exactly the width the gutter took")
            } finally {
                win.noteAppearance.clearCodeModeEnabledOverride()
            }
        }

        function test_the_gutter_shows_one_number_per_newline_separated_line() {
            const win = codeModeTestWindow
            try {
                win.noteAppearance.setCodeModeEnabledOverride(true)
                tryCompare(win, "codeModeEnabled", true)

                win.docHandler.setMarkdown("one\ntwo\nthree")
                wait(50)

                compare(win.lineNumberGutter.lineStarts.length, 3,
                        "three newline-separated lines should produce three line starts")
                compare(win.lineNumberGutter.lineStarts[0], 0,
                        "line 1 starts at the very top of the document")
                compare(win.lineNumberGutter.lineStarts[1], win.editor.text.indexOf("two"),
                        "line 2's start is the document position of its first character")
                compare(win.lineNumberGutter.lineStarts[2], win.editor.text.indexOf("three"))
                compare(gutterNumbers(win.lineNumberGutter), ["1", "2", "3"],
                        "the gutter should render a number for each of those lines, counting from 1")
            } finally {
                win.docHandler.setMarkdown("")
                win.noteAppearance.clearCodeModeEnabledOverride()
                wait(50)
            }
        }

        // The whole point of numbering LOGICAL lines rather than visual rows:
        // a long line that the editor wraps across several rows is still one
        // line as far as any compiler error message the user is trying to
        // match against is concerned, so it gets exactly ONE number, at the
        // row where it begins.
        function test_a_wrapped_line_still_gets_exactly_one_number() {
            const win = codeModeTestWindow
            try {
                win.noteAppearance.setCodeModeEnabledOverride(true)
                tryCompare(win, "codeModeEnabled", true)

                // One logical line with no newline in it at all, deliberately
                // far too long for this 260px-wide note.
                win.docHandler.setMarkdown("wrap ".repeat(60).trim())
                wait(50)

                // Proves the line genuinely wrapped onto more than one visual
                // row — read off the laid-out text itself, so it doesn't
                // depend on what lineCount counts.
                const firstRowY = win.editor.positionToRectangle(0).y
                const lastRowY = win.editor.positionToRectangle(win.editor.length).y
                verify(lastRowY > firstRowY,
                       "precondition: the single long line must actually be wrapped across several rows")

                compare(win.lineNumberGutter.lineStarts.length, 1,
                        "a wrapped line is ONE logical line, so it gets ONE line start — not one per visual row")
                compare(gutterNumbers(win.lineNumberGutter), ["1"],
                        "and exactly one number is rendered for it")
            } finally {
                win.docHandler.setMarkdown("")
                win.noteAppearance.clearCodeModeEnabledOverride()
                wait(50)
            }
        }

        function test_a_per_note_override_makes_one_note_a_code_note_while_the_global_default_stays_off() {
            const codeNote = codeModeTestWindow
            const ordinaryNote = instantiator.objectAt(0)
            try {
                compare(settings.codeModeEnabled, false, "precondition: the global default is off")

                codeNote.noteAppearance.setCodeModeEnabledOverride(true)
                tryCompare(codeNote, "codeModeEnabled", true)
                compare(codeNote.noteAppearance.hasCodeModeEnabledOverride, true)

                compare(settings.codeModeEnabled, false,
                        "turning one note into a code note must not touch the global default")
                compare(ordinaryNote.codeModeEnabled, false,
                        "every other note must stay out of code mode")
                compare(ordinaryNote.lineNumberGutter.gutterWidth, 0,
                        "and must not grow a gutter")
            } finally {
                codeNote.noteAppearance.clearCodeModeEnabledOverride()
            }
            compare(codeNote.codeModeEnabled, false,
                    "clearing the override should drop the note back to the global default")
        }

        function test_turning_on_the_global_default_puts_every_note_without_an_override_into_code_mode() {
            try {
                settings.codeModeEnabled = true

                tryCompare(codeModeTestWindow, "codeModeEnabled", true)
                tryCompare(instantiator.objectAt(0), "codeModeEnabled", true)
                verify(codeModeTestWindow.lineNumberGutter.gutterWidth > 0)
            } finally {
                settings.codeModeEnabled = false
            }
            compare(codeModeTestWindow.codeModeEnabled, false)
            compare(instantiator.objectAt(0).codeModeEnabled, false)
        }

        // Code mode "defaults to a monospaced font": it outranks the global
        // family (a code note is saying it is not an ordinary note), but a
        // family the user deliberately picked for THIS note still wins.
        function test_a_code_notes_editor_uses_a_monospaced_font_unless_the_note_overrides_its_family() {
            const win = codeModeTestWindow
            const families = Qt.fontFamilies()
            const originalGlobalFamily = settings.fontFamily
            try {
                settings.fontFamily = families[0]
                win.noteAppearance.setCodeModeEnabledOverride(true)
                tryCompare(win, "codeModeEnabled", true)

                compare(win.editor.font.family, "monospace",
                        "a code note's font should be monospaced even though a global family is set")

                win.noteAppearance.setFontFamilyOverride(families[1])
                wait(50)
                compare(win.editor.font.family, families[1],
                        "a family picked for this note specifically still wins over code mode's monospace default")
            } finally {
                win.noteAppearance.clearFontFamilyOverride()
                win.noteAppearance.clearCodeModeEnabledOverride()
                settings.fontFamily = originalGlobalFamily
                wait(50)
            }
        }

        function test_code_mode_switches_off_markdown_styling_auto_formatting_and_spellcheck() {
            const win = codeModeTestWindow
            const originalSpellcheckEnabled = settings.spellcheckEnabled
            try {
                compare(settings.codeModeDisablesMarkdownHighlighting, true, "precondition: ships on")
                compare(settings.codeModeDisablesAutoFormatting, true, "precondition: ships on")
                compare(settings.codeModeDisablesSpellcheck, true, "precondition: ships on")
                settings.spellcheckEnabled = true

                compare(win.codeModeSuppressesMarkdown, false,
                        "nothing is suppressed while the note isn't a code note...")
                compare(win.codeModeSuppressesAutoFormatting, false)
                compare(win.codeModeSuppressesSpellcheck, false)
                compare(win.docHandler.markdownDecorationEnabled, true)
                compare(win.docHandler.autoFormattingEnabled, true)
                compare(win.docHandler.spellcheckEnabled, true)

                win.noteAppearance.setCodeModeEnabledOverride(true)
                tryCompare(win, "codeModeEnabled", true)

                compare(win.codeModeSuppressesMarkdown, true, "...and all three are once it is")
                compare(win.codeModeSuppressesAutoFormatting, true)
                compare(win.codeModeSuppressesSpellcheck, true)
                // The suppressions aren't just derived properties — they
                // actually reach the document handler that does the work.
                compare(win.docHandler.markdownDecorationEnabled, false)
                compare(win.docHandler.autoFormattingEnabled, false)
                compare(win.docHandler.spellcheckEnabled, false)

                compare(settings.spellcheckEnabled, true,
                        "a note being code must not rewrite the user's app-wide spellcheck preference")
            } finally {
                win.noteAppearance.clearCodeModeEnabledOverride()
                settings.spellcheckEnabled = originalSpellcheckEnabled
            }
        }

        // Each of the three is independently gated by its own toggle, so a user
        // who wants, say, spellcheck in their code notes can keep it without
        // giving up the rest of code mode. That toggle resolves per-note
        // against a global default, exactly like code mode itself — this test
        // covers the global half, the one below it the per-note half.
        function test_each_suppression_can_be_switched_back_on_globally_while_code_mode_stays_on() {
            const win = codeModeTestWindow
            const originalSpellcheckEnabled = settings.spellcheckEnabled
            try {
                settings.spellcheckEnabled = true
                win.noteAppearance.setCodeModeEnabledOverride(true)
                tryCompare(win, "codeModeEnabled", true)

                settings.codeModeDisablesMarkdownHighlighting = false
                compare(win.codeModeSuppressesMarkdown, false,
                        "markdown styling should come back for code notes when its global toggle is off")
                compare(win.docHandler.markdownDecorationEnabled, true)
                compare(win.codeModeSuppressesAutoFormatting, true,
                        "and the other two suppressions should be unaffected")
                compare(win.codeModeSuppressesSpellcheck, true)

                settings.codeModeDisablesAutoFormatting = false
                compare(win.codeModeSuppressesAutoFormatting, false)
                compare(win.docHandler.autoFormattingEnabled, true)

                settings.codeModeDisablesSpellcheck = false
                compare(win.codeModeSuppressesSpellcheck, false)
                compare(win.docHandler.spellcheckEnabled, true)

                // The note is still a code note throughout: the three toggles
                // say what code mode DOES, not whether it's on.
                compare(win.codeModeEnabled, true)
                verify(win.lineNumberGutter.gutterWidth > 0, "the gutter is still there")
            } finally {
                settings.codeModeDisablesMarkdownHighlighting = true
                settings.codeModeDisablesAutoFormatting = true
                settings.codeModeDisablesSpellcheck = true
                settings.spellcheckEnabled = originalSpellcheckEnabled
                win.noteAppearance.clearCodeModeEnabledOverride()
            }
        }

        // The per-note half: one code note can disagree with the global answer
        // about what code mode does to it, and the disagreement has to reach
        // the document handler that actually does the suppressing — not just
        // the derived property.
        function test_one_note_can_keep_spellcheck_while_the_global_answer_says_code_notes_lose_it() {
            const win = codeModeTestWindow
            const originalSpellcheckEnabled = settings.spellcheckEnabled
            try {
                settings.spellcheckEnabled = true
                compare(settings.codeModeDisablesSpellcheck, true,
                        "precondition: globally, code notes lose spellcheck")

                win.noteAppearance.setCodeModeEnabledOverride(true)
                tryCompare(win, "codeModeEnabled", true)
                compare(win.docHandler.spellcheckEnabled, false,
                        "so this code note starts out without spellcheck")

                win.noteAppearance.setCodeModeDisablesSpellcheckOverride(false)

                tryCompare(win, "codeModeSuppressesSpellcheck", false)
                compare(win.docHandler.spellcheckEnabled, true,
                        "this one note keeps spellcheck...")
                compare(settings.codeModeDisablesSpellcheck, true,
                        "...without changing the global answer for every other code note")
                compare(win.codeModeSuppressesMarkdown, true,
                        "and the other two suppressions still follow the global answer")
                compare(win.codeModeSuppressesAutoFormatting, true)
                compare(win.codeModeEnabled, true, "the note is still a code note throughout")
            } finally {
                win.noteAppearance.clearCodeModeDisablesSpellcheckOverride()
                win.noteAppearance.clearCodeModeEnabledOverride()
                settings.spellcheckEnabled = originalSpellcheckEnabled
            }
        }

        // The end of the chain the popup's "Turn off spellcheck" box drives:
        // override -> NoteAppearance -> codeModeSuppressesSpellcheck ->
        // docHandler.spellcheckEnabled -> the ranges the underline overlay
        // draws. Asserting the derived property alone is what let the live bug
        // through — spellcheckEnabled went false correctly while the squiggles
        // stayed on screen, because misspelledWordRanges ignored it.
        function test_a_code_notes_spellcheck_suppression_clears_the_underlines_it_had_drawn() {
            const win = codeModeTestWindow
            const originalSpellcheckEnabled = settings.spellcheckEnabled
            try {
                settings.spellcheckEnabled = true
                win.docHandler.setMarkdown("teh normal")
                wait(50)
                compare(win.docHandler.misspelledWordRanges.length, 1,
                        "precondition: an ordinary note flags \"teh\"")

                win.noteAppearance.setCodeModeEnabledOverride(true)
                tryCompare(win, "codeModeSuppressesSpellcheck", true)
                wait(50)

                compare(win.docHandler.misspelledWordRanges.length, 0,
                        "a code note that suppresses spellcheck must have no underlines left to draw")

                // The note keeping spellcheck brings them back, which is the
                // per-note override's whole point.
                win.noteAppearance.setCodeModeDisablesSpellcheckOverride(false)
                tryCompare(win, "codeModeSuppressesSpellcheck", false)
                wait(50)
                compare(win.docHandler.misspelledWordRanges.length, 1,
                        "a code note that keeps spellcheck should flag \"teh\" like any other note")
            } finally {
                win.docHandler.setMarkdown("")
                win.noteAppearance.clearCodeModeDisablesSpellcheckOverride()
                win.noteAppearance.clearCodeModeEnabledOverride()
                settings.spellcheckEnabled = originalSpellcheckEnabled
                wait(50)
            }
        }

        // The gutter is sized to the digits the note actually has. It reserved
        // a two-digit box unconditionally once, which right-aligned every
        // number one whole digit clear of the left edge and read as a stray
        // indent on any note under ten lines.
        function test_the_gutter_is_no_wider_for_one_line_than_for_nine() {
            const win = codeModeTestWindow
            try {
                win.noteAppearance.setCodeModeEnabledOverride(true)
                tryCompare(win, "codeModeEnabled", true)

                win.docHandler.setMarkdown("x")
                wait(50)
                const oneLineWidth = win.lineNumberGutter.gutterWidth

                win.docHandler.setMarkdown("x\n".repeat(8) + "x")
                wait(50)
                compare(win.lineNumberGutter.lineStarts.length, 9, "precondition: nine lines")
                compare(win.lineNumberGutter.gutterWidth, oneLineWidth,
                        "1 and 9 are both single digits, so the gutter must be exactly as wide for " +
                        "both — no space reserved for a tenth line that doesn't exist")
            } finally {
                win.docHandler.setMarkdown("")
                win.noteAppearance.clearCodeModeEnabledOverride()
                wait(50)
            }
        }

        function test_the_gutter_widens_once_a_tenth_line_needs_a_second_digit() {
            const win = codeModeTestWindow
            try {
                win.noteAppearance.setCodeModeEnabledOverride(true)
                tryCompare(win, "codeModeEnabled", true)

                win.docHandler.setMarkdown("x\n".repeat(8) + "x")
                wait(50)
                const nineLineWidth = win.lineNumberGutter.gutterWidth

                win.docHandler.setMarkdown("x\n".repeat(9) + "x")
                wait(50)
                compare(win.lineNumberGutter.lineStarts.length, 10, "precondition: ten lines")
                verify(win.lineNumberGutter.gutterWidth > nineLineWidth,
                       "a two-digit line count genuinely needs the extra digit, so here the gutter " +
                       "does grow — that one-off shift is the cost of not indenting every short note")
            } finally {
                win.docHandler.setMarkdown("")
                win.noteAppearance.clearCodeModeEnabledOverride()
                wait(50)
            }
        }

        // --- When the TickTick task note refreshes itself -------------------
        //
        // The task note is the one note in the app whose content lives
        // elsewhere, so it refreshes on its own — but only when the user
        // actually comes BACK to it. These specs pin which events count,
        // because the tempting looser triggers all misfire: refreshing on
        // every click in the body pulls again on a note the user is already
        // reading, and refreshing on raw window activation pulls every time
        // focus returns from the selection toolbar or the find panel, in the
        // middle of editing.
        //
        // They assert on the refresh debounce being armed rather than on a
        // request going out: the module is unauthorized in this harness, so a
        // pull would be a silent no-op and prove nothing either way.

        function skipUnlessTickTickBuilt() {
            if (tickTick === null)
                skip("built without the TickTick module")
        }

        // Leaves the debounce disarmed and the note un-focused, so each test
        // below starts from "nothing is scheduled" and its own trigger is the
        // only thing that could have armed it.
        function resetTaskNoteRefreshState() {
            taskNoteTestWindow.appHasFocus = false
            taskNoteTestWindow.tickTickRefreshDebounce.stop()
            verify(!taskNoteTestWindow.tickTickRefreshDebounce.running,
                   "precondition: no refresh is scheduled")
        }

        function test_the_task_note_is_recognised_as_the_ticktick_note() {
            skipUnlessTickTickBuilt()
            verify(taskNoteTestWindow.isTickTickTaskNote,
                   "precondition for every test below: this window really is the task note")
            verify(!soloWindow.isTickTickTaskNote,
                   "an ordinary note is not")
        }

        function test_returning_to_the_task_note_schedules_a_refresh() {
            skipUnlessTickTickBuilt()
            resetTaskNoteRefreshState()

            taskNoteTestWindow.appHasFocus = true

            verify(taskNoteTestWindow.tickTickRefreshDebounce.running,
                   "coming back to the note is the moment its content might be stale, since the " +
                   "scheduled pull is hourly")
        }

        function test_leaving_the_task_note_schedules_nothing() {
            skipUnlessTickTickBuilt()
            taskNoteTestWindow.appHasFocus = true
            taskNoteTestWindow.tickTickRefreshDebounce.stop()

            taskNoteTestWindow.appHasFocus = false

            verify(!taskNoteTestWindow.tickTickRefreshDebounce.running,
                   "there is nobody looking at the note to refresh it for")
        }

        function test_clicking_the_body_of_an_already_focused_task_note_schedules_nothing() {
            skipUnlessTickTickBuilt()
            taskNoteTestWindow.appHasFocus = true
            taskNoteTestWindow.tickTickRefreshDebounce.stop()

            mouseClick(taskNoteTestWindow.editor,
                       taskNoteTestWindow.editor.width / 2,
                       taskNoteTestWindow.editor.height / 2)

            verify(!taskNoteTestWindow.tickTickRefreshDebounce.running,
                   "a click inside a note the user is already working in is a caret placement, " +
                   "not a request for fresh data — refreshing here meant one network request " +
                   "per click")
        }

        function test_an_ordinary_note_never_schedules_a_ticktick_refresh() {
            skipUnlessTickTickBuilt()
            soloWindow.tickTickRefreshDebounce.stop()

            soloWindow.appHasFocus = true

            verify(!soloWindow.tickTickRefreshDebounce.running,
                   "a normal note's content is a local file — there is nothing to pull")

            soloWindow.appHasFocus = false
        }

        // The explicit gesture, and the reason the implicit one above is
        // allowed to be conservative: whatever the debounce declines to do,
        // clicking the footer always does.
        function test_the_synced_footer_is_always_clickable_on_the_task_note() {
            skipUnlessTickTickBuilt()
            const footer = findChild(taskNoteTestWindow, "tickTickSyncFooter")
            verify(footer !== null, "the task note has a synced-N-ago footer")
            verify(footer.visible)
        }

        // The footer's "synced N ago" text is computed against a clock
        // (footer._now) that only moves on real events — never a repeating
        // timer (see CLAUDE.md). The event that keeps it honest in practice
        // is the pointer entering the note: without it, the label could read
        // "synced just now" forever, because the only idle-time event (the
        // scheduled pull) resets lastSyncTime at the same instant it bumps
        // the clock.
        function test_hovering_the_note_restamps_the_synced_footer_clock() {
            skipUnlessTickTickBuilt()
            const footer = findChild(taskNoteTestWindow, "tickTickSyncFooter")
            verify(footer !== null, "the task note has a synced-N-ago footer")

            // Park the pointer outside the note, then age the footer's clock
            // to a sentinel far in the past.
            mouseMove(taskNoteTestWindow.contentItem, -50, -50)
            tryCompare(taskNoteTestWindow, "hovered", false)
            footer._now = new Date(12345)

            mouseMove(taskNoteTestWindow.contentItem,
                      taskNoteTestWindow.width / 2,
                      taskNoteTestWindow.height / 2)
            tryCompare(taskNoteTestWindow, "hovered", true)

            tryVerify(() => footer._now.getTime() > 12345,
                      5000,
                      "the pointer entering the note re-stamps the clock, so the label " +
                      "reflects how long ago the sync actually was by the time anyone " +
                      "is close enough to read it")
        }

        function test_an_ordinary_note_has_no_synced_footer() {
            skipUnlessTickTickBuilt()
            const footer = findChild(soloWindow, "tickTickSyncFooter")
            verify(footer === null || !footer.visible,
                   "only the task note has a sync state to report")
        }

        function test_the_task_note_is_titled_with_the_ticktick_list_it_shows() {
            skipUnlessTickTickBuilt()
            const title = findChild(taskNoteTestWindow, "tickTickListTitle")
            verify(title !== null, "the task note has a list-title header")
            verify(title.visible)
        }

        function test_an_ordinary_note_has_no_list_title() {
            skipUnlessTickTickBuilt()
            const title = findChild(soloWindow, "tickTickListTitle")
            verify(title === null || !title.visible,
                   "only the task note is showing tasks from a TickTick list")
        }

        function test_the_title_follows_the_selected_ticktick_list() {
            skipUnlessTickTickBuilt()
            const title = findChild(taskNoteTestWindow, "tickTickListTitle")

            tickTick.sync.selectProject("test-project-id", "Groceries")

            tryCompare(title, "text", "Groceries")

            // selectProject's side effect of blanking the cached note must
            // not leak into a later spec — every task-note spec reseeds its
            // own text via seedTaskNote(), but restore the selected project
            // itself so later specs asserting on the title text start clean.
            tickTick.sync.selectProject("", "")
        }

        function test_the_task_note_reserves_room_above_its_text_for_the_title() {
            skipUnlessTickTickBuilt()
            verify(taskNoteTestWindow.editorFlick.anchors.topMargin > 8,
                   "the list title sits above the text, not on top of it")
            compare(soloWindow.editorFlick.anchors.topMargin, 8,
                    "an ordinary note has no title to make room for")
        }

        // --- The task note's editing rules ---------------------------------
        //
        // The task note's line count IS its task list, so the two keystrokes
        // that change it — Enter and Backspace — do not mean in this note what
        // they mean anywhere else. Both were reported as bugs in their default
        // form: Enter made blank lines that repairCheckboxList then turned into
        // ghost checkboxes, and Backspace could never actually remove a line
        // because that same repair regrew the marker under the caret ("you
        // cannot use backspace to delete a note, it will spawn in the [ ]
        // characters").
        //
        // The module is unauthorized in this harness, so nothing here reaches
        // TickTick; what these pin is the note's own text after the keystroke,
        // which is what the sync layer then diffs.

        // Gives `win`'s editor real keyboard focus. keyClick() always targets
        // QGuiApplication::focusWindow() rather than a specific item, and
        // activation is not synchronous with several note windows in play —
        // see the identical dance the arrow-key specs above do.
        function focusEditorOf(win) {
            win.requestActivate()
            tryCompare(win, "active", true)
            win.editor.forceActiveFocus()
            tryCompare(win.editor, "activeFocus", true)
        }

        // Puts the note in a known state: a compose slot on top (see
        // TickTickTaskList::kComposeIndex) and two task lines under it.
        function seedTaskNote(composeText) {
            taskNoteTestWindow.docHandler.setMarkdown(
                "- [ ] " + composeText + "\n- [ ] Walk dog\n- [ ] Buy milk")
            taskNoteTestWindow.refreshComposeSlotFilled()
            focusEditorOf(taskNoteTestWindow)
            // Let the text layout settle and the compose-slot rect debounce
            // fire, then measure once more explicitly. Without this a spec
            // asserting on the rule's geometry can read the PREVIOUS spec's
            // layout — the debounce is 32ms and positionToRectangle needs a
            // frame — which is how a one-line slot was measured against a
            // still-wrapped rect and the rule looked like it had spilled onto
            // the task line below.
            wait(80)
            taskNoteTestWindow.recomputeComposeSlotRect()
        }

        function test_enter_never_adds_a_line_to_the_task_note() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            // On a task line, well away from the compose slot.
            taskNoteTestWindow.editor.cursorPosition =
                taskNoteTestWindow.docHandler.toMarkdown().indexOf("Walk dog") + 4

            keyClick(Qt.Key_Return)

            compare(taskNoteTestWindow.docHandler.toMarkdown(),
                    "- [ ] \n- [ ] Walk dog\n- [ ] Buy milk",
                    "a newline here would be a line with no task behind it, which the checkbox " +
                    "repair then turns into a ghost item the user cannot remove")
        }

        function test_enter_still_continues_a_list_in_an_ordinary_note() {
            skipUnlessTickTickBuilt()
            soloWindow.docHandler.setMarkdown("- [ ] Walk dog")
            focusEditorOf(soloWindow)
            soloWindow.editor.cursorPosition = soloWindow.docHandler.toMarkdown().length

            keyClick(Qt.Key_Return)

            verify(soloWindow.docHandler.toMarkdown().indexOf("\n") >= 0,
                   "the Enter rule is scoped to the task note — a normal note still continues " +
                   "its list")
            soloWindow.docHandler.setMarkdown("")
        }

        function test_backspace_at_the_start_of_an_emptied_task_removes_its_line() {
            skipUnlessTickTickBuilt()
            taskNoteTestWindow.docHandler.setMarkdown("- [ ] \n- [ ] \n- [ ] Buy milk")
            taskNoteTestWindow.refreshComposeSlotFilled()
            focusEditorOf(taskNoteTestWindow)
            // The caret at the start of the SECOND line's text — the emptied
            // task the user has just backspaced the title out of.
            taskNoteTestWindow.editor.cursorPosition = 13

            keyClick(Qt.Key_Backspace)

            compare(taskNoteTestWindow.docHandler.toMarkdown(), "- [ ] \n- [ ] Buy milk",
                    "the whole line goes, which is how the sync layer hears 'I'm done with this " +
                    "task' — the default backspace instead ate into the marker, which the repair " +
                    "pass immediately put back")
        }

        function test_backspace_cannot_delete_the_compose_slot() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            taskNoteTestWindow.editor.cursorPosition =
                taskNoteTestWindow.docHandler.listLabelStartAt(0)

            keyClick(Qt.Key_Backspace)

            compare(taskNoteTestWindow.docHandler.toMarkdown(),
                    "- [ ] \n- [ ] Walk dog\n- [ ] Buy milk",
                    "the slot is permanent furniture — there is always somewhere to type a new " +
                    "task, and letting the default handler eat its marker is the same defect in " +
                    "a place no re-render would fix")
        }

        // Reported: "triple clicking to select the whole task then deleting
        // works on most tasks, but not the task creator — we run into the
        // infinitely regenerating checkbox markdown characters". Selecting the
        // slot and deleting is a real gesture (clear what I typed), and it must
        // leave an empty SLOT: a bare blank line has no glyph, no underline and
        // no commit button, Backspace refuses to remove it, and only the next
        // pull's re-render put the marker back — which is what "regenerating"
        // meant.
        //
        // The selection is made with editor.select() rather than by synthesizing
        // three clicks: what matters is the state a triple-click produces (the
        // whole block selected, marker included), and a real triple-click
        // depends on the platform double-click interval.
        function test_selecting_the_whole_compose_slot_and_deleting_leaves_an_empty_slot() {
            skipUnlessTickTickBuilt()
            seedTaskNote("Call dentist")
            const editor = taskNoteTestWindow.editor
            editor.select(0, taskNoteTestWindow.docHandler.toMarkdown().indexOf("\n"))

            keyClick(Qt.Key_Backspace)

            compare(taskNoteTestWindow.docHandler.toMarkdown(),
                    "- [ ] \n- [ ] Walk dog\n- [ ] Buy milk",
                    "the slot is permanent furniture — clearing its text leaves an empty slot, " +
                    "not a blank line only a pull could repair")
            compare(editor.cursorPosition, taskNoteTestWindow.docHandler.listLabelStartAt(0),
                    "and the caret is where the next task gets typed, not inside the marker")
        }

        // The other half of the same gesture, pinned unchanged: on a REAL task
        // line, deleting the selection leaves the line blank, which is step one
        // of "I'm done with this" — the second Backspace then removes it (see
        // test_backspace_at_the_start_of_an_emptied_task_removes_its_line). Only
        // line 0 is furniture.
        function test_selecting_a_whole_task_line_and_deleting_leaves_it_blank() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            const editor = taskNoteTestWindow.editor
            const md = taskNoteTestWindow.docHandler.toMarkdown()
            const lineStart = md.indexOf("- [ ] Walk dog")
            editor.select(lineStart, md.indexOf("\n", lineStart))

            keyClick(Qt.Key_Backspace)

            compare(taskNoteTestWindow.docHandler.toMarkdown(),
                    "- [ ] \n\n- [ ] Buy milk",
                    "a task line is not furniture — it goes blank and a second Backspace removes it")
        }

        // --- Multi-line selections in the task note --------------------------
        //
        // Every test above stays within ONE task line (or the whole compose
        // slot, itself one block). A selection that instead SPANS lines — and
        // whose endpoints don't sit exactly on line boundaries — used to be
        // decided entirely by TickTickTaskList::diff comparing the merged
        // result against its last render: a merge matching nothing there
        // uploaded nothing and silently reverted on the very next pull, with
        // no visible error (see item 007's work order for the full trace).
        // Backspace/Delete/Cut here snap the removal to whole task lines
        // instead — see NoteWindow.qml's selectionSpansMultipleTaskLines and
        // Keys.onPressed — which the diff already reads as bulk completion,
        // same as a clean triple-click-drag-and-delete always has. Typing or
        // pasting over such a selection instead collapses it to its start,
        // so the merge never happens at all.

        function test_backspace_over_a_multiline_partial_selection_removes_whole_lines_without_merging() {
            skipUnlessTickTickBuilt()
            taskNoteTestWindow.docHandler.setMarkdown(
                "- [ ] \n- [ ] Walk dog\n- [ ] Buy milk\n- [ ] Water plants")
            taskNoteTestWindow.refreshComposeSlotFilled()
            focusEditorOf(taskNoteTestWindow)
            const editor = taskNoteTestWindow.editor
            const md = taskNoteTestWindow.docHandler.toMarkdown()
            // Mid "Walk dog" to mid "Buy milk" — neither endpoint sits on a
            // line boundary.
            editor.select(md.indexOf("Walk dog") + 2, md.indexOf("Buy milk") + 2)

            keyClick(Qt.Key_Backspace)

            compare(taskNoteTestWindow.docHandler.toMarkdown(),
                    "- [ ] \n- [ ] Water plants",
                    "both touched task lines are gone whole, and the untouched fourth line " +
                    "survives — never merged with anything")
        }

        function test_delete_over_a_multiline_selection_including_the_compose_slot_leaves_it_present() {
            skipUnlessTickTickBuilt()
            seedTaskNote("Call dentist")
            const editor = taskNoteTestWindow.editor
            const md = taskNoteTestWindow.docHandler.toMarkdown()
            // Mid the compose slot's own text to mid "Walk dog" — spans line
            // 0 (the slot) and line 1.
            editor.select(md.indexOf("dentist") + 2, md.indexOf("Walk dog") + 2)

            keyClick(Qt.Key_Delete)

            compare(taskNoteTestWindow.docHandler.toMarkdown(),
                    "- [ ] Call dentist\n- [ ] Buy milk",
                    "the compose slot's own text is untouched — it is permanent furniture — " +
                    "only the fully-contained task line beneath it is removed")
        }

        function test_ctrl_x_over_a_multiline_selection_copies_the_text_before_removing_the_lines() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            const editor = taskNoteTestWindow.editor
            const md = taskNoteTestWindow.docHandler.toMarkdown()
            const selectionStart = md.indexOf("Walk dog") + 2
            const selectionEnd = md.indexOf("Buy milk") + 4
            editor.select(selectionStart, selectionEnd)
            const cutText = md.substring(selectionStart, selectionEnd)

            keyClick(Qt.Key_X, Qt.ControlModifier)

            compare(taskNoteTestWindow.docHandler.toMarkdown(), "- [ ] ",
                    "both task lines the selection touched are removed, same as Backspace/Delete")

            // Same clipboard-via-paste verification as
            // test_ctrl_c_copies_the_selection_and_shows_the_copy_toast.
            editor.cursorPosition = editor.length
            editor.paste()
            verify(editor.text.indexOf(cutText) !== -1,
                   "Ctrl+X copied the exact selected text — including its mid-line boundaries " +
                   "— before removing the lines")
        }

        function test_typing_over_a_multiline_selection_inserts_at_its_start_without_deleting_lines() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            const editor = taskNoteTestWindow.editor
            const md = taskNoteTestWindow.docHandler.toMarkdown()
            const selectionStart = md.indexOf("Walk dog") + 2   // between "Wa" and "lk dog"
            const selectionEnd = md.indexOf("Buy milk") + 2
            editor.select(selectionStart, selectionEnd)

            keyClick(Qt.Key_Z)

            compare(editor.cursorPosition, selectionStart + 1,
                    "the selection collapsed to its start and the typed character landed there")
            compare(taskNoteTestWindow.docHandler.toMarkdown(),
                    "- [ ] \n- [ ] Wazlk dog\n- [ ] Buy milk",
                    "both original lines survive whole and unmerged — only the one character " +
                    "was inserted, at the selection's start")
        }

        function test_backspace_at_the_start_of_a_task_that_still_has_a_title_does_nothing() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            taskNoteTestWindow.editor.cursorPosition =
                taskNoteTestWindow.docHandler.toMarkdown().indexOf("Walk dog")

            keyClick(Qt.Key_Backspace)

            compare(taskNoteTestWindow.docHandler.toMarkdown(),
                    "- [ ] \n- [ ] Walk dog\n- [ ] Buy milk",
                    "the default handling here MERGES this line into the one above, which in " +
                    "this note is one task silently absorbing another's text — clearing the " +
                    "title first is the gesture for finishing with a task")
        }

        function test_backspace_inside_a_task_title_still_deletes_a_character() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            taskNoteTestWindow.editor.cursorPosition =
                taskNoteTestWindow.docHandler.toMarkdown().indexOf("Walk dog") + 4

            keyClick(Qt.Key_Backspace)

            compare(taskNoteTestWindow.docHandler.toMarkdown(),
                    "- [ ] \n- [ ] Wal dog\n- [ ] Buy milk",
                    "only a caret sitting exactly at the start of an item's text means 'remove " +
                    "this line'; ordinary editing is untouched")
        }

        // --- The task note's caret never enters a checkbox ------------------
        //
        // A checkbox's "- [ ] " is six real characters drawn as one glyph, so
        // a caret inside it renders at the box's own edge and does not appear
        // to move at all (reported: "the user can use the arrow keys to move
        // into the checkboxes, but the display doesn't change"). In this note
        // the marker is furniture the sync layer owns, so the caret skips it
        // wholesale. See MarkerCaretGuard.

        function test_left_arrow_at_a_tasks_text_skips_its_checkbox_to_the_line_above() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            taskNoteTestWindow.editor.cursorPosition = 13   // start of "Walk dog"

            keyClick(Qt.Key_Left)

            compare(taskNoteTestWindow.editor.cursorPosition, 6,
                    "one press leaves the line, instead of six presses inching through a marker " +
                    "whose characters all draw at the same two x positions")
        }

        function test_left_arrow_on_the_first_line_keeps_the_caret_on_its_text() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            taskNoteTestWindow.editor.cursorPosition = 6    // the compose slot's label

            keyClick(Qt.Key_Left)

            compare(taskNoteTestWindow.editor.cursorPosition, 6,
                    "there is no line above to skip to, so the key does nothing rather than " +
                    "parking the caret inside the slot's marker")
        }

        function test_home_lands_on_the_tasks_text_not_inside_its_checkbox() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            taskNoteTestWindow.editor.cursorPosition = 17   // mid-"Walk dog"

            keyClick(Qt.Key_Home)

            compare(taskNoteTestWindow.editor.cursorPosition, 13,
                    "the start of the line is marker text; the start of what the user can read " +
                    "and type is the label")
        }

        function test_right_arrow_off_the_end_of_a_line_skips_the_next_checkbox() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            taskNoteTestWindow.editor.cursorPosition = 6    // end of line 0

            keyClick(Qt.Key_Right)

            compare(taskNoteTestWindow.editor.cursorPosition, 13,
                    "entering a line puts the caret on its text, not on the first of six " +
                    "invisible marker characters")
        }

        function test_typing_after_skipping_a_checkbox_leaves_the_marker_intact() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            taskNoteTestWindow.editor.cursorPosition = 13

            keyClick(Qt.Key_Home)
            keyClick(Qt.Key_X)

            compare(taskNoteTestWindow.docHandler.toMarkdown(),
                    "- [ ] \n- [ ] xWalk dog\n- [ ] Buy milk",
                    "this is the point of the rule: a caret inside the marker used to split it " +
                    "into '- [ x] ', which stops parsing as a checkbox")
        }

        function test_an_ordinary_note_still_lets_the_caret_into_a_checkbox_marker() {
            skipUnlessTickTickBuilt()
            soloWindow.docHandler.setMarkdown("- [ ] Walk dog")
            focusEditorOf(soloWindow)
            soloWindow.editor.cursorPosition = 6

            keyClick(Qt.Key_Left)

            compare(soloWindow.editor.cursorPosition, 5,
                    "an ordinary note's '- [ ] ' is markdown the user typed and may want to " +
                    "select or edit — the skip is scoped to the task note")
            soloWindow.docHandler.setMarkdown("")
        }

        function test_an_ordinary_notes_home_key_still_goes_to_the_line_start() {
            skipUnlessTickTickBuilt()
            soloWindow.docHandler.setMarkdown("- [ ] Walk dog")
            focusEditorOf(soloWindow)
            soloWindow.editor.cursorPosition = 10

            keyClick(Qt.Key_Home)

            compare(soloWindow.editor.cursorPosition, 0,
                    "same scoping, for the other key the guard owns")
            soloWindow.docHandler.setMarkdown("")
        }

        // --- The compose slot's check button --------------------------------
        //
        // Typing into the slot turns its checkbox into a filled check button
        // (feature request: "when typing begins, let's change the checkbox icon
        // to a check button") — the affordance that says this line is not a
        // task yet and pressing this is what makes it one.

        // The glyph drawn over the compose slot's (invisible) marker text. The
        // overlay's delegates are created off a model synced from C++ ranges,
        // so a freshly-seeded note needs a moment before they exist.
        function composeSlotGlyph() {
            for (let i = 0; i < taskNoteTestWindow.listMarkerRepeater.count; ++i) {
                const glyph = taskNoteTestWindow.listMarkerRepeater.itemAt(i)
                // A marker range starts at the drawn glyph (the "["), not the
                // line's first character — so the slot's glyph is the one
                // beginning before line 0's own text does.
                if (glyph && glyph.start < taskNoteTestWindow.composeSlotLabelStart)
                    return glyph
            }
            return null
        }

        function test_an_empty_compose_slot_draws_a_plain_checkbox() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")

            verify(!taskNoteTestWindow.composeSlotFilled,
                   "there is nothing to commit yet")
            tryVerify(() => composeSlotGlyph() !== null, 2000,
                      "the slot has a checkbox glyph")
            verify(!composeSlotGlyph().showsAsCommitButton,
                   "an empty slot looks like every other unticked box")
        }

        function test_typing_into_the_compose_slot_turns_its_box_into_a_check_button() {
            skipUnlessTickTickBuilt()
            seedTaskNote("Call dentist")

            verify(taskNoteTestWindow.composeSlotFilled,
                   "there is now something worth committing")
            tryVerify(() => composeSlotGlyph() !== null, 2000,
                      "the slot has a checkbox glyph")
            verify(composeSlotGlyph().showsAsCommitButton,
                   "the box reads as a button to press, not a box to tick")
        }

        function test_a_filled_task_line_is_never_a_check_button() {
            skipUnlessTickTickBuilt()
            seedTaskNote("Call dentist")
            tryVerify(() => composeSlotGlyph() !== null, 2000)

            for (let i = 0; i < taskNoteTestWindow.listMarkerRepeater.count; ++i) {
                const glyph = taskNoteTestWindow.listMarkerRepeater.itemAt(i)
                if (glyph && glyph.start >= taskNoteTestWindow.composeSlotLabelStart)
                    verify(!glyph.showsAsCommitButton,
                           "only the compose slot commits — a real task's box still ticks")
            }
        }

        // --- The compose slot's underline ----------------------------------
        //
        // A thin rule along the slot's row, so the note's top line reads as a
        // field to type a task into rather than as a task that has lost its
        // text — in a plain-text editor there is nothing else to tell the two
        // apart. It brightens on hover, the same language every other
        // interactable in the app speaks.

        function composeSlotUnderlineOf(win) {
            return findChild(win, "composeSlotUnderline")
        }

        function test_the_compose_slot_is_underlined() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            const rule = composeSlotUnderlineOf(taskNoteTestWindow)
            verify(rule !== null, "the underline exists")
            tryVerify(() => rule.visible && rule.width > 0, 2000,
                      "it spans the slot's row once the text layout has settled")
        }

        function test_the_underline_sits_on_the_slots_own_row() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            const rule = composeSlotUnderlineOf(taskNoteTestWindow)
            tryVerify(() => rule.visible && rule.height > 0, 2000)

            // Within the first line's band, and above wherever the SECOND line
            // starts — a rule that drifted onto the next row would underline a
            // real task instead of the slot.
            const firstRow = taskNoteTestWindow.editor.positionToRectangle(0)
            const secondRow = taskNoteTestWindow.editor.positionToRectangle(
                taskNoteTestWindow.docHandler.toMarkdown().indexOf("Walk dog"))
            verify(rule.y >= firstRow.y, "not above the slot's own row")
            verify(rule.y + rule.height <= secondRow.y + 1,
                   "not spilling onto the first real task's row")
        }

        function test_the_underline_starts_where_typing_starts_not_under_the_checkbox() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            const rule = composeSlotUnderlineOf(taskNoteTestWindow)
            tryVerify(() => rule.visible && rule.width > 0, 2000)

            // The rule marks the area that can be typed into. Running it under
            // the checkbox would underline the commit button — a control, not a
            // field.
            const labelRect = taskNoteTestWindow.editor.positionToRectangle(
                taskNoteTestWindow.composeSlotLabelStart)
            verify(rule.x > 0, "it does not start at the editor's left edge")
            fuzzyCompare(rule.x, labelRect.x, 1,
                         "it starts exactly where the slot's own text does")
            fuzzyCompare(rule.x + rule.width, taskNoteTestWindow.editor.width, 1,
                         "and runs to the editor's edge, which is where wrapping happens")
        }

        function test_the_underline_follows_the_slot_down_when_its_text_wraps() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            const rule = composeSlotUnderlineOf(taskNoteTestWindow)
            tryVerify(() => rule.visible && rule.height > 0, 2000)
            const oneLineY = rule.y

            // Long enough to certainly wrap in a 260px-wide note.
            seedTaskNote("buy oat milk and eggs and bread and coffee beans before the shop closes")

            tryVerify(() => rule.y > oneLineY, 2000,
                      "the rule tracks the LAST visual line the slot's text occupies — left on " +
                      "the first, it would strike through the wrapped text instead of " +
                      "underlining the field")

            const firstVisualLine = taskNoteTestWindow.editor.positionToRectangle(
                taskNoteTestWindow.composeSlotLabelStart)
            verify(rule.y >= firstVisualLine.y + firstVisualLine.height,
                   "it is genuinely below the first visual line, not merely nudged")
        }

        function test_a_wrapped_slot_does_not_thicken_its_underline() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            const rule = composeSlotUnderlineOf(taskNoteTestWindow)
            tryVerify(() => rule.visible && rule.height > 0, 2000)
            const thickness = rule.height

            seedTaskNote("buy oat milk and eggs and bread and coffee beans before the shop closes")
            tryVerify(() => taskNoteTestWindow.composeSlotRect.height > thickness * 4, 2000,
                      "precondition: the slot's band really did grow by wrapping")

            compare(rule.height, thickness,
                    "thickness comes from the font, not from the band — deriving it from the " +
                    "rect would fatten the rule with every wrapped line")
        }

        function test_the_underline_brightens_on_hover_and_dims_again() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            const rule = composeSlotUnderlineOf(taskNoteTestWindow)
            tryVerify(() => rule.visible && rule.height > 0, 2000)

            const resting = rule.color.a
            taskNoteTestWindow.composeSlotHovered = true
            tryVerify(() => rule.color.a > resting, 2000,
                      "hovering the row makes the rule more intense")

            taskNoteTestWindow.composeSlotHovered = false
            tryVerify(() => rule.color.a <= resting + 0.001, 2000,
                      "and it settles back once the pointer leaves")
        }

        function test_only_the_slots_own_row_counts_as_hovering_the_underline() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            tryVerify(() => taskNoteTestWindow.composeSlotRect.height > 0, 2000)
            const row = taskNoteTestWindow.composeSlotRect

            verify(taskNoteTestWindow.composeSlotRowUnderPointer(4, row.y + row.height / 2),
                   "a pointer on the row lights it up")
            verify(!taskNoteTestWindow.composeSlotRowUnderPointer(4, row.y + row.height + 2),
                   "a pointer on the task line below does not")
            verify(!taskNoteTestWindow.composeSlotRowUnderPointer(4, -20),
                   "neither does one in the HUD strip above the editor, which maps to a " +
                   "negative y here")
        }

        function test_moving_the_pointer_onto_the_slots_row_lights_the_underline() {
            skipUnlessTickTickBuilt()
            seedTaskNote("")
            tryVerify(() => taskNoteTestWindow.composeSlotRect.height > 0, 2000)
            const row = taskNoteTestWindow.composeSlotRect
            const editor = taskNoteTestWindow.editor

            // Joins up the two halves the specs above cover separately: the
            // pointer really does reach this through mouseDragArea, which sits
            // ON TOP of the editor and claims hover for the whole area — a
            // HoverHandler declared on the underline itself would never see a
            // single event.
            mouseMove(editor, 8, row.y + row.height / 2)
            tryCompare(taskNoteTestWindow, "composeSlotHovered", true)

            mouseMove(editor, 8, row.y + row.height * 2)
            tryCompare(taskNoteTestWindow, "composeSlotHovered", false)
        }

        function test_an_ordinary_note_has_no_compose_slot_underline() {
            skipUnlessTickTickBuilt()
            soloWindow.docHandler.setMarkdown("- [ ] Walk dog")
            soloWindow.refreshComposeSlotFilled()

            const rule = composeSlotUnderlineOf(soloWindow)
            tryVerify(() => rule === null || !rule.visible, 2000,
                      "the rule is an affordance for the one note that has a compose slot")
            soloWindow.docHandler.setMarkdown("")
        }

        function test_an_ordinary_notes_first_checkbox_is_never_a_check_button() {
            skipUnlessTickTickBuilt()
            soloWindow.docHandler.setMarkdown("- [ ] Walk dog")
            soloWindow.refreshComposeSlotFilled()

            verify(!soloWindow.composeSlotFilled,
                   "an ordinary note has no compose slot at all — its first line is just a task")
            soloWindow.docHandler.setMarkdown("")
        }

        // --- The completion fade's undo-window guard ------------------------
        //
        // TickTickSync::kUploadDebounceMs is kept longer than
        // tickTickCompletionFade's three-second hold specifically so a tick and
        // the fade's eventual removal of it land together as one edit — see
        // that constant's own comment in TickTickSync.cpp. But an undo landing
        // AFTER the fade has already removed the line is a separate hazard: a
        // restored `- [x] …` line would otherwise start the fade running a
        // second time over a task that is already closed in TickTick (nothing
        // can take that completion back — see TickTickClient.h).
        // tickTickLastFadedLabel (NoteWindow.qml) is the guard against that,
        // set by the fade's own ScriptAction on every real removal. These pin
        // it directly, setting it by hand rather than waiting out a real
        // three-second fade to arm it.

        function resetFadeGuardState() {
            taskNoteTestWindow.tickTickCompletionFade.stop()
            taskNoteTestWindow.tickTickFadeOpacity = 1
            taskNoteTestWindow.tickTickLastFadedLabel = ""
            taskNoteTestWindow.docHandler.setMarkdown("- [ ] ")
            wait(50)
        }

        function test_the_fade_does_not_rearm_for_a_line_restored_after_it_already_faded() {
            skipUnlessTickTickBuilt()
            resetFadeGuardState()

            // Stands in for the fade having already removed this exact task
            // once this session — the same bookkeeping its own ScriptAction
            // does on a real removal.
            taskNoteTestWindow.tickTickLastFadedLabel = "Buy milk"

            // An undo (or a paste) brings a ticked line with that same label
            // back into the note.
            taskNoteTestWindow.docHandler.setMarkdown("- [ ] \n- [x] Buy milk")
            wait(50)

            verify(!taskNoteTestWindow.tickTickCompletionFade.running,
                   "a line this window already faded out once must not fade a second time over a " +
                   "task that is already closed in TickTick")
            verify(taskNoteTestWindow.docHandler.toMarkdown().indexOf("- [x] Buy milk") !== -1,
                   "since the fade never started, the restored line is left exactly as it came " +
                   "back rather than being silently removed again")

            resetFadeGuardState()
        }

        function test_the_fade_still_arms_for_a_different_ticked_line() {
            skipUnlessTickTickBuilt()
            resetFadeGuardState()

            taskNoteTestWindow.tickTickLastFadedLabel = "Buy milk"

            // A DIFFERENT task ticked — the guard is keyed on the label that
            // was actually faded, not a blanket "never fade again" switch.
            taskNoteTestWindow.docHandler.setMarkdown("- [ ] \n- [x] Walk dog")
            wait(50)

            verify(taskNoteTestWindow.tickTickCompletionFade.running,
                   "only a line matching the one already faded is guarded — every other tick " +
                   "still gets its undo window")

            resetFadeGuardState()
        }

        // Bug found live: two ADJACENT ticked lines both fading in the same
        // burst. Removing the first shifts the second's checkbox glyph up by
        // exactly the first line's own length — which, for two lines sitting
        // directly next to each other, lands the second glyph at the EXACT
        // SAME character offset the first one's used to occupy.
        // tickTickFadingPosition's computed VALUE therefore does not change
        // even though what it refers to completely has, so the change
        // handler the fade is normally started from never fires, and the
        // second line sat there checked forever with no undo window and no
        // path to TickTick until the next pull silently swept it away.
        function test_two_adjacent_ticked_lines_both_eventually_fade() {
            skipUnlessTickTickBuilt()
            resetFadeGuardState()

            taskNoteTestWindow.docHandler.setMarkdown(
                "- [ ] \n- [x] Buy milk\n- [x] Walk dog")
            wait(50)

            verify(taskNoteTestWindow.tickTickCompletionFade.running,
                   "the fade arms for the first checked line the moment both are ticked")

            wait(3200)

            compare(taskNoteTestWindow.docHandler.toMarkdown(), "- [ ] \n- [x] Walk dog",
                    "the first line is gone after its own fade completes")
            verify(taskNoteTestWindow.tickTickCompletionFade.running,
                   "the second still-checked line's fade must start too, even though its " +
                   "glyph now sits at the exact position the first line's glyph just vacated")

            wait(3200)

            compare(taskNoteTestWindow.docHandler.toMarkdown(), "- [ ] ",
                    "the second line is gone too — nothing is left checked and stuck")
            verify(!taskNoteTestWindow.tickTickCompletionFade.running,
                   "the queue has nothing left to fade")

            resetFadeGuardState()
        }

        // A reload — a pull among them — starts the "already faded this one"
        // memory over: a pull only ever asks TickTick for open tasks, so by
        // then the server's own account of things has moved on regardless of
        // what this window remembers fading. Exercised on a plain note rather
        // than taskNoteTestWindow, the same way
        // test_an_external_file_change_can_be_reloaded_into_the_open_note
        // above does — tickTickLastFadedLabel is cleared unconditionally by
        // loadFromDisk, with no dependency on the TickTick module being built.
        function test_reloading_a_note_clears_the_fade_guard() {
            const id = noteStore.createNote("original text")
            const win = restoreOrderTestComponent.createObject(root, {
                noteId: id,
                index: 93,
                noteX: 0,
                noteY: 0,
                noteWidth: 260,
                noteHeight: 260,
                screenName: "",
                alwaysOnTop: false,
            })
            verify(win !== null)
            tryCompare(win.editor, "ready", true)

            win.tickTickLastFadedLabel = "Buy milk"

            noteStore.saveNoteContent(id, "changed on disk")
            wait(500) // past NoteStore's 300ms save debounce, so the write lands
            verify(!noteStore.hasUnsavedChanges(id))

            win.editor.loadFromDisk(false)

            compare(win.tickTickLastFadedLabel, "",
                    "loadFromDisk must clear the memory of the last line faded, or a task " +
                    "restored under the SAME label after a later tick would be wrongly guarded")

            noteStore.deleteNote(id)
            win.destroy()
        }
    }
}
