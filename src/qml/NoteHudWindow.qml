import QtQuick
import QtQuick.Window

// Single, app-wide windowed version of the note ribbon (NoteHud), owned by
// Main.qml and shown for whichever note the pointer has settled on. Shown on
// hover, exactly like the inline ribbon it replaces — but as its own top-level
// window anchored just OUTSIDE the note's top-right edge (a vertical column, so
// it never collides with the toolbar, which sits above the note). Gated by
// Settings.hudAsWindow: when that is off, NoteWindow renders the old inline
// horizontal ribbon over its own text instead, and this window stays
// parked/empty.
//
// This window also owns the hover-TRACKING policy — which note gets the ribbon,
// given raw per-note hover events from Main.qml's delegate. That decision is
// not the trivial "whichever note was hovered last" it looks like, because the
// ribbon sits outside the note and therefore over its neighbour; see
// noteHoverChanged() for the whole story.
//
// A genuine top-level Window, not an Item in a note's tree, for the same
// reasons SelectionToolbar/columnOptionsPopup are (see their headers): a note
// enforces no minimum size, so a fixed ribbon overflowing a tiny note's own
// surface would be clipped/unclickable — and living outside the note is the
// whole point here, since the inline ribbon's problem was covering the note's
// text on small notes.
//
// This window follows the exact same permanently-mapped lifecycle discipline
// the toolbar documents at length: shown once at startup and never
// hidden/shown again (re-mapping a surface while a note holds keyboard focus
// disrupts its text input); "hidden" thereafter is content-invisible +
// parked off-screen + input-region-rejected, never an actual hide(). Read
// SelectionToolbar.qml's own Component.onCompleted / parkOffscreen /
// wantsVisible comments for the full trace-confirmed reasoning behind every
// step below — it is not re-transcribed here.
Window {
    id: hudWindow

    // The note the ribbon is currently showing for, or null. This is the
    // OUTPUT of the hover-tracking policy below, not an input: Main.qml feeds
    // raw per-note hover events in through noteHoverChanged(), and this window
    // decides which of them actually earns the ribbon.
    //
    // The policy lives here rather than in Main.qml (where it started) because
    // this window is its only consumer, and because Main.qml — a tray icon plus
    // an Instantiator over the real note model — cannot be instantiated in a
    // test, so the policy was untestable there. Assigning this property
    // directly still works and is exactly what the placement tests below do:
    // it forces a tracked note, bypassing the policy.
    property var noteWindow: null

    // Whether this permanently-mapped window should currently be showing
    // anything — true exactly while a note is tracked. Same wantsVisible
    // convention the toolbar's popups use.
    readonly property bool wantsVisible: noteWindow !== null

    // Pointer-over-the-ribbon keep-alive signal (see NoteHud's own `hovered`
    // alias): the ribbon sits just outside the note, so the pointer crossing
    // from note to ribbon leaves the note un-hovered — noteClearDebounce below
    // reads this to hold the tracked note across that gap. Main.qml also reads
    // it, as one of the "chrome is in use" terms in noteOrChromeHovered().
    readonly property alias hovered: ribbon.hovered

    // How long a DIFFERENT note has to stay hovered before it takes the ribbon
    // away from the note currently showing it. A property rather than a
    // hardcoded interval so tests can drive the dwell explicitly instead of
    // racing a real timer — same reasoning as NoteHud.closeArmTimeoutMs.
    property int noteSwitchDwellMs: 150

    // The note waiting out noteSwitchDebounce — a candidate for noteWindow that
    // has not proven intent yet. Null whenever no switch is pending.
    property var _pendingNote: null

    // Raw per-note hover events, fed in by Main.qml's delegate. The whole input
    // to the tracking policy.
    //
    // The bug this shape exists to fix (reported live: "with two notes next to
    // each other, you cannot access the left note's top right ribbon due to the
    // right note stealing focus"): reposition() below anchors the ribbon just
    // OUTSIDE the note's right edge, so a note placed to the right of the
    // tracked one sits under the ribbon, and reaching for that ribbon means
    // dragging the pointer across a sliver of the neighbour on the way. This
    // used to hand the ribbon to whichever note reported hover LAST,
    // immediately and unconditionally — so that crossing re-anchored the ribbon
    // to the neighbour, and the ribbon being reached for vanished out from
    // under the pointer. With a note to its right, the left note's ribbon could
    // not be clicked at all.
    //
    // The ASYMMETRY was the bug, not the tracking: clearing was already
    // debounced (noteClearDebounce below) precisely so the note→ribbon gap
    // wouldn't drop tracking — but stealing wasn't, and a steal calls .stop()
    // on that debounce, so the grace period it was owed never even got to fire.
    // Both directions now go through a one-shot timer that RE-CHECKS live hover
    // state when it fires (event-driven, never a poll — see CLAUDE.md), so a
    // note merely passed over keeps its hands off, while a note actually
    // settled on wins the ribbon.
    function noteHoverChanged(note, hovered) {
        if (!hovered) {
            // The pointer left a note that was still waiting out its dwell — it
            // was only ever passing through, so it never earns the ribbon.
            if (hudWindow._pendingNote === note) {
                hudWindow._cancelPendingSwitch()
                // Bug found live ("the ribbon stays forever" — brush a second
                // note and leave it BEFORE the dwell hands it the ribbon):
                // entering that candidate ran the unconditional
                // noteClearDebounce.stop() below, which may have been the only
                // thing scheduled to drop the ORIGINAL tracked note once the
                // pointer left IT. Abandoning the candidate here has to restore
                // that, exactly as noteSwitchDebounce's own abandon path does
                // when the dwell fires — otherwise the ribbon stays stuck on the
                // original note with nothing left to notice the pointer is gone.
                hudWindow._rearmClearIfPointerGone()
            }
            if (hudWindow.noteWindow === note)
                noteClearDebounce.restart()
            return
        }

        noteClearDebounce.stop()

        // Back on the note already being shown (e.g. the pointer returning from
        // that note's own ribbon): nothing to switch to, and any pending steal
        // is now moot.
        if (hudWindow.noteWindow === note) {
            hudWindow._cancelPendingSwitch()
            return
        }

        // Nothing tracked at all — take it immediately, so a first hover still
        // feels instant. This case can skip the dwell precisely because there is
        // no ribbon on screen for it to steal from.
        if (!hudWindow.noteWindow) {
            hudWindow._cancelPendingSwitch()
            hudWindow.noteWindow = note
            return
        }

        // A different note while one is already showing: either a real switch or
        // the pointer transiting on its way to the tracked note's ribbon. Make
        // it prove intent by still being hovered when the timer fires.
        hudWindow._pendingNote = note
        noteSwitchDebounce.restart()
    }

    function _cancelPendingSwitch() {
        hudWindow._pendingNote = null
        noteSwitchDebounce.stop()
    }

    // Re-arm the clear debounce, but only if the pointer is off the tracked
    // note AND off the ribbon — i.e. there is genuinely nothing keeping the
    // ribbon alive. Safe to call speculatively: noteClearDebounce re-checks
    // this same state at fire time, so a call made while the note (or ribbon)
    // is still legitimately hovered simply no-ops when the timer fires. Shared
    // by every "the tracked note may now have nothing scheduled to notice the
    // pointer left it" path — an abandoned switch (both the early-exit and the
    // dwell-fired variants) and the ribbon itself losing hover — so those three
    // can't drift apart on what "the pointer is gone" means.
    function _rearmClearIfPointerGone() {
        if (hudWindow.noteWindow && !hudWindow.noteWindow.hovered && !ribbon.hovered)
            noteClearDebounce.restart()
    }

    // One-shot, restarted only by a real "a different note is hovered" event
    // (never a recurring poll — see CLAUDE.md). The re-check at fire time IS the
    // fix: a pointer that was merely crossing this note on its way to the
    // tracked note's ribbon has, by now, already moved on — so the candidate is
    // no longer hovered and the steal is correctly abandoned.
    Timer {
        id: noteSwitchDebounce
        interval: hudWindow.noteSwitchDwellMs
        repeat: false
        onTriggered: {
            const candidate = hudWindow._pendingNote
            hudWindow._pendingNote = null
            // Deliberately re-reads the note's LIVE hovered property instead of
            // trusting the event that scheduled this: the pointer has had
            // noteSwitchDwellMs to move on, and whether it did is the entire
            // question being asked here.
            if (candidate && candidate.hovered) {
                hudWindow.noteWindow = candidate
                return
            }
            // The steal was abandoned. noteClearDebounce stood down for this
            // pending switch (see its own comment), so the tracked note may have
            // been left meanwhile with nothing left to notice — re-arm it, or
            // the ribbon could stay stuck on a note the pointer is nowhere near.
            hudWindow._rearmClearIfPointerGone()
        }
    }

    // Debounces clearing the tracked note, restarted whenever that note OR the
    // ribbon itself loses hover. Re-checks at fire time rather than just
    // delaying: moving the pointer from the note onto the ribbon un-hovers the
    // note for an instant, and this is what stops the ribbon vanishing out from
    // under the pointer mid-reach.
    Timer {
        id: noteClearDebounce
        interval: 150
        repeat: false
        onTriggered: {
            if (hudWindow.noteWindow
                && (hudWindow.noteWindow.hovered || ribbon.hovered))
                return
            // A switch is already in flight: it is strictly newer information
            // than this timer has, so let it resolve rather than parking the
            // ribbon for the few ms until it does (which reads as a flicker).
            // Only reachable if the pointer's "left the old note" event arrives
            // AFTER the "entered the new note" one — hovering a note stops this
            // timer, so the usual leave-then-enter order never arms both. That
            // ordering isn't ours to rely on, hence the guard; noteSwitchDebounce
            // re-arms this timer if it ends up abandoning the switch.
            if (hudWindow._pendingNote)
                return
            hudWindow.noteWindow = null
        }
    }

    // The ribbon itself just lost hover — re-check whether the note under it
    // should still be tracked. The Timer above only ever checks at the moment it
    // fires and never reschedules itself, so this re-arm is what keeps a note
    // from staying tracked forever after the pointer leaves via the ribbon (the
    // same re-arm the toolbar/panel/menu each do for Main.qml's active-note
    // debounce).
    onHoveredChanged: {
        if (!hudWindow.hovered)
            hudWindow._rearmClearIfPointerGone()
    }

    // Search can't be served from here — the find panel is an app-wide
    // singleton bound to the tracked (active) note, and this ribbon acts on a
    // merely-hovered one. Re-emitted for Main.qml to route (it makes the note
    // active first, then opens the panel against it). Every other ribbon
    // action operates on `noteWindow` directly below, needing nothing from
    // Main.
    signal findRequested(var note)

    // Registered in PositionEventService::pseudoNoteIds() under "noteHud" —
    // NOT optional (see NoteContextMenu.qml's own title comment): KWin's
    // watcher hooks every "suopNotes-" window and, on an interactive move
    // (alt+drag reaches any window), would otherwise feed "noteHud" to
    // mergeLivePosition as a phantom note id.
    title: "suopNotes-noteHud"

    // Same flags and no-fight-for-focus stance as the toolbar (see its own
    // flags comment): Qt.WindowDoesNotAcceptFocus was found to swallow the
    // pointer click itself on this compositor, so this accepts focus like any
    // normal window and Main.qml just counts it as "in use" while active or
    // hovered rather than trying to hand focus back.
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    color: "transparent"

    // The single source of truth for how big this window must be right now —
    // the ribbon's own intrinsic size (vertical + alwaysExpanded, so it is a
    // fixed column with no collapse animation running). Named so enforceSize()
    // can re-assert the exact same expression rather than a drifting copy.
    // Floored to 1 (never 0 — a 0-sized xdg-shell geometry commit is a
    // protocol error that kills the Wayland connection; see the toolbar's own
    // width/height comment) and rounded (window geometry is whole-pixel).
    readonly property int wantedWidth: Math.max(1, Math.round(ribbon.width))
    readonly property int wantedHeight: Math.max(1, Math.round(ribbon.height))
    width: wantedWidth
    height: wantedHeight

    // Shown exactly once, here, and never hidden/shown again — see the
    // toolbar's Component.onCompleted for the full reasoning (re-mapping a
    // surface while a note holds keyboard focus disrupts its text input; the
    // seed is off-screen so the first real show never flashes at the wrong
    // spot; the show/hide/show cycle forces KWin to re-evaluate the freshly
    // written Force-above rule; the input mask is applied AFTER that cycle
    // because it is per-surface state).
    visible: false
    Component.onCompleted: {
        const seed = offscreenPoint()
        positionManager.applyPosition("noteHud", hudWindow.title, seed.x, seed.y, true)
        hudWindow.show()
        hudWindow.hide()
        hudWindow.show()
        updateBlur()
        windowInputMask.setAcceptsInput(hudWindow, false)
    }

    // Verbatim copies of the toolbar's helpers — there is still no shared base
    // component for these permanently-mapped popups (NoteContextMenu.qml notes
    // this is now the fourth hand-copy and that one is overdue); kept
    // consistent with the other three deliberately rather than inventing a
    // fifth variant.
    function offscreenPoint() {
        let maxX = 0
        let maxY = 0
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; i++) {
            maxX = Math.max(maxX, screens[i].virtualX + screens[i].width)
            maxY = Math.max(maxY, screens[i].virtualY + screens[i].height)
        }
        return Qt.point(maxX + 1000, maxY + 1000)
    }

    function parkOffscreen(win) {
        const pt = offscreenPoint()
        win.x = pt.x
        win.y = pt.y
        positionManager.moveWindow(win.title, pt.x, pt.y)
    }

    // Compositor blur-behind is ALWAYS on while the ribbon is showing (feature
    // request: the ribbon should read as glass regardless of the hovered note's
    // own blur toggle — unlike the toolbar, which mirrors the note). Gated on
    // noteWindow !== null, computed fresh here rather than read off the
    // `wantsVisible` property — bugfix: updateBlur() is called synchronously
    // from onNoteWindowChanged below, which is the handler for the very
    // property `wantsVisible` derives from, and QML does not guarantee that
    // binding has already re-evaluated to its new value by the time a
    // same-signal handler's body runs (this is the identical race
    // onNoteWindowChanged's own `tracking` local already works around for
    // ribbon.visible — see its comment). Reading the stale `wantsVisible`
    // here left blur ON, with ribbon.visible already false and the park still
    // in flight, for a real observed window: a blurred, icon-less ghost of
    // the chip floating at the note's last hover spot instead of the ribbon
    // being fully hidden. The blur region's corner radius follows the note's
    // window radius, matching the ribbon chip's own cornerRadius below.
    function updateBlur() {
        blurEffect.applyTo(hudWindow, hudWindow.noteWindow !== null, ribbon.cornerRadius)
    }

    // Re-derives the blur region and self-corrects a platform geometry clobber
    // on every size change — same reasoning, same Qt.binding() re-install (not
    // a bare assignment, which would destroy the binding and freeze the size),
    // as the toolbar's enforceSize(). Also re-runs placement when visible, so
    // the first reposition (fired 50ms after the note is hovered) isn't stuck
    // with a not-yet-laid-out column height.
    function enforceSize() {
        if (width !== wantedWidth)
            width = Qt.binding(() => hudWindow.wantedWidth)
        if (height !== wantedHeight)
            height = Qt.binding(() => hudWindow.wantedHeight)
    }
    onWidthChanged: {
        enforceSize()
        updateBlur()
        if (hudWindow.wantsVisible)
            repositionDebounce.restart()
    }
    onHeightChanged: {
        enforceSize()
        updateBlur()
        if (hudWindow.wantsVisible)
            repositionDebounce.restart()
    }

    // Re-anchors to the note whenever its geometry or screen changes — a
    // completed drag (noteX/noteY are model-backed, so they only land once per
    // drag, per NoteWindow's architecture notes), a resize (noteWidth/Height
    // are client-side and update continuously — the debounce coalesces those),
    // or a late-arriving Wayland screen assignment. Same set of triggers, same
    // debounce rationale, as the toolbar's own.
    Connections {
        target: hudWindow.noteWindow
        function onScreenChanged() { if (hudWindow.noteWindow) repositionDebounce.restart() }
        function onNoteXChanged() { if (hudWindow.noteWindow) repositionDebounce.restart() }
        function onNoteYChanged() { if (hudWindow.noteWindow) repositionDebounce.restart() }
        function onNoteWidthChanged() { if (hudWindow.noteWindow) repositionDebounce.restart() }
        function onNoteHeightChanged() { if (hudWindow.noteWindow) repositionDebounce.restart() }
    }

    // One-shot, restarted by real events only (never a recurring poll — see
    // CLAUDE.md): coalesces the burst of geometry/size changes above into one
    // KWin move round-trip once things settle.
    Timer {
        id: repositionDebounce
        interval: 50
        repeat: false
        onTriggered: hudWindow.reposition()
    }

    onNoteWindowChanged: {
        // Computed fresh, not read from wantsVisible — both are listeners on
        // this same signal and QML doesn't order a binding re-eval against an
        // on-handler (same reasoning the toolbar's onNoteWindowChanged gives).
        const tracking = (noteWindow !== null)
        ribbon.visible = tracking
        if (tracking) {
            repositionDebounce.restart()
        } else {
            repositionDebounce.stop()
            parkOffscreen(hudWindow)
        }
        updateBlur()
    }

    // Anchors the column just outside the note's top-right edge, top-aligned
    // (feature request: a right-side column that never conflicts with the
    // toolbar sitting above the note). If there is no room to the right on the
    // note's own screen, falls back to overlapping the note's own top-right
    // corner instead (the old inline position) rather than running off-screen
    // — the accepted trade-off for a note pinned against the right screen edge.
    //
    // Reads noteX/noteY/noteWidth (model-backed, PositionEventService-confirmed),
    // NOT Window.x/y — plain Window.x/y are never updated after a compositor
    // drag on Wayland (see NoteWindow's architecture notes), so they'd anchor
    // the ribbon to wherever the note used to be. Applied both as x/y (for this
    // file's own math and for tests) AND via positionManager.moveWindow(), the
    // only thing KWin honours for an already-mapped top-level.
    function reposition() {
        if (!hudWindow.noteWindow)
            return

        const note = hudWindow.noteWindow
        const screen = note.screen
        const gap = 4
        const noteX = note.noteX
        const noteY = note.noteY
        const noteW = note.noteWidth
        const w = hudWindow.width
        const h = hudWindow.height

        // Default: just outside the right edge, top-aligned with the note.
        let targetX = noteX + noteW + gap
        let targetY = noteY

        if (screen) {
            const minX = screen.virtualX
            const minY = screen.virtualY
            const maxX = screen.virtualX + screen.width
            const maxY = screen.virtualY + screen.height

            // No room outside on the right → tuck it inside the note's own
            // top-right corner instead (overlaps the note, but stays visible).
            if (targetX + w > maxX)
                targetX = noteX + noteW - w - gap

            targetX = Math.max(minX, Math.min(targetX, maxX - w))
            targetY = Math.max(minY, Math.min(targetY, maxY - h))
        }

        hudWindow.x = targetX
        hudWindow.y = targetY
        positionManager.moveWindow(hudWindow.title, targetX, targetY)
    }

    // Exposed for tests, same readonly-alias convention as the other popups.
    readonly property alias hud: ribbon

    NoteHud {
        id: ribbon
        // Windowed mode is always the full vertical column: the whole window
        // shows/hides on note-hover, so there is no separate collapsed-dot
        // state to reveal (alwaysExpanded), and the column layout is what keeps
        // it clear of the above-note toolbar (vertical).
        vertical: true
        alwaysExpanded: true

        // Starts hidden; toggled from onNoteWindowChanged above. Its own
        // visibility IS this window's "showing content" flag — the input
        // region is hooked straight to it (same one-hook-can't-drift reasoning
        // as the toolbar's background.onVisibleChanged), so a parked window
        // still rejects the clicks the off-screen park alone can't be trusted
        // to stop (see parkOffscreen()).
        visible: false
        onVisibleChanged: windowInputMask.setAcceptsInput(hudWindow, ribbon.visible)

        // No frosted-glass backdrop here — a separate top-level window cannot
        // sample the note's own text surface, and it doesn't need to: the
        // ribbon sits outside the note, so there is no text of the note's under
        // it to blur. backdropSource stays null (its default), which leaves all
        // of NoteHud's capture/hole-punch machinery inert.
        pinned: hudWindow.noteWindow ? hudWindow.noteWindow.alwaysOnTop : false
        cornerRadius: hudWindow.noteWindow ? hudWindow.noteWindow.noteAppearance.windowRadius : 8
        iconColor: hudWindow.noteWindow ? hudWindow.noteWindow.noteAppearance.fontColor : "#ffffff"

        // Glass sheen follows the hovered note's own appearance, matching how
        // the toolbar's popups mirror the tracked note (feature parity with the
        // inline ribbon, which already did this).
        glassEnabled: hudWindow.noteWindow ? hudWindow.noteWindow.noteAppearance.glassEnabled : false
        glassTintColor: hudWindow.noteWindow ? hudWindow.noteWindow.noteAppearance.glassTintColor : "#ffffff"
        glassSheenOpacity: hudWindow.noteWindow ? hudWindow.noteWindow.noteAppearance.glassSheenOpacity : 0.12
        glassSheenStrength: hudWindow.noteWindow ? hudWindow.noteWindow.noteAppearance.glassSheenStrength : 10
        glassSheenSize: hudWindow.noteWindow ? hudWindow.noteWindow.noteAppearance.glassSheenSize : 0.18

        // Every action operates on the hovered note directly, except search
        // (see findRequested above). Guarded on noteWindow — a signal can still
        // arrive in the instant after the pointer left and it went null.
        onPinToggleRequested: if (hudWindow.noteWindow) hudWindow.noteWindow.setPinned(!hudWindow.noteWindow.alwaysOnTop)
        onSettingsRequested: if (hudWindow.noteWindow) hudWindow.noteWindow.openSettings()
        onDuplicateRequested: if (hudWindow.noteWindow) hudWindow.noteWindow.duplicateNote()
        onSearchRequested: if (hudWindow.noteWindow) hudWindow.findRequested(hudWindow.noteWindow)
        onCloseRequested: if (hudWindow.noteWindow) hudWindow.noteWindow.deleteNote()
    }
}
