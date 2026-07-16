// NoteHud — top-right corner hover-expand icon strip for NoteWindow.qml.
// Modeled on suopPanel's TileGridHud.qml (same collapsed-dot / hover-expand
// / glyph-as-Text-with-HoverHandler-and-TapHandler interaction pattern) but
// deliberately simpler: this app has only a handful of actions (see Signals
// below) and no shared theme system, so colors are plain properties with sensible
// defaults instead of a required `theme` token object. No glass/blur
// treatment here — that's wired independently elsewhere.
//
// Properties (host sets/binds these):
//   iconColor      — glyph color when not toggled "on" (default white)
//   accentColor     — glyph color when toggled "on" (default light blue)
//   pinned          — when true, the pin glyph renders in accentColor
//   alwaysExpanded  — keep the full icon row showing even without hover
//                     (kept for parity with the reference; not currently
//                     used by the host, defaults false so hover-only is
//                     the default behavior)
//
// Signals (host wires these to its own actions):
//   pinToggleRequested() — pin glyph tapped
//   settingsRequested()  — gear glyph tapped (from collapsed placeholder
//                          or the expanded row)
//   duplicateRequested() — duplicate glyph tapped
//   searchRequested()    — search glyph tapped (host opens find/replace)
//   closeRequested()     — close glyph tapped TWICE (armed, then confirmed)
//                          within closeArmTimeoutMs — see closeArmed below.
//                          Deleting a note's file is irreversible, so a
//                          single stray click on a small hover-revealed icon
//                          must not be enough to do it.

import QtQuick

