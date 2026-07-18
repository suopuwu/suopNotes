import QtQuick
import QtQuick.Window

// Shared base for this app's frameless "chrome" windows — the selection
// toolbar and its popups, the find/replace panel, the note HUD, the context
// menu. Each is a permanently-mapped, always-on-top, transparent Tool window
// that "hides" by parking its content off-screen rather than by unmapping.
//
// This base owns ONLY the parts that are identical across all of them: the
// window flags, the one-time startup ritual that maps the surface and seeds its
// KWin "keep above" rule, and the off-screen park. Everything window-specific —
// size, content, blur, where it places itself, when its content is visible —
// stays in the deriving window, which connects to mapped() for any after-map
// work of its own.
//
// It exists because that ritual had been hand-copied across several windows,
// and a divergence between copies is exactly how a chrome window has shipped
// broken before (a forgotten input-mask hook eats every click while parked, a
// missing pseudo-id turns a drag into a phantom note). One definition, one
// place to get it right. See PositionManager::offscreenPoint() for the parked
// point's own shared definition.
Window {
    id: chrome

    // The opaque key this window's "keep above" KWin rule is filed under — NOT
    // a real note id; applyPosition() uses it only as a rule-group name. The
    // deriving window MUST also register this id in
    // PositionEventService::pseudoNoteIds(): KWin's watcher hooks every window
    // whose caption starts with "suopNotes-" and, on an interactive move, feeds
    // the caption's remainder to mergeLivePosition as a real note id — creating
    // a phantom note without that exclusion. `title` (set by the deriving
    // window) is the "suopNotes-<id>" caption KWin matches moves against.
    required property string ruleId

    // Emitted once the map-once ritual below has finished (surface created,
    // rule seeded) but before input is blocked, matching the per-window
    // ordering the copies used (blur was applied here, then the mask). A
    // deriving window connects to this to run its own after-map work — applying
    // blur, say — without re-implementing or overriding the ritual itself.
    signal mapped()

    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    color: "transparent"

    // Mapped exactly once, here, and never hidden/shown again for the rest of
    // the session: repeatedly mapping/unmapping a surface while a note holds
    // Wayland keyboard focus disrupts that note's text input every time,
    // regardless of flags. "Hidden" is a park with invisible content (see
    // park()), never show()/hide(). This is also why each chrome window is an
    // app-wide singleton, not one instance per note — N notes would mean N
    // surfaces all mapped at startup, forever.
    visible: false
    Component.onCompleted: {
        // Seed the "keep above" rule OFF-screen ("Apply Initially", in effect
        // from the first show() below) so the very first real show never
        // flashes at a throwaway on-screen spot before real positioning lands.
        const seed = positionManager.offscreenPoint()
        positionManager.applyPosition(chrome.ruleId, chrome.title, seed.x, seed.y, true)

        // hide()+show() is required, not redundant: KWin does not retroactively
        // apply a freshly-written above/Force rule to an already-existing
        // window — only a re-created one re-evaluates it.
        chrome.show()
        chrome.hide()
        chrome.show()

        // Deriving window's after-map work (e.g. blur) runs here — surface
        // present, before the input mask, as the original per-window ritual did.
        chrome.mapped()

        // AFTER show/hide/show, never before: hide() destroys the wl_surface and
        // the next show() creates a fresh one, and an input region is
        // per-surface state. Nothing is tracked yet, so "block all input" is the
        // correct starting state; the deriving window's own content-visibility
        // hook takes over from the first real transition on.
        windowInputMask.setAcceptsInput(chrome, false)

        // Deliberately NOT parking here: positionManager.moveWindow() spawns an
        // untracked thread per call with no ordering guarantee, so a park queued
        // at startup can land AFTER the first real show and drag the window back
        // off-screen. The seed above is already off-screen — nothing on-screen
        // to correct.
    }

    // Parks the window where no monitor can show it — this app's way of
    // "hiding" a permanently-mapped chrome window. Load-bearing for VISIBILITY
    // only, never input correctness: moveWindow() is async and racy, so a park
    // can land before the show it was meant to undo, and it no-ops entirely if
    // KWin scripting is unreachable. The deriving window's own input-mask hook
    // is what actually blocks clicks. The mask stops clicks, the park stops
    // pixels.
    function park() {
        const pt = positionManager.offscreenPoint()
        chrome.x = pt.x
        chrome.y = pt.y
        positionManager.moveWindow(chrome.title, pt.x, pt.y)
    }
}
