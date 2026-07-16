import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Window

// NoteContextMenu — the single, app-wide right-click menu, owned by Main.qml
// and bound to whichever note currently has OS focus via the `noteWindow`
// property. Opened by that note's own contextMenuRequested signal (see
// NoteWindow.qml's mouseDragArea, which is what decides a right-click is a
// click and not a drag); this file only owns the menu itself.
//
// A genuine top-level Window, not an Item inside NoteWindow's own tree, for
// exactly the reason SelectionToolbar.qml and FindReplacePanel.qml each
// record for themselves: a note enforces no minimum size, and this menu is
// ~176x168, so inside a small note it would either overflow the note's real
// Wayland surface (clipped and unclickable) or cover the very text it acts
// on. A right-click near the note's bottom-right corner puts the menu mostly
// OUTSIDE the note by construction, so this is not a marginal case here — it
// is the common one.
//
// Being an app-wide singleton rather than one instance per note is also what
// keeps it to ONE permanently-mapped window; see Component.onCompleted for
// why mapping on demand is not an option.
//
// Like FindReplacePanel (and unlike the four windows in SelectionToolbar.qml)
// this window deliberately TAKES keyboard focus when it opens, even though it
// has no text fields. That is what makes "click anywhere else to dismiss"
// work at all: losing `active` is the only signal this app gets that the user
// clicked somewhere that isn't the menu (see onActiveChanged). It has the
// same consequence for Main.qml as the find panel does — its
// noteOrChromeActive()/noteOrChromeHovered() predicates must count this
// window as "in use", or the tracked note clears 250ms after the menu opens
// and pulls `noteWindow` out from under it. They do; keep it that way.
//
// Properties (Main.qml sets/binds these):
//   noteWindow — the tracked NoteWindow, or null when no note has focus.
//                The menu acts on this note and closes on ANY change to it.
//
// Functions (Main.qml calls this, from the tracked note's own signal):
//   openAt(screenX, screenY) — show with the top-left at that screen point
//
// Behavior notes:
//   - Cut/Copy/Paste delegate to the same editor calls SelectionToolbar's own
//     cut/copy/paste buttons use, so the two surfaces can't drift apart.
//   - Duplicate Note lands the copy at the menu's own point (feature
//     request), unlike the HUD's duplicate which uses a fixed diagonal offset.
//   - Delete Note is immediate and permanent, matching the HUD's own close
//     button (NoteWindow.deleteNote()) — no confirmation there, none here.
Window {
    id: menu

    // Scope the menu acts on — the currently-focused NoteWindow, or null when
    // no note has focus. Sourced from Main.qml, the one place that observes
    // every NoteWindow's own activeChanged. Same convention as
    // SelectionToolbar.qml's and FindReplacePanel.qml's own `noteWindow`.
    property var noteWindow: null

    // Whether the user has the menu open. NOT the same thing as "is anything
    // showing" — see wantsVisible.
    property bool open: false

    signal closed()

    // The screen point openAt() was last asked for — the menu's top-left,
    // before reposition() flips/clamps it against the screen edges. Held
    // rather than passed straight through to reposition() because a re-open
    // at a new point while already showing has to re-place from the NEW
    // request, and because the flip logic needs the original pointer position
    // to mirror around.
    property real _requestedX: 0
    property real _requestedY: 0

    // Whether this window should currently be showing anything at all.
    // Unlike the find panel, losing the tracked note doesn't merely hide this
    // — onNoteWindowChanged closes it outright (see there for why), so in
    // practice `open` never outlives its note. The && is kept anyway so that
    // this reads as the same invariant every other chrome window states, and
    // so _syncVisible's argument can never disagree with it.
    readonly property bool wantsVisible: noteWindow !== null && open

    // Whether the pointer is anywhere over this window — Main.qml treats this
    // as a keep-alive signal for the tracked note exactly like the toolbar's
    // and the find panel's own `hovered`.
    readonly property alias hovered: menuHoverHandler.hovered
    HoverHandler { id: menuHoverHandler }

    // Exposed for tests only — the interactable pieces themselves, matching
    // FindReplacePanel.qml's and NoteHud.qml's readonly-item-alias pattern.
    // Deliberately ITEMS, never a pass-through to a child's bound value (e.g.
    // a .color or .enabled): see NoteHud.qml's pinGlyphItem comment for the
    // AOT staleness trap a value alias hits under qmlcachegen.
    readonly property alias cutItem: cutItem
    readonly property alias copyItem: copyItem
    readonly property alias pasteItem: pasteItem
    readonly property alias duplicateItem: duplicateItem
    readonly property alias deleteItem: deleteItem
    readonly property alias background: background
    readonly property alias content: content
    readonly property alias escapeShortcut: escapeShortcut

    // Matched by title (not wmclass alone, which is shared app-wide) by the
    // KWin rule/scripts below — same convention NoteWindow.qml's own
    // "suopNotes-" + noteId titles use, just with a fixed, non-note id since
    // this is a single app-wide singleton.
    //
    // "noteContextMenu" is registered in PositionEventService's
    // pseudoNoteIds() as a result, and that is NOT optional: KWin's watcher
    // script hooks every window whose caption starts with "suopNotes-" and,
    // on an interactive move, feeds the caption's remainder to
    // mergeLivePosition as if it were a real note id — creating a phantom
    // note in the persisted OpenNotes list. This menu has no drag handle, but
    // the compositor can still start an interactive move on any window
    // (alt+drag), and that bug has already shipped three times here
    // (selectionToolbar, columnOptionsPopup, spellingSuggestionsPopup), so
    // the exclusion is registered up front rather than after a fourth.
    title: "suopNotes-noteContextMenu"

    // Qt.WindowDoesNotAcceptFocus is deliberately absent, and must stay
    // absent — twice over. SelectionToolbar.qml records that on this
    // compositor the flag silently swallowed the pointer click itself, not
    // just keyboard focus, so every menu row would be dead. And this window
    // needs real focus regardless: dismiss-on-click-elsewhere is implemented
    // as "this window lost active" (see onActiveChanged), which a window that
    // never becomes active can never report.
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    color: "transparent"

    // FIXED, not derived from content's implicitWidth/implicitHeight. Two
    // reasons, both learned next door: a Window whose size binds to content
    // can commit a 0-dimension geometry on its first frame, which is an
    // xdg-shell protocol error that kills the Wayland connection and takes
    // the app with it; and any resize of a mapped window is a real
    // client<->compositor negotiation, two of which racing can leave the
    // surface at a stale size while the QML inside has already reflowed —
    // SelectionToolbar.qml has a whole self-correcting size guard for exactly
    // that. This menu's rows are a constant list, so a constant size costs
    // nothing and sidesteps both outright.
    //
    // Even numbers: reposition()'s math and the compositor both round to
    // whole pixels, and SelectionToolbar.qml records a half-pixel mismatch
    // surfacing from an odd length. Height: 5 rows * 28 + one 10px separator
    // + 5 gaps * 2 spacing + 2 * 4 margins = 168. Width is not derived from
    // the longest label ("Duplicate Note") — a fixed 176 leaves it room at
    // the default font with the padding below, and the labels elide rather
    // than resize the window if a font ever outgrows it.
    readonly property int wantedWidth: 176
    readonly property int wantedHeight: 168
    readonly property int itemHeight: 28
    readonly property int separatorHeight: 10

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
    // That is also precisely why this menu is a singleton rather than one per
    // note: N notes would mean N windows all mapped at startup, forever.
    visible: false
    Component.onCompleted: {
        // Seeds the KWin rule (same mechanism NoteWindow's pin feature uses)
        // Force-keeping this window above others. Seeded at offscreenPoint(),
        // not an on-screen throwaway: this is an "Apply Initially" rule in
        // effect from the very first show() below, and SelectionToolbar.qml
        // records that an on-screen seed produced a real once-per-session
        // flash at the wrong spot before the first reposition landed.
        const seed = menu.offscreenPoint()
        positionManager.applyPosition("noteContextMenu", menu.title, seed.x, seed.y, true)

        // The hide()+show() is deliberate, not redundant: KWin does not
        // retroactively apply a freshly-written above/Force rule to a window
        // that already exists — only a re-created one re-evaluates it.
        // NoteWindow.setPinned() documents the same requirement.
        menu.show()
        menu.hide()
        menu.show()
        menu.updateBlur()
        // AFTER the show/hide/show, never before: hide() destroys the
        // wl_surface and the next show() creates a fresh one, and an input
        // region is per-surface state — a mask set before that cycle would be
        // set on a surface that no longer exists. Nothing is tracked yet, so
        // "blocks all input" is the correct starting state (background.visible
        // starts false to match).
        windowInputMask.setAcceptsInput(menu, false)

        // Deliberately NOT parking here, unlike every real hide below:
        // positionManager.moveWindow() spawns an untracked thread per call
        // with no ordering guarantee between calls, so a park queued at
        // startup can land AFTER the first real show and drag the just-shown
        // menu back off-screen. The rule above already seeds it off-screen,
        // so there is no on-screen resting spot left to correct.
    }

    // Returns a point guaranteed to fall outside the combined virtual desktop
    // of every connected screen, regardless of monitor arrangement (including
    // layouts with negative-coordinate monitors above/left of the primary,
    // where a fixed -10000,-10000 could still land ON a real monitor).
    //
    // Duplicated from SelectionToolbar.qml/FindReplacePanel.qml rather than
    // shared: each copy is a function on that file's own root Window,
    // unreachable from here. Same for parkOffscreen() below. This is now the
    // THIRD hand-copy of both, which is the point FindReplacePanel.qml's own
    // note said they want to become a real shared component — see the summary
    // note in that file; extracting them is a mechanical change worth doing
    // on its own, not folded into this feature.
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

    // Parks this window where no monitor can show it. Load-bearing for
    // VISIBILITY ONLY — never for input correctness: moveWindow() is async,
    // spawns a fresh untracked thread per call, and its completions arrive in
    // COMPLETION order rather than call order, so a park can land before the
    // show it was meant to undo; it also no-ops entirely if KWin scripting is
    // unreachable. Input correctness is windowInputMask's job instead (see
    // background.onVisibleChanged), which is synchronous and client-side. The
    // mask stops clicks, the park stops pixels.
    function parkOffscreen() {
        const pt = menu.offscreenPoint()
        menu.x = pt.x
        menu.y = pt.y
        positionManager.moveWindow(menu.title, pt.x, pt.y)
    }

    // Mirrors the tracked note's real compositor blur-behind, exactly like the
    // toolbar and the find panel do. Gated on background.visible (the real "is
    // anything showing" flag) rather than wantsVisible: blur is an immediate
    // compositor effect on the window's CURRENT geometry, and
    // SelectionToolbar.qml records that enabling it any earlier put a blurred
    // patch at the stale position before the content ever appeared.
    function updateBlur() {
        const on = background.visible && menu.noteWindow
            ? menu.noteWindow.noteAppearance.blurEnabled
            : false
        blurEffect.applyTo(menu, on, background.radius)
    }

    // Re-derives the blur region whenever this window's size changes — the
    // region KWindowEffects blurs is tied to the window's current dimensions.
    // (This window is fixed-size, so in practice this only fires if the
    // platform itself writes width/height — which is exactly the clobber
    // enforceSize() exists to catch.)
    onWidthChanged: {
        menu.enforceSize()
        menu.updateBlur()
    }
    onHeightChanged: {
        menu.enforceSize()
        menu.updateBlur()
    }

    // Self-correction for a platform-level geometry clobber (a stale
    // configure/ack landing late). Observable at the QML level despite being a
    // platform write: SelectionToolbar.qml records `width` ITSELF dropping to
    // 1 while the QML feeding it never changed.
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
        if (menu.width !== menu.wantedWidth)
            menu.width = Qt.binding(() => menu.wantedWidth)
        if (menu.height !== menu.wantedHeight)
            menu.height = Qt.binding(() => menu.wantedHeight)
    }

    // The one place this window flips between "showing" and "parked". Takes
    // `showing` as an argument rather than reading wantsVisible, because the
    // call site is an on-handler for the very change wantsVisible depends on,
    // and a binding is not ordered against an on-handler for the same change
    // (SelectionToolbar.qml hit this and documents it).
    function _syncVisible(showing) {
        background.visible = showing
        content.visible = showing
        content.enabled = showing
        if (showing) {
            // Called straight through, NOT via a debounce — deliberately
            // unlike the toolbar and the find panel, which both debounce
            // 50ms to coalesce the reposition pair Wayland's async
            // QWindow::screen() produces. Those two ANCHOR to the note, so a
            // stale screen picks a visibly wrong spot for them; this menu
            // anchors to a screen point the pointer already named, and reads
            // note.screen only to clamp against that screen's edges. The note
            // was focused and settled long before the right-click that got us
            // here, so there is no stale-screen pair to coalesce — only
            // ~50ms of latency to not add to a menu that should feel instant.
            menu.reposition()
        } else {
            menu.parkOffscreen()
        }
        menu.updateBlur()
    }

    onOpenChanged: menu._syncVisible(menu.noteWindow !== null && menu.open)

    // Closes on ANY change of the tracked note, not just losing it. This menu
    // is bound to one point on one specific note, and neither survives focus
    // moving to a different note: left open across a retarget it would keep
    // showing at the OLD note's coordinates while Cut/Delete/Duplicate
    // silently began acting on the NEW one. The find panel deliberately
    // survives a retarget (following focus is the point of one app-wide find
    // bar); a context menu has no such meaning.
    //
    // Assigning `open` is the whole implementation — onOpenChanged above
    // funnels into _syncVisible for us. Reading menu.noteWindow there during
    // this handler is safe (it is this very property, already updated); the
    // ordering TRAP the other chrome files warn about applies to DERIVED
    // readonly bindings on noteWindow, which this file deliberately has none
    // of.
    onNoteWindowChanged: menu.open = false

    // The tracked note's own blur toggle lives in PerNoteSettingsPopup, which
    // is reachable without losing focus — so the CURRENTLY tracked note's
    // blurEnabled can flip under us. A `target: menu.noteWindow.noteAppearance`
    // binding re-targets itself automatically as noteWindow changes, including
    // to null (Connections simply goes inert with a null target).
    Connections {
        target: menu.noteWindow ? menu.noteWindow.noteAppearance : null
        function onBlurEnabledChanged() { menu.updateBlur() }
    }

    // Deliberately NO Connections on the note's noteX/noteY/noteWidth/
    // noteHeight, unlike the toolbar and the find panel which both re-anchor
    // when their note moves. A context menu belongs to the point on screen the
    // user right-clicked, not to the note — and the note cannot move while the
    // menu is up anyway: dragging it means right-click-dragging or grabbing
    // its edge, either of which activates the note and dismisses the menu
    // first (see onActiveChanged).

    // Places the menu's top-left at the requested point, flipping to the other
    // side of it rather than merely sliding when it would overrun the screen's
    // right/bottom edge. Flipping is what every desktop menu does and it is
    // not cosmetic: a clamp alone slides the menu back UNDER the pointer, so
    // the row sitting under the cursor at open time would be whatever happened
    // to land there — one stray click away from Delete Note. Mirroring around
    // the pointer keeps the pointer outside the menu, on its corner, exactly
    // as it is in the normal case.
    //
    // The clamp still runs after the flip, as a floor for the case where the
    // menu fits on neither side (a screen narrower/shorter than the menu, or a
    // point near two edges at once) — partly clipped beats off-screen.
    //
    // The result is applied TWO ways: menu.x/y (for this file's own math and
    // for tests) AND positionManager.moveWindow() — confirmed live next door
    // that the plain property assignment alone is not honored by KWin for an
    // already-mapped top-level window; the KWin script is what actually moves
    // it.
    function reposition() {
        if (!menu.noteWindow)
            return

        let targetX = menu._requestedX
        let targetY = menu._requestedY

        // note.screen, not Qt.application.screens[0] or the point's own
        // screen: the tracked note is by definition the window the user just
        // right-clicked, so its screen is the one the pointer is on.
        const screen = menu.noteWindow.screen
        if (screen) {
            const minX = screen.virtualX
            const minY = screen.virtualY
            const maxX = screen.virtualX + screen.width
            const maxY = screen.virtualY + screen.height

            if (targetX + menu.wantedWidth > maxX)
                targetX = menu._requestedX - menu.wantedWidth
            if (targetY + menu.wantedHeight > maxY)
                targetY = menu._requestedY - menu.wantedHeight

            targetX = Math.max(minX, Math.min(targetX, maxX - menu.wantedWidth))
            targetY = Math.max(minY, Math.min(targetY, maxY - menu.wantedHeight))
        }

        menu.x = targetX
        menu.y = targetY
        positionManager.moveWindow(menu.title, targetX, targetY)
    }

    // Shows the menu with its top-left at a SCREEN point — NoteWindow.qml
    // resolves the click's window-local coordinates against its own
    // model-backed origin before emitting, since a note's plain Window.x/y is
    // never updated after a compositor-driven drag on Wayland (see
    // NoteWindow.noteOrigin()).
    function openAt(screenX, screenY) {
        if (!menu.noteWindow)
            return
        menu._requestedX = screenX
        menu._requestedY = screenY
        if (menu.open) {
            // Already showing: onOpenChanged won't fire for open=true->true,
            // so _syncVisible's own reposition never runs and the menu would
            // sit at the previous point. In practice a second right-click
            // dismisses the first menu (the press activates the note, which
            // deactivates this window) long before the release that lands
            // here, so this is a guard against that ordering changing rather
            // than a path with a known trigger.
            menu.reposition()
        } else {
            menu.open = true
        }
        // raise() before requestActivate(): requestActivate() alone was found
        // next door not to reliably restore stacking order on this compositor.
        // The activation itself is load-bearing well beyond keyboard focus —
        // see onActiveChanged.
        menu.raise()
        menu.requestActivate()
    }

    // Hides the menu AND hands focus back to the note. For the paths where the
    // user is done with the menu but is still working in their note: Escape,
    // and running any of the actions.
    function close() {
        if (!menu.open)
            return
        menu.open = false
        menu._returnFocusToNote()
        menu.closed()
    }

    // Hides the menu WITHOUT the focus handoff. Split from close() because
    // this fires when focus has already gone somewhere the user chose — a
    // different note, another app, the desktop — and close()'s handoff would
    // yank it straight back to the tracked note, overriding that choice. The
    // whole reason this window takes focus is to be able to notice this.
    function dismiss() {
        if (!menu.open)
            return
        menu.open = false
        menu.closed()
    }

    // Dismiss-on-click-elsewhere. A click in the note, in another window, or
    // on the desktop all deactivate this window, and none of them report
    // themselves to this app any other way — a frameless Tool window gets no
    // "the user clicked outside you" event.
    //
    // Only ever fires on a real edge, so opening (which activates this window)
    // can't trip it. The re-entrant path is closed by dismiss()'s own !open
    // guard: close() hands focus back to the note, which deactivates this
    // window, which lands right back here with open already false.
    onActiveChanged: {
        if (!menu.active)
            menu.dismiss()
    }

    // Hands focus back to the tracked note. A cross-WINDOW handoff, so a bare
    // editor.forceActiveFocus() is not enough (that only moves focus within
    // the note's own window, which is not the active one while this menu is
    // up); it needs the same raise() + requestActivate() ritual
    // SelectionToolbar's runAction() and FindReplacePanel's own close() use.
    function _returnFocusToNote() {
        const note = menu.noteWindow
        if (!note)
            return
        note.raise()
        note.requestActivate()
    }

    // Runs one of the menu's actions against the tracked note and closes.
    //
    // The close happens BEFORE the action, and that order is load-bearing:
    // deleteNote() destroys `note` outright — and, through NoteStore's
    // synchronous noteIdsChanged, Main.qml's own activeNoteWindow and this
    // menu's noteWindow along with it, all on this call stack (see
    // NoteWindow.deleteNote()'s comment for the full trace). close() must
    // therefore do its focus handoff while there is still a live note to hand
    // focus to. Nothing any action does reads menu state, so closing first
    // costs the other four nothing.
    //
    // `note` is passed to fn as a plain local rather than letting fn re-read
    // menu.noteWindow, for the same reason: by the time fn runs, close() has
    // already fired closed() and menu.noteWindow may be mid-teardown.
    function runAction(fn) {
        const note = menu.noteWindow
        if (!note)
            return
        menu.close()
        fn(note)
    }

    // One row of the menu.
    //
    // A plain Text contentItem, never the default IconLabel: SelectionToolbar
    // .qml records that IconLabel silently rendered nothing at all in a
    // frameless Tool window under the real compositor (while rendering fine in
    // the offscreen Quick Test harness), root cause never identified, and
    // every button in that file bypasses it for exactly that reason. This is
    // not a case where the test suite passing means anything.
    component MenuAction: ItemDelegate {
        id: action
        Layout.fillWidth: true
        implicitHeight: menu.itemHeight
        hoverEnabled: true
        // The same InteractableHighlight every other interactable in the app
        // uses (toolbar buttons, NoteHud glyphs, the note's scrollbar), so a
        // menu row reads as the same kind of control and picks up the global
        // highlight color/opacity/glass tuning from Settings.
        background: InteractableHighlight {
            anchors.fill: parent
            active: action.hovered || action.down
        }
        contentItem: Text {
            text: action.text
            font: action.font
            // Dimmed rather than hidden when the action can't run: a menu
            // whose rows appear and disappear between openings never lets the
            // user build muscle memory for where anything is, and this menu's
            // whole point is being faster than the toolbar.
            //
            // Colored from the tracked note's own fontColor, matching how the
            // toolbar colors its labels — the chrome mimics whichever note it
            // is acting on, so it stays legible against that note's
            // background rather than a fixed theme's.
            color: menu.noteWindow
                ? Qt.rgba(menu.noteWindow.noteAppearance.fontColor.r,
                          menu.noteWindow.noteAppearance.fontColor.g,
                          menu.noteWindow.noteAppearance.fontColor.b,
                          action.enabled ? 1.0 : 0.4)
                : "#ffffff"
            verticalAlignment: Text.AlignVCenter
            // The window is a fixed width (see wantedWidth) — a label that
            // outgrows it at some future font elides instead of silently
            // painting past the surface's edge.
            elide: Text.ElideRight
        }
    }

    // Mimics whichever note is currently tracked, exactly like the toolbar's
    // and the find panel's own backgrounds. The plain dark chip is only ever
    // seen in the instant before any note has been tracked — at which point
    // this is invisible anyway.
    Rectangle {
        id: background
        visible: false

        // Keeps this window's input region in lockstep with whether it is
        // actually showing anything — see parkOffscreen() for why the
        // off-screen park cannot be trusted with this job, and
        // WindowInputMask.h for the Wayland mechanics. A transparent window
        // with invisible CONTENT still eats every click that lands on it.
        //
        // Hooked to `visible` rather than called from each handler that flips
        // it: this Rectangle's visibility IS this file's "is the menu really
        // showing" flag (the same one updateBlur() keys off), so one hook here
        // cannot drift out of sync with a flip site someone forgets to update.
        onVisibleChanged: windowInputMask.setAcceptsInput(menu, background.visible)

        anchors.fill: parent
        radius: 6
        color: menu.noteWindow
            ? Qt.rgba(menu.noteWindow.noteAppearance.backgroundColor.r,
                      menu.noteWindow.noteAppearance.backgroundColor.g,
                      menu.noteWindow.noteAppearance.backgroundColor.b,
                      menu.noteWindow.noteAppearance.backgroundColor.a * menu.noteWindow.noteAppearance.windowOpacity)
            : Qt.rgba(0.15, 0.15, 0.15, 0.96)
        border.width: 1
        border.color: menu.noteWindow ? menu.noteWindow.noteAppearance.borderColor : Qt.rgba(1, 1, 1, 0.18)

        // Escape closes without running anything — the universal way out of an
        // open menu. Qt's default Shortcut context (Qt.WindowShortcut) scopes
        // this to this window, so it only fires while the menu actually holds
        // focus and never competes with the tracked note's own Escape handling
        // (which disarms autoscroll / closes the find panel — see
        // NoteWindow.qml).
        //
        // Declared in HERE, inside an Item, and NOT as a direct child of the
        // Window — that placement is load-bearing and silently wrong the other
        // way. Qt resolves a Shortcut's window by walking up its QObject
        // parents for something window-ish; parented straight to a Window that
        // walk finds nothing, the shortcut matches no context, and it simply
        // never fires. No warning, no error. Measured on Qt 6.11 with a bare
        // two-shortcut probe window: the one under an Item fired, the one
        // directly under the Window did not, with nothing else different.
        // NoteWindow.qml has always nested its own shortcuts inside mainFrame
        // for the same reason.
        Shortcut {
            id: escapeShortcut
            sequence: "Escape"
            onActivated: menu.close()
        }
    }

    // Same glass treatment as every other chrome window's background, honoring
    // the same chromeGlassFollowsNote setting the toolbar's popups do.
    GlassOverlay {
        anchors.fill: background
        cornerRadius: background.radius
        visible: background.visible && (settings.chromeGlassFollowsNote
            ? (menu.noteWindow ? menu.noteWindow.noteAppearance.glassEnabled : false)
            : settings.chromeGlassEnabled)
        tintColor: menu.noteWindow ? menu.noteWindow.noteAppearance.glassTintColor : "#ffffff"
        sheenOpacity: menu.noteWindow ? menu.noteWindow.noteAppearance.glassSheenOpacity : 0.12
        ringCount: menu.noteWindow ? menu.noteWindow.noteAppearance.glassSheenStrength : 10
        sheenSize: menu.noteWindow ? menu.noteWindow.noteAppearance.glassSheenSize : 0.18
    }

    ColumnLayout {
        id: content
        visible: false
        enabled: false
        anchors.fill: parent
        anchors.margins: 4
        spacing: 2

        MenuAction {
            id: cutItem
            text: "Cut"
            // Cut has nothing to fall back on with an empty selection, so it
            // simply doesn't apply — deliberately NOT mirroring Copy's
            // whole-note behavior below, which would silently empty the note.
            // Same rule, same expression, as SelectionToolbar's cut button.
            enabled: menu.noteWindow ? menu.noteWindow.editor.selectionStart !== menu.noteWindow.editor.selectionEnd : false
            onClicked: menu.runAction(note => note.editor.cut())
        }
        MenuAction {
            id: copyItem
            text: "Copy"
            // Always enabled: performCopy() copies the WHOLE note when nothing
            // is selected rather than doing nothing, and shows the note's
            // "Copied" toast either way. Routed through the note's own
            // function, not editor.copy(), so this menu and the toolbar's Copy
            // button cannot drift apart.
            onClicked: menu.runAction(note => note.performCopy())
        }
        MenuAction {
            id: pasteItem
            text: "Paste"
            // canPaste is TextEdit's own live clipboard check — the only
            // clipboard read available from QML, and the same one
            // SelectionToolbar's paste button gates on.
            enabled: menu.noteWindow ? menu.noteWindow.editor.canPaste : false
            onClicked: menu.runAction(note => note.editor.paste())
        }

        // Separates the two clipboard actions above (which act on TEXT INSIDE
        // the note) from the two below (which act on the NOTE ITSELF, one of
        // them destructively). Its height is part of wantedHeight's arithmetic
        // — see there before changing it.
        Item {
            Layout.fillWidth: true
            implicitHeight: menu.separatorHeight

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 1
                // The same rule color the toolbar's own dividers use.
                color: Qt.rgba(1, 1, 1, 0.15)
            }
        }

        MenuAction {
            id: duplicateItem
            text: "Duplicate Note"
            // Lands the copy at the menu's own top-left — i.e. right where the
            // user right-clicked (feature request), rather than the fixed
            // diagonal offset from the source note that the HUD's own
            // duplicate uses. menu.x/y, not _requestedX/_requestedY: after a
            // flip/clamp near a screen edge (see reposition()) those two
            // disagree, and the menu's real on-screen spot is the one the user
            // is actually looking at.
            //
            // The position is read into locals BEFORE runAction(), and that is
            // load-bearing rather than style: runAction() closes the menu
            // first (see there for why it must), and closing PARKS this window
            // off-screen — so by the time the callback runs, menu.x/y are the
            // park coordinates, roughly a thousand pixels past the last
            // monitor. Caught by
            // test_duplicate_creates_an_independent_note_at_the_menus_own_position,
            // which read back 1800 for an expected 500.
            onClicked: {
                const menuX = menu.x
                const menuY = menu.y
                menu.runAction(note => note.duplicateNoteAt(menuX, menuY))
            }
        }
        MenuAction {
            id: deleteItem
            text: "Delete Note"
            // Permanent and immediate — deletes the .md file, no undo, no
            // confirmation. Matches the HUD's own close button exactly
            // (NoteHud's onCloseRequested calls this same function); adding a
            // confirmation here alone would make the two disagree about what
            // deleting a note means.
            onClicked: menu.runAction(note => note.deleteNote())
        }
    }
}
