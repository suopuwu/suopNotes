// NoteHud — top-right corner hover-expand icon strip for NoteWindow.qml.
// Modeled on suopPanel's TileGridHud.qml (same collapsed-dot / hover-expand
// / glyph-as-Text-with-HoverHandler-and-TapHandler interaction pattern) but
// deliberately simpler: this app has only a handful of actions (see Signals
// below) and no shared theme system, so colors are plain properties with sensible
// defaults instead of a required `theme` token object.
//
// Properties (host sets/binds these):
//   backdropSource  — the item whose pixels this ribbon renders blurred
//                     behind itself (frosted glass). See backdropSource's
//                     own comment below for the two-part contract the host
//                     has to hold up; null (the default) disables the
//                     effect entirely and the ribbon is plainly
//                     translucent, as it was before this existed.
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
import QtQuick.Effects
import Qt5Compat.GraphicalEffects

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
    // Frosted glass (feature request: "blur text beneath the ribbon"). The
    // note's own compositor blur (NoteAppearance.blurEnabled → KWindowEffects)
    // cannot do this job: it blurs whatever sits behind the note's WINDOW,
    // and this ribbon's problem is the note's own text, drawn by the same
    // window a few nodes earlier. So this is an in-scene effect, and it needs
    // the host's cooperation twice over — a blurred copy alone is not enough,
    // because the sharp text underneath is already painted by the time this
    // ribbon draws, and compositing a blurred copy on top of it yields a
    // double image rather than frosted glass. The host must therefore:
    //
    //   1. set this to the item holding the text to blur, and
    //   2. stop that text being drawn under the ribbon's own footprint in
    //      the first place (NoteWindow.qml punches a rounded-rect hole in
    //      editorBackdrop's rendering — see its layer.effect).
    //
    // Crucially this must be set to a DESCENDANT of the item carrying the
    // hole, never the holed item itself. Confirmed empirically, and it is the
    // opposite of what the docs suggest: a ShaderEffectSource aimed at an item
    // with layer.enabled hands back that layer's texture — hole and all — so
    // pointing both at the same item blurs a texture the hole has already
    // emptied, i.e. blurs nothing. NoteWindow therefore punches the hole on
    // editorFlick and points this at editorFlick.contentItem.
    //
    // The ribbon's own translucency survives all this: the hole removes only
    // the TEXT, so the note's background — and whatever compositor blur shows
    // through it — is still what this ribbon sits on, exactly as before.
    property Item backdropSource: null
    readonly property bool backdropActive: hud.backdropSource !== null

    // The item whose bounds are the visible window onto backdropSource —
    // NoteWindow passes editorFlick, whose bounds are the scroll viewport.
    //
    // Needed because a ShaderEffectSource does NOT honour its source item's
    // clip (confirmed empirically: a Flickable scrolled so a marker was fully
    // out of view still captured that marker). Without this clamp the ribbon
    // blurs in text from ABOVE the viewport — text the user cannot see — which
    // is both wrong and, since the ribbon lives at the note's top edge, the
    // case that comes up every time a note is scrolled at all. Clamping the
    // captured rect to these bounds is the clip the capture doesn't do itself.
    property Item backdropViewport: null

    // Registers a binding dependency on geometry that mapToItem() walks but
    // that QML cannot see it walking — mapToItem() is a function call, so it
    // reads nothing as far as dependency tracking is concerned. Callers pass a
    // sum of the properties involved; isNaN makes the answer always false, so
    // the `if (_dependOn(...)) return` guards this sits behind never fire. The
    // guard shape exists only to give the sum somewhere to go.
    //
    // Writing those reads the obvious way — `void someItem.x`, a read whose
    // value is thrown away — does NOT work, and fails silently. Observed, not
    // theorised: with void reads the captured rect never re-aimed when a note
    // was scrolled, so the ribbon blurred whatever text happened to be under
    // it when the note opened, forever. The same file loaded INTERPRETED
    // tracked scrolling perfectly, which is the tell — the same "works one
    // way, not the other under qmlcachegen" trap pinGlyphItem documents above.
    //
    // What is load bearing is that the reads are consumed by this CALL:
    // arguments get evaluated at the call site, so they survive. It is not the
    // isNaN — replacing this body with `void geometrySum; return false` still
    // works, because the caller has already done the reading. Both facts are
    // pinned by test_captured_region_follows_scrolling, which fails the moment
    // any call site here goes back to void reads. So: don't "simplify" these
    // call sites, and don't inline the sum as a bare statement — an unused sum
    // is just as droppable, and the failure is silent every time.
    function _dependOn(geometrySum) {
        return isNaN(geometrySum)
    }

    function _intersect(a, b) {
        const x1 = Math.max(a.x, b.x)
        const y1 = Math.max(a.y, b.y)
        const x2 = Math.min(a.x + a.width, b.x + b.width)
        const y2 = Math.min(a.y + a.height, b.y + b.height)
        return Qt.rect(x1, y1, Math.max(0, x2 - x1), Math.max(0, y2 - y1))
    }

    // Captured margin around the ribbon, so the blur has real neighbouring
    // pixels to pull in at the edges instead of ClampToEdge smearing the
    // outermost column/row of the captured rect inward — most visible along
    // the ribbon's left edge, where genuine text usually continues.
    readonly property int backdropPadding: 16

    // The ribbon's own width animates (see the Behavior below), and
    // sourceRect drives the captured texture's size — so binding the capture
    // to the live width would reallocate an FBO on every frame of that 180ms
    // expand. Capturing the EXPANDED footprint unconditionally keeps the
    // texture a fixed size and a fixed spot on screen: this Rectangle is
    // right-anchored by the host, so `x + width` never moves, and the
    // expression below is anchored to that fixed right edge. The chip then
    // simply grows across an already-captured, already-blurred region rather
    // than dragging it along.
    readonly property real _expandedWidth: hudRow.implicitWidth + 12

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

    // The chip's translucent fill. Used to be this Rectangle's own `color`,
    // moved out to a sibling (see chipTint below) once backdropSource
    // arrived: a Rectangle's fill always paints BENEATH its children, so
    // leaving it there would have forced the blurred backdrop to composite
    // on top of the tint rather than under it — making blurred text read
    // brighter on the ribbon than the sharp text does on the note itself,
    // which is the opposite of what frosted glass looks like.
    property color chipColor: Qt.rgba(0, 0, 0, 0.28)

    height: 26
    width: hud.expanded ? hudRow.implicitWidth + 12 : 28
    clip: true
    radius: hud.cornerRadius
    color: "transparent"

    Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: (event) => event.accepted = true
    }

    HoverHandler { id: hudHover }

    readonly property bool expanded: hud.alwaysExpanded || hudHover.hovered

    // The region to capture, in this ribbon's OWN coordinates, clamped to the
    // viewport (see backdropViewport). Also the geometry the blur is drawn
    // at, so the captured pixels land back exactly where they came from —
    // the two cannot disagree, because both read this one property.
    //
    // mapToItem() is a plain function call, not a property read, so QML
    // registers no dependency on any of the geometry it walks. _dependOn()
    // below is what makes these bindings re-evaluate at all; without it the
    // captured region silently freezes wherever it first resolved, and a note
    // that is later moved or resized has its ribbon cheerfully blurring some
    // other part of the document.
    //
    // Deliberately reads NO backdropSource geometry: it does not need any
    // (the clamp is pure viewport arithmetic), and reading it HERE produced a
    // real binding loop — confirmed by bisection — because this feeds
    // sourceRect, and touching the source's geometry from the same binding
    // that drives the capture closes the cycle. The source-side read lives in
    // _backdropSourceRect below, alone, where it terminates.
    readonly property rect _backdropLocalRect: {
        if (!hud.backdropSource)
            return Qt.rect(0, 0, 0, 0)
        if (hud._dependOn(hud.x + hud.y + hud.width + hud.height))
            return Qt.rect(0, 0, 0, 0)
        const p = hud.backdropPadding
        const padded = Qt.rect(hud.width - hud._expandedWidth - p, -p,
                               hud._expandedWidth + p * 2, hud.height + p * 2)
        if (!hud.backdropViewport)
            return padded
        const vp = hud.backdropViewport
        if (hud._dependOn(vp.x + vp.y + vp.width + vp.height))
            return Qt.rect(0, 0, 0, 0)
        return hud._intersect(padded, vp.mapToItem(hud, 0, 0, vp.width, vp.height))
    }

    // The same region in backdropSource's coordinates — what
    // ShaderEffectSource.sourceRect wants.
    //
    // The backdropSource position read is also what makes the blur follow
    // scrolling: NoteWindow points backdropSource at a Flickable's
    // contentItem, whose y IS -contentY, so scrolling moves it and this
    // re-resolves onto whatever text is under the ribbon now. Without that
    // read the ribbon keeps blurring the text that happened to be there when
    // the note opened — which is exactly what a test caught.
    readonly property rect _backdropSourceRect: {
        const r = hud._backdropLocalRect
        if (!hud.backdropSource || r.width <= 0 || r.height <= 0)
            return Qt.rect(0, 0, 0, 0)
        const src = hud.backdropSource
        if (hud._dependOn(src.x + src.y))
            return Qt.rect(0, 0, 0, 0)
        return hud.mapToItem(src, r.x, r.y, r.width, r.height)
    }

    // Exposed for tests only, same readonly-item-alias convention as the
    // glyphs above.
    readonly property alias backdropItem: backdrop

    // Declared before every other visual child so it composites underneath
    // them: blurred text first, then chipTint over it, then the glyphs.
    Item {
        id: backdrop
        anchors.fill: parent
        visible: hud.backdropActive

        // Crops the blur to the ribbon's rounded silhouette. The parent's
        // own `clip: true` is not enough on its own — clip is rectangular,
        // so the blur would square off the corners the chip rounds. Same
        // layer + OpacityMask idiom mainFrame and GlassOverlay already use
        // for exactly this.
        layer.enabled: true
        layer.effect: OpacityMask {
            maskSource: Rectangle {
                width: backdrop.width
                height: backdrop.height
                radius: hud.radius
            }
        }

        // visible: false because this exists only to hand a texture to the
        // MultiEffect below — a ShaderEffectSource keeps its texture updated
        // regardless of its own visibility. `live` follows backdropActive so
        // a host that never sets backdropSource pays nothing at all.
        ShaderEffectSource {
            id: backdropCapture
            visible: false
            live: hud.backdropActive
            sourceItem: hud.backdropSource
            sourceRect: hud._backdropSourceRect
        }

        // Drawn at exactly the rect that was captured, so the blurred pixels
        // land back where they came from. autoPaddingEnabled is off because
        // the padding is already inside the captured rect — letting
        // MultiEffect add its own on top would silently shift this geometry
        // out from under that math.
        MultiEffect {
            source: backdropCapture
            x: hud._backdropLocalRect.x
            y: hud._backdropLocalRect.y
            width: hud._backdropLocalRect.width
            height: hud._backdropLocalRect.height
            autoPaddingEnabled: false
            blurEnabled: true
            blur: 1.0
            blurMax: 32
        }
    }

    Rectangle {
        id: chipTint
        anchors.fill: parent
        radius: hud.radius
        color: hud.chipColor
    }

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
