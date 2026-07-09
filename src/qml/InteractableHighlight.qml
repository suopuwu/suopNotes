import QtQuick

// Shared hover/press highlight box for interactable chrome (feature
// request): the popup toolbar's buttons, NoteHud's pin/gear/close glyphs,
// and the note's own scrollbar handle all place one of these behind/around
// themselves rather than each hand-rolling their own hover feedback, so the
// color/opacity/glass tuning lives in exactly one place — the global
// `settings` singleton — and stays visually consistent across all three
// surfaces. No input handling of its own (`enabled: false`, matching
// GlassOverlay's own convention), so it never steals a click/hover from
// whatever it's layered on top of or behind.
Rectangle {
    id: root

    // Host sets this from its own hover/press state (e.g. `parent.hovered
    // || parent.down`, or a HoverHandler's `hovered`) — this component has
    // no opinion on what counts as "interacting", only on how that state
    // is rendered.
    property bool active: false

    radius: 4
    enabled: false
    color: settings.interactableHighlightColor
    opacity: active ? settings.interactableHighlightOpacity : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 100 } }

    GlassOverlay {
        anchors.fill: parent
        cornerRadius: root.radius
        visible: settings.interactableHighlightGlassEnabled && root.active
        tintColor: settings.glassTintColor
        sheenOpacity: settings.glassSheenOpacity
        ringCount: settings.glassSheenStrength
        sheenSize: settings.glassSheenSize
    }
}
