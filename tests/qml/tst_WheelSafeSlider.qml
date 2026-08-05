import QtQuick
import QtQuick.Controls
import QtTest
import SuopNotesUi

// WheelSafeSlider exists because the desktop style lets a wheel drag a
// slider's value. These tests document the three things it must get right at
// once: the wheel never moves the value, the wheel still scrolls the view the
// slider lives in, and the pointer can still drag the handle.
Item {
    id: root
    width: 200
    height: 100

    // The slider sits at the very top of the column so it is inside the
    // viewport (and therefore reachable by a synthetic mouse/wheel event) at
    // the default contentY of 0; the spacer below it is what gives the wheel
    // somewhere to scroll to.
    ScrollView {
        id: view
        anchors.fill: parent
        contentWidth: availableWidth

        Column {
            width: view.availableWidth
            WheelSafeSlider {
                id: slider
                width: parent.width
                from: 0
                to: 100
                value: 50
            }
            Item { width: 1; height: 400 }
        }
    }

    TestCase {
        name: "WheelSafeSlider"
        when: windowShown

        // QuickTest runs test_ functions in alphabetical, not declaration,
        // order, so the scroll test below runs before this file's other
        // tests and leaves the view mid-scroll. Reset it each time so every
        // test starts with the slider at the top of the viewport — off the
        // top or bottom of a 100px-tall window is off the edge of the
        // window itself, where a synthetic mouse/wheel event can't land.
        function init() {
            view.contentItem.contentY = 0
        }

        function test_a_wheel_over_the_slider_leaves_its_value_alone() {
            var before = slider.value
            mouseWheel(slider, slider.width / 2, slider.height / 2, 0, -120)
            compare(slider.value, before)
        }

        function test_a_wheel_over_the_slider_scrolls_the_view_instead() {
            var flickable = view.contentItem
            flickable.contentY = 0
            mouseWheel(slider, slider.width / 2, slider.height / 2, 0, -120)
            verify(flickable.contentY > 0)
        }

        // The overlay accepts no mouse buttons precisely so presses still
        // reach the slider underneath it.
        function test_dragging_the_handle_still_changes_the_value() {
            slider.value = 0
            mousePress(slider, 2, slider.height / 2)
            mouseMove(slider, slider.width - 2, slider.height / 2)
            mouseRelease(slider, slider.width - 2, slider.height / 2)
            verify(slider.value > 0)
        }
    }
}