Rectangle {
    id: hud

    property color iconColor: "#ffffff"
    property color accentColor: "#4da6ff"
    property color dangerColor: "#ff5555"
    property bool pinned: false
    property bool alwaysExpanded: false
    // Follows the note window's own curvature (feature request) — host
    // binds this to mainFrame.radius, so the ribbon reads as an extension
    // of the note's frame rather than a fixed-radius chip glued on top of
    // it. Qt's Rectangle.radius auto-clamps to half of min(width,height)
    // (see GlassOverlay.qml's own note on this), so a large window radius
    // just rounds the HUD toward a full pill rather than overflowing.
    property real cornerRadius: 8
    // Glass treatment (feature request): mirrors suopPanel's TileGridHud,
    // which layers its own GlassOverlay on top of the HUD's chrome so the
    // ribbon reads as the same "material" as the rest of a glass-mode
    // surface, not a flat dark chip sitting on top of it. Host binds these
    // straight from NoteAppearance, same properties mainFrame's own
    // GlassOverlay in NoteWindow.qml already uses.
    property bool glassEnabled: false
    property color glassTintColor: "#ffffff"
    property real glassSheenOpacity: 0.12
    property real glassSheenStrength: 10
    property real glassSheenSize: 0.18
    // First tap on the close glyph "arms" it (turns dangerColor) instead of
    // immediately requesting deletion; a second tap within this window
    // confirms. Exposed as a property (not a hardcoded constant) so tests
    // can shorten it rather than actually sleeping ~3s.
    property int closeArmTimeoutMs: 3000
    // Exposed for tests only.
    readonly property alias closeArmed: hud._closeArmed
    property bool _closeArmed: false

    signal pinToggleRequested()
    signal settingsRequested()
    signal duplicateRequested()
    signal searchRequested()
    signal closeRequested()

    Timer {
        id: closeArmTimer
        interval: hud.closeArmTimeoutMs
        onTriggered: hud._closeArmed = false
    }

    // Collapsing (mouse leaves the note entirely) always disarms — an armed
    // close glyph shouldn't silently wait to be confirmed by a later,
    // unrelated hover.
    onExpandedChanged: if (!expanded) { hud._closeArmed = false; closeArmTimer.stop() }

    // Exposed for tests only — direct access to the glyph items (so a test
    // can mouseClick() the glyph itself rather than guess its pixel offset
    // inside the packed Row), consistent with NoteWindow.qml's
    // readonly-property-alias testability pattern (e.g. mouseDragArea).
    // Deliberately NOT also aliasing straight through to `.color` (e.g.
    // `alias pinGlyphColor: pinGlyph.color`): confirmed live under this Qt
    // build that a readonly alias to a child's *bound* (not plain-stored)
    // property doesn't reliably re-evaluate through the alias chain once
    // qmlcachegen AOT-compiles it — reading pinGlyph.color directly through
    // the item reference below always reflects the live value; the
    // equivalent color-only alias was observed stuck at its first-resolved
    // value across later `pinned` changes. Read `.pinGlyphItem.color`
    // directly instead.
    readonly property alias pinGlyphItem: pinGlyph
    readonly property alias gearGlyphItem: gearGlyph
    readonly property alias duplicateGlyphItem: duplicateGlyph
    readonly property alias searchGlyphItem: searchGlyph
    readonly property alias closeGlyphItem: closeGlyph

    height: 26
    width: hud.expanded ? hudRow.implicitWidth + 12 : 28
    clip: true
    radius: hud.cornerRadius
    color: Qt.rgba(0, 0, 0, 0.28)

    Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: (event) => event.accepted = true
    }

    HoverHandler { id: hudHover }

    readonly property bool expanded: hud.alwaysExpanded || hudHover.hovered

    // Collapsed-state placeholder: a single always-visible, always-tappable
    // gear so idle state is never an empty dark square. Hidden once the
    // row below expands (hover or always-expanded).
    Text {
        visible: !hud.expanded
        anchors.centerIn: parent
        text: "⚙"
        font.pixelSize: 16
        color: hud.iconColor
        opacity: 0.7

        TapHandler { onTapped: hud.settingsRequested() }
    }

    Row {
        id: hudRow
        // Right-anchor keeps the row flush against the right edge; close
        // stays last/rightmost (see its own comment below). Only shown
        // when expanded (hover or always-expanded) — the collapsed state
        // shows the standalone gear above instead.
        visible: hud.expanded
        anchors.right:          parent.right
        anchors.rightMargin:    6
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

        // Each glyph sits inside a fixed-size wrapper (feature request: a
        // configurable highlight box behind each interactable, matching
        // the popup toolbar's and scrollbar's own InteractableHighlight —
        // see that file) rather than highlighting the bare glyph text
        // itself, so the hoverable/tappable area and the visible highlight
        // agree on the same bounds. HoverHandler/TapHandler move to the
        // wrapper (was on the Text itself) so the whole box — not just the
        // glyph's own tiny text bounds — is interactive; the Text
        // keeps its own id/objectName/color/opacity bindings unchanged, so
        // existing tests reading e.g. `pinGlyphItem.color` are unaffected.
        Item {
            width: 22
            height: 22
            anchors.verticalCenter: parent.verticalCenter

            InteractableHighlight {
                anchors.fill: parent
                active: pinHov.hovered
            }

            // "⚲" not the pushpin emoji "📌" — the reference file
            // (TileGridHud) notes the pushpin emoji renders in fixed color
            // regardless of the font, ignoring any `color:` binding
            // entirely and silently breaking an on/off color indicator.
            // "⚲" is plain-presentation and actually responds to it.
            Text {
                id: pinGlyph
                objectName: "pinGlyph"
                text: "⚲"
                font.pixelSize: 14
                color: hud.pinned ? hud.accentColor : hud.iconColor
                opacity: pinHov.hovered ? 1.0 : 0.7
                anchors.centerIn: parent
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }

            HoverHandler { id: pinHov; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: hud.pinToggleRequested() }
        }

        Item {
            width: 22
            height: 22
            anchors.verticalCenter: parent.verticalCenter

            InteractableHighlight {
                anchors.fill: parent
                active: gearHov.hovered
            }

            Text {
                id: gearGlyph
                objectName: "gearGlyph"
                text: "⚙"
                font.pixelSize: 16
                color: hud.iconColor
                opacity: gearHov.hovered ? 1.0 : 0.7
                anchors.centerIn: parent
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }

            HoverHandler { id: gearHov; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: hud.settingsRequested() }
        }

        Item {
            width: 22
            height: 22
            anchors.verticalCenter: parent.verticalCenter

            InteractableHighlight {
                anchors.fill: parent
                active: duplicateHov.hovered
            }

            // "⧉" for the same reason "⚲" is used for pin above rather than
            // an emoji: it's plain-presentation, so it actually honors the
            // `color` binding below instead of rendering in a fixed emoji
            // color and ignoring it.
            Text {
                id: duplicateGlyph
                objectName: "duplicateGlyph"
                text: "⧉"
                font.pixelSize: 16
                color: hud.iconColor
                opacity: duplicateHov.hovered ? 1.0 : 0.7
                anchors.centerIn: parent
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }

            HoverHandler { id: duplicateHov; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: hud.duplicateRequested() }
        }

        Item {
            width: 22
            height: 22
            anchors.verticalCenter: parent.verticalCenter

            InteractableHighlight {
                anchors.fill: parent
                active: searchHov.hovered
            }

            // "⌕" for the same reason "⚲"/"⧉" are used above rather than
            // the 🔍 emoji: plain-presentation, so it honors the `color`
            // binding instead of rendering in fixed emoji color.
            Text {
                id: searchGlyph
                objectName: "searchGlyph"
                text: "⌕"
                font.pixelSize: 16
                color: hud.iconColor
                opacity: searchHov.hovered ? 1.0 : 0.7
                anchors.centerIn: parent
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }

            HoverHandler { id: searchHov; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: hud.searchRequested() }
        }

        // Rightmost, matching the reference ribbon's own convention of
        // putting close last. Deleting a note's file can't be undone, so
        // this requires two taps (arm, then confirm) rather than firing
        // closeRequested() on the first — see closeArmTimeoutMs above.
        Item {
            width: 22
            height: 22
            anchors.verticalCenter: parent.verticalCenter

            InteractableHighlight {
                anchors.fill: parent
                active: closeHov.hovered || hud._closeArmed
            }

            Text {
                id: closeGlyph
                objectName: "closeGlyph"
                text: "✕"
                font.pixelSize: 14
                color: hud._closeArmed ? hud.dangerColor : hud.iconColor
                opacity: closeHov.hovered || hud._closeArmed ? 1.0 : 0.7
                anchors.centerIn: parent
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }

            HoverHandler { id: closeHov; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                onTapped: {
                    if (hud._closeArmed) {
                        hud._closeArmed = false
                        closeArmTimer.stop()
                        hud.closeRequested()
                    } else {
                        hud._closeArmed = true
                        closeArmTimer.restart()
                    }
                }
            }
        }
    }

    // Layered on top of the chrome (declared last), same as mainFrame's own
    // glass treatment in NoteWindow.qml — has no input handling of its own,
    // so it never blocks the glyph taps above.
    GlassOverlay {
        anchors.fill: parent
        cornerRadius: hud.radius
        visible: hud.glassEnabled
        tintColor: hud.glassTintColor
        sheenOpacity: hud.glassSheenOpacity
        ringCount: hud.glassSheenStrength
        sheenSize: hud.glassSheenSize
    }
}
