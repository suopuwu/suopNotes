import QtQuick
import QtQuick.Controls

// A Slider that never lets the mouse wheel move its own value, and scrolls the
// view it sits in instead.
//
// `wheelEnabled: false` — all a ComboBox or SpinBox needs — does nothing here.
// The KDE desktop style bypasses the template's wheel handling for Slider and
// puts its own MouseArea inside the slider's background (it wants wheel steps
// to snap to tick marks; QTBUG-93081). The only way to keep that MouseArea
// from seeing the event is to sit on top of it and consume it first — and
// since consuming it would also stop the settings pane from scrolling, this
// forwards the scroll on by hand.
Slider {
    id: control

    MouseArea {
        anchors.fill: parent
        // Above the style's background — and so above the wheel MouseArea
        // inside it — whatever order Control creates its deferred background
        // in.
        z: 1
        // Never accept a button: pressing and dragging the handle must still
        // reach the slider underneath. This exists only for the wheel.
        acceptedButtons: Qt.NoButton

        // The nearest enclosing Flickable, which for a slider inside a
        // ScrollView is that view's contentItem. Null when the slider is not
        // in a scrollable view at all, in which case the wheel is simply
        // swallowed.
        function enclosingFlickable() {
            for (var item = parent; item; item = item.parent) {
                if (item instanceof Flickable)
                    return item
            }
            return null
        }

        onWheel: wheel => {
            // Consumed either way. An ignored event would fall straight
            // through to the style's own MouseArea, which is the one thing
            // this must not allow.
            var view = enclosingFlickable()
            if (!view)
                return
            // Touchpads report real pixels; a wheel reports 120 units per
            // notch, which the platform's own scroll step turns into pixels.
            var lines = Qt.application.styleHints.wheelScrollLines || 3
            var dy = wheel.pixelDelta.y !== 0
                     ? wheel.pixelDelta.y
                     : wheel.angleDelta.y / 120 * lines * 20
            var maxY = Math.max(0, view.contentHeight - view.height)
            view.contentY = Math.max(0, Math.min(maxY, view.contentY - dy))
        }
    }
}
