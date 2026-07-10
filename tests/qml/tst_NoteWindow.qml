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

    SignalSpy {
        id: blurSpy
        target: blurEffect
        signalName: "appliedTo"
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

        // Right-click copy/paste menu (feature request): a plain right-click
        // (no drag in between press and release) opens the menu instead of
        // starting a window move.
        function test_right_click_without_dragging_opens_the_context_menu() {
            const win = instantiator.objectAt(0)
            const editor = win.editor
            const cx = editor.width / 2
            const cy = editor.height / 2

            mouseClick(editor, cx, cy, Qt.RightButton)
            verify(win.editorContextMenu.visible,
                   "a plain right-click (no drag) should open the context menu")

            win.editorContextMenu.close()
        }

        // The other half of the same fix: dragging past the threshold
        // should still move the window like before, and must NOT also open
        // the menu once the drag has started.
        function test_right_click_drag_past_threshold_starts_a_move_not_the_menu() {
            const win = instantiator.objectAt(0)
            const area = win.mouseDragArea
            const editor = win.editor
            const cx = editor.width / 2
            const cy = editor.height / 2

            mouseDrag(editor, cx, cy, 20, 20, Qt.RightButton, Qt.NoModifier, 50)

            verify(area.rightDragStarted,
                   "moving past the threshold while right-button is held should start a drag")
            verify(!win.editorContextMenu.visible, "a drag must not also open the context menu")
        }

        // Cut/Copy only make sense with an actual selection; Select All
        // should always work as long as there's any text.
        function test_context_menu_cut_and_copy_are_disabled_without_a_selection() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("hello world")
            wait(50)
            editor.cursorPosition = 0
            editor.deselect()

            verify(!win.editorContextMenu.cutItem.enabled, "Cut should be disabled with no selection")
            verify(!win.editorContextMenu.copyItem.enabled, "Copy should be disabled with no selection")

            editor.select(0, 5)
            verify(win.editorContextMenu.cutItem.enabled, "Cut should be enabled once text is selected")
            verify(win.editorContextMenu.copyItem.enabled, "Copy should be enabled once text is selected")

            win.docHandler.setMarkdown("")
            wait(50)
        }

        function test_context_menu_select_all_selects_the_whole_note() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("hello world")
            wait(50)
            editor.cursorPosition = 0
            editor.deselect()

            win.editorContextMenu.selectAllItem.triggered()

            compare(editor.selectionStart, 0)
            compare(editor.selectionEnd, editor.length)

            win.docHandler.setMarkdown("")
            wait(50)
        }

        function test_context_menu_cut_removes_the_selected_text() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("hello world")
            wait(50)
            editor.select(0, 5)

            win.editorContextMenu.cutItem.triggered()

            // editor.text always carries TextArea's own trailing "\n\n" for
            // a single-paragraph document, independent of anything cut()
            // does — asserting the visible content survives via
            // startsWith(), not an exact match against it.
            verify(editor.text.startsWith(" world"), "expected \"hello\" to be cut, leaving \" world\": " + editor.text)

            win.docHandler.setMarkdown("")
            wait(50)
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
            const cy = 20

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
            const cy = 20

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
            const cy = 20

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
            win.docHandler.setMarkdown("line one\n\nline two\n")
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

            win.docHandler.setMarkdown("line one\n\nline two\n")
            wait(50)

            mouseClick(flick, flick.width - 2, flick.height - 5, Qt.LeftButton)
            compare(editor.cursorPosition, editor.length)
        }

        // Pressing Up has nowhere to go once the cursor is already on the
        // editor's topmost line — rather than leaving it stuck mid-line, it
        // should jump to that line's start, the same place Home would land.
        function test_up_arrow_on_top_line_moves_cursor_to_start_of_line() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("line one\n\nline two\n")
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

            win.docHandler.setMarkdown("line one\n\nline two\n")
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

            win.docHandler.setMarkdown("line one\n\nline two\n")
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

            win.docHandler.setMarkdown("line one\n\nline two\n")
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

            win.docHandler.setMarkdown("line one\n\nline two\n")
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

            win.docHandler.setMarkdown("line one\n\nline two\n")
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

            win.docHandler.setMarkdown("line one\n\nline two\n")
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

            win.docHandler.setMarkdown("line one\n\nline two\n")
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
        // editorFlick.contentY actually moved.
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

            win.docHandler.setMarkdown("- item one\n")
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

            win.docHandler.setMarkdown("- item one\n")
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

            win.docHandler.setMarkdown("1. item one\n")
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

            win.docHandler.setMarkdown("1. item one\n")
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

        // Same two behaviors, checkbox variant.
        function test_pressing_enter_in_a_checkbox_list_item_continues_the_list() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("- [ ] item one\n")
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

            win.docHandler.setMarkdown("- [ ] item one\n")
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

            win.docHandler.setMarkdown("- [ ] milk\n")
            wait(50)

            // "- [ ] " is 6 characters — click near its start, well inside
            // the glyph itself, not the "milk" text after it.
            const rect = editor.positionToRectangle(2)
            mouseClick(editor, rect.x, rect.y + rect.height / 2, Qt.LeftButton)

            verify(win.docHandler.toMarkdown().indexOf("- [x] milk") !== -1,
                   "clicking the checkbox glyph should check it: " + win.docHandler.toMarkdown())
        }

        // Selectable numbers (feature request): unlike Qt's own QTextList
        // markers (paint-time decoration with no character position, never
        // selectable), a hand-rolled numbered marker is literal text —
        // selecting from the very start of the line includes it.
        function test_numbered_list_marker_text_is_selectable() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("1. milk\n")
            wait(50)

            editor.select(0, 2) // "1."
            compare(editor.selectedText, "1.")
        }

        // Configurable keyboard shortcuts (feature request): Ctrl+B (the
        // documented default for settings.shortcutBold) must toggle bold the
        // same as clicking the toolbar's "B" button — proves the Shortcut
        // item in NoteWindow.qml is actually wired to Settings and to
        // docHandler, not just present.
        function test_ctrl_b_shortcut_toggles_bold_on_the_current_selection() {
            const win = instantiator.objectAt(0)
            const editor = win.editor

            win.docHandler.setMarkdown("bold me\n")
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
            win.docHandler.setMarkdown("| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |\n")
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
            win.docHandler.setMarkdown("| Header 1 | Header 2 |\n| --- | --- |\n|  |  |\n\nintro\n")
            wait(50)
            verify(win.docHandler.toMarkdown().indexOf("Header 1") !== -1, "precondition: the table exists")

            win.docHandler.deleteTable(1)

            const md = win.docHandler.toMarkdown()
            verify(md.indexOf("Header 1") === -1, "the table should be gone")
            verify(md.indexOf("intro") !== -1, "surrounding text should survive")
        }
    }
}
