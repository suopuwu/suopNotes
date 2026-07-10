import QtQml
import QtQuick
import QtQuick.Controls
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
            noteA.currentInTable = false
            noteB.currentInTable = false
            // Resets hover state left over from any mouseMove() a test made
            // (e.g. onto insertTableButton) — otherwise the pointer's last
            // synthetic position leaks into the next test, which could
            // start already-hovering something it never touched itself.
            // `bar` (and columnOptionsPopup) are their own top-level
            // Windows, separate from this file's implicit TestCase window —
            // a reset aimed at `root` wouldn't touch either of them, so each
            // gets its own explicit reset instead.
            mouseMove(bar.contentItem, -50, -50)
            mouseMove(bar.columnOptionsPopup.contentItem, -50, -50)
            wait(20)
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

        // Tooltip on hover (feature request): every toolbar button gets a
        // tooltip identifying it, shown after a short hover delay rather
        // than instantly (matching the platform-standard feel of a tooltip,
        // not a label that pops the instant the pointer passes over). Each
        // button's own ToolbarButtonTip instance just holds its text and
        // hover-delay bookkeeping — the actual rendering happens in the
        // single shared bar.tooltipWindow (see both their own comments).
        function test_toolbar_buttons_have_hover_tooltips() {
            compare(bar.boldButtonTip.text, "Bold")
            compare(bar.italicButtonTip.text, "Italic")
            compare(bar.strikethroughButtonTip.text, "Strikethrough")
        }

        // The tooltip must never overlap its own button — that was the
        // actual root cause of an infinite show/hide loop this design used
        // to have (a tooltip sitting on top of the button intercepts the
        // next hit-test, hover flips off, tooltip hides, hover flips back
        // on, forever). Now structurally impossible to reintroduce: the
        // tooltip is a wholly separate top-level window from the button's
        // own, always positioned below it.
        function test_toolbar_button_tooltip_is_offset_below_the_button_not_overlapping_it() {
            bar.noteWindow = noteA
            wait(20)

            mouseMove(bar.boldButton, bar.boldButton.width / 2, bar.boldButton.height / 2)
            tryCompare(bar.tooltipWindow, "activeButton", bar.boldButton, 2000)
            tryCompare(bar.tooltipWindow, "contentVisible", true)

            const buttonBottom = bar.boldButton.mapToGlobal(0, bar.boldButton.height).y
            verify(bar.tooltipWindow.y >= buttonBottom,
                   "the tooltip should sit below the button, not overlap it")

            mouseMove(bar.boldButton, -50, -50)
            tryCompare(bar.tooltipWindow, "contentVisible", false)
        }

        // Bugfix: an earlier version rendered the tooltip inside bar's own
        // window, which meant bar's window had to reserve extra invisible
        // height below the visible bar just so the tooltip wouldn't be
        // clipped by that window's own bounds — but KWindowEffects blurs
        // whatever region a window's own width/height cover with no way to
        // carve out a smaller sub-rect, so that reserve stayed blurred
        // (and, since it was real window surface, kept intercepting right-
        // click-drags and swallowing clicks meant for whatever was
        // underneath) permanently, whether or not a tooltip was even
        // showing. A separate window sized to just its own text has
        // nothing left over to leak that way — its size should reflect the
        // tooltip's own short text, not the whole toolbar.
        function test_toolbar_button_tooltip_is_a_separate_window_sized_to_its_own_text() {
            bar.noteWindow = noteA
            wait(20)

            mouseMove(bar.strikethroughButton, bar.strikethroughButton.width / 2, bar.strikethroughButton.height / 2)
            tryCompare(bar.tooltipWindow, "contentVisible", true, 2000)

            compare(bar.tooltipWindow.text, "Strikethrough")
            verify(bar.tooltipWindow.width < bar.barLength,
                   "a single word's tooltip must not be sized to the whole toolbar")

            mouseMove(bar.strikethroughButton, -50, -50)
            tryCompare(bar.tooltipWindow, "contentVisible", false)
        }

        // Real hover, real delay, real (non-)oscillation — the actual
        // end-to-end behavior the fix above exists for, not just the
        // structural checks above it.
        function test_toolbar_button_tooltip_shows_after_hovering_and_stays_shown() {
            bar.noteWindow = noteA
            wait(20)

            mouseMove(bar.boldButton, bar.boldButton.width / 2, bar.boldButton.height / 2)
            tryCompare(bar.tooltipWindow, "contentVisible", true, 2000)
            compare(bar.tooltipWindow.text, "Bold")

            // If the tooltip overlapped the button, it would flip
            // invisible again shortly after showing (see this test file's
            // own comment above) — holding it visible across another
            // wait proves that isn't happening.
            wait(300)
            compare(bar.tooltipWindow.contentVisible, true,
                    "the tooltip must stay visible while genuinely still hovering, not oscillate")

            mouseMove(bar.boldButton, -50, -50)
            tryCompare(bar.tooltipWindow, "contentVisible", false)
        }

        // Column options popup (feature request): the row/column-editing
        // buttons no longer sit permanently in the main toolbar row — they
        // only appear, in their own popup anchored to the grid icon, once
        // the cursor is actually inside a table AND the pointer is hovering
        // that icon (or the popup itself).
        function test_column_options_popup_hidden_outside_a_table() {
            bar.noteWindow = noteA
            noteA.currentInTable = false
            wait(20)

            compare(bar.columnOptionsPopup.wantsVisible, false)
            compare(bar.columnOptionsPopup.background.visible, false)
        }

        // Feature request: shown automatically the instant the cursor is
        // inside a table — no hovering the grid icon required, since the
        // main bar itself no longer relocates to the table (see
        // SelectionToolbar.qml's reposition()), so this popup is what
        // actually shows a table-local toolbar now.
        function test_column_options_popup_shown_automatically_when_entering_a_table() {
            bar.noteWindow = noteA
            noteA.currentInTable = false
            wait(20)

            compare(bar.columnOptionsPopup.wantsVisible, false, "precondition: not shown outside a table")

            noteA.currentInTable = true
            wait(20)

            compare(bar.columnOptionsPopup.wantsVisible, true)
            compare(bar.columnOptionsPopup.background.visible, true)
            compare(bar.columnOptionsPopup.content.enabled, true)

            noteA.currentInTable = false
            wait(20)
            compare(bar.columnOptionsPopup.wantsVisible, false,
                    "leaving the table again should hide it")
        }

        // Feature request: the popup anchors to the table's own position
        // (not the main bar's grid icon, which can be anywhere — the bar
        // itself doesn't relocate to the table anymore).
        function test_column_options_popup_is_anchored_below_the_table() {
            bar.noteWindow = noteA
            noteA.docHandler.setMarkdown("")
            wait(50)
            noteA.docHandler.insertTable(0)
            wait(50)
            // Position 1 lands inside "Header 1"'s own text, solidly
            // inside the table's first cell — position 0 sits right at
            // the table frame's own boundary, which currentTable() does
            // not resolve as "inside" the table (same convention
            // tst_NoteWindow.qml's own table-editing tests use).
            noteA.editor.cursorPosition = 1
            noteA.currentInTable = true
            wait(20)

            const rect = noteA.docHandler.tableRect(noteA.editor.selectionEnd)
            verify(rect.width > 0 && rect.height > 0, "precondition: the table has a real, non-empty rect")
            const topLeft = noteA.editor.mapToGlobal(rect.x, rect.y)

            // Within a pixel, not exactly equal: the popup's own window
            // position snaps to whole pixels, while the table's real
            // layout-derived rect can be fractional.
            verify(Math.abs(bar.columnOptionsPopup.x - topLeft.x) < 1,
                   "expected popup.x (" + bar.columnOptionsPopup.x + ") to be within 1px of the table's own x (" + topLeft.x + ")")
            verify(bar.columnOptionsPopup.y >= topLeft.y + rect.height,
                   "the popup should sit below the table by default")

            noteA.currentInTable = false
            noteA.docHandler.setMarkdown("")
            wait(50)
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

        // Main.qml's toolbar-persistence fix (feature request: the toolbar
        // used to disappear after a while even though the note was still
        // focused) relies on this `hovered` alias as a mouse-presence signal
        // independent of window activation — verify it actually tracks the
        // pointer.
        function test_hovered_tracks_whether_the_pointer_is_over_the_toolbar() {
            bar.noteWindow = noteA
            wait(20)

            mouseMove(bar.contentItem, bar.width / 2, bar.height / 2)
            tryCompare(bar, "hovered", true)

            mouseMove(bar.contentItem, -50, -50)
            tryCompare(bar, "hovered", false)
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
            compare(bar.y, noteA.noteY - bar.barThickness - 8)
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

        // Feature request: the table-options popup should match the main
        // bar's own styling and behavior — blur, a right-click drag area,
        // and a grip handle — not just its glass sheen (already covered
        // by test_column_options_popup_is_anchored_below_the_table's own
        // GlassOverlay). Blur only applies once the popup actually wants
        // to be visible (i.e. the cursor is inside a table) — see this
        // popup's own updateBlur() comment for the bug that guards
        // against: this window stays permanently mapped even outside a
        // table, so blurring it unconditionally left a real, blurred
        // window idling near the main bar whether or not any table was
        // ever involved.
        function test_blur_is_applied_to_the_column_options_popup_only_while_it_wants_to_be_visible() {
            settings.blurEnabled = true
            bar.noteWindow = noteA
            wait(20)
            blurSpy.clear()

            noteA.currentInTable = true
            wait(20)

            let foundOn = false
            for (let i = 0; i < blurSpy.count; ++i) {
                if (blurSpy.signalArguments[i][0] === bar.columnOptionsPopup && blurSpy.signalArguments[i][1] === true)
                    foundOn = true
            }
            verify(foundOn, "entering a table with blur enabled should apply blur to the table-options popup")

            blurSpy.clear()
            noteA.currentInTable = false
            wait(20)

            let foundOff = false
            for (let i = 0; i < blurSpy.count; ++i) {
                if (blurSpy.signalArguments[i][0] === bar.columnOptionsPopup && blurSpy.signalArguments[i][1] === false)
                    foundOff = true
            }
            verify(foundOff, "leaving the table again should turn the table-options popup's blur back off")
        }

        // Regression test for the same bug: outside a table, this
        // permanently-mapped popup must never be blurred at all, even the
        // instant a note is first tracked (before any table is involved).
        function test_blur_is_not_applied_to_the_column_options_popup_outside_a_table() {
            settings.blurEnabled = true
            blurSpy.clear()

            bar.noteWindow = noteA
            wait(20)

            let foundOn = false
            for (let i = 0; i < blurSpy.count; ++i) {
                if (blurSpy.signalArguments[i][0] === bar.columnOptionsPopup && blurSpy.signalArguments[i][1] === true)
                    foundOn = true
            }
            verify(!foundOn, "tracking a note without being in a table must not blur the table-options popup")
        }

        // Regression test mirroring the blur fix above: the drag area must
        // also be disabled outside a table, so a stray right-click near
        // wherever this permanently-mapped popup last sat doesn't drag it.
        function test_column_options_popup_drag_area_disabled_outside_a_table() {
            bar.noteWindow = noteA
            noteA.currentInTable = false
            wait(20)

            compare(bar.columnOptionsPopup.dragArea.enabled, false)

            noteA.currentInTable = true
            wait(20)
            compare(bar.columnOptionsPopup.dragArea.enabled, true)

            noteA.currentInTable = false
            wait(20)
            compare(bar.columnOptionsPopup.dragArea.enabled, false)
        }

        function test_column_options_popup_drag_area_is_scoped_to_the_right_button_only() {
            compare(bar.columnOptionsPopup.dragArea.acceptedButtons, Qt.RightButton)
        }

        function test_column_options_popup_has_its_own_grip_handle() {
            verify(bar.columnOptionsPopup.gripHandle !== null)
            compare(bar.columnOptionsPopup.gripHandle.implicitWidth, 9)
            compare(bar.columnOptionsPopup.gripHandle.implicitHeight, 21)
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

        // Feature request: cut/copy/paste toolbar buttons replace the
        // removed right-click context menu (see NoteWindow.qml's
        // mouseDragArea, which used to open one on a plain right-click).
        function test_cut_button_disabled_without_a_selection_enabled_with_one() {
            noteA.docHandler.setMarkdown("hello world\n")
            wait(50)
            noteA.editor.cursorPosition = 0
            noteA.editor.deselect()

            bar.noteWindow = noteA
            wait(20)

            compare(bar.cutButton.enabled, false, "Cut should be disabled with no selection")

            noteA.editor.select(0, 5)
            wait(20)

            compare(bar.cutButton.enabled, true, "Cut should be enabled once text is selected")
        }

        function test_clicking_cut_removes_the_selected_text_and_copies_it_to_the_clipboard() {
            noteA.docHandler.setMarkdown("hello world\n")
            wait(50)
            noteA.editor.select(0, 5)

            bar.noteWindow = noteA
            wait(20)

            mouseClick(bar.cutButton, bar.cutButton.width / 2, bar.cutButton.height / 2)

            verify(noteA.editor.text.startsWith(" world"),
                   "expected \"hello\" to be cut, leaving \" world\": " + noteA.editor.text)

            noteA.editor.cursorPosition = noteA.editor.length
            mouseClick(bar.pasteButton, bar.pasteButton.width / 2, bar.pasteButton.height / 2)

            verify(noteA.editor.text.indexOf("hello") !== -1,
                   "cut should have copied the removed text to the clipboard, pasteable afterward")
        }

        function test_clicking_copy_then_paste_round_trips_the_selection() {
            noteA.docHandler.setMarkdown("copy me\n")
            wait(50)
            noteA.editor.select(0, "copy me".length)

            bar.noteWindow = noteA
            wait(20)

            mouseClick(bar.copyButton, bar.copyButton.width / 2, bar.copyButton.height / 2)

            noteA.editor.cursorPosition = noteA.editor.length
            mouseClick(bar.pasteButton, bar.pasteButton.width / 2, bar.pasteButton.height / 2)

            // Not asserting on exact surrounding text: this app's editor
            // round-trips its content through markdown serialization (see
            // MarkdownSerialization.cpp), which can normalize whitespace
            // unrelated to copy/paste itself — counting occurrences of the
            // copied text is what actually proves paste worked.
            const occurrences = noteA.editor.text.split("copy me").length - 1
            compare(occurrences, 2,
                    "pasting after copy should reinsert the exact text that was selected, alongside the original")
        }
    }
}
