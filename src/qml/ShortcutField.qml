import QtQuick

// Click-to-record key sequence field (feature request, keyboard shortcut
// remapping): click it, then press the desired combination — captures
// modifiers + key directly into a QKeySequence-parsable string like
// "Ctrl+Alt+T" rather than requiring the user to type that syntax by hand.
// Escape, or clicking away, cancels without changing anything.
//
// `sequence` is display-only, meant to be bound straight from a Settings
// property (e.g. `sequence: settings.shortcutBold`) — this component never
// writes to it directly (which would sever that binding); it only emits
// `recorded(newSequence)` and leaves persisting the new value to the caller
// (e.g. `onRecorded: settings.shortcutBold = newSequence`), after which the
// original binding re-evaluates on its own once Settings' NOTIFY fires.
Rectangle {
    id: field

    property string sequence: ""
    property bool listening: false

    signal recorded(string newSequence)

    width: 150
    height: 28
    radius: 4
    color: field.listening ? Qt.rgba(0.25, 0.45, 0.65, 0.35) : Qt.rgba(1, 1, 1, 0.06)
    border.width: 1
    border.color: field.listening ? "#4da6ff" : Qt.rgba(1, 1, 1, 0.2)

    function isModifierOnly(key) {
        return key === Qt.Key_Control || key === Qt.Key_Shift || key === Qt.Key_Alt
            || key === Qt.Key_Meta || key === Qt.Key_AltGr
    }

    function keyToString(key) {
        if (key >= Qt.Key_A && key <= Qt.Key_Z)
            return String.fromCharCode(key)
        if (key >= Qt.Key_0 && key <= Qt.Key_9)
            return String.fromCharCode(key)
        if (key >= Qt.Key_F1 && key <= Qt.Key_F35)
            return "F" + (key - Qt.Key_F1 + 1)
        switch (key) {
        case Qt.Key_Escape: return "Esc"
        case Qt.Key_Tab: return "Tab"
        case Qt.Key_Backspace: return "Backspace"
        case Qt.Key_Return:
        case Qt.Key_Enter: return "Return"
        case Qt.Key_Space: return "Space"
        case Qt.Key_Delete: return "Delete"
        case Qt.Key_Comma: return ","
        case Qt.Key_Period: return "."
        case Qt.Key_Minus: return "-"
        case Qt.Key_Plus:
        case Qt.Key_Equal: return "+"
        case Qt.Key_Slash: return "/"
        case Qt.Key_Backslash: return "\\"
        default: return ""
        }
    }

    function sequenceStringFor(event) {
        const parts = []
        if (event.modifiers & Qt.ControlModifier)
            parts.push("Ctrl")
        if (event.modifiers & Qt.AltModifier)
            parts.push("Alt")
        if (event.modifiers & Qt.ShiftModifier)
            parts.push("Shift")
        if (event.modifiers & Qt.MetaModifier)
            parts.push("Meta")
        const keyName = keyToString(event.key)
        if (keyName.length === 0)
            return ""
        parts.push(keyName)
        return parts.join("+")
    }

    Text {
        anchors.centerIn: parent
        text: field.listening ? "Press keys…" : field.sequence
        color: "#ffffff"
        font.pixelSize: 12
    }

    Keys.onPressed: (event) => {
        if (!field.listening)
            return
        event.accepted = true
        if (event.key === Qt.Key_Escape) {
            field.listening = false
            return
        }
        if (isModifierOnly(event.key))
            return // wait for a real, non-modifier key
        const seq = sequenceStringFor(event)
        if (seq.length === 0)
            return
        field.listening = false
        field.recorded(seq)
    }

    onActiveFocusChanged: if (!activeFocus) field.listening = false

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            field.listening = true
            field.forceActiveFocus()
        }
    }
}
