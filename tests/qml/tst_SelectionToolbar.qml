import QtQml
import QtQuick
import QtTest
import SuopNotesUi

Item {
    id: root

    // Real NoteWindow instances stand in for "the currently focused note" —
    // SelectionToolbar reads noteWindow.editor/docHandler/screen and the
    // model-backed noteX/noteY/noteWidth/noteHeight (NOT the plain Window
    // x/y/width/height — see SelectionToolbar.qml's reposition() for why),
    // all of which a real NoteWindow exposes exactly like Main.qml's
    // activeNoteWindow tracking would feed it in the app.
    NoteWindow {
        id: noteA
        noteId: "toolbar-test-note-a"
        index: 90
        noteX: 300
        noteY: 300
        noteWidth: 260
        noteHeight: 260
        screenName: ""
        alwaysOnTop: false
    }

    NoteWindow {
        id: noteB
        noteId: "toolbar-test-note-b"
        index: 91
        noteX: 700
        noteY: 300
        noteWidth: 260
        noteHeight: 260
        screenName: ""
        alwaysOnTop: false
    }

    SelectionToolbar {
        id: bar
    }

    // Shared across every NoteWindow in this file too (blurEffect is a
    // single app-wide singleton, same as in tst_NoteWindow.qml) — tests
    // below filter signalArguments for `bar` itself rather than assuming
    // it's the only emitter.
    SignalSpy {
        id: blurSpy
        target: blurEffect
        signalName: "appliedTo"
    }

    TestCase {
        name: "SelectionToolbar"
        when: windowShown

        // Ensures each test starts from the same "nothing tracked" state,
        // regardless of what an earlier test left bar.noteWindow pointing
        // at.
        function cleanup() {
            bar.noteWindow = null
        }

        // The bar is shown exactly once (at startup) and never hidden
        // again — repeatedly mapping/unmapping this window while a note
        // holds Wayland keyboard focus was found to disrupt that note's
        // own text input (see SelectionToolbar.qml's Component.onCompleted
        // comment). "Hidden" is instead simulated by making its content
        // invisible and non-interactive, which these tests check for
        // instead of bar.visible (permanently true after that first
        // show()).
        function test_content_invisible_and_disabled_when_no_note_is_tracked() {
            compare(bar.background.visible, false)
            compare(bar.content.visible, false)
            compare(bar.boldButton.enabled, false)
            compare(bar.dragArea.enabled, false)
        }

        function test_content_visible_and_enabled_once_a_note_is_tracked() {
            bar.noteWindow = noteA
            wait(20)

            compare(bar.background.visible, true)
            compare(bar.content.visible, true)
            compare(bar.boldButton.enabled, true)
            compare(bar.dragArea.enabled, true)
        }

        function test_content_hidden_again_once_the_tracked_note_is_cleared() {
            bar.noteWindow = noteA
            wait(20)
            verify(bar.content.visible)

            bar.noteWindow = null

            compare(bar.background.visible, false)
            compare(bar.content.visible, false)
            compare(bar.boldButton.enabled, false)
        }

        // Default placement (feature request): the bar should sit just
        // above the tracked note whenever the screen has room for that —
        // the top edge is first in the top/left/right/bottom priority
        // order reposition() follows.
        function test_defaults_to_above_the_tracked_note_when_there_is_room() {
            noteA.noteX = 300
            noteA.noteY = 300

            bar.noteWindow = noteA
            wait(20)

            verify(bar.y < noteA.noteY, "the bar should default to sitting above the note, not below it")
            compare(bar.y, noteA.noteY - bar.height - 8)
            compare(bar.x, noteA.noteX + noteA.noteWidth / 2 - bar.width / 2,
                    "the bar should default to horizontally centered over the note")
        }

        // With no room above (pinned at the screen's own top edge) but
        // room to the left, left is next in priority order.
        function test_falls_back_to_the_left_of_the_note_when_theres_no_room_above() {
            verify(noteA.screen !== null, "precondition: the note must be associated with a real screen")

            noteA.noteX = 300
            noteA.noteY = noteA.screen.virtualY

            bar.noteWindow = noteA
            wait(20)

            compare(bar.x, noteA.noteX - bar.width - 8, "with no room above, left is next in priority order")
        }

        // With no room above OR to the left (pinned at the screen's
        // top-left corner), right is next in priority order.
        function test_falls_back_to_the_right_of_the_note_when_theres_no_room_above_or_left() {
            verify(noteA.screen !== null, "precondition: the note must be associated with a real screen")

            noteA.noteX = noteA.screen.virtualX
            noteA.noteY = noteA.screen.virtualY

            bar.noteWindow = noteA
            wait(20)

            compare(bar.x, noteA.noteX + noteA.noteWidth + 8,
                    "with no room above or to the left, right is next in priority order")
        }

        // Layout direction (feature request): left/right placement should
        // switch the bar to a vertical column instead of its default
        // horizontal row.
        function test_lays_out_as_a_column_when_placed_to_the_left_or_right() {
            verify(noteA.screen !== null, "precondition: the note must be associated with a real screen")

            // Top-left corner: no room above or to the left, forcing a
            // fall-back to "right" — see the equivalent position test
            // above for the same setup.
            noteA.noteX = noteA.screen.virtualX
            noteA.noteY = noteA.screen.virtualY

            bar.noteWindow = noteA
            wait(20)

            compare(bar.isVertical, true, "left/right placement should switch the bar to a vertical column")
            // content.columns is bound to -1 ("auto") in vertical mode, so
            // it never reads back a computed value (Qt leaves the property
            // itself at whatever literal was assigned) — rows is the axis
            // actually pinned explicitly, capped high enough to fit every
            // current child without wrapping into a second column; that's
            // what genuinely proves this renders as a single column.
            verify(bar.content.rows >= bar.content.children.length,
                   "the column's row capacity must fit every current child without wrapping")
            compare(bar.width, bar.barThickness)
            compare(bar.height, bar.barLength)
        }

        // Drag handle dot grid (feature request): 4 dots wide, 2 tall when
        // the bar is a vertical column — not the 2-wide/4-tall shape row
        // mode uses (see the row-mode counterpart below), and not the two
        // rows collapsing on top of each other (a real regression once hit
        // live — see gripDotsGrid's own comment in SelectionToolbar.qml).
        // Read directly off each dot's real x/y rather than trusting a
        // positioner's own row/column properties, since that's exactly the
        // class of bug this guards against.
        function test_grip_dot_grid_is_4_wide_2_tall_when_vertical() {
            verify(noteA.screen !== null, "precondition: the note must be associated with a real screen")
            noteA.noteX = noteA.screen.virtualX
            noteA.noteY = noteA.screen.virtualY

            bar.noteWindow = noteA
            wait(20)
            verify(bar.isVertical)

            const xs = new Set()
            const ys = new Set()
            for (let i = 0; i < bar.gripDotsGrid.children.length; ++i) {
                const dot = bar.gripDotsGrid.children[i]
                xs.add(dot.x)
                ys.add(dot.y)
            }

            compare(xs.size, 4, "4 distinct x positions => 4 dots wide")
            compare(ys.size, 2, "2 distinct y positions => 2 dots tall")
        }

        // The row-mode counterpart: 2 wide, 4 tall.
        function test_grip_dot_grid_is_2_wide_4_tall_when_row() {
            noteA.noteX = 300
            noteA.noteY = 300

            bar.noteWindow = noteA
            wait(20)
            verify(!bar.isVertical)

            const xs = new Set()
            const ys = new Set()
            for (let i = 0; i < bar.gripDotsGrid.children.length; ++i) {
                const dot = bar.gripDotsGrid.children[i]
                xs.add(dot.x)
                ys.add(dot.y)
            }

            compare(xs.size, 2, "2 distinct x positions => 2 dots wide")
            compare(ys.size, 4, "4 distinct y positions => 4 dots tall")
        }

        // The other half: above/below placement keeps the bar as a single
        // horizontal row (the default/most common case).
        function test_lays_out_as_a_row_when_placed_above_or_below() {
            noteA.noteX = 300
            noteA.noteY = 300

            bar.noteWindow = noteA
            wait(20)

            compare(bar.isVertical, false, "top/bottom placement should keep the bar as a horizontal row")
            // columns is capped at "maxButtonsBeforeWrap buttons + grip/
            // divider" (see nonButtonChildCount), deliberately higher than
            // the current button count so a future button still wraps
            // instead of silently growing the bar past its fixed
            // barLength — so this only needs to prove no wrap occurred
            // yet, not an exact match.
            verify(bar.content.columns >= bar.content.children.length,
                   "the row's column capacity must fit every current child without wrapping")
            compare(bar.width, bar.barLength)
            compare(bar.height, bar.barThickness)
        }

        // Switching the tracked note (the way Main.qml does when focus
        // moves to a different note) must re-default the position for the
        // newly-tracked note, not leave the bar wherever it was for the
        // previous one.
        function test_switching_the_tracked_note_repositions_for_the_new_note() {
            // Coordinates deliberately kept well within a modest screen
            // (the offscreen QPA test platform's default is 800x600) so
            // this exercises the "recenter over the new note" behavior in
            // isolation, without also tripping the screen-edge clamp
            // covered separately elsewhere.
            noteA.noteX = 50
            noteA.noteY = 300
            noteB.noteX = 400
            noteB.noteY = 300

            bar.noteWindow = noteA
            wait(20)
            const barXForNoteA = bar.x

            bar.noteWindow = noteB
            wait(20)

            verify(bar.x !== barXForNoteA, "the bar should re-center over whichever note is now tracked")
            compare(bar.x, noteB.noteX + noteB.noteWidth / 2 - bar.width / 2)
        }

        // Clicking a formatting button acts on the tracked note's own
        // editor/docHandler — proves the bar is actually wired to whichever
        // note it's tracking, not just visually following it around.
        function test_clicking_bold_toggles_bold_on_the_tracked_notes_selection() {
            noteA.docHandler.setMarkdown("bold me\n")
            wait(50)
            noteA.editor.select(0, "bold me".length)

            bar.noteWindow = noteA
            wait(20)

            mouseClick(bar.boldButton, bar.boldButton.width / 2, bar.boldButton.height / 2)

            verify(noteA.docHandler.toMarkdown().indexOf("**bold me**") !== -1,
                   "clicking Bold on the shared toolbar should format the tracked note's own selection")
        }

        // Feature request: the toolbar previously never blurred at all,
        // regardless of the tracked note's own blur setting — it should
        // inherit that setting like the glass sheen (GlassOverlay) already
        // does.
        function test_blur_is_applied_to_the_bar_when_a_note_with_blur_enabled_is_tracked() {
            settings.blurEnabled = true
            blurSpy.clear()

            bar.noteWindow = noteA
            wait(20)

            let found = false
            for (let i = 0; i < blurSpy.count; ++i) {
                if (blurSpy.signalArguments[i][0] === bar && blurSpy.signalArguments[i][1] === true)
                    found = true
            }
            verify(found, "tracking a note with blur enabled should apply blur to the bar itself, not just the note")
        }

        function test_blur_is_turned_off_again_once_the_tracked_note_is_cleared() {
            bar.noteWindow = noteA
            wait(20)
            blurSpy.clear()

            bar.noteWindow = null

            let found = false
            for (let i = 0; i < blurSpy.count; ++i) {
                if (blurSpy.signalArguments[i][0] === bar && blurSpy.signalArguments[i][1] === false)
                    found = true
            }
            verify(found, "clearing the tracked note should turn the bar's own blur back off")
        }

        // Mirrors tst_NoteWindow.qml's own
        // test_blur_is_reapplied_when_blurEnabled_changes: the bar must
        // react live, not just at the moment tracking starts, since the
        // per-note blur toggle (PerNoteSettingsPopup) is reachable without
        // the note ever losing focus.
        function test_blur_is_reapplied_when_the_tracked_notes_blurEnabled_changes() {
            bar.noteWindow = noteA
            wait(20)
            blurSpy.clear()

            settings.blurEnabled = !settings.blurEnabled

            let found = false
            for (let i = 0; i < blurSpy.count; ++i) {
                if (blurSpy.signalArguments[i][0] === bar)
                    found = true
            }
            verify(found, "the bar should re-apply blur when the tracked note's own blurEnabled changes")

            settings.blurEnabled = !settings.blurEnabled
        }

        // Structural check mirroring NoteWindow's own mouseDragArea test:
        // the drag area must be scoped to the right button only, so the
        // left button still reaches the buttons underneath it.
        function test_drag_area_is_scoped_to_the_right_button_only() {
            compare(bar.dragArea.acceptedButtons, Qt.RightButton)
        }

        // Feature request: the toolbar should show, not just apply,
        // formatting — a button reflects whether its formatting is already
        // active at the tracked note's current selection, not just
        // hover/press feedback.
        function test_bold_button_shows_checked_when_the_selection_is_already_bold() {
            noteA.docHandler.setMarkdown("bold me\n")
            wait(50)
            noteA.editor.select(0, "bold me".length)
            noteA.docHandler.toggleBold(0, "bold me".length)

            bar.noteWindow = noteA
            wait(20)

            compare(bar.boldButton.checked, true,
                    "the Bold button should show checked when the selection is already bold")
        }

        function test_bold_button_is_not_checked_for_plain_unformatted_text() {
            noteA.docHandler.setMarkdown("plain text\n")
            wait(50)
            noteA.editor.select(0, "plain text".length)

            bar.noteWindow = noteA
            wait(20)

            compare(bar.boldButton.checked, false,
                    "the Bold button should not show checked for plain, unformatted text")
        }

        // Clicking Bold must flip the button's own checked state immediately
        // — not just the document's formatting — even though a held
        // selection's start/end never move across the click
        // (persistentSelection keeps them exactly where they were; see
        // runAction()'s own comment).
        function test_bold_button_becomes_checked_immediately_after_clicking_it() {
            noteA.docHandler.setMarkdown("bold me\n")
            wait(50)
            noteA.editor.select(0, "bold me".length)

            bar.noteWindow = noteA
            wait(20)
            compare(bar.boldButton.checked, false, "precondition: not bold yet")

            mouseClick(bar.boldButton, bar.boldButton.width / 2, bar.boldButton.height / 2)

            compare(bar.boldButton.checked, true,
                    "the Bold button should flip to checked right after toggling bold on the held selection")
        }

        // Each formatting button tracks its own, independent state — proves
        // the whole set is wired up, not just Bold.
        function test_italic_and_strikethrough_buttons_track_their_own_independent_state() {
            noteA.docHandler.setMarkdown("emphasis\n")
            wait(50)
            noteA.editor.select(0, "emphasis".length)
            noteA.docHandler.toggleItalic(0, "emphasis".length)

            bar.noteWindow = noteA
            wait(20)

            compare(bar.italicButton.checked, true, "italic is active, so the Italic button should be checked")
            compare(bar.strikethroughButton.checked, false,
                    "strikethrough was never applied, so its button should not be checked")
        }
    }
}
