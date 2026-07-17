import QtQml
import QtQuick
import QtTest
import SuopNotesUi

Item {
    id: root

    // Real NoteWindow instances stand in for "the note the pointer is over" —
    // NoteHudWindow reads the same model-backed noteX/noteY/noteWidth/noteHeight
    // and screen a real hovered note exposes (NOT plain Window x/y — see
    // NoteHudWindow.reposition() for why), exactly as Main.qml's
    // hoveredNoteWindow tracking would feed it.
    NoteWindow {
        id: noteA
        noteId: "hudwindow-test-note-a"
        index: 80
        noteX: 300
        noteY: 300
        noteWidth: 260
        noteHeight: 260
        screenName: ""
        alwaysOnTop: false
    }

    // A second note sitting directly to the RIGHT of noteA — the arrangement
    // that broke the ribbon outright (see NoteHudWindow.noteHoverChanged).
    // noteA's ribbon is anchored just outside noteA's right edge, which is
    // exactly where this note sits, so reaching for that ribbon drags the
    // pointer across this one on the way.
    NoteWindow {
        id: noteB
        noteId: "hudwindow-test-note-b"
        index: 81
        noteX: 300 + 260 + 4
        noteY: 300
        noteWidth: 260
        noteHeight: 260
        screenName: ""
        alwaysOnTop: false
    }

    NoteHudWindow {
        id: hudWindow
    }

    // Emitted for search (the one action Main.qml routes rather than the
    // ribbon acting on the note directly — the find panel is bound to the
    // ACTIVE note, not a merely-hovered one).
    SignalSpy {
        id: findSpy
        target: hudWindow
        signalName: "findRequested"
    }

    TestCase {
        name: "NoteHudWindow"
        when: windowShown

        // Each test starts from "nothing hovered", regardless of what the
        // previous one left tracked. The pointer is parked off both notes and
        // off the ribbon first, and the tracking timers are given time to fire,
        // BEFORE the tracked note is forced back to null — otherwise a dwell
        // still in flight from the test just finished could re-track a note
        // moments into the next one.
        function cleanup() {
            mouseMove(noteA.contentItem, -50, -50)
            mouseMove(noteB.contentItem, -50, -50)
            mouseMove(hudWindow.hud, -50, -50)
            hudWindow.noteHoverChanged(noteA, false)
            hudWindow.noteHoverChanged(noteB, false)
            wait(hudWindow.noteSwitchDwellMs + 200)
            hudWindow.noteWindow = null
            findSpy.clear()
            wait(80)
        }

        // Like the toolbar, this window is shown exactly once at startup and
        // never hidden again (re-mapping a surface while a note holds keyboard
        // focus disrupts its text input). "Hidden" is content-invisible +
        // parked off-screen, so the ribbon's own visibility — not the window's
        // — is what these tests check.
        function test_ribbon_is_hidden_when_no_note_is_hovered() {
            compare(hudWindow.hud.visible, false)
        }

        function test_ribbon_is_shown_when_a_note_is_hovered() {
            hudWindow.noteWindow = noteA
            wait(80)
            compare(hudWindow.hud.visible, true)
        }

        // The ribbon is always the full vertical column in windowed mode: the
        // whole window shows/hides on hover, so there is no collapsed-dot state
        // to reveal, and the column orientation is what keeps it clear of the
        // toolbar (which sits above the note).
        function test_ribbon_is_an_always_expanded_vertical_column() {
            compare(hudWindow.hud.vertical, true)
            compare(hudWindow.hud.alwaysExpanded, true)
        }

        // Default placement (feature request): a column just OUTSIDE the note's
        // right edge, top-aligned — so it never overlaps the note's text, and
        // never collides with the toolbar sitting above the note.
        function test_anchors_just_outside_the_notes_right_edge_top_aligned() {
            const screen = noteA.screen
            verify(screen, "precondition: the tracked note reports a real screen")
            // Keep the note well clear of the right/bottom screen edges so the
            // outside-right placement isn't clamped or forced to fall back.
            noteA.noteX = screen.virtualX + 100
            noteA.noteY = screen.virtualY + 100

            hudWindow.noteWindow = noteA
            wait(80)

            compare(hudWindow.x, noteA.noteX + noteA.noteWidth + 4,
                    "the ribbon should sit just outside the note's right edge")
            compare(hudWindow.y, noteA.noteY,
                    "the ribbon should be top-aligned with the note")
        }

        // When the note is pinned against the right screen edge there is no
        // room for a column outside it — the ribbon tucks inside the note's own
        // top-right corner instead (the old inline position) rather than
        // running off-screen. The accepted trade-off for that edge case.
        function test_falls_back_inside_the_note_when_there_is_no_room_on_the_right() {
            const screen = noteA.screen
            verify(screen, "precondition: the tracked note reports a real screen")

            // Note's right edge just 6px from the screen edge — less than the
            // ribbon's own width, so no room for it outside.
            noteA.noteWidth = 260
            noteA.noteX = screen.virtualX + screen.width - noteA.noteWidth - 6
            noteA.noteY = screen.virtualY + 150

            hudWindow.noteWindow = noteA
            wait(80)

            const w = hudWindow.width
            compare(hudWindow.x, noteA.noteX + noteA.noteWidth - w - 4,
                    "with no room outside, the ribbon tucks inside the note's top-right corner")
            verify(hudWindow.x + w <= noteA.noteX + noteA.noteWidth,
                   "the fallback position sits entirely within the note's width")

            noteA.noteX = 300 // restore for other tests
        }

        // Follows the note when it's dragged elsewhere — noteX/noteY are what a
        // finished compositor drag looks like to this component (the client is
        // told nothing mid-gesture; the position only arrives via
        // PositionEventService -> Settings -> the model's noteX/noteY once the
        // drag ends). This drives that same property.
        function test_reanchors_to_the_note_after_it_is_dragged() {
            const screen = noteA.screen
            verify(screen && screen.width >= 750,
                   "precondition: a screen wide enough to fit the ribbon outside the note at both spots")

            noteA.noteX = screen.virtualX + 100
            noteA.noteY = screen.virtualY + 100
            hudWindow.noteWindow = noteA
            wait(80)
            compare(hudWindow.x, noteA.noteX + noteA.noteWidth + 4)

            noteA.noteX = screen.virtualX + 200
            noteA.noteY = screen.virtualY + 180
            wait(80)
            compare(hudWindow.x, noteA.noteX + noteA.noteWidth + 4,
                    "the ribbon should re-anchor to the note's new position")
            compare(hudWindow.y, noteA.noteY)
        }

        // "Hidden" = parked off every screen (plus content-invisible + input
        // rejected), never an actual hide() — so a still-mapped window can't
        // sit on top of and eat clicks meant for whatever is underneath.
        function test_parks_offscreen_once_the_hovered_note_is_cleared() {
            hudWindow.noteWindow = noteA
            wait(80)
            const screen = noteA.screen
            verify(screen, "precondition: the tracked note reports a real screen")

            hudWindow.noteWindow = null
            wait(80)

            verify(hudWindow.x > screen.virtualX + screen.width
                   || hudWindow.y > screen.virtualY + screen.height,
                   "clearing the hovered note parks the still-mapped ribbon off every screen")
            compare(hudWindow.hud.visible, false)
        }

        // `hovered` is the keep-alive signal Main.qml reads so the ribbon
        // doesn't vanish as the pointer crosses the gap from the note onto it —
        // verify it actually tracks the pointer being over the ribbon.
        function test_hovered_tracks_whether_the_pointer_is_over_the_ribbon() {
            hudWindow.noteWindow = noteA
            wait(80)

            mouseMove(hudWindow.hud, hudWindow.width / 2, hudWindow.height / 2)
            tryCompare(hudWindow, "hovered", true)

            mouseMove(hudWindow.hud, -50, -50)
            tryCompare(hudWindow, "hovered", false)
        }

        // ---- Hover tracking: which note actually earns the ribbon ----
        //
        // Main.qml's note delegates forward raw hover events in via
        // noteHoverChanged(); this window decides what to do with them. These
        // tests drive that entry point exactly as the delegate does, and move a
        // real pointer over the real note windows so the policy's re-checks read
        // genuine hover state rather than a stubbed flag.

        // With no ribbon on screen there is nothing to steal from, so the first
        // note hovered takes it on the spot — no dwell, so a first hover feels
        // instant.
        function test_the_first_note_hovered_takes_the_ribbon_immediately() {
            compare(hudWindow.noteWindow, null, "precondition: nothing is tracked")

            mouseMove(noteA.contentItem, 20, 20)
            tryCompare(noteA, "hovered", true)
            hudWindow.noteHoverChanged(noteA, true)

            // Deliberately no wait() — this case is meant to be synchronous.
            compare(hudWindow.noteWindow, noteA,
                    "the first note hovered takes the ribbon immediately")
        }

        // THE regression, reported live: "with two notes next to each other, you
        // cannot access the left note's top right ribbon due to the right note
        // stealing focus". noteA's ribbon sits just outside noteA's right edge —
        // i.e. over noteB — so reaching for it drags the pointer across noteB.
        // Tracking used to hand the ribbon to whichever note reported hover
        // LAST, immediately and unconditionally, so that crossing re-anchored
        // the ribbon to noteB and it vanished out from under the pointer
        // mid-reach. The left note's ribbon could not be clicked at all.
        function test_a_note_merely_crossed_on_the_way_to_the_ribbon_never_steals_it() {
            mouseMove(noteA.contentItem, 20, 20)
            tryCompare(noteA, "hovered", true)
            hudWindow.noteHoverChanged(noteA, true)
            compare(hudWindow.noteWindow, noteA, "precondition: noteA has the ribbon")

            // The pointer crosses noteB, which reports hover exactly as the
            // delegate would.
            mouseMove(noteB.contentItem, 5, 5)
            tryCompare(noteB, "hovered", true)
            hudWindow.noteHoverChanged(noteB, true)
            compare(hudWindow.noteWindow, noteA,
                    "a note reporting hover does not take the ribbon on the spot")

            // ...and the pointer carries on onto noteA's ribbon, which is what
            // it was reaching for all along. noteB has to be left explicitly:
            // synthetic mouse events are delivered per-window here, so entering
            // the ribbon's window is not by itself a leave for noteB's.
            mouseMove(noteB.contentItem, -50, -50)
            tryCompare(noteB, "hovered", false)
            mouseMove(hudWindow.hud, hudWindow.width / 2, hudWindow.height / 2)
            hudWindow.noteHoverChanged(noteB, false)
            wait(hudWindow.noteSwitchDwellMs + 100)

            compare(hudWindow.noteWindow, noteA,
                    "the ribbon stays with the note being reached toward, so it can actually be clicked")
            compare(hudWindow.hud.visible, true, "and it is still on screen to be clicked")
        }

        // The dwell re-reads the candidate's LIVE hovered property rather than
        // trusting the event that scheduled it — so a note the pointer has since
        // left doesn't take the ribbon even if its "pointer left" event never
        // arrives at all. Same re-check-at-fire-time discipline as the clear
        // debounce. Drives only the hover-IN event, then moves the pointer away
        // without ever reporting the leave.
        function test_the_switch_dwell_rechecks_live_hover_rather_than_trusting_the_event() {
            mouseMove(noteA.contentItem, 20, 20)
            tryCompare(noteA, "hovered", true)
            hudWindow.noteHoverChanged(noteA, true)
            compare(hudWindow.noteWindow, noteA, "precondition: noteA has the ribbon")

            mouseMove(noteB.contentItem, 5, 5)
            tryCompare(noteB, "hovered", true)
            hudWindow.noteHoverChanged(noteB, true)

            // The pointer leaves noteB for noteA's ribbon; the leave event is
            // deliberately NOT forwarded, so only the fire-time re-check can
            // save the ribbon here.
            mouseMove(noteB.contentItem, -50, -50)
            tryCompare(noteB, "hovered", false)
            mouseMove(hudWindow.hud, hudWindow.width / 2, hudWindow.height / 2)
            wait(hudWindow.noteSwitchDwellMs + 100)

            compare(hudWindow.noteWindow, noteA,
                    "the dwell re-read noteB's live hover, found the pointer gone, and kept the ribbon on noteA")
        }

        // The other half of the rule: a note the pointer actually SETTLES on is
        // a real switch and must still get the ribbon — the dwell delays that
        // hand-off, it never blocks it. Without this, the fix above would just
        // be a different bug (the ribbon stuck on the first note forever).
        function test_a_note_the_pointer_settles_on_takes_the_ribbon() {
            mouseMove(noteA.contentItem, 20, 20)
            tryCompare(noteA, "hovered", true)
            hudWindow.noteHoverChanged(noteA, true)
            compare(hudWindow.noteWindow, noteA, "precondition: noteA has the ribbon")

            // The pointer moves onto noteB and stays there.
            mouseMove(noteB.contentItem, 20, 20)
            tryCompare(noteB, "hovered", true)
            hudWindow.noteHoverChanged(noteB, true)
            hudWindow.noteHoverChanged(noteA, false)

            // A note the pointer settles on wins the ribbon once the dwell
            // elapses. (No message arg — tryCompare's 4th positional is the
            // timeout, which is what waits out the dwell here.)
            tryCompare(hudWindow, "noteWindow", noteB)
        }

        // The clear timer stands down while a switch is pending (the switch is
        // newer information, and parking the ribbon for the few ms until it
        // resolves would read as a flicker). This pins the other end of that
        // deal: if the switch is then ABANDONED because the pointer moved on,
        // the ribbon must not be left stranded on a note the pointer has also
        // already left. Both notes are left here, and neither the ribbon nor
        // either note ends up under the pointer — so the ribbon must clear.
        function test_an_abandoned_switch_still_clears_a_note_the_pointer_has_left() {
            mouseMove(noteA.contentItem, 20, 20)
            tryCompare(noteA, "hovered", true)
            hudWindow.noteHoverChanged(noteA, true)
            compare(hudWindow.noteWindow, noteA, "precondition: noteA has the ribbon")

            // The pointer crosses noteB (arming a switch), then leaves BOTH
            // notes entirely — off to some other window, not to the ribbon.
            mouseMove(noteB.contentItem, 5, 5)
            tryCompare(noteB, "hovered", true)
            hudWindow.noteHoverChanged(noteB, true)

            mouseMove(noteB.contentItem, -50, -50)
            mouseMove(noteA.contentItem, -50, -50)
            tryCompare(noteB, "hovered", false)
            tryCompare(noteA, "hovered", false)
            hudWindow.noteHoverChanged(noteA, false)

            // The switch is abandoned (noteB no longer hovered) and noteA is
            // gone too, so tracking must end rather than stick to either.
            tryCompare(hudWindow, "noteWindow", null)
            compare(hudWindow.hud.visible, false, "and the ribbon is off screen")
        }

        // THE "stays forever" regression, reported live: "moved onto another
        // note, and exited that same note before the ribbon moved onto it."
        // Hovering noteA shows its ribbon; the pointer then brushes noteB and
        // leaves it again BEFORE the switch dwell hands noteB the ribbon — so
        // noteB never becomes the tracked note. Entering noteB ran the
        // unconditional clear-debounce stop(), cancelling the clear that leaving
        // noteA had armed; abandoning the never-completed switch to noteB must
        // re-arm that clear, or the ribbon stays stuck on noteA forever even
        // though the pointer is now nowhere near either note.
        //
        // The ORDERING is the whole point: leaving noteA is forwarded BEFORE
        // entering noteB (the leave-then-enter order the compositor usually
        // delivers), so the clear armed by noteA's leave is exactly what
        // entering noteB then stops. The dwell is never waited out here — noteB
        // is left while its switch is still merely pending, which is the path
        // that used to leak.
        function test_brushing_a_second_note_and_leaving_before_the_dwell_still_clears() {
            mouseMove(noteA.contentItem, 20, 20)
            tryCompare(noteA, "hovered", true)
            hudWindow.noteHoverChanged(noteA, true)
            compare(hudWindow.noteWindow, noteA, "precondition: noteA has the ribbon")

            // Pointer leaves noteA first (arming the clear)...
            mouseMove(noteA.contentItem, -50, -50)
            tryCompare(noteA, "hovered", false)
            hudWindow.noteHoverChanged(noteA, false)

            // ...then brushes noteB (which stops that clear and arms a switch
            // dwell that noteB has not yet earned)...
            mouseMove(noteB.contentItem, 5, 5)
            tryCompare(noteB, "hovered", true)
            hudWindow.noteHoverChanged(noteB, true)

            // ...then leaves noteB well before the dwell elapses, so the switch
            // is abandoned. The pointer is now off every note and off the ribbon.
            mouseMove(noteB.contentItem, -50, -50)
            tryCompare(noteB, "hovered", false)
            hudWindow.noteHoverChanged(noteB, false)

            // With nothing left under the pointer, the ribbon must clear rather
            // than stay stuck on noteA.
            tryCompare(hudWindow, "noteWindow", null)
            compare(hudWindow.hud.visible, false, "and the ribbon is off screen")
        }

        // Tapping search re-emits findRequested with the hovered note, for
        // Main.qml to activate it and open the find panel against it (search
        // can't act on the note directly — see NoteHudWindow's findRequested).
        function test_search_glyph_emits_findRequested_with_the_hovered_note() {
            hudWindow.noteWindow = noteA
            wait(80)

            const glyph = hudWindow.hud.searchGlyphItem
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)

            compare(findSpy.count, 1)
            compare(findSpy.signalArguments[0][0], noteA,
                    "findRequested should carry the very note the ribbon is acting on")
        }
    }
}
