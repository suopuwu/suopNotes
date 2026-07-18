import QtQuick
import QtQuick.Controls
import QtQuick.Window

// FindReplacePanel — the single, app-wide floating find/replace card, owned
// by Main.qml and bound to whichever note currently has OS focus via the
// `noteWindow` property. All matching/replacing is delegated to that note's
// DocumentHandler search API (searchMatches / searchPatternValid /
// replaceSearchMatch / replaceAllMatches); this file only owns the UI state:
// which match is "current", debounced re-searching, and keyboard handling.
//
// A genuine top-level Window, not an Item inside NoteWindow's own tree (which
// is what the first version of this feature was): a note enforces no minimum
// size, and this card is ~370px wide, so it simply did not fit inside a small
// note at all — it either overflowed past the note's real window surface
// (unclickable/clipped, since each note is its own separate Wayland window) or
// covered the very text being searched. Exactly the same reasoning, and the
// same resolution, as SelectionToolbar.qml's own header comment records for
// the formatting toolbar. Being an app-wide singleton (rather than one
// instance per open note) is also what keeps it to ONE permanently-mapped
// window — see Component.onCompleted for why mapping on demand is not an
// option.
//
// UNLIKE the four windows in SelectionToolbar.qml, this one holds keyboard
// focus: it has real TextFields, and the user types into them for as long as
// they are searching. That has one consequence worth knowing before touching
// Main.qml: focusing this window DEACTIVATES the note, and Main.qml's
// activeNoteWindow tracking would clear the tracked note 250ms later — pulling
// `noteWindow` out from under this panel mid-keystroke — if its
// noteOrChromeActive()/noteOrChromeHovered() predicates did not count this
// window as "in use" alongside the toolbar. They do; keep it that way.
//
// Properties (Main.qml sets/binds these):
//   noteWindow — the tracked NoteWindow, or null when no note has focus.
//                Everything (docHandler/editor/flick below) derives from it.
//   open       — whether the user has the panel open. Content shows only while
//                open AND a note is tracked; see wantsVisible.
//   replaceChecked — alias to the Replace checkbox's checked state; the replace
//                field and both replace buttons are enabled ONLY while this is
//                checked (find-only by default)
//
// Functions (Main.qml calls these, from the tracked note's own signals):
//   openFind()    — show with the Replace checkbox unchecked
//   openReplace() — show with the Replace checkbox checked
//   close()       — hide, hand focus back to the note, emit closed()
//
// Behavior notes:
//   - Search is case-insensitive always (v1 decision — no case checkbox);
//     caseSensitive=false is passed on every engine call.
//   - Next/prev cycle with wraparound; opening anchors the current match at
//     the first match at/after the editor's cursor/selection.
//   - Enter in the find field = next, Shift+Enter = prev; Enter in the replace
//     field = replace current; Escape closes.
//   - The tracked note's selection stays visible while this window holds focus
//     because the editor sets persistentSelection (NoteWindow.qml) — that is
//     what makes "the current match stays highlighted while you type in the
//     find field" work at all.
Window {
    id: panel

    // Scope the panel acts on — the currently-focused NoteWindow, or null when
    // no note has focus. Sourced from Main.qml, the one place that observes
    // every NoteWindow's own activeChanged. Same convention as
    // SelectionToolbar.qml's own `noteWindow`.
    property var noteWindow: null

    // Whether the user has opened the panel. NOT the same thing as "is
    // anything showing" — see wantsVisible.
    property bool open: false

    property alias replaceChecked: replaceCheck.checked

    signal closed()

    // The note's pieces this panel drives, named once here rather than
    // re-spelling `panel.noteWindow.docHandler` at each of the dozen call
    // sites below (SelectionToolbar.qml does the latter; at this many uses it
    // reads worse). Null whenever no note is tracked — every function below
    // bails on !panel.noteWindow before touching them.
    //
    // TRAP: do NOT read these from inside onNoteWindowChanged. A binding and an
    // on-handler listening to the same change are not ordered against each
    // other (see SelectionToolbar.qml:865-874 for the same warning), so that
    // handler reads `panel.noteWindow.editor` directly instead.
    readonly property var docHandler: noteWindow ? noteWindow.docHandler : null
    readonly property var editor: noteWindow ? noteWindow.editor : null
    readonly property var flick: noteWindow ? noteWindow.editorFlick : null

    // Live search state, set imperatively by recompute() below (not
    // declarative bindings — the engine calls are only correct to re-run when
    // an actual search-relevant event fires, which is exactly what the debounce
    // timer collects).
    property var _matches: []
    property int matchCount: 0
    property int currentMatchIndex: -1
    property bool patternValid: true
    // The docHandler this panel last pushed match highlighting into, or null.
    // Held as a direct reference rather than re-deriving it from noteWindow at
    // clear time because the two disagree exactly when it matters: by the time
    // focus has moved to another note, `docHandler` already points at the NEW
    // note, and the note that still has stale highlights painted on it is the
    // one only this property remembers. It is also the reason _clearHighlight()
    // is safe to call from onNoteWindowChanged, which must never read the
    // derived docHandler binding (see its own TRAP note).
    property var _highlightedDocHandler: null

    // Editor position the next recompute picks its current match at/after —
    // kept at the current match's own start while one is selected, so retyping
    // the pattern or editing the note stays near where the user already was
    // instead of snapping back to the top.
    property int _anchor: 0

    // Whether this window should currently be showing anything at all: the
    // user has it open AND there is a note to search. Losing the tracked note
    // (focus moved to another app entirely) hides the card without discarding
    // `open` — refocusing the note brings the panel back with the search term
    // still in it, which is the same "the chrome follows the focused note"
    // behavior the toolbar already has.
    readonly property bool wantsVisible: noteWindow !== null && open

    // Whether the pointer is anywhere over this window — Main.qml treats this
    // as a keep-alive signal for the tracked note exactly like the toolbar's
    // own `hovered`.
    readonly property alias hovered: panelHoverHandler.hovered
    HoverHandler { id: panelHoverHandler }

    // Exposed for tests only — the interactable pieces themselves, matching
    // NoteHud.qml's readonly-item-alias testability pattern. Deliberately
    // ITEMS, never a pass-through to a child's bound value (e.g. a .color or
    // .enabled): see NoteHud.qml's pinGlyphItem comment for the AOT staleness
    // trap a value alias hits under qmlcachegen.
    readonly property alias findField: findField
    readonly property alias replaceField: replaceField
    readonly property alias replaceCheckBox: replaceCheck
    readonly property alias regexCheckBox: regexCheck
    readonly property alias matchCountLabel: matchCountLabel
    readonly property alias prevButton: prevButton
    readonly property alias nextButton: nextButton
    readonly property alias replaceButton: replaceButton
    readonly property alias replaceAllButton: replaceAllButton
    readonly property alias closeButton: closeButton
    readonly property alias background: background
    readonly property alias content: content

    // Matched by title (not wmclass alone, which is shared app-wide) by the
    // KWin rule/scripts below — same convention NoteWindow.qml's own
    // "suopNotes-" + noteId titles use, just with a fixed, non-note id since
    // this is a single app-wide singleton.
    //
    // "findReplacePanel" is registered in PositionEventService's
    // pseudoNoteIds() as a result. That is NOT optional: KWin's watcher script
    // hooks every window whose caption starts with "suopNotes-" and, on an
    // interactive move, feeds the caption's remainder to mergeLivePosition as
    // if it were a real note id — creating a phantom note in the persisted
    // OpenNotes list. This panel has no drag handle of its own, but the
    // compositor can still start an interactive move on any window (alt+drag),
    // and that bug has already shipped twice here (selectionToolbar, then
    // columnOptionsPopup) — so the exclusion is registered up front rather
    // than after it happens a third time.
    title: "suopNotes-findReplacePanel"

    // Qt.WindowDoesNotAcceptFocus is deliberately absent, and must stay absent:
    // SelectionToolbar.qml records (its own flags comment) that on this
    // compositor the flag silently swallowed the pointer click itself, not just
    // keyboard focus. This window needs real keyboard focus anyway — it is the
    // only one in the app with text fields the user types into.
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    color: "transparent"

    // FIXED, not derived from content's implicitWidth/implicitHeight. Two
    // reasons, both learned next door in SelectionToolbar.qml: a Window whose
    // size binds to content can commit a 0-dimension geometry on its first
    // frame, which is an xdg-shell protocol error that kills the Wayland
    // connection and takes the app with it; and any resize of a mapped window
    // is a real client<->compositor negotiation, two of which racing can leave
    // the surface at a stale size. This card's layout is a constant two rows,
    // so a constant size costs nothing and sidesteps both outright.
    //
    // Even numbers: reposition()'s math and the compositor both round to whole
    // pixels, and SelectionToolbar.qml records a half-pixel mismatch surfacing
    // from an odd length. Width covers row 1 (the wider of the two): 130 find +
    // 66 count + 22 prev + 22 next + 64 regex + 22 close + 5*6 spacing + 12
    // margins = 368. Height: 22 + 4 spacing + 22 + 12 margins = 60.
    readonly property int wantedWidth: 368
    readonly property int wantedHeight: 60

    width: wantedWidth
    height: wantedHeight

    // Shown exactly ONCE, here, and never hidden/shown again. SelectionToolbar
    // .qml records the reason at length: repeatedly mapping/unmapping a window
    // while a note holds Wayland keyboard focus disrupts that note's own text
    // input (cursor flicker, dropped keystrokes) every single time, regardless
    // of flags — the disruption comes from mapping a surface at all. So this
    // window maps at startup, before any note has been focused, and "hidden"
    // for the rest of the session means invisible CONTENT plus an off-screen
    // park (see _syncVisible), never show()/hide().
    //
    // That is also precisely why this panel is a singleton rather than one per
    // note: N notes would mean N windows all mapped at startup, forever.
    visible: false
    Component.onCompleted: {
        // Seeds the KWin rule (same mechanism NoteWindow's pin feature uses)
        // Force-keeping this window above others. Seeded at offscreenPoint(),
        // not an on-screen throwaway: this is an "Apply Initially" rule in
        // effect from the very first show() below, and SelectionToolbar.qml
        // records that an on-screen seed produced a real once-per-session flash
        // at the wrong spot before the first reposition landed.
        const seed = positionManager.offscreenPoint()
        positionManager.applyPosition("findReplacePanel", panel.title, seed.x, seed.y, true)

        // The hide()+show() is deliberate, not redundant: KWin does not
        // retroactively apply a freshly-written above/Force rule to a window
        // that already exists — only a re-created one re-evaluates it.
        // NoteWindow.setPinned() documents the same requirement.
        panel.show()
        panel.hide()
        panel.show()
        panel.updateBlur()
        // AFTER the show/hide/show, never before: hide() destroys the
        // wl_surface and the next show() creates a fresh one, and an input
        // region is per-surface state — a mask set before that cycle would be
        // set on a surface that no longer exists. Nothing is tracked yet, so
        // "blocks all input" is the correct starting state (background.visible
        // starts false to match).
        windowInputMask.setAcceptsInput(panel, false)

        // Deliberately NOT parking here, unlike every real hide below:
        // positionManager.moveWindow() spawns an untracked thread per call with
        // no ordering guarantee between calls, so a park queued at startup can
        // land AFTER the first real show and drag the just-shown panel back
        // off-screen. The rule above already seeds it off-screen, so there is
        // no on-screen resting spot left to correct.
    }

    // Returns a point guaranteed to fall outside the combined virtual desktop
    // of every connected screen, regardless of monitor arrangement (including
    // Parks this window where no monitor can show it. Load-bearing for
    // VISIBILITY ONLY — never for input correctness: moveWindow() is async,
    // spawns a fresh untracked thread per call, and its completions arrive in
    // COMPLETION order rather than call order, so a park can land before the
    // show it was meant to undo; it also no-ops entirely if KWin scripting is
    // unreachable. Input correctness is windowInputMask's job instead (see
    // background.onVisibleChanged), which is synchronous and client-side. The
    // mask stops clicks, the park stops pixels.
    function parkOffscreen() {
        const pt = positionManager.offscreenPoint()
        panel.x = pt.x
        panel.y = pt.y
        positionManager.moveWindow(panel.title, pt.x, pt.y)
    }

    // Mirrors the tracked note's real compositor blur-behind, exactly like the
    // toolbar does. Gated on background.visible (the real "is anything showing"
    // flag) rather than wantsVisible: blur is an immediate compositor effect on
    // the window's CURRENT geometry, and SelectionToolbar.qml records that
    // enabling it any earlier put a blurred patch at the stale position before
    // the content ever appeared.
    function updateBlur() {
        const on = background.visible && panel.noteWindow
            ? panel.noteWindow.noteAppearance.blurEnabled
            : false
        blurEffect.applyTo(panel, on, background.radius)
    }

    // Re-derives the blur region whenever this window's size changes — the
    // region KWindowEffects blurs is tied to the window's current dimensions.
    // (This window is fixed-size, so in practice this only fires if the
    // platform itself writes width/height — which is exactly the clobber
    // enforceSize() exists to catch.)
    onWidthChanged: {
        panel.enforceSize()
        panel.updateBlur()
    }
    onHeightChanged: {
        panel.enforceSize()
        panel.updateBlur()
    }

    // Self-correction for a platform-level geometry clobber (a stale
    // configure/ack landing late). Observable at the QML level despite being a
    // platform write: SelectionToolbar.qml records `width` ITSELF dropping to 1
    // while the QML feeding it never changed.
    //
    // Qt.binding(), NOT a plain `width = wantedWidth`: a bare imperative
    // assignment to a property that currently has a binding DESTROYS that
    // binding, freezing width at whatever this guard last wrote — turning a
    // transient clipped surface into a permanent one, i.e. a worse version of
    // the bug being fixed.
    //
    // Converges rather than recursing: the corrective assignment re-fires
    // onWidthChanged, but width already equals wantedWidth by then, so the
    // guard makes no further write. The !== check is what makes that true — it
    // must stay.
    function enforceSize() {
        if (panel.width !== panel.wantedWidth)
            panel.width = Qt.binding(() => panel.wantedWidth)
        if (panel.height !== panel.wantedHeight)
            panel.height = Qt.binding(() => panel.wantedHeight)
    }

    // The one place that flips this window between "showing" and "parked".
    // Takes `showing` as an argument rather than reading wantsVisible, because
    // both call sites are on-handlers for the very changes wantsVisible depends
    // on, and a binding is not ordered against an on-handler for the same
    // change (SelectionToolbar.qml hit this and documents it).
    function _syncVisible(showing) {
        background.visible = showing
        content.visible = showing
        content.enabled = showing
        if (showing) {
            // Debounced rather than called straight through — see
            // repositionDebounce for the stale-screen race. The window is still
            // parked off-screen at this instant, so the ~50ms + KWin round-trip
            // before it lands is invisible rather than a flash at a stale spot.
            repositionDebounce.restart()
        } else {
            // Dropped, not left pending: a reposition landing after the park
            // would drag the panel back on-screen for a note that is no longer
            // tracked.
            repositionDebounce.stop()
            panel.parkOffscreen()
            // The one place match highlighting comes off, hooked here for the
            // same reason background.onVisibleChanged owns the input mask:
            // every path that stops showing this card — close(), losing the
            // tracked note to another app — funnels through here, so one hook
            // cannot drift out of sync with a call site someone forgets. The
            // highlighting only means anything while the card explaining it is
            // on screen; the current match's own SELECTION deliberately
            // survives (see close()).
            panel._clearHighlight()
        }
        panel.updateBlur()
    }

    onOpenChanged: panel._syncVisible(panel.noteWindow !== null && panel.open)

    onNoteWindowChanged: {
        // Computed fresh, NOT read from wantsVisible — see _syncVisible.
        const tracking = (panel.noteWindow !== null)
        panel._syncVisible(tracking && panel.open)
        // Focus moved to a DIFFERENT note while the panel is open: re-search
        // the note the user is actually looking at now, anchored at its own
        // cursor. The find term deliberately survives — a find bar that follows
        // focus is the point of a single app-wide panel.
        //
        // Deferred through searchDebounce rather than calling recompute()
        // straight from here, and that is load-bearing: recompute() reads the
        // derived docHandler/editor bindings, which are NOT ordered against
        // this handler (see their own TRAP note). Calling it inline searched
        // the PREVIOUS note's document and selected the match in it — caught by
        // test_the_panel_follows_the_tracked_note_and_researches_the_new_one.
        // Letting the existing one-shot debounce fire it means the bindings
        // have settled by the time it runs.
        //
        // The anchor is captured HERE though, from noteWindow.editor directly
        // (never the panel.editor binding, for the same reason): the debounce
        // fires recompute(_anchor), and _anchor otherwise still holds an offset
        // into the note we just left.
        if (tracking && panel.open) {
            panel._anchor = panel.noteWindow.editor.selectionStart
            searchDebounce.restart()
        }
    }

    // Re-anchors to the tracked note whenever its geometry actually changes.
    //
    // noteX/noteY (model-backed), NOT noteWindow.x/y: per NoteWindow.qml's
    // architecture notes, plain Window.x/y are never updated after a real
    // compositor-driven drag on Wayland, so they would never fire here at all.
    // That indirection makes this a drag-END trigger for free. noteWidth/
    // noteHeight are client-side and DO update per frame during a resize —
    // repositionDebounce absorbs that into one KWin round-trip per gesture.
    Connections {
        target: panel.noteWindow
        function onScreenChanged() { if (panel.wantsVisible) repositionDebounce.restart() }
        function onNoteXChanged() { if (panel.wantsVisible) repositionDebounce.restart() }
        function onNoteYChanged() { if (panel.wantsVisible) repositionDebounce.restart() }
        function onNoteWidthChanged() { if (panel.wantsVisible) repositionDebounce.restart() }
        function onNoteHeightChanged() { if (panel.wantsVisible) repositionDebounce.restart() }
    }

    // The tracked note's own blur toggle lives in PerNoteSettingsPopup, which
    // is reachable without losing focus — so the CURRENTLY tracked note's
    // blurEnabled can flip under us. A `target: panel.noteWindow.noteAppearance`
    // binding re-targets itself automatically as noteWindow changes, including
    // to null (Connections simply goes inert with a null target).
    Connections {
        target: panel.noteWindow ? panel.noteWindow.noteAppearance : null
        function onBlurEnabledChanged() { panel.updateBlur() }
    }

    // Coalesces the reposition calls that Wayland's async QWindow::screen()
    // routinely produces in pairs (one from the tracking change with a STALE
    // screen, one from onScreenChanged once the real wl_surface enter/leave
    // lands), and the per-frame storm during a resize drag, into a single call
    // against settled state. One-shot, restarted by real events — never
    // `repeat: true`, which this project bans outright (CLAUDE.md); it costs
    // nothing while idle. 50ms is well inside the ~60-100ms KWin round-trip
    // moveWindow() needs anyway, so it adds no perceptible latency.
    Timer {
        id: repositionDebounce
        interval: 50
        repeat: false
        onTriggered: panel.reposition()
    }

    // Anchors the card just ABOVE the tracked note, horizontally centered on
    // it, falling back to below when there is no room above, and clamped into
    // the screen either way.
    //
    // Above and centered is a deliberate reversal of this file's first version
    // (below, right-edges-aligned), and it knowingly gives up the one thing
    // that bought: SelectionToolbar's reposition() also prefers the note's TOP
    // edge and also centers there, so while a selection is live BOTH chrome
    // windows want the same strip of screen and will overlap. That was the
    // stated reason for preferring below here. It is now a smaller cost than
    // the placement being wrong every time: the toolbar only exists while
    // there is a selection, whereas the panel is up for as long as the user is
    // searching. If the overlap becomes a real annoyance, the fix is for
    // Main.qml (which owns both windows) to offset one of them — not for this
    // file to go back to a worse resting spot.
    //
    // Reads the model-backed noteX/noteY/noteWidth/noteHeight, NOT
    // noteWindow.x/y/width/height — per NoteWindow.qml's architecture notes,
    // plain Window.x/y are never updated after a compositor-driven drag on
    // Wayland, and the toolbar reproduced exactly that bug (landing at whatever
    // stale x/y the Window object happened to default to).
    //
    // The result is applied TWO ways: panel.x/y (for this file's own math and
    // for tests) AND positionManager.moveWindow() — confirmed live next door
    // that the plain property assignment alone is not honored by KWin for an
    // already-mapped top-level window; the KWin script is what actually moves
    // it.
    function reposition() {
        if (!panel.noteWindow)
            return

        const note = panel.noteWindow
        const screen = note.screen
        const gap = 8

        // Rounded, unlike the plain subtraction the right-aligned version used:
        // a centered X halves noteWidth, so an odd-width note lands this on a
        // half pixel — exactly the mismatch wantedWidth's own even-number note
        // above records surfacing from odd lengths.
        let targetX = Math.round(note.noteX + note.noteWidth / 2 - panel.wantedWidth / 2)
        let targetY = note.noteY - panel.wantedHeight - gap

        if (screen) {
            const minX = screen.virtualX
            const minY = screen.virtualY
            const maxX = screen.virtualX + screen.width
            const maxY = screen.virtualY + screen.height

            const aboveY = note.noteY - panel.wantedHeight - gap
            const belowY = note.noteY + note.noteHeight + gap

            if (aboveY >= minY)
                targetY = aboveY
            else if (belowY + panel.wantedHeight <= maxY)
                targetY = belowY
            else
                targetY = aboveY

            targetX = Math.max(minX, Math.min(targetX, maxX - panel.wantedWidth))
            targetY = Math.max(minY, Math.min(targetY, maxY - panel.wantedHeight))
        }

        panel.x = targetX
        panel.y = targetY
        positionManager.moveWindow(panel.title, targetX, targetY)
    }

    function openFind() { panel._openPanel(false) }
    function openReplace() { panel._openPanel(true) }

    function _openPanel(withReplace) {
        if (!panel.noteWindow)
            return
        replaceCheck.checked = withReplace
        if (!panel.open) {
            panel.open = true
            // A held selection is almost always what the user wants to search
            // for — prefill it. selectAll() below means typing something else
            // just overwrites it either way.
            if (panel.noteWindow.editor.selectedText.length > 0)
                findField.text = panel.noteWindow.editor.selectedText
            // First match at/after the cursor/selection, not the document top —
            // mirrors every editor's find behavior.
            //
            // _anchor is reset here, not just passed to recompute(): one panel
            // now serves every note for the whole session, so the anchor left
            // behind by the LAST search outlives it. recompute() only refreshes
            // _anchor when it actually selects a match, so opening with an
            // empty find field (recompute returns early, no matches) would
            // leave the old note's offset in place — and the first term typed
            // afterwards would then start searching from there instead of from
            // the user's cursor. Found by test_typing_a_term_live_updates...,
            // which jumped to the second match on a fresh open.
            panel._anchor = panel.noteWindow.editor.selectionStart
            panel.recompute(panel._anchor)
        }
        // Already open: just retarget the checkbox above and re-focus.
        //
        // raise() before requestActivate(): requestActivate() alone was found
        // next door not to reliably restore stacking order on this compositor,
        // and forceActiveFocus() on its own only moves focus WITHIN this
        // window — the window itself has to be activated for the field to
        // actually receive keystrokes.
        panel.raise()
        panel.requestActivate()
        findField.forceActiveFocus()
        findField.selectAll()
    }

    function close() {
        if (!panel.open)
            return
        panel.open = false
        searchDebounce.stop()
        // The last match's selection is deliberately left in place — the editor
        // uses persistentSelection, and clearing it would throw away exactly
        // the thing the user just searched for.
        panel._returnFocusToNote()
        panel.closed()
    }

    // Hands focus back to the tracked note — the whole point of closing is to
    // resume editing where the search left off. This is a cross-WINDOW handoff
    // now, so a bare editor.forceActiveFocus() is not enough (that only moves
    // focus within the note's own window, which may not be the active one);
    // it needs the same raise() + requestActivate() ritual SelectionToolbar's
    // runAction() uses after every formatting button.
    function _returnFocusToNote() {
        const note = panel.noteWindow
        if (!note)
            return
        note.raise()
        note.requestActivate()
        note.editor.forceActiveFocus()
    }

    // Re-runs the search and picks the current match: the first one whose start
    // is at/after anchorPos, wrapping to the first match overall if the anchor
    // is past every match. Called directly (open, replace, next/prev never need
    // it) or via searchDebounce below.
    function recompute(anchorPos) {
        searchDebounce.stop()
        if (!panel.open || !panel.noteWindow)
            return
        const pattern = findField.text
        panel.patternValid = panel.docHandler.searchPatternValid(pattern, regexCheck.checked)
        const found = panel.patternValid
            ? panel.docHandler.searchMatches(pattern, regexCheck.checked, false)
            : []
        panel._matches = found
        panel.matchCount = found.length
        if (found.length === 0) {
            panel.currentMatchIndex = -1
            // Pushed even with nothing found — an empty list is what takes the
            // PREVIOUS search's highlighting back off (clearing the find field,
            // or editing the note until the term no longer matches).
            panel._pushHighlight()
            return
        }
        let picked = 0
        for (let i = 0; i < found.length; i++) {
            if (found[i].start >= anchorPos) {
                picked = i
                break
            }
        }
        panel.currentMatchIndex = picked
        // Before _selectCurrent(), so the highlight is already on the document
        // when the editor's own selection lands over the current match.
        panel._pushHighlight()
        panel._selectCurrent()
    }

    // Repaints every match in the tracked note — all of them washed, the
    // current one stronger. Called after anything that changes either the match
    // list or which match is current; the engine side no-ops when neither
    // actually moved, so over-calling this is cheap and under-calling it is
    // what leaves a stale highlight on screen.
    function _pushHighlight() {
        // Whatever we highlighted last is a different note now — take its
        // highlighting off before painting the new one, or the note the user
        // just left keeps a permanent set of match highlights it has no panel
        // to explain them.
        if (panel._highlightedDocHandler && panel._highlightedDocHandler !== panel.docHandler)
            panel._highlightedDocHandler.clearSearchHighlight()
        panel._highlightedDocHandler = panel.docHandler
        if (panel.docHandler)
            panel.docHandler.setSearchHighlight(panel._matches, panel.currentMatchIndex)
    }

    function _clearHighlight() {
        if (panel._highlightedDocHandler)
            panel._highlightedDocHandler.clearSearchHighlight()
        panel._highlightedDocHandler = null
    }

    function nextMatch() { panel._step(1) }
    function prevMatch() { panel._step(-1) }

    // Wraparound cycling: stepping past the last match lands on the first and
    // vice versa.
    function _step(delta) {
        if (panel.matchCount === 0)
            return
        panel.currentMatchIndex =
            (panel.currentMatchIndex + delta + panel.matchCount) % panel.matchCount
        // The match list itself didn't move, but which one is CURRENT did — the
        // one being left drops back to the weaker highlight, the one being
        // entered takes the stronger one.
        panel._pushHighlight()
        panel._selectCurrent()
    }

    function _selectCurrent() {
        if (!panel.noteWindow)
            return
        const m = panel._matches[panel.currentMatchIndex]
        panel._anchor = m.start
        panel.editor.select(m.start, m.end)
        // select() lands the cursor at the match's end, so the editor's own
        // cursorRectangle is exactly the rect to scroll into view — same call
        // NoteWindow's typing/arrow-key handling already uses.
        if (panel.flick)
            panel.flick.ensureCursorVisible(panel.editor.cursorRectangle)
    }

    // Replaces the current match. On success the engine returns the inserted
    // text's own range, and the next current match is the first one at/after
    // that replacement's end — so repeated Enter walks forward through the
    // document. An empty map means the range went stale (the document changed
    // under us); just re-searching is the whole recovery.
    function replaceCurrent() {
        if (!replaceCheck.checked || panel.currentMatchIndex < 0 || !panel.noteWindow)
            return
        const m = panel._matches[panel.currentMatchIndex]
        const inserted = panel.docHandler.replaceSearchMatch(
            m.start, m.end, findField.text, replaceField.text,
            regexCheck.checked, false)
        if (inserted && inserted.end !== undefined)
            panel.recompute(inserted.end)
        else
            panel.recompute(panel._anchor)
    }

    function replaceAll() {
        if (!replaceCheck.checked || !panel.noteWindow)
            return
        panel.docHandler.replaceAllMatches(
            findField.text, replaceField.text, regexCheck.checked, false)
        panel.recompute(panel._anchor)
    }

    // Live-search debounce: one-shot, restarted by each real event (find text
    // edited, regex toggled, the document itself edited) and firing once after
    // they settle — event-driven, never a recurring poll (see CLAUDE.md). Keeps
    // a fast typist from running a full-document search per keystroke.
    Timer {
        id: searchDebounce
        interval: 150
        onTriggered: panel.recompute(panel._anchor)
    }

    // Document edits (typing, undo, our own replaces) invalidate the match list
    // — re-search so the count/current selection never go stale. Only wired
    // while the panel is actually open and a note is tracked; the ternary is
    // what lets the target follow noteWindow (including to null).
    Connections {
        target: panel.noteWindow ? panel.noteWindow.docHandler : null
        enabled: panel.open
        function onContentEdited() { searchDebounce.restart() }
    }

    // NO window-level Shortcut items here, deliberately — not for Escape, and
    // not for Ctrl+F/Ctrl+H either.
    //
    // Bug found by this file's own tests, and it is a nasty one: giving this
    // window its own "Escape"/"Ctrl+F"/"Ctrl+H" Shortcuts SILENTLY KILLED the
    // identical per-note shortcuts in NoteWindow.qml — Ctrl+F stopped opening
    // the panel at all. Qt.WindowShortcut context does NOT reliably keep two
    // windows' shortcuts out of each other's way here: the shortcut map is
    // application-global, both windows matched, and an ambiguous match makes
    // QML's Shortcut emit activatedAmbiguously() INSTEAD of activated(). There
    // is no warning printed and no error — the shortcut simply stops working,
    // which is exactly how the app's main entry point would have regressed
    // unnoticed. (SelectionToolbar.qml's own notes already record avoiding "an
    // ambiguous second Escape Shortcut" for the same reason.)
    //
    // So: Escape in this window is handled by each field's own
    // Keys.onEscapePressed (direct key delivery, no shortcut map involved), and
    // a field always has focus while the panel is open because every button
    // here is focusPolicy: NoFocus. Ctrl+F/Ctrl+H stay the NOTE's shortcuts
    // only — pressing them again while already typing in the find field is a
    // no-op, which costs nothing next to breaking them outright, and the
    // Replace checkbox is right there for switching modes by mouse.

    // Compact dark-card variants of the Basic controls: the app convention is
    // unstyled QQC2 Basic everywhere, but Basic's default light-grey field/
    // button chrome is illegible on this translucent dark card, so these keep
    // Basic's behavior and swap only the visuals — same approach as
    // SelectionToolbar.qml's ToolbarButtonLabel/Highlight components.
    component PanelField: TextField {
        id: field
        // A no-op now that the panel owns its own window (Qt.WindowShortcut is
        // scoped per QWindow, so NoteWindow's "Escape" Shortcut is no longer
        // even a candidate for a key typed in here), and kept deliberately: it
        // is free, and it is the one thing standing between a compositor that
        // routes keys in some way we did not predict and an Escape that silently
        // stops closing the panel. Removing it wants its own live-verified
        // change, not a drive-by.
        Keys.onShortcutOverride: (event) => {
            if (event.key === Qt.Key_Escape)
                event.accepted = true
        }
        font.pixelSize: 12
        color: "#ffffff"
        placeholderTextColor: Qt.rgba(1, 1, 1, 0.45)
        selectionColor: Qt.rgba(0.3, 0.65, 1.0, 0.5)
        selectedTextColor: "#ffffff"
        selectByMouse: true
        implicitHeight: 22
        topPadding: 2
        bottomPadding: 2
        leftPadding: 6
        rightPadding: 6
        opacity: enabled ? 1.0 : 0.35
        background: Rectangle {
            radius: 4
            color: Qt.rgba(1, 1, 1, 0.1)
            border.width: 1
            border.color: field.activeFocus ? Qt.rgba(1, 1, 1, 0.45)
                                            : Qt.rgba(1, 1, 1, 0.18)
        }
    }

    // NoFocus so clicking a button doesn't steal focus from the find/replace
    // field — Enter must keep working right after clicking ›.
    component PanelButton: Button {
        id: btn
        focusPolicy: Qt.NoFocus
        font.pixelSize: 12
        implicitHeight: 22
        topPadding: 0
        bottomPadding: 0
        leftPadding: 6
        rightPadding: 6
        contentItem: Text {
            text: btn.text
            font: btn.font
            color: "#ffffff"
            opacity: btn.enabled ? 1.0 : 0.35
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: InteractableHighlight {
            active: btn.enabled && (btn.hovered || btn.down)
        }
    }

    component PanelCheck: CheckBox {
        id: check
        focusPolicy: Qt.NoFocus
        font.pixelSize: 12
        implicitHeight: 22
        padding: 0
        spacing: 4
        opacity: enabled ? 1.0 : 0.35
        indicator: Rectangle {
            width: 14
            height: 14
            anchors.verticalCenter: parent.verticalCenter
            radius: 3
            color: check.checked ? Qt.rgba(0.3, 0.65, 1.0, 0.9)
                                 : Qt.rgba(1, 1, 1, 0.1)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.35)
            Text {
                anchors.centerIn: parent
                visible: check.checked
                text: "✓"
                color: "#ffffff"
                font.pixelSize: 10
            }
        }
        contentItem: Text {
            text: check.text
            font: check.font
            color: "#ffffff"
            verticalAlignment: Text.AlignVCenter
            leftPadding: check.indicator.width + check.spacing
        }
    }

    // The card itself. Styled after copyToast's dark translucent chip
    // (NoteWindow.qml) rather than the note's own colors, since it floats over
    // a note whose background color/opacity the user can set to anything —
    // unlike the toolbar, whose buttons are bare glyphs that need to mimic the
    // note to stay legible.
    //
    // Starts visible:false and is only ever set imperatively from
    // _syncVisible() — a declarative binding here would race the on-handlers
    // that drive it.
    Rectangle {
        id: background
        visible: false
        anchors.fill: parent
        radius: 8
        color: Qt.rgba(0, 0, 0, 0.6)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.18)

        // Keeps this window's input region in lockstep with whether it is
        // actually showing anything. Hooked to `visible` here rather than
        // called from each site that flips it: this Rectangle's own visibility
        // IS the file's "is this window really showing content" flag (the same
        // one updateBlur() keys off), so one hook on it cannot drift out of
        // sync with a flip site someone forgets to update later.
        //
        // This is what actually stops a parked-but-mapped transparent window
        // from eating clicks meant for whatever is underneath — parkOffscreen()
        // is racy and cannot be relied on for input correctness. See its
        // comment.
        onVisibleChanged: {
            windowInputMask.setAcceptsInput(panel, background.visible)
            panel.updateBlur()
        }
    }

    Item {
        id: content
        visible: false
        enabled: false
        anchors.fill: parent

        Column {
            id: panelColumn
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4

            Row {
                spacing: 6

                PanelField {
                    id: findField
                    width: 130
                    placeholderText: "Find"
                    onTextChanged: if (panel.open) searchDebounce.restart()
                    Keys.onReturnPressed: (event) => {
                        if (event.modifiers & Qt.ShiftModifier)
                            panel.prevMatch()
                        else
                            panel.nextMatch()
                    }
                    Keys.onEnterPressed: (event) => {
                        if (event.modifiers & Qt.ShiftModifier)
                            panel.prevMatch()
                        else
                            panel.nextMatch()
                    }
                    Keys.onEscapePressed: panel.close()
                }

                Text {
                    id: matchCountLabel
                    // Fixed width so the panel doesn't resize as the count's
                    // digits change while typing.
                    width: 66
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    color: panel.patternValid ? "#ffffff" : "#ff8080"
                    text: !panel.patternValid ? "bad pattern"
                        : panel.matchCount === 0 ? "0 of 0"
                        : (panel.currentMatchIndex + 1) + " of " + panel.matchCount
                }

                PanelButton {
                    id: prevButton
                    width: 22
                    text: "‹"
                    font.pixelSize: 14
                    enabled: panel.matchCount > 0
                    onClicked: panel.prevMatch()
                }

                PanelButton {
                    id: nextButton
                    width: 22
                    text: "›"
                    font.pixelSize: 14
                    enabled: panel.matchCount > 0
                    onClicked: panel.nextMatch()
                }

                PanelCheck {
                    id: regexCheck
                    // Explicit width, unlike its Replace twin below: this one
                    // sits in the row that decides the window's own fixed
                    // wantedWidth, so letting it size to its label's font
                    // metrics would silently clip the row on a font where
                    // "Regex" renders wider.
                    width: 64
                    text: "Regex"
                    // Toggling changes what the pattern means — same debounced
                    // re-search as editing the pattern itself.
                    onCheckedChanged: if (panel.open) searchDebounce.restart()
                }

                PanelButton {
                    id: closeButton
                    width: 22
                    text: "✕"
                    onClicked: panel.close()
                }
            }

            Row {
                spacing: 6

                PanelCheck {
                    id: replaceCheck
                    text: "Replace"
                }

                // Replace controls are gated on the checkbox, not hidden — the
                // row keeps its footprint, it just reads (and is) inert until
                // the user opts into replacing. Hiding it would also resize
                // this window, which is exactly the client<->compositor
                // negotiation the fixed wantedWidth/wantedHeight above exist to
                // avoid.
                PanelField {
                    id: replaceField
                    width: 130
                    placeholderText: "Replace with"
                    enabled: replaceCheck.checked
                    Keys.onReturnPressed: panel.replaceCurrent()
                    Keys.onEnterPressed: panel.replaceCurrent()
                    Keys.onEscapePressed: panel.close()
                }

                PanelButton {
                    id: replaceButton
                    text: "Replace"
                    enabled: replaceCheck.checked
                    onClicked: panel.replaceCurrent()
                }

                PanelButton {
                    id: replaceAllButton
                    text: "All"
                    enabled: replaceCheck.checked
                    onClicked: panel.replaceAll()
                }
            }
        }
    }
}
