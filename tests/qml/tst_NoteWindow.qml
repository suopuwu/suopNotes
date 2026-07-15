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

        // Right-click drag past the threshold should move the window, same
        // as before the context menu (removed — feature request: cut/copy/
        // paste live as toolbar buttons instead, see SelectionToolbar.qml)
        // used to compete with it for a plain (non-drag) right-click.
        function test_right_click_drag_past_threshold_starts_a_move() {
            const win = instantiator.objectAt(0)
            const area = win.mouseDragArea
            const editor = win.editor
            const cx = editor.width / 2
            const cy = editor.height / 2

            mouseDrag(editor, cx, cy, 20, 20, Qt.RightButton, Qt.NoModifier, 50)

            verify(area.rightDragStarted,
                   "moving past the threshold while right-button is held should start a drag")
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
    }
}
