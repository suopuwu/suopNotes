import QtQuick
import QtQuick.Window

// One hit-region for a single resize edge or corner (req. 8b: every edge
// and corner should be resizeable, not just a bottom-right grip).
// startSystemResize() is startSystemMove()'s sibling API — the same
// compositor-driven mechanism NoteWindow.qml's whole-window right-click
// drag already relies on, for the same reason (Wayland forbids a client
// from resizing itself directly). Calls startSystemResize() directly from
// onPressed (not via an intermediate signal) — Wayland's interactive-resize
// grab is tied to the input serial of the triggering press, so the call
// needs to happen in the same handler that receives that press, not one
// signal-emission hop away.
MouseArea {
    id: root

    required property int edges
    required property Window resizeTarget

    hoverEnabled: true
    onPressed: resizeTarget.startSystemResize(edges)

    DragHandler {
        target: null
        acceptedDevices: PointerDevice.TouchScreen
        onActiveChanged: if (active) root.resizeTarget.startSystemResize(root.edges)
    }
}
