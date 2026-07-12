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
            noteA.currentMisspelledWord = ""
            noteA.currentSpellingSuggestions = []
            noteA.misspelledWordStart = -1
            noteA.misspelledWordEnd = -1
            noteB.currentMisspelledWord = ""
            noteB.currentSpellingSuggestions = []
            noteB.misspelledWordStart = -1
            noteB.misspelledWordEnd = -1
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

        // Bug found live: tooltipWindow is a real top-level Window kept
        // permanently mapped (never hidden again once shown — see its own
        // Component.onCompleted comment), so only contentVisible used to
        // change once a hover ended. Its own width/height stayed bound to
        // the last tooltip text's size regardless, and its x/y stayed at
        // the last-hovered button's position — the still-mapped,
        // still-on-top tooltip kept sitting there, silently eating clicks
        // meant for whatever was underneath even after visibly
        // disappearing (same root cause as bar's/columnOptionsPopup's/
        // spellingSuggestionsPopup's own, covered by their own tests).
        // Verifies hideTip() moves the window off every screen entirely —
        // NOT that it shrinks (a later bug found live: shrinking to 1x1 on
        // every hide, then back to real size on every show, forced a real
        // Wayland resize negotiation each cycle, and two close together
        // could race and leave the window stuck at the wrong size — see
        // width/height's own comment; off-screen position alone is what
        // actually stops it from blocking clicks, regardless of size).
        function test_tooltip_window_moves_offscreen_once_hover_ends() {
            bar.noteWindow = noteA
            wait(20)

            mouseMove(bar.boldButton, bar.boldButton.width / 2, bar.boldButton.height / 2)
            tryCompare(bar.tooltipWindow, "contentVisible", true, 2000)

            verify(bar.tooltipWindow.width > 1, "precondition: the tooltip has real size while shown")
            verify(bar.tooltipWindow.height > 1, "precondition: the tooltip has real size while shown")

            const screen = noteA.screen
            verify(screen, "precondition: the tracked note reports a real screen")

            mouseMove(bar.boldButton, -50, -50)
            tryCompare(bar.tooltipWindow, "contentVisible", false)

            verify(bar.tooltipWindow.x > screen.virtualX + screen.width || bar.tooltipWindow.y > screen.virtualY + screen.height,
                   "hover ending should move the still-mapped tooltip window off every screen so it stops blocking clicks")
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

        // Bug found live: columnOptionsPopup is a real top-level Window kept
        // permanently mapped (never hidden again once shown, to avoid
        // disrupting Wayland keyboard focus on the note — see its own
        // Component.onCompleted comment), so only content.visible/enabled
        // used to change when leaving a table. Its x/y stayed at its last
        // anchored position — the still-mapped, still-on-top window kept
        // sitting there, silently eating clicks meant for the note
        // underneath even after the popup had visibly "closed". Verifies
        // the window itself now moves off every screen — NOT that it
        // shrinks (a later bug found live: shrinking to 1x1 on every hide,
        // then back to real size on every show, forced a real Wayland
        // resize negotiation each cycle, and two close together could race
        // and leave the window stuck at the wrong size — see width/
        // height's own comment; off-screen position alone is what
        // actually stops it from blocking clicks, regardless of size).
        function test_column_options_popup_moves_offscreen_when_leaving_a_table() {
            bar.noteWindow = noteA
            noteA.currentInTable = true
            wait(20)

            verify(bar.columnOptionsPopup.width > 1, "precondition: the popup has real size while in a table")
            verify(bar.columnOptionsPopup.height > 1, "precondition: the popup has real size while in a table")

            const screen = noteA.screen
            verify(screen, "precondition: the tracked note reports a real screen")

            noteA.currentInTable = false
            wait(20)

            verify(bar.columnOptionsPopup.x > screen.virtualX + screen.width || bar.columnOptionsPopup.y > screen.virtualY + screen.height,
                   "leaving the table should move the still-mapped window off every screen so it stops blocking clicks")
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

        // Bug found live: bar itself is a real top-level Window kept
        // permanently mapped (never hidden again once shown, to avoid
        // disrupting Wayland keyboard focus on the note — see its own
        // Component.onCompleted comment), so only content/background
        // visibility used to change once the tracked note cleared — the
        // still-mapped, still-on-top toolbar kept sitting at its last
        // position, silently eating clicks meant for the note underneath
        // (or whatever was there) even after visibly "disappearing".
        // Verifies the window now moves off every screen (parkOffscreen())
        // — NOT that it shrinks (a later bug found live: shrinking to 1x1
        // on every hide, then back to real size on every show, forced a
        // real Wayland resize negotiation each cycle, and two close
        // together could race and leave the window stuck at the wrong
        // size — see width/height's own comment; off-screen position
        // alone is what actually stops it from blocking clicks, regardless
        // of size).
        function test_bar_moves_offscreen_once_the_tracked_note_is_cleared() {
            bar.noteWindow = noteA
            wait(20)

            verify(bar.width > 1, "precondition: the bar has real size while tracking a note")
            verify(bar.height > 1, "precondition: the bar has real size while tracking a note")

            const screen = noteA.screen
            verify(screen, "precondition: the tracked note reports a real screen")

            bar.noteWindow = null
            wait(20)

            verify(bar.x > screen.virtualX + screen.width || bar.y > screen.virtualY + screen.height,
                   "clearing the tracked note should move the still-mapped bar off every screen so it stops blocking clicks")
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

        // Bug found live ("becomes visible before the position changes,
        // leading to bad UX"): the popup used to reveal its content the
        // INSTANT wantsVisible went true, before positionManager.
        // moveWindow()'s asynchronous KWin script call (~60-100ms observed
        // live) had any chance to actually land the window at its real,
        // computed position — so it visibly popped in wherever it happened
        // to already be sitting (its off-screen park spot), then jumped.
        // Content reveal now waits for actual CONFIRMATION via
        // positionManager's own windowMoved signal (see
        // spellingSuggestionsRevealIfReady's own comment) rather than a
        // fixed delay — verified here with tryCompare's polling wait, not a
        // fixed wait(), since the real completion time is genuinely
        // variable (a live KWin round-trip), not a constant.
        function test_spelling_suggestions_popup_content_stays_hidden_until_the_move_is_confirmed() {
            noteA.currentMisspelledWord = "teh"
            noteA.currentSpellingSuggestions = ["the", "ten"]
            noteA.misspelledWordStart = 2
            noteA.misspelledWordEnd = 5

            bar.noteWindow = noteA
            wait(20)

            compare(bar.spellingSuggestionsPopup.wantsVisible, true,
                    "precondition: the popup should want to be visible")
            compare(bar.spellingSuggestionsPopup.background.visible, false,
                    "content must stay hidden immediately after wantsVisible goes true, before the move is confirmed")

            tryCompare(bar.spellingSuggestionsPopup.background, "visible", true, 2000)

            noteA.currentMisspelledWord = ""
        }

        // Spelling-suggestion popup (feature request): tapping a misspelled
        // word (NoteWindow's own TapHandler, not exercised here — see below)
        // populates currentMisspelledWord/currentSpellingSuggestions/
        // misspelledWordStart/misspelledWordEnd on the tracked note, which
        // this popup reads to show a short list of replacement buttons.
        //
        // No real dictionary is installed on this dev machine, so a real
        // misspelling can't actually get flagged here — this test instead
        // sets the tracked note's fixture state directly, exactly the way
        // the real TapHandler would have, and verifies only THIS popup's own
        // wiring (does clicking a suggestion call replaceRange with the
        // right args and close the popup) — not tap-detection itself, which
        // is NoteWindow's own concern to test.
        function test_clicking_a_spelling_suggestion_replaces_the_misspelled_word() {
            noteA.docHandler.setMarkdown("I teh best\n")
            wait(50)

            // "teh" sits at offset 2..5 in "I teh best" (I=0, space=1,
            // t=2, e=3, h=4, space=5).
            noteA.currentMisspelledWord = "teh"
            noteA.currentSpellingSuggestions = ["the", "ten"]
            noteA.misspelledWordStart = 2
            noteA.misspelledWordEnd = 5

            bar.noteWindow = noteA

            compare(bar.spellingSuggestionsPopup.wantsVisible, true,
                    "the popup should want to be visible once currentMisspelledWord is non-empty")
            // Content reveal waits for positionManager to confirm the move
            // actually landed (see spellingSuggestionsRevealIfReady's own
            // comment) — polled here, not a fixed wait(), since real
            // completion time is genuinely variable.
            tryCompare(bar.spellingSuggestionsPopup.background, "visible", true, 2000)

            const firstSuggestion = bar.spellingSuggestionsPopup.repeater.itemAt(0)
            verify(firstSuggestion !== null, "precondition: the first suggestion button must exist")
            compare(firstSuggestion.text, "the")

            mouseClick(firstSuggestion, firstSuggestion.width / 2, firstSuggestion.height / 2)

            verify(noteA.docHandler.toMarkdown().indexOf("teh") === -1,
                   "clicking the suggestion should remove the misspelled word")
            verify(noteA.docHandler.toMarkdown().indexOf("the") !== -1,
                   "clicking the suggestion should insert the replacement word")
            compare(noteA.currentMisspelledWord, "",
                    "the popup should close itself immediately after a replacement is applied")
        }

        // Feature request: suggestion buttons of very different text
        // lengths used to each be individually centered in the popup's
        // ColumnLayout, so their left edges landed at different x
        // positions and the list read as jagged/staggered. They should
        // instead all start flush against the same left edge.
        function test_spelling_suggestion_buttons_are_left_aligned_rather_than_centered() {
            noteA.docHandler.setMarkdown("I teh best\n")
            wait(50)

            noteA.currentMisspelledWord = "teh"
            noteA.currentSpellingSuggestions = ["a", "a much longer suggestion"]
            noteA.misspelledWordStart = 2
            noteA.misspelledWordEnd = 5

            bar.noteWindow = noteA
            tryCompare(bar.spellingSuggestionsPopup.background, "visible", true, 2000)
            compare(bar.spellingSuggestionsPopup.repeater.count, 2, "precondition: both suggestions rendered")

            const shortButton = bar.spellingSuggestionsPopup.repeater.itemAt(0)
            const longButton = bar.spellingSuggestionsPopup.repeater.itemAt(1)
            verify(shortButton !== null && longButton !== null, "precondition: both suggestion buttons must exist")
            verify(shortButton.width < longButton.width,
                   "precondition: the two suggestions must actually differ in rendered width")
            compare(shortButton.x, longButton.x,
                    "buttons of different widths should share the same left edge, not be individually centered")

            noteA.currentMisspelledWord = ""
            noteA.docHandler.setMarkdown("")
            wait(50)
        }

        // Bug found live: spellingSuggestionsPopup is a real top-level
        // Window kept permanently mapped (never hidden again once shown,
        // same "shown once, content-visibility toggled from then on"
        // discipline as columnOptionsPopup's own — see its
        // Component.onCompleted comment), so only content/background
        // visibility used to change when currentMisspelledWord cleared —
        // the still-mapped, still-on-top window kept sitting at its last
        // anchored position, silently eating clicks meant for the note
        // underneath even after the popup had visibly "closed" (this is
        // the reported bug: the note behind the popup stayed unresponsive
        // there after the popup disappeared). Verifies the window itself
        // now moves off every screen — NOT that it shrinks (a later bug
        // found live, confirmed via trace logging: shrinking to 1x1 on
        // every hide, then back to real size on every show, forced a real
        // Wayland resize negotiation each cycle, and two close together
        // — e.g. hiding then immediately showing again for the next word —
        // could race, leaving the window stuck at a stale size with no
        // corresponding change to spellingSuggestionsBackground's own
        // width; off-screen position alone is what actually stops it from
        // blocking clicks, regardless of size).
        function test_spelling_suggestions_popup_moves_offscreen_when_word_is_cleared() {
            noteA.currentMisspelledWord = "teh"
            noteA.currentSpellingSuggestions = ["the", "ten"]
            noteA.misspelledWordStart = 2
            noteA.misspelledWordEnd = 5

            bar.noteWindow = noteA
            wait(20)

            verify(bar.spellingSuggestionsPopup.width > 1, "precondition: the popup has real size while a word is flagged")
            verify(bar.spellingSuggestionsPopup.height > 1, "precondition: the popup has real size while a word is flagged")

            const screen = noteA.screen
            verify(screen, "precondition: the tracked note reports a real screen")

            noteA.currentMisspelledWord = ""
            wait(20)

            verify(bar.spellingSuggestionsPopup.x > screen.virtualX + screen.width || bar.spellingSuggestionsPopup.y > screen.virtualY + screen.height,
                   "clearing the misspelled word should move the still-mapped window off every screen so it stops blocking clicks")
        }

        // Bug found live ("doesn't transition position between two
        // misspelled words" / "stuck at the prior word until the user
        // clicks out"): wantsVisible only depends on currentMisspelledWord
        // being empty vs non-empty, so tapping misspelled word A then
        // DIRECTLY misspelled word B — both non-empty — never toggles it,
        // and onWantsVisibleChanged (the only thing that used to trigger a
        // reposition) never re-fired. Mirrors NoteWindow.qml's own
        // TapHandler property-assignment order (suggestions/start/end,
        // THEN currentMisspelledWord last) to exercise the same
        // misspelledWordStart-changes-first sequence that actually
        // happens live.
        function test_spelling_suggestions_popup_repositions_when_jumping_directly_between_two_misspelled_words() {
            noteA.docHandler.setMarkdown("teh gud\n")
            wait(50)

            noteA.currentSpellingSuggestions = ["the", "ten"]
            noteA.misspelledWordStart = 0
            noteA.misspelledWordEnd = 3
            noteA.currentMisspelledWord = "teh"

            bar.noteWindow = noteA
            tryCompare(bar.spellingSuggestionsPopup.background, "visible", true, 2000)

            const firstX = bar.spellingSuggestionsPopup.x
            const firstY = bar.spellingSuggestionsPopup.y

            // Jumps directly from "teh" to "gud" — currentMisspelledWord
            // never passes through "" in between, same as a real tap on a
            // second misspelled word right after the first.
            noteA.currentSpellingSuggestions = ["good", "gum"]
            noteA.misspelledWordStart = 4
            noteA.misspelledWordEnd = 7
            noteA.currentMisspelledWord = "gud"

            const secondWordRect = noteA.editor.positionToRectangle(4)
            const secondWordTopLeft = noteA.editor.mapToGlobal(secondWordRect.x, secondWordRect.y)

            // Polled, not a single tryCompare: the popup's own window
            // position snaps to whole pixels while the editor's own
            // layout-derived rect can be fractional (same reasoning
            // test_column_options_popup_is_anchored_below_the_table's own
            // comment gives), so an exact-value tryCompare would be
            // fragile — this waits for the real, async moveWindow
            // confirmation to land within a couple of pixels instead.
            let settled = false
            for (let i = 0; i < 20 && !settled; i++) {
                wait(100)
                settled = Math.abs(bar.spellingSuggestionsPopup.x - secondWordTopLeft.x) < 2
            }
            verify(settled,
                   "the popup should reposition to the second word's own anchor (" + secondWordTopLeft.x +
                   "), not stay stuck at the first word's (" + firstX + "); ended at " + bar.spellingSuggestionsPopup.x)
            verify(Math.abs(bar.spellingSuggestionsPopup.x - firstX) > 1 || Math.abs(bar.spellingSuggestionsPopup.y - firstY) > 1,
                   "the popup must actually move for the second word, not stay stuck at the first word's position")

            noteA.currentMisspelledWord = ""
            noteA.docHandler.setMarkdown("")
            wait(50)
        }

        // Bug found live ("sometimes the popup will be cut off — only 2
        // options show up... sometimes cut off but the space is reserved
        // [you can see the blur where the third option would be]...
        // sometimes it works normally"): revealing the popup used to depend
        // ONLY on positionManager confirming the move — nothing gated it on
        // spellingSuggestionsContent's own size having actually finished
        // settling (see spellingSuggestionsResettleDebounce's own comment
        // on the Repeater-rebuild wobble every suggestion-list change goes
        // through). A fast jump from a word with FEWER suggestions to one
        // with MORE — mirroring a real click on a second misspelled word
        // shortly after the first — is exactly the case where position can
        // land before the longer list has finished laying out. Checks the
        // FINAL revealed state has every one of the second word's
        // suggestions, not a partial count caught mid-wobble.
        function test_spelling_suggestions_popup_reveals_with_the_full_suggestion_list_after_a_fast_word_to_word_jump() {
            noteA.docHandler.setMarkdown("teh gud\n")
            wait(50)

            noteA.currentSpellingSuggestions = ["a"]
            noteA.misspelledWordStart = 0
            noteA.misspelledWordEnd = 3
            noteA.currentMisspelledWord = "teh"

            bar.noteWindow = noteA
            tryCompare(bar.spellingSuggestionsPopup.background, "visible", true, 2000)
            compare(bar.spellingSuggestionsPopup.repeater.count, 1, "precondition: the first word's own single suggestion")

            // Jumps directly to a word with a LONGER suggestion list —
            // currentMisspelledWord never passes through "" in between,
            // same as a real tap on a second misspelled word right after
            // the first.
            noteA.currentSpellingSuggestions = ["good", "gum", "gud2", "gudly"]
            noteA.misspelledWordStart = 4
            noteA.misspelledWordEnd = 7
            noteA.currentMisspelledWord = "gud"

            tryCompare(bar.spellingSuggestionsPopup.background, "visible", true, 2000)
            compare(bar.spellingSuggestionsPopup.repeater.count, 4,
                    "the popup should reveal with the second word's FULL suggestion count, not whatever partial " +
                    "count the content happened to be at mid-wobble when position confirmed")

            noteA.currentMisspelledWord = ""
            noteA.docHandler.setMarkdown("")
            wait(50)
        }

        // Bug found live ("switching between misspelled words that are on
        // different notes causes the position to not update to the new
        // note, though the suggestions do... inconsistent, rarely works but
        // mostly doesn't"): spellingSuggestionsPopup.wantsVisible depends
        // on bar.noteWindow.currentMisspelledWord, so it only reacts to a
        // false->true (or true->false) EDGE. If the newly-focused note's
        // own currentMisspelledWord is already non-empty by the time
        // bar.noteWindow retargets to it — a real race between window
        // activation and tap-handling order, reproduced here by flagging
        // noteB's word BEFORE switching bar.noteWindow to it — wantsVisible
        // reads true both immediately before and after the retarget (noteA's
        // word, then noteB's), so it never toggles and
        // onWantsVisibleChanged never re-fires. bar.onNoteWindowChanged now
        // forces a reposition unconditionally whenever the newly-tracked
        // note already has a word flagged, independent of that edge.
        function test_spelling_suggestions_popup_repositions_when_the_tracked_note_itself_switches_while_a_word_is_already_flagged() {
            noteA.docHandler.setMarkdown("teh\n")
            wait(50)
            noteA.currentSpellingSuggestions = ["the", "ten"]
            noteA.misspelledWordStart = 0
            noteA.misspelledWordEnd = 3
            noteA.currentMisspelledWord = "teh"

            bar.noteWindow = noteA
            tryCompare(bar.spellingSuggestionsPopup.background, "visible", true, 2000)
            const firstX = bar.spellingSuggestionsPopup.x
            const firstY = bar.spellingSuggestionsPopup.y

            noteB.docHandler.setMarkdown("gud\n")
            wait(50)
            // Simulates the race: noteB's own word is already flagged (as
            // its own TapHandler would do) BEFORE bar.noteWindow ever
            // retargets to it — the exact ordering that used to leave
            // wantsVisible reading true throughout, with no edge to react to.
            noteB.currentSpellingSuggestions = ["good", "gum"]
            noteB.misspelledWordStart = 0
            noteB.misspelledWordEnd = 3
            noteB.currentMisspelledWord = "gud"

            bar.noteWindow = noteB

            // Polled, not a single tryCompare — see the jumping-between-
            // words test's own comment above for why (fractional editor
            // rect vs. whole-pixel window position). The target itself is
            // recomputed on every poll (not captured once up front): this
            // is noteB's very first real use of editor.mapToGlobal() in
            // this file, so its own real window geometry may still be
            // settling independently of anything under test here — polling
            // a freshly-recomputed target keeps the check anchored to
            // noteB's actual, current geometry either way. Mirrors
            // repositionSpellingSuggestionsPopup()'s own X-axis screen
            // clamp — noteB sits near the test screen's right edge, close
            // enough that the popup's real target legitimately gets
            // clamped to stay fully on-screen, same as it would live.
            let settled = false
            let lastTargetX = 0
            for (let i = 0; i < 20 && !settled; i++) {
                wait(100)
                const rect = noteB.editor.positionToRectangle(0)
                lastTargetX = noteB.editor.mapToGlobal(rect.x, rect.y).x
                const screen = noteB.screen
                if (screen) {
                    lastTargetX = Math.max(screen.virtualX,
                        Math.min(lastTargetX, screen.virtualX + screen.width - bar.spellingSuggestionsPopup.width))
                }
                settled = Math.abs(bar.spellingSuggestionsPopup.x - lastTargetX) < 2
            }
            verify(settled,
                   "switching the tracked note to noteB (already flagging its own word) should reposition the popup to " +
                   "noteB's word (" + lastTargetX + "), not leave it stuck at noteA's (" + firstX +
                   "); ended at " + bar.spellingSuggestionsPopup.x)
            verify(Math.abs(bar.spellingSuggestionsPopup.x - firstX) > 1 || Math.abs(bar.spellingSuggestionsPopup.y - firstY) > 1,
                   "the popup must actually move to noteB's word, not stay stuck at noteA's position")

            noteB.currentMisspelledWord = ""
            noteB.docHandler.setMarkdown("")
            noteA.currentMisspelledWord = ""
            noteA.docHandler.setMarkdown("")
            wait(50)
        }

        // Bug found live: a misspelled word can have zero suggestions (e.g.
        // too garbled for the dictionary to find any close match) — the
        // popup used to just show an empty Repeater in that case, reading as
        // "nothing happened" even though it was genuinely open.
        function test_popup_shows_a_no_suggestions_label_when_there_are_none() {
            noteA.currentMisspelledWord = "zzznotarealwordzzz"
            noteA.currentSpellingSuggestions = []
            noteA.misspelledWordStart = 0
            noteA.misspelledWordEnd = 18

            bar.noteWindow = noteA

            compare(bar.spellingSuggestionsPopup.wantsVisible, true,
                    "the popup should still open even with zero suggestions, as long as a word is flagged")
            // See test_clicking_a_spelling_suggestion_replaces_the_
            // misspelled_word's own comment: content reveal (and the
            // Repeater/layout settling it depends on) waits for
            // positionManager to confirm the move actually landed.
            tryCompare(bar.spellingSuggestionsPopup.background, "visible", true, 2000)
            compare(bar.spellingSuggestionsPopup.repeater.count, 0)
            compare(bar.noSuggestionsLabel.visible, true)

            noteA.currentMisspelledWord = ""
        }

        // "Add to dictionary" (feature request): a static button below the
        // suggestion list that permanently exempts the currently-flagged
        // word from spellcheck (Settings::addUserDictionaryWord), then
        // forces this note's highlighter to re-check immediately
        // (DocumentHandler::rehighlightSpellcheck) and closes the popup —
        // mirrors test_clicking_a_spelling_suggestion_replaces_the_
        // misspelled_word's fixture-state setup, since no real dictionary
        // is installed on this dev machine either.
        function test_clicking_add_to_dictionary_adds_the_word_and_closes_the_popup() {
            // Deliberately not a real word, and unlikely to already sit in
            // the user dictionary from another test or a prior run on this
            // machine.
            noteA.currentMisspelledWord = "someuniquetestword"
            noteA.currentSpellingSuggestions = []
            noteA.misspelledWordStart = 0
            noteA.misspelledWordEnd = 0

            bar.noteWindow = noteA

            compare(bar.spellingSuggestionsPopup.wantsVisible, true,
                    "the popup should want to be visible once currentMisspelledWord is non-empty")
            // See test_clicking_a_spelling_suggestion_replaces_the_
            // misspelled_word's own comment: content reveal waits for
            // positionManager to confirm the move actually landed, and
            // addToDictionaryButton lives inside that content, so clicking
            // it needs to wait for the same confirmation first.
            tryCompare(bar.spellingSuggestionsPopup.background, "visible", true, 2000)

            mouseClick(bar.addToDictionaryButton, bar.addToDictionaryButton.width / 2, bar.addToDictionaryButton.height / 2)

            verify(settings.userDictionaryWords.indexOf("someuniquetestword") !== -1,
                   "clicking 'Add to dictionary' should persist the word in the user dictionary")
            compare(noteA.currentMisspelledWord, "",
                    "the popup should close itself immediately after the word is added")

            // Cleanup happens here rather than in cleanup() (which only
            // resets the four per-note spellcheck properties, not
            // Settings' own persisted, cross-test-surviving user
            // dictionary) — otherwise this word would leak into other
            // tests' dictionary state and into the real QSettings file
            // across separate test runs.
            settings.removeUserDictionaryWord("someuniquetestword")
        }

        // Feature request ("the popup takes a bit of time to show up —
        // precache its position and size"): precacheSuggestionsPopupSize()
        // measures a suggestion list's real popup size in the background
        // (sizeMeasureContent, a hidden mirror of the real
        // spellingSuggestionsContent layout) and stores it in
        // suggestionsSizeCache, keyed by the suggestion list itself. This
        // doesn't need a real dictionary or a tracked note at all — it's
        // exercised directly, the same way the popup's own
        // repositionSpellingSuggestionsPopup() will read it back later.
        function test_precacheSuggestionsPopupSize_measures_and_caches_a_real_popup_size() {
            const popup = bar.spellingSuggestionsPopup
            const suggestions = ["the", "ten", "tea"]
            const key = popup.suggestionsSizeCacheKey(suggestions)
            verify(!(key in popup.suggestionsSizeCache),
                   "precondition: this exact suggestion list hasn't been measured yet")

            popup.precacheSuggestionsPopupSize(suggestions)

            tryCompare(popup.sizeMeasureQueue, "length", 0, 2000)
            wait(50) // past sizeMeasureSettleDebounce's own 40ms interval

            verify(key in popup.suggestionsSizeCache,
                   "the suggestion list's size should be cached once background measurement settles")
            const size = popup.suggestionsSizeCache[key]
            verify(size.width > 1 && size.height > 1,
                   "the cached size should be the real measured layout, not the placeholder floor: " +
                   JSON.stringify(size))
        }

        // Two different misspelled words that happen to produce the exact
        // same suggestion list (e.g. two different typos of "the") share
        // one popup size — the size only depends on what gets DISPLAYED,
        // not which word triggered it — so caching should treat identical
        // suggestion lists as one cache entry, not measure twice.
        function test_precacheSuggestionsPopupSize_is_a_no_op_for_an_already_cached_list() {
            const popup = bar.spellingSuggestionsPopup
            const suggestions = ["alpha", "beta"]

            popup.precacheSuggestionsPopupSize(suggestions)
            tryCompare(popup.sizeMeasureQueue, "length", 0, 2000)
            wait(50)
            const key = popup.suggestionsSizeCacheKey(suggestions)
            verify(key in popup.suggestionsSizeCache, "precondition: the list is already cached")

            // A second, distinct array with the same contents (not the same
            // JS object reference) — proves the dedup is by CONTENT, not by
            // identity.
            popup.precacheSuggestionsPopupSize(["alpha", "beta"])
            compare(popup.sizeMeasureQueue.length, 0,
                    "an already-cached suggestion list should never be queued for re-measurement")
        }

        // The actual payoff: once a suggestion list's popup size is
        // precached, beginSpellingSuggestionsTransition() (triggered here
        // by tracking a note whose currentSpellingSuggestions already
        // matches the precached list) should mark
        // spellingSuggestionsSizeSettled true IMMEDIATELY, synchronously —
        // not after the usual spellingSuggestionsResettleDebounce wait —
        // since repositionSpellingSuggestionsPopup() already has a real
        // size to target on its very first move request.
        function test_a_precached_popup_size_skips_the_settle_wait_on_tap() {
            const popup = bar.spellingSuggestionsPopup
            const suggestions = ["the", "ten"]

            popup.precacheSuggestionsPopupSize(suggestions)
            tryCompare(popup.sizeMeasureQueue, "length", 0, 2000)
            wait(50)
            const key = popup.suggestionsSizeCacheKey(suggestions)
            verify(key in popup.suggestionsSizeCache, "precondition: the list is already cached")

            // Real content, same as test_clicking_a_spelling_suggestion_
            // replaces_the_misspelled_word's own setup — not relied upon
            // for auto-detection (fixture state below is what actually
            // drives the popup, per this file's own "no real dictionary on
            // this dev machine" convention), just so misspelledWordStart/
            // End below resolve to real, in-range document positions
            // instead of positionToRectangle() logging a live "position
            // out of range" warning against an empty document.
            noteA.docHandler.setMarkdown("I teh best\n")
            wait(50)

            noteA.currentMisspelledWord = "teh"
            noteA.currentSpellingSuggestions = suggestions
            noteA.misspelledWordStart = 2
            noteA.misspelledWordEnd = 5

            bar.noteWindow = noteA

            compare(popup.spellingSuggestionsSizeSettled, true,
                    "a precached suggestion list's size should be trusted immediately, with no wait for " +
                    "the content Repeater to rebuild and settle on its own")

            noteA.currentMisspelledWord = ""
            noteA.docHandler.setMarkdown("")
            wait(50)
        }
    }
}
