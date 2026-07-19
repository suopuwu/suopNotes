import QtQuick

// The frosted-glass sheen every chrome window paints over its background — the
// selection toolbar and its popups, the context menu, the find/replace panel.
// It wraps GlassOverlay with the one binding set those windows had hand-copied
// identically: the `chromeGlassFollowsNote` gate (glass either mirrors the
// tracked note's own glass toggle, or follows the app-wide chrome setting) and
// the tracked note's tint/sheen tuning, with a neutral fallback when no note is
// tracked.
//
// The host still positions it — `anchors.fill` the background it decorates and
// `cornerRadius` that background's radius — and passes the tracked note plus
// whether that background is currently showing. (NoteHud, the note's own
// scrollbar, and InteractableHighlight deliberately do NOT use this: their glass
// follows the note's own appearance directly, not the chrome-follows-note gate.)
GlassOverlay {
    id: chromeGlass

    // The tracked note whose glass tuning drives tint/sheen and, when
    // chromeGlassFollowsNote is on, whether glass shows at all. Null → the dark
    // fallback below.
    property var noteWindow: null

    // Whether the decorated chrome is actually showing its content right now —
    // the glass only paints then. A window that is always mapped-and-showing
    // (the toolbar itself) leaves this at its default true.
    property bool contentShowing: true

    visible: chromeGlass.contentShowing && (settings.chromeGlassFollowsNote
        ? (chromeGlass.noteWindow ? chromeGlass.noteWindow.noteAppearance.glassEnabled : false)
        : settings.chromeGlassEnabled)
    tintColor: chromeGlass.noteWindow ? chromeGlass.noteWindow.noteAppearance.glassTintColor : "#ffffff"
    sheenOpacity: chromeGlass.noteWindow ? chromeGlass.noteWindow.noteAppearance.glassSheenOpacity : 0.12
    ringCount: chromeGlass.noteWindow ? chromeGlass.noteWindow.noteAppearance.glassSheenStrength : 10
    sheenSize: chromeGlass.noteWindow ? chromeGlass.noteWindow.noteAppearance.glassSheenSize : 0.18
}
