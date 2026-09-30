import QtQml
import QtQuick
import QtTest
import SuopNotesUi

Item {
    id: root
    // Large enough that every fixture the click-based tests use fits INSIDE
    // the test window. Synthesized mouse events are delivered in window
    // coordinates, so a fixture parked past the window's edge silently never
    // receives them — the geometry-only fixtures below (verticalHud, the
    // backdrop note frame) can sit outside because mapToItem() works
    // regardless, but tickTickHud is clicked and cannot.
    width: 400
    height: 400

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

    // A ribbon with the optional TickTick integration switched on, so the
    // new-task glyph exists. Separate from `hud` above rather than toggled on
    // it mid-test: the glyph's absence is the interesting default, and the
    // untouched fixture keeps asserting it.
    NoteHud {
        id: tickTickHud
        x: 0
        y: 120
        alwaysExpanded: true
        tickTickEnabled: true
    }

    // Vertical column variant (the layout NoteHudWindow uses): the same
    // glyphs stacked top-to-bottom instead of left-to-right. Placed clear of
    // the other fixtures so its geometry assertions read cleanly.
    NoteHud {
        id: verticalHud
        x: 0
        y: 450
        vertical: true
        alwaysExpanded: true
    }

    // Windowed-mode stand-in (animateSize: false, matching the ribbon
    // NoteHudWindow.qml actually instantiates) used to verify a note switch
    // snaps straight to its new height instead of easing into it — covering
    // change A directly. Starts with TickTick off so the test below has a
    // toggle to make.
    NoteHud {
        id: snappedHud
        x: 0
        y: 650
        vertical: true
        alwaysExpanded: true
        animateSize: false
        tickTickEnabled: false
    }

    // Frosted-backdrop fixture, mirroring NoteWindow.qml's real geometry: the
    // ribbon is inset 4px from the note's frame while the editor is inset 8px,
    // so the ribbon genuinely overhangs the editor's viewport on two sides.
    // That overhang is the whole reason the capture has to be clamped, so a
    // fixture that didn't reproduce it would prove nothing.
    Item {
        id: noteFrame
        y: 200
        width: 300
        height: 200

        Flickable {
            id: backdropFlick
            anchors.fill: parent
            anchors.margins: 8
            clip: true
            contentWidth: width
            contentHeight: 2000
            Item {
                width: backdropFlick.width
                height: 2000
            }
        }

        NoteHud {
            id: backdropHud
            alwaysExpanded: true
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 4
            // contentItem, not the Flickable itself — see NoteHud's
            // backdropSource for why the captured item must sit below the
            // layer carrying the hole.
            backdropSource: backdropFlick.contentItem
            backdropViewport: backdropFlick
        }
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
        id: newNoteSpy
        target: hud
        signalName: "newNoteRequested"
    }

    SignalSpy {
        id: newTaskSpy
        target: hud
        signalName: "newTaskRequested"
    }

    SignalSpy {
        id: closeSpy
        target: hud
        signalName: "closeRequested"
    }

    SignalSpy {
        id: tickTickNewTaskSpy
        target: tickTickHud
        signalName: "newTaskRequested"
    }

    SignalSpy {
        id: tickTickNewNoteSpy
        target: tickTickHud
        signalName: "newNoteRequested"
    }

    SignalSpy {
        id: searchSpy
        target: hud
        signalName: "searchRequested"
    }

    TestCase {
        name: "NoteHud"
        when: windowShown

        function init() {
            pinSpy.clear()
            settingsSpy.clear()
            newNoteSpy.clear()
            newTaskSpy.clear()
            tickTickNewTaskSpy.clear()
            tickTickNewNoteSpy.clear()
            closeSpy.clear()
            searchSpy.clear()
            behindArea.behindClickCount = 0
            hud._closeArmed = false
        }

        function test_instantiates_with_default_properties() {
            compare(defaultHud.pinned, false)
            compare(defaultHud.alwaysExpanded, false)
            compare(defaultHud.iconColor, "#ffffff")
            compare(defaultHud.accentColor, "#4da6ff")
            // TickTick is an optional module, so the ribbon assumes it is
            // absent unless a host says otherwise.
            compare(defaultHud.tickTickEnabled, false)
        }

        // ---- The new-task glyph, gated on the optional TickTick module ----

        // Absent, not merely disabled: a button that could only ever report
        // "TickTick isn't connected" is worse than no button at all.
        function test_new_task_glyph_is_absent_when_tickTick_is_disabled() {
            verify(!hud.newTaskGlyphItem.visible,
                   "the new-task glyph must not be shown when TickTick is off")
        }

        // With the glyph gone the row closes up rather than leaving a gap, so
        // the ribbon is genuinely narrower — this is what proves it was removed
        // from the layout, not just painted transparent.
        function test_ribbon_is_narrower_without_the_new_task_glyph() {
            verify(hud.width < tickTickHud.width,
                   "a ribbon without the new-task glyph must be narrower than one with it")
        }

        function test_new_task_glyph_appears_when_tickTick_is_enabled() {
            verify(tickTickHud.newTaskGlyphItem.visible)
        }

        // Tapping it asks the host for a blank TickTick task — distinctly, not
        // as a second way to trigger the plain new-note action beside it.
        function test_tapping_new_task_glyph_emits_newTaskRequested() {
            const glyph = tickTickHud.newTaskGlyphItem
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)
            compare(tickTickNewTaskSpy.count, 1)
            compare(tickTickNewNoteSpy.count, 0)
        }

        // The two creation glyphs sit next to each other, with the plain note
        // first — they are a pair, and reading order should say so.
        function test_new_note_glyph_precedes_the_new_task_glyph() {
            const notePos = tickTickHud.newNoteGlyphItem.mapToItem(root, 0, 0)
            const taskPos = tickTickHud.newTaskGlyphItem.mapToItem(root, 0, 0)
            const gearPos = tickTickHud.gearGlyphItem.mapToItem(root, 0, 0)
            verify(notePos.x < taskPos.x)
            verify(taskPos.x < gearPos.x)
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

        // Tapping the plus glyph asks the host for a brand-new blank note
        // (once, and without also triggering any other action). This glyph used
        // to duplicate the note it belongs to; duplicating now lives only in
        // the right-click menu.
        function test_tapping_new_note_glyph_emits_newNoteRequested() {
            const glyph = hud.newNoteGlyphItem
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)
            compare(newNoteSpy.count, 1)
            compare(newTaskSpy.count, 0)
            compare(pinSpy.count, 0)
            compare(settingsSpy.count, 0)
            compare(closeSpy.count, 0)
        }

        // Tapping the search glyph asks the host to open its find/replace
        // panel (once, and without also triggering any other action).
        function test_tapping_search_glyph_emits_searchRequested() {
            const glyph = hud.searchGlyphItem
            mouseClick(glyph, glyph.width / 2, glyph.height / 2)
            compare(searchSpy.count, 1)
            compare(pinSpy.count, 0)
            compare(settingsSpy.count, 0)
            compare(newNoteSpy.count, 0)
            compare(closeSpy.count, 0)
        }

        // The glyph order was reversed (feature request): close now LEADS the
        // row as the leftmost icon (pin trails as the rightmost). Close leading
        // is safe — a stray click can't land before the row's start any more
        // than past its end — and deletion's real safeguard is the two-tap
        // arm/confirm, not close's position. Verified via the glyphs'
        // scene-mapped x positions.
        function test_close_glyph_leads_as_the_leftmost_icon() {
            const closePos = hud.closeGlyphItem.mapToItem(root, 0, 0)
            const pinPos = hud.pinGlyphItem.mapToItem(root, 0, 0)
            const gearPos = hud.gearGlyphItem.mapToItem(root, 0, 0)
            const newNotePos = hud.newNoteGlyphItem.mapToItem(root, 0, 0)
            const searchPos = hud.searchGlyphItem.mapToItem(root, 0, 0)
            verify(closePos.x < pinPos.x)
            verify(closePos.x < gearPos.x)
            verify(closePos.x < newNotePos.x)
            verify(closePos.x < searchPos.x)
            // Pin trails as the rightmost, the mirror of close leading.
            verify(pinPos.x > gearPos.x)
            verify(pinPos.x > newNotePos.x)
            verify(pinPos.x > searchPos.x)
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

        // ---- Vertical column layout (the windowed ribbon's orientation) ----

        // In vertical mode the glyphs stack top-to-bottom in a single column,
        // in the reversed order (feature request): close LEADS at the top
        // (nearest the note's own top-right corner, the conventional close
        // spot), pin trails at the bottom.
        function test_vertical_stacks_glyphs_in_a_column_with_close_on_top() {
            const closePos = verticalHud.closeGlyphItem.mapToItem(root, 0, 0)
            const pinPos = verticalHud.pinGlyphItem.mapToItem(root, 0, 0)
            const gearPos = verticalHud.gearGlyphItem.mapToItem(root, 0, 0)
            const newNotePos = verticalHud.newNoteGlyphItem.mapToItem(root, 0, 0)
            const searchPos = verticalHud.searchGlyphItem.mapToItem(root, 0, 0)

            // Stacked downward with close at the top: close sits above every
            // other glyph, pin below every other.
            verify(closePos.y < pinPos.y)
            verify(closePos.y < gearPos.y)
            verify(closePos.y < newNotePos.y)
            verify(closePos.y < searchPos.y)
            verify(pinPos.y > gearPos.y)
            verify(pinPos.y > newNotePos.y)
            verify(pinPos.y > searchPos.y)

            // A single column: every glyph sits at essentially the same x.
            // Compared with a tolerance rather than exactly equal because each
            // alias points at the centered glyph Text, whose own width varies
            // by a pixel or two with the glyph/font-size — so their centered x
            // differs slightly even though the column is perfectly aligned. The
            // tolerance (10px) is far below the vertical gap between rows (~34px
            // per the y values above), so this still unambiguously asserts a
            // column, not a row.
            const xs = [pinPos.x, gearPos.x, newNotePos.x, searchPos.x, closePos.x]
            verify(Math.max(...xs) - Math.min(...xs) < 10,
                   "all glyphs share one narrow column (x spread stays tiny)")
        }

        // The column is a fixed narrow strip that grows in height, the mirror
        // image of the horizontal row (fixed height, grows in width).
        function test_vertical_ribbon_is_a_narrow_fixed_width_column() {
            verify(verticalHud.width <= 30, "the vertical ribbon stays a narrow fixed-width column")
            verify(verticalHud.height > verticalHud.width, "and is taller than it is wide")
        }

        // Covers change A directly: in windowed mode (animateSize: false)
        // toggling tickTickEnabled must snap the ribbon straight to its new
        // required height rather than easing into it over the 180ms
        // Behavior — a note switch should look like a hard cut, matching the
        // buttons that already change on the same tick (see
        // NoteHudWindow.qml's onNoteWindowChanged).
        function test_animateSize_false_snaps_height_without_easing() {
            compare(snappedHud.animateSize, false,
                    "precondition: this fixture has size animation switched off, like the windowed ribbon")
            const before = snappedHud.height

            snappedHud.tickTickEnabled = true

            // One short wait lets the GridLayout recompute implicitHeight
            // (an unrelated layout pass, nothing to do with any animation)
            // and settle on the new value.
            wait(30)
            const settled = snappedHud.height
            verify(settled !== before,
                   "the new-task glyph changed the ribbon's required height")

            // If the 180ms Behavior were (wrongly) still active, height would
            // still be creeping toward its target this far in — 30ms plus
            // another 100ms is comfortably short of 180ms. With no animation
            // running there's nothing left to finish, so it must read
            // exactly the same value a good while later.
            wait(100)
            compare(snappedHud.height, settled,
                    "height held steady — nothing was still easing toward a target")
        }

        // ---- Frosted backdrop (blurring the note's text under the ribbon) ----

        // A host that asks for nothing gets nothing: the ribbon is plainly
        // translucent, as it was before the backdrop existed, and none of the
        // capture/blur machinery is switched on.
        function test_backdrop_is_off_when_no_source_is_given() {
            compare(defaultHud.backdropSource, null)
            compare(defaultHud.backdropViewport, null)
            compare(defaultHud.backdropActive, false)
            compare(defaultHud.backdropItem.visible, false)
        }

        function test_backdrop_turns_on_once_a_source_is_given() {
            compare(backdropHud.backdropActive, true)
            compare(backdropHud.backdropItem.visible, true)
        }

        // The captured region never reaches outside what is actually on
        // screen. This matters because a ShaderEffectSource does NOT honour
        // its source item's clip, and the ribbon deliberately overhangs the
        // editor's viewport (4px inset vs 8px) — so without the clamp the
        // ribbon would blur in text scrolled above the viewport, i.e. text
        // the user cannot see.
        function test_captured_region_is_clamped_to_the_visible_viewport() {
            const r = backdropHud._backdropLocalRect
            verify(r.width > 0)
            verify(r.height > 0)

            // Expressed in the viewport's own coordinates, the captured rect
            // must sit entirely inside it.
            const inViewport = backdropHud.mapToItem(backdropFlick, r.x, r.y,
                                                     r.width, r.height)
            verify(inViewport.x >= -0.01)
            verify(inViewport.y >= -0.01)
            verify(inViewport.x + inViewport.width <= backdropFlick.width + 0.01)
            verify(inViewport.y + inViewport.height <= backdropFlick.height + 0.01)
        }

        // Scrolling moves the text under the ribbon, so the blur has to
        // re-aim at whatever is there now. The captured rect is fixed in
        // VIEWPORT terms (the ribbon doesn't move) but slides in the
        // scrolling content's own terms, by exactly the scrolled distance.
        //
        // This is the test that caught the `void`-read dependency being
        // optimized away — see NoteHud's _backdropSourceRect. It failed with
        // the blur permanently aimed at whatever text was under the ribbon
        // when the note opened, which is precisely the bug a reader would
        // never guess from the binding's source.
        // Each "before" value is read out as a NUMBER, never held as a rect.
        // Assigning the rect property to a local does not snapshot it — QML
        // hands back a live wrapper onto the property, so a "before" rect and
        // an "after" rect would both report the current value and every
        // comparison here would pass vacuously.
        function test_captured_region_follows_scrolling() {
            backdropFlick.contentY = 0
            tryCompare(backdropFlick.contentItem, "y", 0)
            const localYBefore = backdropHud._backdropLocalRect.y
            const localHeightBefore = backdropHud._backdropLocalRect.height
            const sourceYBefore = backdropHud._backdropSourceRect.y
            const sourceHeightBefore = backdropHud._backdropSourceRect.height

            backdropFlick.contentY = 120
            // A Flickable repositions its contentItem on its own update
            // cycle, not synchronously from the contentY assignment — and
            // contentItem.y (which IS -contentY) is exactly what the capture
            // is bound to, so wait for that move rather than the assignment.
            tryCompare(backdropFlick.contentItem, "y", -120)

            // Fixed relative to the ribbon/viewport...
            compare(backdropHud._backdropLocalRect.y, localYBefore)
            compare(backdropHud._backdropLocalRect.height, localHeightBefore)
            // ...but tracking the content that scrolled under it.
            compare(backdropHud._backdropSourceRect.y - sourceYBefore, 120)
            compare(backdropHud._backdropSourceRect.height, sourceHeightBefore)

            backdropFlick.contentY = 0
        }

        // The capture is deliberately sized to the EXPANDED ribbon and pinned
        // to its fixed right edge, so the 180ms expand animation never
        // resizes the texture (which would reallocate an FBO every frame).
        // Scalars, not rects — see test_captured_region_follows_scrolling for
        // why holding the rect itself would make this pass no matter what.
        function test_captured_region_does_not_resize_as_the_ribbon_expands() {
            backdropHud.alwaysExpanded = true
            const expandedX = backdropHud._backdropSourceRect.x
            const expandedY = backdropHud._backdropSourceRect.y
            const expandedWidth = backdropHud._backdropSourceRect.width
            const expandedHeight = backdropHud._backdropSourceRect.height
            const expandedChipWidth = backdropHud.width

            backdropHud.alwaysExpanded = false
            // The chip really does get narrower — via the 180ms width
            // Behavior, so this is the animation whose per-frame width
            // changes must not drag the captured texture along with them.
            tryVerify(function() { return backdropHud.width < expandedChipWidth })
            // ...while the captured region stays put, at the same size.
            compare(backdropHud._backdropSourceRect.x, expandedX)
            compare(backdropHud._backdropSourceRect.y, expandedY)
            compare(backdropHud._backdropSourceRect.width, expandedWidth)
            compare(backdropHud._backdropSourceRect.height, expandedHeight)

            backdropHud.alwaysExpanded = true
        }
    }
}
