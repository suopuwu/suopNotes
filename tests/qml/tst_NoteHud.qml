import QtQml
import QtQuick
import QtTest
import SuopNotesUi

Item {
    id: root
    width: 200
    height: 100

    // Sits directly behind the HUD (declared first + explicit z below the
    // HUD's own z, so it never shadows the HUD's glyphs for hit-testing) so
    // a click that incorrectly fell through the HUD's chrome would land
    // here instead.
    MouseArea {
        id: behindArea
        z: -1
        anchors.fill: hud
        onClicked: behindClickCount++
        property int behindClickCount: 0
    }
    property alias behindClickCount: behindArea.behindClickCount

    // alwaysExpanded so the full pin+gear row is laid out and tappable
    // without needing to synthesize a real hover-enter first.
    NoteHud {
        id: hud
        z: 1
        alwaysExpanded: true
        anchors.right: parent.right
        anchors.top: parent.top
        // Shortened so the timeout-revert test doesn't need to actually
        // wait ~3s.
        closeArmTimeoutMs: 100
    }

    // A plain default-constructed instance (no properties bound) to verify
    // the component comes up cleanly with just its declared defaults.
    NoteHud {
        id: defaultHud
    }

    SignalSpy {
        id: pinSpy
        target: hud
        signalName: "pinToggleRequested"
    }

    SignalSpy {
        id: settingsSpy
        target: hud
        signalName: "settingsRequested"
    }

    SignalSpy {
        id: duplicateSpy
        target: hud
        signalName: "duplicateRequested"
    }

    SignalSpy {
        id: closeSpy
        target: hud
        signalName: "closeRequested"
    }

    TestCase {
        name: "NoteHud"
        when: windowShown

        function init() {
            pinSpy.clear()
            settingsSpy.clear()
            duplicateSpy.clear()
            closeSpy.clear()
            behindArea.behindClickCount = 0
            hud._closeArmed = false
        }

        function test_instantiates_with_default_properties() {
            compare(defaultHud.pinned, false)
            compare(defaultHud.alwaysExpanded, false)
            compare(defaultHud.iconColor, "#ffffff")
            compare(defaultHud.accentColor, "#4da6ff")
        }

        function test_tapping_gear_glyph_emits_settingsRequested() {
            const glyph = hud.gearGlyphItem
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)
            compare(settingsSpy.count, 1)
        }

        function test_tapping_pin_glyph_emits_pinToggleRequested() {
            const glyph = hud.pinGlyphItem
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)
            compare(pinSpy.count, 1)
            compare(settingsSpy.count, 0)
        }

        // Tapping the duplicate glyph asks the host to make a copy of the
        // note (once, and without also triggering any other action).
        function test_tapping_duplicate_glyph_emits_duplicateRequested() {
            const glyph = hud.duplicateGlyphItem
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)
            compare(duplicateSpy.count, 1)
            compare(pinSpy.count, 0)
            compare(settingsSpy.count, 0)
            compare(closeSpy.count, 0)
        }

        // The close glyph deletes the note's file, so it must stay the
        // rightmost (last-reached) icon in the row — a copy or settings tap
        // must never sit past it where a stray click meant for close could
        // land. Verified by comparing the glyphs' scene-mapped x positions.
        function test_close_glyph_stays_rightmost_of_all_icons() {
            const closePos = hud.closeGlyphItem.mapToItem(root, 0, 0)
            const pinPos = hud.pinGlyphItem.mapToItem(root, 0, 0)
            const gearPos = hud.gearGlyphItem.mapToItem(root, 0, 0)
            const duplicatePos = hud.duplicateGlyphItem.mapToItem(root, 0, 0)
            verify(closePos.x > pinPos.x)
            verify(closePos.x > gearPos.x)
            verify(closePos.x > duplicatePos.x)
        }

        // Reads pinGlyph's color via the pinGlyphItem alias (a live item
        // reference), not a chained color-only alias — see NoteHud.qml's
        // comment on pinGlyphItem for why the latter doesn't reliably
        // re-evaluate under this Qt build's AOT compilation. Also compares
        // via .toString(), not raw !==: confirmed live that QML `color`
        // values don't support meaningful strict-inequality comparison in
        // this engine (two demonstrably different colors compared !== as
        // equal) — compare()/Qt.colorEqual handle color specially, but bare
        // !== does not, so string form is the safe, unambiguous way to
        // assert two colors differ.
        function test_pinned_glyph_color_differs_from_unpinned() {
            hud.pinned = false
            wait(0)
            const unpinnedColor = hud.pinGlyphItem.color.toString()
            hud.pinned = true
            wait(0)
            const pinnedColor = hud.pinGlyphItem.color.toString()
            verify(unpinnedColor !== pinnedColor)
            compare(pinnedColor, hud.accentColor.toString())
            hud.pinned = false
            wait(0)
            compare(hud.pinGlyphItem.color.toString(), hud.iconColor.toString())
        }

        function test_hud_mouse_area_swallows_clicks_before_they_reach_whats_behind() {
            mouseClick(hud, hud.width / 2, hud.height / 2)
            compare(behindClickCount, 0, "a click on the HUD's own chrome must not fall through to the item behind it")
        }

        // Deleting a note's file is irreversible — a single tap must only
        // arm the close glyph (turn it dangerColor), not fire closeRequested.
        function test_single_tap_on_close_glyph_arms_but_does_not_request_close() {
            const glyph = hud.closeGlyphItem
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)
            compare(closeSpy.count, 0)
            verify(hud.closeArmed)
            compare(glyph.color.toString(), hud.dangerColor.toString())
        }

        function test_second_tap_within_timeout_confirms_close() {
            const glyph = hud.closeGlyphItem
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)
            compare(closeSpy.count, 1)
            verify(!hud.closeArmed, "confirming should disarm, not leave it armed for a third tap")
        }

        function test_arm_reverts_after_timeout_without_a_second_tap() {
            const glyph = hud.closeGlyphItem
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)
            verify(hud.closeArmed)
            wait(hud.closeArmTimeoutMs + 50)
            verify(!hud.closeArmed)
            compare(glyph.color.toString(), hud.iconColor.toString())

            // A tap now is a fresh first tap (arms again), not a confirm.
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)
            compare(closeSpy.count, 0)
            verify(hud.closeArmed)
        }

        function test_close_tap_does_not_trigger_pin_or_settings() {
            const glyph = hud.closeGlyphItem
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)
            compare(pinSpy.count, 0)
            compare(settingsSpy.count, 0)
        }
    }
}
