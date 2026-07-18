import QtQml
import QtQuick
import QtTest
import SuopNotesUi

// ChromeWindow is the shared base for the app's frameless, permanently-mapped
// "chrome" windows. These tests document the wiring it owns — the map-once
// startup ritual and the off-screen park — at the level the offscreen test
// platform can observe (the window stays mapped, the input mask is applied,
// mapped() fires, park() moves the window off every screen). The compositor-
// level behavior the ritual exists for (surface remap not disrupting a note's
// focus, blur/park races) only reproduces on real Wayland and is verified
// there, not here.
Item {
    id: root
    width: 200
    height: 200

    ChromeWindow {
        id: chrome
        ruleId: "chromeWindowTest"
        title: "suopNotes-chromeWindowTest"
        width: 120
        height: 40
    }

    SignalSpy {
        id: mappedSpy
        target: chrome
        signalName: "mapped"
    }

    SignalSpy {
        id: maskSpy
        target: windowInputMask
        signalName: "inputMaskApplied"
    }

    TestCase {
        name: "ChromeWindow"
        when: windowShown

        function test_it_maps_once_and_stays_visible_rather_than_toggling_show_hide() {
            // The whole point of the ritual: the window is mapped at startup and
            // never unmapped again (unmapping disrupts a focused note's input),
            // so its own `visible` stays true for the session — "hidden" is a
            // park, not a hide().
            tryCompare(chrome, "visible", true)
        }

        function test_the_map_once_ritual_emits_mapped_exactly_once() {
            // Fired from Component.onCompleted, so it has already happened by the
            // time the test runs — the count is what a deriving window's
            // after-map work (blur) hangs off.
            compare(mappedSpy.count, 1)
        }

        function test_the_ritual_blocks_input_until_content_is_shown() {
            // The ritual applies the input mask (setAcceptsInput false) so a
            // just-mapped, empty chrome window can't eat clicks before it has
            // anything to show. Offscreen QPA can't route real clicks, so this
            // asserts the wiring fired via WindowInputMask's own signal.
            verify(maskSpy.count > 0)
        }

        function test_park_moves_the_window_off_every_screen() {
            chrome.park()
            const point = positionManager.offscreenPoint()
            compare(chrome.x, point.x)
            compare(chrome.y, point.y)
            for (var i = 0; i < Qt.application.screens.length; ++i) {
                var s = Qt.application.screens[i]
                verify(chrome.x > s.virtualX + s.width)
                verify(chrome.y > s.virtualY + s.height)
            }
        }
    }
}
