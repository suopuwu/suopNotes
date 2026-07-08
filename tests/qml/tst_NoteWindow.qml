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

    TestCase {
        name: "NoteWindow"
        when: windowShown

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

        function test_mouse_drag_is_scoped_to_right_button_only() {
            const area = instantiator.objectAt(0).mouseDragArea
            compare(area.acceptedButtons, Qt.RightButton)
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
            flick.contentY = 0

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

            // mouseDrag() (not raw mousePress/mouseMove/mouseRelease) is
            // required here — MouseArea.drag needs the drag threshold
            // crossed via an intermediate move step before it latches onto
            // drag.target at all; a single large mouseMove jump straight to
            // the destination never triggers that (confirmed live: drag.active
            // stayed false with a raw press+single-move+release sequence).
            mouseDrag(win.scrollIndicator, startX, startY, 0, 80, Qt.LeftButton)
            wait(50)

            verify(flick.contentY > 0, "dragging the scroll handle downward should scroll the note's content")

            win.docHandler.setMarkdown("hi")
            wait(50)
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
    }
}
