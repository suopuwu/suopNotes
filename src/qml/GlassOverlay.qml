import QtQuick
import Qt5Compat.GraphicalEffects

Item {
    id: root

    required property real cornerRadius
    property color tintColor: "#ffffff"
    property real sheenOpacity: 0.12
    property real grainOpacity: 0.035
    property real grainDensity: 0.5
    property real ringCount: 10   // 0-100 density: 0 = 1 ring, 100 = _maxSheenRings
    // Extra corner radius, ramped in per sheen ring rather than applied as a
    // flat boost to everything: ring 0 (outermost, widest+faintest, painted
    // first — see the Repeater below) stays at exactly `cornerRadius`, so
    // its edge is flush with the tile/element underneath; each successive,
    // narrower/brighter ring interpolates further toward
    // `cornerRadius + curveBoost`. A flat boost applied to every ring (and to
    // rim/rimFade) made the *entire* stack's edge — including the crisp 1px
    // rim border — visibly disagree with the tile's actual corner, reading
    // as a mismatched border rather than a softer glass edge; ramping it in
    // means only the innermost rings grow rounder, and rim/rimFade (the
    // outermost, edge-tracing accents) stay unboosted at `cornerRadius`
    // below, so there's no visible seam at the tile's real edge. Qt's own
    // Rectangle.radius silently clamps to half of min(width,height), so no
    // manual clamping is needed here even at extreme values.
    property real curveBoost: 0
    function _sheenRingRadius(index) {
        const t = root._sheenRings > 1 ? index / (root._sheenRings - 1) : 0
        return Math.max(0, root.cornerRadius + root.curveBoost * t)
    }
    // The innermost ring's radius — the most any single ring is boosted.
    readonly property real _effectiveRadius: root._sheenRingRadius(root._sheenRings - 1)
    // Sheen ring spread as a fraction of the overlay's own smaller dimension
    // — was a hardcoded 0.18 constant, now themeable (see Style.qml's
    // glassSheenSize) so users can make the sheen band wider or tighter.
    property real sheenSize: 0.18

    enabled: false

    // This mask defines the glass pane's own outer silhouette — it must
    // stay flush with the tile/element underneath (unboosted `cornerRadius`,
    // not `_effectiveRadius`), or the translucent overlay's visible edge
    // would disagree with the actual tile boundary it's covering (a gap or
    // overhang at the corners). curveBoost only rounds the sheen/rim
    // decorations drawn *within* this silhouette, below.
    layer.enabled: true
    layer.effect: OpacityMask {
        maskSource: Rectangle {
            width:  root.width
            height: root.height
            radius: root.cornerRadius
        }
    }

    // Sheen: a stack of concentric borders, widest+faintest first,
    // narrowest+brightest on top — see GlassAccent.qml's "Glow" comment for
    // why border.width must stay <= radius. Here each ring also has its own
    // (possibly curve-boosted, see _sheenRingRadius above) radius, so each
    // ring clamps its spread to its *own* radius (below) rather than a
    // single shared clamp: ring 0's radius is the smallest (unboosted) while
    // its spread is the largest.
    readonly property real _rawSheenSpread: Math.min(root.width, root.height) * root.sheenSize
    // ringCount is a 0-100 density dial, not a literal count — it always
    // feeds this one formula rather than bypassing it, so there's no
    // separate "auto" mode to fall back to.
    readonly property int _maxSheenRings: 40
    readonly property int _sheenRings: Math.max(1, Math.round(1 + (root.ringCount / 100) * (root._maxSheenRings - 1)))

    // See GlassAccent.qml's _glowInk for why: every ring's band starts at
    // the edge, so all N rings overlap there, and per-ring alpha simply
    // proportional to (index+1)/N made the total composited "ink" grow
    // with N — dividing the target ink by N² instead keeps it roughly
    // constant as ring count changes, so ring count mostly changes
    // smoothness rather than perceived intensity.
    readonly property real _sheenInk: -Math.log(1 - Math.min(root.sheenOpacity, 0.99))

    Repeater {
        model: root.sheenOpacity > 0 ? root._sheenRings : 0
        delegate: Rectangle {
            required property int index
            objectName: "sheenRing_" + index
            anchors.fill: parent
            readonly property real _radius: root._sheenRingRadius(index)
            radius: _radius
            color: "transparent"
            border.width: Math.min(root._rawSheenSpread, _radius) * (root._sheenRings - index) / root._sheenRings
            border.color: Qt.rgba(root.tintColor.r, root.tintColor.g, root.tintColor.b,
                                   Math.min(root.sheenOpacity,
                                            root._sheenInk * 2 * (index + 1) / (root._sheenRings * root._sheenRings)))
        }
    }

    // The outermost edge-tracing accents — deliberately unboosted (plain
    // cornerRadius, matching ring 0 above), so there's no visible seam
    // between these and the tile's actual corner.
    Rectangle {
        id: rim
        objectName: "rim"
        anchors.fill: parent
        radius: root.cornerRadius
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(root.tintColor.r, root.tintColor.g, root.tintColor.b, 0.35)
    }

    Rectangle {
        id: rimFade
        objectName: "rimFade"
        anchors.fill: parent
        radius: root.cornerRadius
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(0, 0, 0, 0.25)

        layer.enabled: true
        layer.effect: OpacityMask {
            maskSource: Rectangle {
                width:  rimFade.width
                height: rimFade.height
                gradient: Gradient {
                    orientation: Gradient.Vertical
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.55; color: "black" }
                    GradientStop { position: 1.0; color: "black" }
                }
            }
        }
    }

    Canvas {
        id: grain
        anchors.fill: parent
        opacity: root.grainOpacity
        visible: root.grainOpacity > 0
        renderStrategy: Canvas.Cooperative

        onWidthChanged:      requestPaint()
        onHeightChanged:     requestPaint()
        onVisibleChanged:    if (visible) requestPaint()

        Connections {
            target: root
            function onGrainDensityChanged() { grain.requestPaint() }
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (width <= 0 || height <= 0) return

            let seed = 424243
            function rand() {
                seed = (seed * 1103515245 + 12345) & 0x7fffffff
                return (seed % 1000) / 1000
            }

            const step = 2
            for (let y = 0; y < height; y += step) {
                for (let x = 0; x < width; x += step) {
                    if (rand() < root.grainDensity * 0.5) {
                        const shade = rand() > 0.5 ? 255 : 0
                        const a = 0.2 + rand() * 0.3
                        ctx.fillStyle = "rgba(" + shade + "," + shade + "," + shade + "," + a + ")"
                        ctx.fillRect(x, y, 1, 1)
                    }
                }
            }
        }
    }
}
