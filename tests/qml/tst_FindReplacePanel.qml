import QtQml
import QtQuick
import QtTest
import SuopNotesUi

// Behavior tests for the app-wide floating find/replace window
// (FindReplacePanel.qml), driven through real NoteWindows the way the app
// itself wires it up: the notes' Ctrl+F/Ctrl+H shortcuts, the HUD's search
// glyph, and the panel's own public functions. The match/replace MATH
// (offsets, regex semantics, capture groups) lives in DocumentHandler and is
// unit-tested in C++ — these tests document the UI behavior layered on top:
// open/close, cross-window focus handoff, live match counting, wraparound
// cycling, the replace controls' opt-in gating, and the singleton's
// follows-the-focused-note lifecycle.
//
// The panel is declared STATICALLY here and shared by every test, unlike the
// per-test NoteWindows below. That is not a style choice: it is a permanently
// mapped window (see FindReplacePanel.qml's Component.onCompleted — mapping
// and unmapping it repeatedly disrupts the focused note's text input), so it
// cannot be created and destroyed per test. cleanup() resets its state
// instead.
Item {
    id: root

    // Deferred (not instantiated until createObject() is called) so each test
    // gets a genuinely fresh note. The PANEL's cross-call state (search term,
    // current match, anchor) is shared now, so cleanup() resets that by hand.
    Component {
        id: noteWindowComponent
        NoteWindow {}
    }

    FindReplacePanel {
        id: panel
    }

    // Shared across every window in the process (windowInputMask is a single
    // app-wide singleton) — lastInputMaskFor() below filters by window rather
    // than assuming the panel is the only emitter.
    SignalSpy {
        id: inputMaskSpy
        target: windowInputMask
        signalName: "inputMaskApplied"
    }

    TestCase {
        name: "FindReplacePanel"
        when: windowShown

        // Destroyed by cleanup() rather than by each test, so the panel is
        // always unbound from a note BEFORE that note is torn down.
        property var createdWindows: []

        // Creates a fresh NoteWindow showing the given markdown, wires it to
        // the panel exactly as Main.qml does, and makes it the tracked note.
        //
        // The signal wiring here is a hand copy of Main.qml's (see the
        // Instantiator delegate's onFindRequested/onFindCloseRequested). It
        // has to be: Main.qml is not part of the test QML module — it builds a
        // system tray icon and an Instantiator over the real notes model — so
        // the join between a note's request and this window is the one part of
        // this feature the suite cannot verify for real. Keep the two in sync
        // by hand; the guard Main.qml adds (ignore a note that isn't the
        // tracked one) is deliberately omitted, since these tests only ever
        // drive the tracked note.
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
            w.findRequested.connect(function (withReplace) {
                if (withReplace)
                    panel.openReplace()
                else
                    panel.openFind()
            })
            w.findCloseRequested.connect(function () {
                panel.close()
            })
            w.docHandler.setMarkdown(markdown)
            createdWindows.push(w)
            panel.noteWindow = w
            wait(50)
            return w
        }

        // The panel outlives every test, so every piece of its state has to be
        // put back by hand. Order matters: unbind the note first, so nothing
        // reaches into a window that is about to be destroyed. `open = false`
        // rather than close(), which would try to hand focus back to a note
        // that may already be gone.
        function cleanup() {
            panel.open = false
            panel.noteWindow = null
            panel.findField.text = ""
            panel.replaceField.text = ""
            panel.regexCheckBox.checked = false
            panel.replaceCheckBox.checked = false
            for (let i = 0; i < createdWindows.length; ++i)
                createdWindows[i].destroy()
            createdWindows = []
            // Each top-level window needs its own hover reset — one aimed at
            // root never reaches a separate window's content item.
            mouseMove(panel.contentItem, -50, -50)
            wait(50)
        }

        // keyClick() always targets QGuiApplication::focusWindow(), so the
        // window a test means to type into must really be active before any
        // synthetic key lands. With the panel now being its own window, that
        // is no longer a formality — the note and the panel genuinely compete
        // for focus, and a key aimed at the wrong one lands somewhere real.
        function activateWindow(win) {
            win.requestActivate()
            tryCompare(win, "active", true)
        }

        // This Qt version's QML TestCase has no built-in "type this whole
        // string" helper — plain per-character keyClick() loop, same as
        // tst_NoteWindow.qml.
        function typeText(text) {
            for (let i = 0; i < text.length; ++i)
                keyClick(text[i])
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

        // ---- Opening ----

        function test_ctrl_f_on_the_note_opens_the_panel_in_find_mode_with_focus_in_the_find_field() {
            const win = makeWindow("frp-ctrl-f", 80, "alpha beta alpha\n\ngamma alpha")

            compare(panel.open, false, "precondition: the panel starts closed")

            activateWindow(win)
            win.editor.forceActiveFocus()
            tryCompare(win.editor, "activeFocus", true)

            keyClick(Qt.Key_F, Qt.ControlModifier)

            compare(panel.open, true, "Ctrl+F should show the panel")
            compare(panel.replaceChecked, false,
                    "Ctrl+F is find-only — the Replace checkbox starts unchecked")
            // Opening should put the keyboard focus straight into the find
            // field — tryCompare because that now involves activating a
            // different window, not just moving focus within one.
            tryCompare(panel.findField, "activeFocus", true)
        }

        function test_ctrl_h_on_the_note_opens_the_panel_with_the_replace_checkbox_checked() {
            const win = makeWindow("frp-ctrl-h", 81, "alpha beta alpha\n\ngamma alpha")

            activateWindow(win)
            win.editor.forceActiveFocus()
            tryCompare(win.editor, "activeFocus", true)

            keyClick(Qt.Key_H, Qt.ControlModifier)

            compare(panel.open, true, "Ctrl+H should show the panel")
            compare(panel.replaceChecked, true,
                    "Ctrl+H opens straight into replace mode — the checkbox comes up checked")
            // The find field still takes focus first (the term comes before
            // the replacement).
            tryCompare(panel.findField, "activeFocus", true)
        }

        // The HUD's search glyph is the mouse path to the same place Ctrl+F
        // goes. The glyph's own tap mechanics (hit area, signal emission,
        // isolation from the other glyphs) are covered in tst_NoteHud.qml;
        // here the signal is emitted directly — in a real NoteWindow the HUD
        // is hidden until the note is hovered (visible: noteHoverHandler
        // .hovered), which makes synthetic clicks on it flaky offscreen — to
        // document the wiring: searchRequested opens the panel in find mode.
        function test_hud_search_request_opens_the_panel_in_find_mode() {
            const win = makeWindow("frp-hud", 90, "alpha beta alpha\n\ngamma alpha")

            compare(panel.open, false, "precondition: the panel starts closed")

            win.noteHud.searchRequested()

            compare(panel.open, true, "the HUD's search glyph opens the find panel")
            compare(panel.replaceChecked, false,
                    "the glyph is a find action — replace stays opt-in via its checkbox")
        }

        function test_opening_with_a_selection_prefills_the_find_field_with_the_selected_text() {
            const win = makeWindow("frp-prefill", 89, "alpha beta alpha\n\ngamma alpha")
            const editor = win.editor

            const betaStart = editor.text.indexOf("beta")
            editor.select(betaStart, betaStart + "beta".length)
            compare(editor.selectedText, "beta", "precondition: a real selection is held")

            panel.openFind()

            compare(panel.findField.text, "beta",
                    "a held selection is almost always the term the user wants — it's prefilled")
            compare(panel.matchCount, 1)
            compare(editor.selectionStart, betaStart,
                    "the current match anchors at the selection itself, not back at the top")
        }

        // ---- Closing, across two windows ----

        function test_escape_in_the_find_field_closes_the_panel_and_returns_focus_to_the_note() {
            const win = makeWindow("frp-escape", 82, "alpha beta alpha\n\ngamma alpha")

            activateWindow(win)
            panel.openFind()
            // openFind() activates the panel's own window — without that, the
            // Escape below would land back in the note.
            tryCompare(panel, "active", true)
            tryCompare(panel.findField, "activeFocus", true)

            keyClick(Qt.Key_Escape)

            compare(panel.open, false, "Escape while typing a search term should dismiss the panel")
            // A cross-WINDOW handoff now: the panel has to raise and reactivate
            // the note, not just move focus within it — so closing hands the
            // note back its OS focus and editing resumes immediately.
            tryCompare(win, "active", true)
            tryCompare(win.editor, "activeFocus", true)
        }

        // Escape belongs to the panel as a whole, not just to its own fields:
        // clicking into the note to look at a match (or read around it) moves
        // focus to a different window entirely, and Escape must still dismiss
        // the panel from there. The note's own Escape shortcut is what covers
        // this — the panel's shortcuts are scoped to the panel's window.
        function test_escape_in_the_note_closes_the_panel() {
            const win = makeWindow("frp-escape-editor", 92, "alpha beta alpha\n\ngamma alpha")

            panel.openFind()
            compare(panel.open, true, "precondition: the panel is open")

            activateWindow(win)
            win.editor.forceActiveFocus()
            tryCompare(win.editor, "activeFocus", true)

            keyClick(Qt.Key_Escape)

            compare(panel.open, false,
                    "Escape dismisses the panel even though focus is in a different window")
        }

        // Escape's other job on the note (disarming a middle-click autoscroll)
        // takes precedence, so an armed autoscroll isn't silently left running
        // by a keypress the user aimed at it.
        function test_escape_in_the_note_disarms_an_armed_autoscroll_before_it_closes_the_panel() {
            const win = makeWindow("frp-escape-autoscroll", 93, "alpha beta alpha\n\ngamma alpha")

            panel.openFind()
            activateWindow(win)
            win.editor.forceActiveFocus()
            tryCompare(win.editor, "activeFocus", true)

            win.mouseDragArea.middleScrollArmed = true

            keyClick(Qt.Key_Escape)

            compare(win.mouseDragArea.middleScrollArmed, false, "the armed autoscroll is disarmed first")
            compare(panel.open, true, "the panel stays open — that Escape was spent on the autoscroll")

            keyClick(Qt.Key_Escape)

            compare(panel.open, false, "a second Escape then closes the panel")
        }

        // Regression test for a silent, total failure of the feature's main
        // entry point. The panel briefly had its own window-level
        // Shortcut items for Escape/Ctrl+F/Ctrl+H, on the reasoning that
        // Qt.WindowShortcut scopes a shortcut to its own window and so could
        // not collide with the notes' identical ones. It does not hold: the
        // shortcut map is application-global, both windows match, and on an
        // ambiguous match QML's Shortcut emits activatedAmbiguously() INSTEAD
        // of activated() — no warning, no error, Ctrl+F simply stopped opening
        // the panel.
        //
        // So this checks the notes' shortcuts still fire with the panel window
        // alive and open — i.e. that nothing has re-introduced a competing
        // Shortcut for these sequences on the panel side.
        function test_the_notes_shortcuts_still_fire_while_the_panel_window_is_open() {
            const win = makeWindow("frp-no-clash", 99, "alpha beta alpha\n\ngamma alpha")

            panel.openFind()
            compare(panel.open, true, "precondition: the panel window is up and showing")

            // Escape, from the note: closes the panel via the note's own
            // Escape shortcut.
            activateWindow(win)
            win.editor.forceActiveFocus()
            tryCompare(win.editor, "activeFocus", true)
            keyClick(Qt.Key_Escape)
            compare(panel.open, false, "the note's Escape shortcut still fires")

            // Ctrl+F, from the note: re-opens it.
            keyClick(Qt.Key_F, Qt.ControlModifier)
            compare(panel.open, true, "the note's Ctrl+F shortcut still fires")
            compare(panel.replaceChecked, false)

            // Ctrl+H, from the note, with the panel already open: switches it
            // into replace mode.
            activateWindow(win)
            win.editor.forceActiveFocus()
            tryCompare(win.editor, "activeFocus", true)
            keyClick(Qt.Key_H, Qt.ControlModifier)
            compare(panel.replaceChecked, true, "the note's Ctrl+H shortcut still fires")
        }

        // ---- Searching ----

        // The ONE test that exercises the live-search debounce itself (the
        // 150ms one-shot timer restarted by each keystroke): typing a term must
        // update the match count on its own, without any explicit "search"
        // action. Every other test calls recompute() directly for determinism
        // instead of waiting the debounce out.
        function test_typing_a_term_live_updates_the_match_count_and_selects_the_first_match() {
            const win = makeWindow("frp-live", 83, "alpha beta alpha\n\ngamma alpha")
            const editor = win.editor

            editor.cursorPosition = 0 // no selection → nothing to prefill, cursor at the top
            activateWindow(win)
            panel.openFind()
            tryCompare(panel, "active", true)
            tryCompare(panel.findField, "activeFocus", true)

            typeText("alpha")

            tryCompare(panel, "matchCount", 3, 1000)

            // The first match at/after the cursor is selected in the editor
            // itself — the CURRENT match is shown as a real text selection, on
            // top of the highlighting every match gets (see the highlighting
            // tests below). That selection stays visible even though the note
            // is not the active window, because the editor sets
            // persistentSelection. Offsets computed from the live editor text,
            // not hardcoded, so markdown's paragraph-break serialization can't
            // skew them.
            const firstAlpha = editor.text.indexOf("alpha")
            compare(editor.selectionStart, firstAlpha)
            compare(editor.selectionEnd, firstAlpha + "alpha".length)
            compare(panel.currentMatchIndex, 0)
            compare(panel.matchCountLabel.text, "1 of 3",
                    "the count label reads as a human position, 1-based")
        }

        function test_next_match_cycles_forward_and_wraps_from_the_last_match_back_to_the_first() {
            const win = makeWindow("frp-next", 84, "alpha beta alpha\n\ngamma alpha")
            const editor = win.editor

            const first = editor.text.indexOf("alpha")
            const second = editor.text.indexOf("alpha", first + 1)
            const third = editor.text.indexOf("alpha", second + 1)

            editor.cursorPosition = 0
            panel.openFind()
            panel.findField.text = "alpha"
            panel.recompute(0) // deterministic path — skips the live debounce

            compare(panel.currentMatchIndex, 0)
            compare(editor.selectionStart, first)

            panel.nextMatch()
            compare(panel.currentMatchIndex, 1)
            compare(editor.selectionStart, second)

            panel.nextMatch()
            compare(panel.currentMatchIndex, 2)
            compare(editor.selectionStart, third)

            panel.nextMatch()
            compare(panel.currentMatchIndex, 0,
                    "stepping past the last match should wrap around to the first")
            compare(editor.selectionStart, first)
        }

        function test_prev_match_wraps_from_the_first_match_back_to_the_last() {
            const win = makeWindow("frp-prev", 85, "alpha beta alpha\n\ngamma alpha")
            const editor = win.editor

            const first = editor.text.indexOf("alpha")
            const second = editor.text.indexOf("alpha", first + 1)
            const third = editor.text.indexOf("alpha", second + 1)

            editor.cursorPosition = 0
            panel.openFind()
            panel.findField.text = "alpha"
            panel.recompute(0)

            compare(panel.currentMatchIndex, 0)

            panel.prevMatch()
            compare(panel.currentMatchIndex, 2,
                    "stepping back from the first match should wrap around to the last")
            compare(editor.selectionStart, third)
        }

        function test_regex_mode_matches_patterns_and_flags_invalid_ones_without_matching() {
            const win = makeWindow("frp-regex", 88, "a 12 b 345")
            const editor = win.editor

            editor.cursorPosition = 0
            panel.openFind()
            panel.regexCheckBox.checked = true
            panel.findField.text = "\\d+"
            panel.recompute(0)

            compare(panel.patternValid, true)
            compare(panel.matchCount, 2, "\\d+ finds the two digit runs, 12 and 345")
            compare(editor.selectionStart, editor.text.indexOf("12"))
            compare(editor.selectedText, "12",
                    "the first regex match is selected as a whole run, not a single character")

            // An unfinished pattern (a lone open-paren) is reported as invalid
            // rather than matching nothing silently — the count label switches
            // to an explicit error read-out.
            panel.findField.text = "("
            panel.recompute(0)
            compare(panel.patternValid, false)
            compare(panel.matchCount, 0)
            compare(panel.matchCountLabel.text, "bad pattern")
        }

        // ---- Highlighting every match ----

        // Only the CURRENT match gets the editor's selection, so on its own a
        // search says nothing about where the term's other occurrences are.
        // Every match is highlighted instead, so the whole picture is visible
        // at once. The painting itself (colors, merging over bold/headings,
        // multi-line matches) is unit-tested in C++ against
        // MarkdownHighlighter; what these tests own is the PANEL's half of the
        // contract — pushing the right ranges at the right times, and taking
        // them off again.
        function test_searching_highlights_every_match_and_marks_which_one_is_current() {
            const win = makeWindow("frp-hl-all", 97, "alpha beta alpha\n\ngamma alpha")

            panel.openFind()
            panel.findField.text = "alpha"
            panel.recompute(0)
            compare(panel.matchCount, 3, "precondition: three matches")

            const ranges = win.docHandler.searchHighlightRanges()
            compare(ranges.length, 3, "every match is highlighted, not just the selected one")
            for (let i = 0; i < ranges.length; ++i) {
                compare(ranges[i].start, panel._matches[i].start)
                compare(ranges[i].end, panel._matches[i].end)
            }
            compare(win.docHandler.searchHighlightCurrentIndex(), 0,
                    "and the current match is marked, so it reads differently from the rest")
        }

        function test_stepping_through_matches_moves_which_one_is_marked_current() {
            const win = makeWindow("frp-hl-step", 98, "alpha beta alpha\n\ngamma alpha")

            panel.openFind()
            panel.findField.text = "alpha"
            panel.recompute(0)
            compare(win.docHandler.searchHighlightCurrentIndex(), 0)

            panel.nextMatch()
            compare(win.docHandler.searchHighlightCurrentIndex(), 1,
                    "the mark follows the user forward")
            compare(win.docHandler.searchHighlightRanges().length, 3,
                    "and the other matches stay highlighted the whole time")

            panel.prevMatch()
            compare(win.docHandler.searchHighlightCurrentIndex(), 0, "and back")
        }

        function test_a_term_that_stops_matching_takes_the_previous_highlighting_off() {
            const win = makeWindow("frp-hl-stale", 99, "alpha beta alpha\n\ngamma alpha")

            panel.openFind()
            panel.findField.text = "alpha"
            panel.recompute(0)
            compare(win.docHandler.searchHighlightRanges().length, 3, "precondition: highlighted")

            panel.findField.text = "nothingmatchesthis"
            panel.recompute(0)

            compare(panel.matchCount, 0)
            compare(win.docHandler.searchHighlightRanges().length, 0,
                    "the old term's highlighting must not linger once it no longer matches")
        }

        // The highlighting only means anything while the panel explaining it is
        // on screen. The current match's SELECTION deliberately survives a
        // close (see FindReplacePanel.close()) — the highlighting does not.
        function test_closing_the_panel_takes_the_highlighting_off_but_keeps_the_selection() {
            const win = makeWindow("frp-hl-close", 100, "alpha beta alpha\n\ngamma alpha")

            activateWindow(win)
            panel.openFind()
            panel.findField.text = "alpha"
            panel.recompute(0)
            compare(win.docHandler.searchHighlightRanges().length, 3, "precondition: highlighted")

            panel.close()
            wait(50)

            compare(win.docHandler.searchHighlightRanges().length, 0,
                    "a closed panel leaves no highlighting behind")
            compare(win.editor.selectedText, "alpha",
                    "but the match the user landed on stays selected, ready to edit")
        }

        // The panel is a singleton that follows focus, so the note it just left
        // has to be cleaned up explicitly — nothing else is watching it.
        function test_following_focus_to_another_note_unhighlights_the_one_left_behind() {
            const winA = makeWindow("frp-hl-follow-a", 101, "alpha beta alpha\n\ngamma alpha")

            panel.openFind()
            panel.findField.text = "alpha"
            panel.recompute(0)
            compare(winA.docHandler.searchHighlightRanges().length, 3, "precondition: highlighted")

            const winB = makeWindow("frp-hl-follow-b", 102, "alpha, once")
            tryCompare(panel, "matchCount", 1)

            compare(winA.docHandler.searchHighlightRanges().length, 0,
                    "the note the user left must not keep highlighting it has no panel to explain")
            compare(winB.docHandler.searchHighlightRanges().length, 1,
                    "and the note they moved to picks it up")
        }

        function test_losing_the_tracked_note_takes_its_highlighting_off_too() {
            const win = makeWindow("frp-hl-untrack", 103, "alpha beta alpha\n\ngamma alpha")

            panel.openFind()
            panel.findField.text = "alpha"
            panel.recompute(0)
            compare(win.docHandler.searchHighlightRanges().length, 3, "precondition: highlighted")

            // Focus gone to another app entirely: the card hides, so its
            // highlighting has to go with it even though the panel stays open.
            panel.noteWindow = null
            wait(80)

            compare(win.docHandler.searchHighlightRanges().length, 0)

            panel.noteWindow = win

            // tryVerify, not tryCompare(panel, "matchCount", 3): matchCount is
            // deliberately NOT reset when the highlighting is cleared (the
            // panel is still open, its count still reads "1 of 3"), so it
            // already says 3 here and a tryCompare on it would pass instantly —
            // before the re-search debounce has re-pushed anything. Waiting on
            // the highlighting itself is what actually proves it came back.
            tryVerify(() => win.docHandler.searchHighlightRanges().length === 3, 1000,
                      "the highlighting comes back with the search when the note is focused again")
        }

        // ---- Replacing ----

        // Replacing is strictly opt-in via the Replace checkbox: while it's
        // unchecked the replace field and both buttons are disabled, and even a
        // direct replaceCurrent() call must not touch the document.
        function test_replace_controls_are_inert_until_the_replace_checkbox_is_checked() {
            const win = makeWindow("frp-gate", 86, "alpha beta alpha\n\ngamma alpha")
            const editor = win.editor

            editor.cursorPosition = 0
            panel.openFind() // find mode — Replace checkbox unchecked
            panel.findField.text = "alpha"
            panel.recompute(0)
            compare(panel.matchCount, 3, "precondition: there IS a current match to (not) replace")

            compare(panel.replaceChecked, false)
            compare(panel.replaceField.enabled, false)
            compare(panel.replaceButton.enabled, false)
            compare(panel.replaceAllButton.enabled, false)

            const textBefore = editor.text
            panel.replaceField.text = "OMEGA"
            panel.replaceCurrent()
            compare(editor.text, textBefore,
                    "replaceCurrent() with the checkbox unchecked must be a no-op")

            // Checking the box arms everything, and replaceCurrent() then
            // replaces exactly the current (first) match — nothing else.
            panel.replaceCheckBox.checked = true
            compare(panel.replaceField.enabled, true)
            compare(panel.replaceButton.enabled, true)
            compare(panel.replaceAllButton.enabled, true)

            panel.replaceCurrent()
            compare(editor.text, textBefore.replace("alpha", "OMEGA"),
                    "only the current match is replaced; the other two occurrences stay")
            compare(panel.matchCount, 2,
                    "the match list re-searches right away, so the count reflects the replacement")
        }

        function test_replace_all_replaces_every_occurrence_and_undoes_as_one_step() {
            const win = makeWindow("frp-all", 87, "alpha beta alpha\n\ngamma alpha")
            const editor = win.editor

            editor.cursorPosition = 0
            panel.openReplace()
            panel.findField.text = "alpha"
            panel.replaceField.text = "OMEGA"
            panel.recompute(0)
            compare(panel.matchCount, 3, "precondition: all three occurrences are found")

            const textBefore = editor.text

            panel.replaceAll()
            compare(editor.text, textBefore.split("alpha").join("OMEGA"))
            compare(panel.matchCount, 0, "nothing left to find once every occurrence is replaced")

            // A single undo — not one per occurrence — restores the whole
            // document, because the engine wraps replace-all in one edit block.
            // Deliberately ONE undo() call, no drain loop.
            editor.undo()
            compare(editor.text, textBefore,
                    "replace-all must be one undo step, not three")
        }

        // ---- Singleton lifecycle: following the focused note ----

        // One panel serves every note, so the tracked note can change under it.
        // The search term deliberately survives that — a find bar that follows
        // you is the whole point of there being exactly one — but the results
        // must be re-derived from the note the user is actually looking at now.
        function test_the_panel_follows_the_tracked_note_and_researches_the_new_one() {
            const winA = makeWindow("frp-follow-a", 94, "alpha beta alpha\n\ngamma alpha")

            panel.openFind()
            panel.findField.text = "alpha"
            panel.recompute(0)
            compare(panel.matchCount, 3, "precondition: three matches in the first note")

            // makeWindow() retargets the panel at the new note, exactly as
            // Main.qml's activeNoteWindow tracking would on a focus change.
            const winB = makeWindow("frp-follow-b", 95, "alpha, once")

            compare(panel.findField.text, "alpha", "the search term follows the user to the new note")
            // The matches are re-derived from the newly tracked note, not stale.
            tryCompare(panel, "matchCount", 1)
            compare(winB.editor.selectionStart, winB.editor.text.indexOf("alpha"),
                    "and the new note's own match is selected")
        }

        // Focus moving to something that is neither a note nor the app's chrome
        // (another app entirely) clears the tracked note. The panel must stop
        // showing itself then — but WITHOUT throwing away the search, so
        // returning to the note picks up where the user left off.
        function test_losing_the_tracked_note_hides_the_panel_without_discarding_the_search() {
            const win = makeWindow("frp-untrack", 96, "alpha beta alpha\n\ngamma alpha")

            panel.openFind()
            panel.findField.text = "alpha"
            panel.recompute(0)
            verify(panel.background.visible, "precondition: the panel is really showing content")

            panel.noteWindow = null
            wait(80)

            compare(panel.content.visible, false, "nothing is shown while no note is tracked")
            compare(panel.open, true,
                    "but the panel is still logically open — this is not the user closing it")
            compare(panel.findField.text, "alpha", "and the search term is untouched")

            panel.noteWindow = win
            wait(80)

            compare(panel.content.visible, true, "refocusing the note brings the same search back")
            tryCompare(panel, "matchCount", 3)
        }

        // ---- Window mechanics ----

        // The panel is shown exactly once (at startup) and never hidden again —
        // repeatedly mapping/unmapping a window while a note holds Wayland
        // keyboard focus was found to disrupt that note's own text input (see
        // FindReplacePanel.qml's Component.onCompleted). "Closed" is instead
        // achieved by making its content invisible and parking it off-screen,
        // which is what these tests check rather than panel.visible
        // (permanently true after that first show()).
        function test_the_panel_window_stays_mapped_whether_open_or_closed() {
            const win = makeWindow("frp-mapped", 98, "alpha")

            compare(panel.visible, true, "the window itself is mapped from startup onward")

            panel.openFind()
            compare(panel.visible, true)
            compare(panel.content.visible, true)

            panel.close()
            compare(panel.visible, true,
                    "closing hides the CONTENT — the window is never unmapped")
            compare(panel.content.visible, false)
            compare(panel.background.visible, false)
        }

        // Bug class found live elsewhere in this app ("invisible windows
        // blocking input in some cases"): a mapped Wayland surface eats clicks
        // regardless of whether its QML content is invisible, so hidden content
        // alone is not enough to let a click through to whatever is underneath.
        //
        // What's checked here is the WIRING (which window is told to stop
        // accepting input, and when). Whether the compositor then really routes
        // the click elsewhere is not observable offscreen — QWindow::setMask
        // has no effect under the offscreen QPA plugin at all — and belongs to
        // a real Wayland session. The off-screen park is deliberately NOT what
        // this rests on: it is async, unordered, and can no-op outright.
        function test_the_panel_stops_accepting_pointer_input_once_it_is_closed() {
            const win = makeWindow("frp-mask", 97, "alpha")

            panel.openFind()
            wait(80)
            verify(panel.background.visible, "precondition: the panel is really showing content")

            inputMaskSpy.clear()
            panel.close()
            wait(80)

            compare(lastInputMaskFor(panel), false,
                    "a closed, still-mapped panel must stop accepting input rather than silently eat clicks meant for whatever is underneath it")

            inputMaskSpy.clear()
            panel.openFind()
            wait(80)

            compare(lastInputMaskFor(panel), true,
                    "an open panel's own fields and buttons must be clickable again")
        }

        // The window is a fixed size on purpose (see wantedWidth/wantedHeight):
        // binding a Window's size to its content risks committing a
        // 0-dimension geometry on the first frame, which is an xdg-shell
        // protocol error that kills the app. enforceSize() is the guard that
        // puts the size back if the platform itself writes it — and it must
        // restore a real BINDING, not freeze the value.
        function test_the_panel_is_a_fixed_size_window_that_self_corrects_a_clobbered_size() {
            compare(panel.width, panel.wantedWidth)
            compare(panel.height, panel.wantedHeight)

            // Stands in for a stale compositor configure/ack landing late — the
            // clobber that was observed live on the toolbar next door.
            //
            // The wait is not incidental: a window's size change reaches QML
            // via a platform geometry-change event, delivered asynchronously,
            // so the guard fires on the next event-loop pass rather than inside
            // this assignment. That is the same path a real stale ack arrives
            // on, which is exactly why the guard hangs off onWidthChanged
            // instead of trying to intercept the write itself.
            panel.width = 1
            wait(80)
            compare(panel.width, panel.wantedWidth,
                    "a clobbered width is put straight back")

            panel.height = 1
            wait(80)
            compare(panel.height, panel.wantedHeight,
                    "and so is a clobbered height")
        }

        // Placement (reposition()): the card sits above the tracked note,
        // horizontally centered on it, and follows the note as it moves.
        //
        // Every placement test below positions its note RELATIVE to the note's
        // own screen (screen.virtualX/virtualY) rather than at absolute
        // coordinates. That is load-bearing on a multi-monitor layout, where a
        // screen can legitimately start at a negative virtual coordinate: a
        // hardcoded 300,300 would land on a different screen than the one
        // reposition() clamps against, and the expectations would be computed
        // from the wrong bounds.
        //
        // The gap between note and card is 8px, spelled literally here rather
        // than read from the implementation — a test that recomputes the value
        // it is checking cannot catch that value changing.
        function test_the_panel_anchors_centered_above_the_tracked_note() {
            const w = makeWindow("place-above", 0, "hello world")
            const screen = w.screen
            verify(screen !== null)

            // Well clear of the top edge, so the PREFERRED placement is what's
            // exercised here and not the fallback (which has its own test).
            w.noteX = screen.virtualX + 300
            w.noteY = screen.virtualY + 300
            w.noteWidth = 260
            w.noteHeight = 260

            activateWindow(w)
            keyClick(Qt.Key_F, Qt.ControlModifier)
            tryCompare(panel, "open", true)

            // tryCompare, not compare: reposition() runs off a 50ms debounce
            // (repositionDebounce), which exists to absorb the stale-screen
            // race Wayland produces on show.
            tryCompare(panel, "y", w.noteY - panel.wantedHeight - 8,
                       2000, "the card rests just above the note's top edge")
            compare(panel.x,
                    Math.round(w.noteX + w.noteWidth / 2 - panel.wantedWidth / 2),
                    "and is centered on the note's width, not aligned to an edge")
        }

        function test_the_panel_falls_back_below_the_note_when_there_is_no_room_above() {
            const w = makeWindow("place-below", 0, "hello world")
            const screen = w.screen
            verify(screen !== null)

            // Flush against the screen's top edge: the card cannot fit above
            // without going off-screen, so the below-the-note fallback is the
            // only placement left.
            w.noteX = screen.virtualX + 300
            w.noteY = screen.virtualY
            w.noteWidth = 260
            w.noteHeight = 260

            activateWindow(w)
            keyClick(Qt.Key_F, Qt.ControlModifier)
            tryCompare(panel, "open", true)

            tryCompare(panel, "y", w.noteY + w.noteHeight + 8,
                       2000, "with no room above, the card goes below the note instead")
            compare(panel.x,
                    Math.round(w.noteX + w.noteWidth / 2 - panel.wantedWidth / 2),
                    "and is still centered on the note")
        }

        // The centering math alone puts the card off-screen for a note near a
        // side edge — the card (368px) is wider than a small note, so half of
        // it hangs past the note on each side. Clamping is what keeps it
        // reachable.
        function test_the_panel_is_clamped_onto_the_screen_near_a_side_edge() {
            const w = makeWindow("place-clamp", 0, "hello world")
            const screen = w.screen
            verify(screen !== null)

            w.noteX = screen.virtualX
            w.noteY = screen.virtualY + 300
            w.noteWidth = 260
            w.noteHeight = 260

            activateWindow(w)
            keyClick(Qt.Key_F, Qt.ControlModifier)
            tryCompare(panel, "open", true)

            // Centering wants x = 0 + 130 - 184 = -54, i.e. off the left edge.
            tryCompare(panel, "x", screen.virtualX,
                       2000, "a card that would hang off the left edge is pulled back onto the screen")
        }

        // The panel is repositioned by the tracked note's MODEL-backed
        // noteX/noteY, not its Window.x/y — per NoteWindow.qml's architecture
        // notes the latter are never updated after a compositor-driven drag on
        // Wayland, and the toolbar next door shipped exactly that bug.
        function test_the_panel_follows_the_tracked_note_when_the_note_moves() {
            const w = makeWindow("place-follow", 0, "hello world")
            const screen = w.screen
            verify(screen !== null)

            w.noteX = screen.virtualX + 300
            w.noteY = screen.virtualY + 300
            w.noteWidth = 260
            w.noteHeight = 260

            activateWindow(w)
            keyClick(Qt.Key_F, Qt.ControlModifier)
            tryCompare(panel, "open", true)
            tryCompare(panel, "y", w.noteY - panel.wantedHeight - 8, 2000)

            // Stands in for a finished drag: mergeLivePosition writes the
            // settled position back into the model, which is what these
            // properties are.
            //
            // Moved LEFT, deliberately: this test is about the card tracking
            // the note, so both positions have to keep the card clear of the
            // screen edges — the clamp would otherwise pin x and mask whether
            // the centering followed at all. (Which it did, first time around:
            // a note near the right edge parked the card at the clamp instead.)
            w.noteX = screen.virtualX + 150
            w.noteY = screen.virtualY + 400

            tryCompare(panel, "y", w.noteY - panel.wantedHeight - 8,
                       2000, "the card re-anchors to the note's new position")
            compare(panel.x,
                    Math.round(w.noteX + w.noteWidth / 2 - panel.wantedWidth / 2),
                    "staying centered on it")
        }
    }
}
