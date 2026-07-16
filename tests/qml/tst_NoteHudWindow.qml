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
        // previous one left tracked. Also clears any synthetic pointer
        // position left inside the ribbon window by a mouseMove/mouseClick.
        function cleanup() {
            hudWindow.noteWindow = null
            mouseMove(hudWindow.hud, -50, -50)
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
