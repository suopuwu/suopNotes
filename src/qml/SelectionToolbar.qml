import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Window

// Single, app-wide floating formatting toolbar (feature request, revised
// from an earlier per-note "selection bubble" design): exactly one
// instance, owned by Main.qml, bound to whichever note currently has OS
// focus via the `noteWindow` property — shown the instant a note is
// focused, hidden once focus leaves every note. Positioned by default just
// above that note (or below it, if there isn't room above the screen),
// and freely right-click-draggable afterward, same convention as
// NoteWindow's own mouseDragArea (right button reserved for moving,
// left button left free for the buttons themselves).
//
// A genuine top-level Window, not an Item embedded in NoteWindow's own
// Rectangle tree (the very first version of this feature): a note enforces
// no minimum size, so a fixed-width button row often doesn't fit inside a
// small note at all. Living in NoteWindow's own tree meant either
// overflowing past the note's real window surface (unclickable/clipped,
// since each note is its own separate OS-level window on Wayland) or
// hiding outright whenever the note was too small — confusing either way.
// As its own window it can render fully outside the note's bounds, and
// being a genuine singleton (rather than one instance per open note) is
// what makes "one toolbar that follows whichever note is focused" possible
// in the first place.
Window {
    id: bar

    // Configurable hover/press highlight (feature request) for every
    // formatting button below — replaces each ToolButton's default style
    // background so the color/opacity/glass tuning comes from Settings
    // instead of whatever the platform style draws. Doesn't reference `bar`
    // (kept independent of the window's own ids beyond this nesting, needed
    // only because Qt requires inline components to live inside the root
    // object rather than beside it), so it's safe to reuse as `background:`
    // on each button below.
    component ToolbarButtonHighlight: InteractableHighlight {
        anchors.fill: parent
        // parent.checked (feature request): shows which formatting is
        // already active at the tracked note's current selection/cursor,
        // not just hover/press feedback — e.g. the Bold button stays
        // highlighted while the selection is bold, even with the pointer
        // elsewhere entirely.
        active: parent.hovered || parent.down || parent.checked
    }

    // Regression found live (real Wayland run only — never reproduced in
    // the offscreen Quick Test harness): Basic style's default ToolButton
    // contentItem is an IconLabel, which silently rendered nothing at all
    // in this window specifically — confirmed by swapping in a plain
    // debug-colored Text as an experiment, which rendered fine, while
    // IconLabel (even after fixing palette.buttonText for contrast) never
    // showed anything. Root cause not fully identified (suspected:
    // IconLabel's icon-theme lookup path interacting badly with this
    // window's Qt.WindowDoesNotAcceptFocus + Tool flags under the real
    // QPA/RHI backend), so the fix bypasses IconLabel entirely rather than
    // chasing it further — a plain Text contentItem, colored from the
    // tracked note's own fontColor for contrast against its background
    // (mirrors the same reasoning a straight palette.buttonText override
    // was trying for, just via a path that's confirmed to actually render).
    component ToolbarButtonLabel: Text {
        text: parent.text
        font: parent.font
        color: bar.noteWindow ? bar.noteWindow.noteAppearance.fontColor : "#ffffff"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    // Toolbar icon glyphs (feature request: replace the plain-symbol
    // buttons' Unicode characters — ✂, ⧉, ▦ etc., and the 📋 clipboard
    // emoji before them — with real vector icons). The emoji specifically
    // couldn't be recolored at all: Noto Color Emoji renders it in fixed
    // full color regardless of the Text.color a glyph is given, so it
    // alone ignored the theme-follows-note-appearance styling
    // ToolbarButtonLabel gives every plain-text button above. A vector
    // icon sidesteps that entirely: Shape/ShapePath's fillColor is an
    // ordinary color property, so it recolors exactly like every text
    // glyph here, at any size, with no emoji font involved.
    //
    // Path data throughout is copied by hand from Google's Material Icons/
    // Material Symbols (Apache License 2.0) — vendored sources + license
    // + a table of which file feeds which button at
    // third_party/material-icons/ (NOTICE.md, LICENSE). Nothing is
    // fetched at build or runtime.
    component ToolbarSvgIcon: Item {
        id: iconRoot
        readonly property color iconColor: bar.noteWindow ? bar.noteWindow.noteAppearance.fontColor : "#ffffff"
        // The glyph's own fillColor'd path data, and the square viewBox it
        // was authored against. Classic Material Icons use viewBox
        // "0 0 24 24" (viewBoxOriginY 0, the default); the two Material
        // *Symbols* icons below (insert row/column) instead use
        // "0 -960 960 960" — viewBoxOriginY compensates for that so both
        // icon families render through this one component.
        property string path: ""
        property real viewBoxSize: 24
        property real viewBoxOriginY: 0
        // Draws an additional corner-to-corner diagonal stroke across the
        // glyph above — used only by the Delete Row/Column icons below,
        // which have no dedicated Material glyph of their own. It mirrors
        // the same diagonal-slash-means-delete/off convention Material's
        // own grid_off glyph (used for Delete Table, just above) already
        // draws baked into its own path data, without hand-splicing a
        // knockout badge into table_rows'/view_column's own path.
        property bool strikethrough: false
        // Rendered smaller than the full button, matching how much
        // whitespace the text-glyph buttons around it (B, I, H1, etc.)
        // leave at buttonSize (32) — a 24x24 SVG glyph drawn at full size
        // would look noticeably larger/heavier than its neighbors.
        readonly property real iconSize: 18

        Shape {
            anchors.centerIn: parent
            width: iconRoot.viewBoxSize
            height: iconRoot.viewBoxSize
            scale: iconRoot.iconSize / iconRoot.viewBoxSize
            preferredRendererType: Shape.CurveRenderer
            // Bugfix: this item's `transform` list is composed in the
            // OUTER, unscaled coordinate space — NOT scaled down together
            // with the rest of this Shape's content by the `scale`
            // property above, confirmed empirically (a raw
            // -viewBoxOriginY offset landed the glyph ~864px away from
            // the tiny icon box, comfortably outside the button's own
            // bounds, i.e. invisible). Scaling the offset by the same
            // iconSize/viewBoxSize factor here compensates, so it lands
            // as the intended pre-scale shift once scale is applied.
            transform: Translate { y: -iconRoot.viewBoxOriginY * (iconRoot.iconSize / iconRoot.viewBoxSize) }

            ShapePath {
                fillColor: iconRoot.iconColor
                strokeWidth: -1
                PathSvg { path: iconRoot.path }
            }
            // Zero-width (rather than absent) when unused, so every
            // instance can share this one ShapePath instead of needing a
            // second variant of the component just for these two buttons.
            ShapePath {
                fillColor: "transparent"
                strokeColor: iconRoot.iconColor
                strokeWidth: iconRoot.strikethrough ? iconRoot.viewBoxSize * 0.11 : 0
                capStyle: ShapePath.RoundCap
                startX: iconRoot.viewBoxSize * 0.125
                startY: iconRoot.viewBoxSize * 0.125
                PathLine {
                    x: iconRoot.viewBoxSize * 0.875
                    y: iconRoot.viewBoxSize * 0.875
                }
            }
        }
    }

    // Hover tooltip (feature request), one instance per button below.
    //
    // A plain Popup, not QtQuick Controls' own ToolTip: ToolTip has a
    // built-in positioner that unconditionally recomputes and overwrites
    // its own y the instant it opens (confirmed empirically — even
    // assigning y from inside onOpened, AFTER that positioner has already
    // run once, reads back unchanged a statement later; something in
    // ToolTip's own C++ implementation re-asserts it synchronously on any
    // relevant change). That default position overlapped the button, which
    // caused a genuine infinite show/hide loop: a tooltip sitting on top
    // of its own button intercepts the next hit-test, HoverHandler sees
    // the pointer leave the button, the tooltip hides, hover returns, it
    // shows again, forever.
    //
    // Doesn't render itself at all, though — it just drives the shared
    // tooltipWindow singleton below. Two earlier approaches were tried and
    // both found to leak a permanent bug once every button got one of
    // these: rendering as a Popup embedded in bar's own window either (a)
    // stayed Popup.Item (the default), which clips to the OWNING window's
    // own small surface, so bar's own window had to reserve extra
    // invisible height below the visible bar just so a tooltip positioned
    // under a button wouldn't be clipped — but KWindowEffects blurs
    // whatever region a window's OWN width/height cover with no way to
    // carve out a smaller sub-rect, so that reserve was blurred right
    // alongside the visible bar even when no tooltip was showing, and
    // being real window surface it also intercepted right-click-drags and
    // swallowed clicks meant for whatever sat beneath it (confirmed live:
    // a dead, blurred, draggable strip permanently under the toolbar); or
    // (b) popupType: Popup.Window, which fixed the clipping but mapped a
    // brand new native window every time a tooltip opened — the same
    // "mapping/unmapping a surface disrupts hover live" class of bug this
    // file's own bar/columnOptionsPopup already learned to avoid (see
    // their own Component.onCompleted comments, "shown once, never
    // hidden/shown again"), causing a genuine flicker loop. A single
    // shared tooltip window, mapped once at startup like bar/
    // columnOptionsPopup and only ever repositioned/toggled via content
    // visibility afterward, sidesteps both: it's sized to just its own
    // text (nothing left to blur/intercept clicks under the rest of the
    // toolbar), and it's never mapped/unmapped again after startup (no
    // flicker).
    component ToolbarButtonTip: Item {
        id: tip
        property string text: ""
        // Lets the "▦" grid icon (whose tooltip meaning changes once the
        // popup toolbar reference below it is relevant) suppress this
        // entirely rather than showing a tooltip for the wrong action.
        property bool suppressed: false
        property bool delayElapsed: false
        readonly property bool shouldShow: parent.hovered && delayElapsed && !suppressed

        onShouldShowChanged: {
            if (shouldShow)
                tooltipWindow.showTip(tip.parent, tip.text)
            else if (tooltipWindow.activeButton === tip.parent)
                tooltipWindow.hideTip()
        }

        Timer {
            interval: 500
            running: tip.parent.hovered && !tip.delayElapsed && !tip.suppressed
            onTriggered: tip.delayElapsed = true
        }

        // Resets once hover leaves the button, so the delay starts fresh
        // again next time rather than showing instantly on the next hover.
        Connections {
            target: tip.parent
            function onHoveredChanged() {
                if (!tip.parent.hovered)
                    tip.delayElapsed = false
            }
        }
    }

    // Drag handle + adjacent divider (feature request/dedup): both the
    // main bar and the table column-options popup below need an identical
    // draggable grip — an explicit 2x4 (row mode) / 4x2 (column mode) grid
    // of dots, not a single Unicode glyph (a rotated braille character
    // "⣿" rendered as a near-illegible sliver in column mode, font
    // hinting/anti-aliasing doesn't preserve its dot pattern under
    // arbitrary rotation) — plus the divider rule that always follows it.
    // Used to be two separate hand-copies (one per toolbar), which had
    // already drifted out of sync once (the popup's own edge padding
    // didn't match the main bar's, a real bug — see edgePadding's own
    // comment); one shared component means there is only one place left
    // to get this right.
    //
    // Regression found live in the original per-toolbar implementation:
    // laying out the 8 dots with a `Grid` positioner (columns bound to
    // 2/4, letting it auto-compute rows) checked out perfectly in the
    // offscreen Quick Test harness (verified via a diagnostic dump of
    // every dot's real x/y — 2 clean rows of 4, correctly spaced), but
    // only ever rendered ONE row's worth of dots in the real windowed app
    // (confirmed live via screenshot, same "works offscreen, not in the
    // real compositor" split as ToolbarButtonLabel's own comment
    // elsewhere). Root cause not identified (suspected Grid-positioner-
    // specific), so rather than keep chasing an environment-specific Grid
    // quirk, this computes each dot's x/y explicitly from its own
    // Repeater index — no positioner involved at all, nothing left to
    // behave differently between environments.
    component ToolbarDragHandleGroup: Item {
        id: dragHandleGroup
        property bool isVertical: false
        // The Window (bar itself, or columnOptionsPopup) to move on a
        // left-click-drag of the grip.
        required property var dragTarget

        // Exposed for tests, same convention as the toolbars' own aliases.
        readonly property alias gripHandle: gripItem
        readonly property alias gripDotsGrid: gripDotsGrid

        readonly property int gripThickness: 9
        readonly property int gripLength: 21
        readonly property int dividerThickness: 1
        readonly property int dividerLength: 18
        // The gap from the grip to the divider is the grid/row layout's
        // own 2px spacing (shared by every other gap in the toolbar this
        // group sits in) plus the divider's own 4px margin below — 6px
        // total. The gap from the grip to the toolbar's OUTER edge
        // doesn't get that base spacing "for free" the way an inter-item
        // gap does (nothing precedes it to create spacing from), so it's
        // spelled out here in full rather than relying on the host
        // layout's shared spacing value — otherwise the grip ends up
        // sitting closer to the outer edge than to the divider, a real
        // bug once spotted live.
        //
        // edgePadding is settable (not baked in like the other constants
        // above) because "distance from the grip to the toolbar's outer
        // edge" isn't ALL internal padding for every host: the main bar's
        // own width is a fixed constant sized tightly to its content (no
        // slack of its own), so it needs the full 8px here; the table
        // popup's background is instead sized as content.implicitWidth +
        // 8 and centered, which already contributes 4px of slack on this
        // side for free — asking this component for another 8 on top
        // double-counted it (bugfix: the popup ended up with 12px total,
        // 4px more than the main bar, despite sharing this component).
        // Each host sets this to whatever closes the gap between its own
        // free slack and the shared 8px total, so the two toolbars still
        // read identically despite their outer sizing models differing.
        property int edgePadding: 8
        readonly property int gripToDividerGap: 6

        Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter

        implicitWidth: isVertical ? gripLength
            : edgePadding + gripThickness + gripToDividerGap + dividerThickness
        implicitHeight: isVertical
            ? edgePadding + gripThickness + gripToDividerGap + dividerThickness
            : gripLength

        Item {
            id: gripItem
            implicitWidth: dragHandleGroup.isVertical ? dragHandleGroup.gripLength : dragHandleGroup.gripThickness
            implicitHeight: dragHandleGroup.isVertical ? dragHandleGroup.gripThickness : dragHandleGroup.gripLength
            width: implicitWidth
            height: implicitHeight
            x: dragHandleGroup.isVertical ? (dragHandleGroup.width - width) / 2 : dragHandleGroup.edgePadding
            y: dragHandleGroup.isVertical ? dragHandleGroup.edgePadding : (dragHandleGroup.height - height) / 2

            Item {
                id: gripDotsGrid
                anchors.centerIn: parent
                width: parent.width
                height: parent.height
                readonly property int columns: dragHandleGroup.isVertical ? 4 : 2

                Repeater {
                    model: 8
                    Rectangle {
                        required property int index
                        width: 3
                        height: 3
                        radius: 1.5
                        color: "#888888"
                        x: (index % gripDotsGrid.columns) * 6
                        y: Math.floor(index / gripDotsGrid.columns) * 6
                    }
                }
            }

            // Left-click-drag directly on the grip (feature request — a
            // drag handle should be draggable with a plain left click, the
            // standard convention, not require the right-click-drag the
            // rest of the bar/NoteWindow's own notes use). Each host's own
            // right-click dragArea still covers right-click-drag anywhere
            // on it including here, for consistency with that convention;
            // this is additive, not a replacement.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.OpenHandCursor
                onPressed: dragHandleGroup.dragTarget.startSystemMove()
            }
        }

        // Rotates to match orientation: a vertical rule under a
        // horizontal (row) layout, a horizontal rule under a vertical
        // (column) one.
        Rectangle {
            width: dragHandleGroup.isVertical ? dragHandleGroup.dividerLength : dragHandleGroup.dividerThickness
            height: dragHandleGroup.isVertical ? dragHandleGroup.dividerThickness : dragHandleGroup.dividerLength
            color: Qt.rgba(1, 1, 1, 0.15)
            x: dragHandleGroup.isVertical
                ? (dragHandleGroup.width - width) / 2
                : dragHandleGroup.edgePadding + dragHandleGroup.gripThickness + dragHandleGroup.gripToDividerGap
            y: dragHandleGroup.isVertical
                ? dragHandleGroup.edgePadding + dragHandleGroup.gripThickness + dragHandleGroup.gripToDividerGap
                : (dragHandleGroup.height - height) / 2
        }
    }

    // Scope the toolbar acts on — the currently-focused NoteWindow, or null
    // when no note has focus. Sourced from Main.qml, the one place that
    // observes every NoteWindow's own activeChanged.
    property var noteWindow: null

    // Whether the pointer is currently anywhere over the toolbar itself —
    // see NoteWindow.qml's own `hovered` alias for why Main.qml treats this
    // as a keep-alive signal for toolbar visibility alongside `active`.
    readonly property alias hovered: barHoverHandler.hovered
    HoverHandler { id: barHoverHandler }

    // Exposed for tests only (structural/behavioral verification), not used
    // by app code outside this file — same convention NoteWindow.qml's own
    // mouseDragArea/editor aliases follow.
    readonly property alias dragArea: dragArea
    readonly property alias boldButton: boldButton
    readonly property alias italicButton: italicButton
    readonly property alias strikethroughButton: strikethroughButton
    readonly property alias boldButtonTip: boldButtonTip
    readonly property alias italicButtonTip: italicButtonTip
    readonly property alias strikethroughButtonTip: strikethroughButtonTip
    readonly property alias insertTableButton: insertTableButton
    readonly property alias cutButton: cutButton
    readonly property alias copyButton: copyButton
    readonly property alias pasteButton: pasteButton
    readonly property alias columnOptionsPopup: columnOptionsPopup
    readonly property alias tooltipWindow: tooltipWindow
    readonly property alias background: background
    readonly property alias content: content
    readonly property alias gripHandle: dragHandleGroup.gripHandle
    readonly property alias gripDotsGrid: dragHandleGroup.gripDotsGrid

    // Fixed cross-axis / main-axis sizes, not derived from content's own
    // implicitWidth/implicitHeight — the toolbar's layout direction itself
    // flips (see isVertical/content below) depending on which edge of the
    // note it lands on, and content's implicit size reflects only
    // whichever direction it's CURRENTLY laid out in, not "the other one";
    // deriving these from it would be circular. Buttons are close enough
    // to square that the row's natural length and the column's natural
    // length are practically identical, so one fixed constant safely
    // covers both orientations. barThickness (36) has a few px of headroom
    // over each button's own fixed 32px size (see boldButton etc. below) —
    // confirmed live that a tighter 32px thickness let each button's own
    // hover/press highlight clip against the window's edge.
    readonly property int barThickness: 36
    readonly property int buttonSize: 32
    // gripHandle (9) + 2 dividers (1 each, one before the formatting/table
    // buttons, one before cut/copy/paste) + 12 buttons * buttonSize (384)
    // + 14 gaps * spacing (28), plus a little breathing room so nothing
    // sits flush against background's rounded corners. Bugfix: previously
    // computed with only ONE divider (466, before cut/copy/paste got
    // their own divider — see nonButtonChildCount's own fix below) — a
    // window a few px narrower than its actual content, silently clipping
    // the rightmost buttons' hit-test region outside the real window
    // surface (confirmed as the explanation for reported missing tooltips
    // and unreliable clicks on the last button(s) in the row). Kept even
    // (436, not the bare-minimum 435): reposition()'s own centering math
    // divides this by 2, and window positioning rounds to whole pixels —
    // an odd barLength surfaced a half-pixel mismatch against that
    // rounding (confirmed empirically), which an even value sidesteps
    // entirely rather than chasing down exactly where the rounding happens.
    readonly property int barLength: 436

    // The grip + dividers always stay in the toolbar's first line/column
    // regardless of button count — only the buttons themselves wrap once
    // there are more than maxButtonsBeforeWrap (feature request: "wrap
    // past N icons"). Kept a little above the current 14-button count —
    // same one-spare-slot margin the original 9-button/10-threshold
    // pairing had — rather than exactly matching it, so the next button
    // added doesn't immediately need this revisited. Deliberately not much
    // higher than that, though: the wrap path reflows overflow buttons
    // into a second row/column, but barThickness/barLength below are
    // still single-line constants that don't grow to fit a second line —
    // wrapping has never actually been exercised (button count has always
    // stayed under this line), so a genuinely wrapped toolbar would render
    // clipped. Grow barThickness/barLength to a real multi-line size
    // alongside this if button count ever needs to cross it for real.
    readonly property int maxButtonsBeforeWrap: 15
    // gripHandle + the two dividers (bugfix: was 2, undercounting the
    // second divider added alongside cut/copy/paste).
    readonly property int nonButtonChildCount: 3

    // True once a note has been placed to its left or right (column
    // layout); false for above/below (row layout, the default) — set by
    // reposition() below.
    property bool isVertical: false

    // Matched by title (not wmclass alone, shared app-wide) by the KWin
    // rule/scripts below — same convention NoteWindow.qml's own
    // "suopNotes-" + noteId titles use, just with a fixed, non-note id
    // since this is a single app-wide singleton, not a per-note window.
    title: "suopNotes-selectionToolbar"

    // Regression found live: Qt.WindowDoesNotAcceptFocus (the original
    // approach here) was found to silently swallow the pointer click
    // itself on this compositor, not just refuse keyboard focus —
    // confirmed live: with the flag present, clicking a button never
    // toggled its formatting at all (not a timing race; the click plainly
    // never reached the button). This window accepts focus like any other
    // as a result.
    //
    // A second attempt tried reactivating the tracked note the instant
    // THIS window's own `active` turned true, to hand focus straight back
    // — that broke every button rather than just some: window activation
    // fires on mouse-*press*, before onClicked ever runs on release, so
    // reactivating there yanked focus away mid-click and cancelled the
    // button's own press/release gesture before it could complete.
    //
    // Current design (see Main.qml's noteOrToolbarActive()): don't fight
    // the OS for focus at all. The toolbar counts as "in use" for as long
    // as either the tracked note or this window itself is active; only
    // when focus moves to neither does Main.qml actually stop tracking.
    // A click anywhere in this window is then just a normal click in a
    // normal window — nothing races, nothing gets swallowed.
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    color: "transparent"

    width: isVertical ? barThickness : barLength
    height: isVertical ? barLength : barThickness

    // Shown exactly ONCE, here, and never hidden/shown again — confirmed
    // live that repeatedly mapping/unmapping this window while a note
    // holds Wayland keyboard focus disrupts that note's own text input
    // (visible cursor flicker, dropped keystrokes) every single time,
    // regardless of this window's flags (Tool/WindowDoesNotAcceptFocus/
    // WindowStaysOnTopHint were each tried individually and in
    // combination — none prevented it, because the disruption comes from
    // mapping a new surface at all, not from anything focus-related).
    // Showing it once at startup — before any note has ever been
    // focused, so there's nothing to disrupt yet — keeps it permanently
    // mapped; "hidden" the rest of the time is achieved by making its
    // *contents* invisible (see background/content below), not by
    // touching this window's own visible/show()/hide() ever again.
    visible: false
    Component.onCompleted: {
        // Seeds a KWin rule (same mechanism NoteWindow's own pin feature
        // uses) Force-keeping this window above others. "selectionToolbar"
        // is a fixed id, not a real note id — applyPosition() only ever
        // uses it as an opaque rule-group key, so this doesn't collide
        // with or need cleanup alongside any actual note's rule. The
        // seeded position itself is essentially a throwaway default
        // (100, 100): it's an Apply-Initially rule, so it only ever
        // matters for this one spawn, and reposition()'s own
        // moveWindow() call takes over for every placement after that,
        // the instant a note first focuses.
        positionManager.applyPosition("selectionToolbar", bar.title, 100, 100, true)

        // The extra hide()+show() right after is deliberate, not
        // redundant: confirmed live that writing an "above"/Force rule
        // and calling reconfigure() (inside applyPosition() above) does
        // NOT retroactively apply it to a window that already exists —
        // NoteWindow.qml's own setPinned() already documents this exact
        // requirement for its pin feature, forcing a hide/flags/show
        // cycle specifically so KWin re-evaluates the rule against a
        // freshly (re)created window. A single show() is safe to repeat
        // here, still at pure startup with no note focused yet, so it
        // can't retrigger the focus-disruption bug described above.
        bar.show()
        bar.hide()
        bar.show()
        updateBlur()
    }

    // Mirrors the tracked note's real compositor blur-behind (feature
    // request — the toolbar never had it before, unlike the glass sheen
    // above which already mimics the note). background.radius (a fixed 6,
    // not noteAppearance.windowRadius) is passed as the blur region's corner
    // radius since the bar's own frame always uses that fixed radius
    // regardless of the tracked note's own window-radius setting. Same
    // no-note-tracked fallback as background/GlassOverlay above: blur is
    // simply disabled rather than left at whatever the previously-tracked
    // note had.
    function updateBlur() {
        blurEffect.applyTo(bar, bar.noteWindow ? bar.noteWindow.noteAppearance.blurEnabled : false, background.radius)
    }

    // Re-derives the blur region whenever this window's own size changes —
    // same reasoning as NoteWindow.qml's own onWidthChanged/onHeightChanged:
    // the region KWindowEffects blurs is tied to the window's current
    // dimensions, and the bar's size does change (barThickness/barLength
    // swap between row and column layout — see isVertical/reposition).
    onWidthChanged: updateBlur()
    onHeightChanged: updateBlur()

    // The tracked note itself can change while still focused (its blur
    // toggle lives in PerNoteSettingsPopup, reachable without losing
    // focus) — re-applies whenever the CURRENTLY tracked note's own
    // blurEnabled flips, mirroring NoteWindow.qml's own
    // onBlurEnabledChanged: noteWindow.updateBlur(). A plain
    // `target: bar.noteWindow.noteAppearance` binding re-targets itself
    // automatically as noteWindow changes, including to null (Connections
    // simply goes inert with a null target).
    Connections {
        target: bar.noteWindow ? bar.noteWindow.noteAppearance : null
        function onBlurEnabledChanged() {
            bar.updateBlur()
            columnOptionsPopup.updateBlur()
        }
    }

    // Bugfix: on Wayland, QWindow::screen() updates asynchronously (the
    // platform plugin only reassigns it once it processes the
    // wl_surface enter/leave event for the note's new output), lagging
    // behind KWin's own activation notification. reposition() below reads
    // noteWindow.screen once, synchronously, from onNoteWindowChanged —
    // if that fires before screen has caught up (e.g. right after a note
    // was moved/activated on a different monitor), the bar gets clamped
    // into the WRONG monitor's geometry and nothing ever re-triggers
    // reposition() once screen settles, since nothing else read it. That's
    // why unfocusing/refocusing "fixes" it: it's just the next thing that
    // happens to re-run onNoteWindowChanged, by which point screen has
    // caught up. Re-running reposition() the moment screen itself changes
    // closes that gap without needing a focus round-trip.
    Connections {
        target: bar.noteWindow
        function onScreenChanged() {
            if (bar.noteWindow)
                bar.reposition()
        }
    }

    // Recomputes the default position only when the tracked note *changes*
    // (including going from/to null) — not on every move of the still-
    // focused note. Once shown, the bar is the user's to drag wherever
    // they like (see dragArea below) until focus switches to a different
    // note, at which point it defaults again.
    onNoteWindowChanged: {
        const tracking = (noteWindow !== null)
        background.visible = tracking
        content.visible = tracking
        content.enabled = tracking
        dragArea.enabled = tracking
        if (tracking)
            reposition()
        updateBlur()
        columnOptionsPopup.updateBlur()
    }

    // Tries each edge of the note in priority order — top, left, right,
    // bottom — and settles on the first one with enough room on-screen;
    // if none do, defaults to the top edge regardless (better to render
    // partly clipped than not show a default position at all).
    //
    // Reads noteX/noteY/noteWidth/noteHeight (the model-backed, PositionEventService
    // -confirmed geometry), NOT noteWindow.x/y/width/height — per
    // NoteWindow.qml's own architecture notes, plain Window.x/y are never
    // updated after a real compositor-driven drag on Wayland (only the
    // compositor itself knows the live position), so reading them here
    // reproduced exactly that bug: the bar kept landing at whatever stale
    // x/y the note's Window object happened to default to, regardless of
    // where the note actually was on screen.
    //
    // The computed position is applied two ways: bar.x/bar.y (kept in
    // sync for this file's own math and for tests) AND
    // positionManager.moveWindow() — confirmed live that the plain
    // property assignment alone is not honored by KWin for an
    // already-mapped top-level window (it kept landing whatever KWin's
    // own default/smart placement chose, ignoring the requested
    // coordinates); the actual live-move KWin script is what makes the
    // bar really land at this spot on screen.
    //
    // Runs a formatting action, then hands focus straight back to the note
    // it just acted on so the user can keep typing without an extra click
    // back into it. Safe to do here (unlike the window-level
    // onActiveChanged this file used to have, and removed): onClicked only
    // ever fires AFTER the button's own press/release has already fully
    // completed, so reactivating the note at this point can't cancel
    // anything — it's only reactivating on the window's raw `active`
    // (which fires on press, before the click completes) that broke every
    // button. raise() first because requestActivate() alone was found to
    // not reliably restore stacking order above the toolbar on this
    // compositor otherwise (mirrors NoteWindow's own onActivated logic).
    function runAction(fn) {
        const note = bar.noteWindow
        if (!note)
            return
        fn()
        note.raise()
        note.requestActivate()
        // Regression found live: the bar was observed rendering BEHIND
        // notes after this back-and-forth focus handoff started (it
        // didn't before, when this window never took focus at all).
        // Qt.WindowStaysOnTopHint + the KWin Force-above rule seeded at
        // startup apparently isn't enough to keep reasserting stacking
        // order through repeated activate/deactivate cycles — an explicit
        // raise() here, after the note reclaims input focus, is a cheap,
        // safe attempt to keep the bar visually on top regardless.
        bar.raise()
    }

    // Top/bottom keep the bar in its default row layout (barLength wide,
    // barThickness tall); left/right switch it to a column instead
    // (barThickness wide, barLength tall) — see isVertical/content.
    function reposition() {
        if (!bar.noteWindow)
            return

        const note = bar.noteWindow
        const screen = note.screen
        const gap = 8

        // Feature request: the main bar always anchors to the whole note,
        // even while the cursor is inside a table — it used to re-anchor
        // to the table's own (usually much smaller) bounds instead, but
        // that meant the bar jumping around the note as the cursor moved
        // in and out of tables. columnOptionsPopup below anchors to the
        // table itself instead, so there's still a table-local toolbar
        // available without the main bar itself relocating.
        let noteX = note.noteX
        let noteY = note.noteY
        let noteW = note.noteWidth
        let noteH = note.noteHeight

        let vertical = false
        let targetX = noteX + noteW / 2 - bar.barLength / 2
        let targetY = noteY - bar.barThickness - gap

        if (screen) {
            const minX = screen.virtualX
            const minY = screen.virtualY
            const maxX = screen.virtualX + screen.width
            const maxY = screen.virtualY + screen.height

            const topY = noteY - bar.barThickness - gap
            const bottomY = noteY + noteH + gap
            const leftX = noteX - bar.barThickness - gap
            const rightX = noteX + noteW + gap

            const clampedHorizontalX =
                Math.max(minX, Math.min(noteX + noteW / 2 - bar.barLength / 2, maxX - bar.barLength))
            const clampedVerticalY =
                Math.max(minY, Math.min(noteY + noteH / 2 - bar.barLength / 2, maxY - bar.barLength))

            if (topY >= minY) {
                vertical = false
                targetX = clampedHorizontalX
                targetY = topY
            } else if (leftX >= minX) {
                vertical = true
                targetX = leftX
                targetY = clampedVerticalY
            } else if (rightX + bar.barThickness <= maxX) {
                vertical = true
                targetX = rightX
                targetY = clampedVerticalY
            } else if (bottomY + bar.barThickness <= maxY) {
                vertical = false
                targetX = clampedHorizontalX
                targetY = bottomY
            } else {
                vertical = false
                targetX = clampedHorizontalX
                targetY = topY
            }
        }

        bar.isVertical = vertical
        bar.x = targetX
        bar.y = targetY
        positionManager.moveWindow(bar.title, targetX, targetY)
    }

    // Mimics whichever note is currently tracked (feature request): falls
    // back to a plain dark chip only in the instant before any note has
    // ever been tracked (background/content are invisible then anyway, so
    // the fallback color is never actually seen).
    Rectangle {
        id: background
        visible: false
        anchors.fill: parent
        radius: 6
        color: bar.noteWindow
            ? Qt.rgba(bar.noteWindow.noteAppearance.backgroundColor.r,
                      bar.noteWindow.noteAppearance.backgroundColor.g,
                      bar.noteWindow.noteAppearance.backgroundColor.b,
                      bar.noteWindow.noteAppearance.backgroundColor.a * bar.noteWindow.noteAppearance.windowOpacity)
            : Qt.rgba(0.15, 0.15, 0.15, 0.96)
        border.width: 1
        border.color: bar.noteWindow ? bar.noteWindow.noteAppearance.borderColor : Qt.rgba(1, 1, 1, 0.18)
    }

    // Same glass treatment as the tracked note's own mainFrame by default
    // (req. 6-style consistency) — sits on top of background, has no input
    // handling of its own, so it never blocks the drag area/buttons.
    // settings.chromeGlassFollowsNote (feature request) lets that default
    // be overridden to a fixed on/off instead, independent of whichever
    // note is currently tracked.
    GlassOverlay {
        anchors.fill: background
        cornerRadius: background.radius
        visible: settings.chromeGlassFollowsNote
            ? (bar.noteWindow ? bar.noteWindow.noteAppearance.glassEnabled : false)
            : settings.chromeGlassEnabled
        tintColor: bar.noteWindow ? bar.noteWindow.noteAppearance.glassTintColor : "#ffffff"
        sheenOpacity: bar.noteWindow ? bar.noteWindow.noteAppearance.glassSheenOpacity : 0.12
        ringCount: bar.noteWindow ? bar.noteWindow.noteAppearance.glassSheenStrength : 10
        sheenSize: bar.noteWindow ? bar.noteWindow.noteAppearance.glassSheenSize : 0.18
    }

    // A GridLayout, not a Grid/Row — it needs to reflow between a single
    // horizontal line (columns == child count, above/below the note) and
    // a single vertical line (columns: 1, left/right of the note)
    // depending on isVertical, AND each child needs to be genuinely
    // centered within its own cell (a plain Grid has no per-item
    // alignment control at all — items just sit top-left in whatever
    // cell size the widest/tallest sibling forces, which is exactly the
    // "icons aren't horizontally centered" bug in column mode: the grip
    // and divider are much narrower than a ToolButton, so they'd hug the
    // cell's left edge instead of sitting in the middle of the column).
    GridLayout {
        id: content
        // Above dragArea (z:0, declared after this in the file): without
        // this, dragArea — not any button or the grip — would be the
        // topmost item for hover/cursor-shape resolution across the whole
        // bar (same class of bug NoteWindow.qml's own toolbar-row-vs-
        // mouseDragArea z:2-over-z:1 comment documents), hiding
        // gripHandle's own Qt.OpenHandCursor. Left-click delivery itself
        // isn't affected either way — dragArea only claims the right
        // button, so a left press already falls through to the buttons
        // beneath it regardless of z (see dragArea's own comment) — this
        // is purely about which item wins cursor-shape resolution.
        z: 1
        visible: false
        anchors.centerIn: parent
        // flow/columns/rows (not a plain fixed `columns: children.length`):
        // wraps once there are more than maxButtonsBeforeWrap *buttons*
        // (feature request), while the grip+divider — always the first two
        // children — stay on the toolbar's first line/column regardless
        // (see nonButtonChildCount's own comment). -1 tells GridLayout to
        // compute that dimension automatically from the other one.
        flow: bar.isVertical ? GridLayout.TopToBottom : GridLayout.LeftToRight
        columns: bar.isVertical ? -1 : (bar.maxButtonsBeforeWrap + bar.nonButtonChildCount)
        rows: bar.isVertical ? (bar.maxButtonsBeforeWrap + bar.nonButtonChildCount) : -1
        rowSpacing: 2
        columnSpacing: 2
        // Disables every button at once whenever there's no tracked note,
        // rather than guarding each onClicked individually. Starts false
        // (matching noteWindow's own null default) and is set imperatively
        // from onNoteWindowChanged above from then on — not a declarative
        // `enabled: bar.noteWindow !== null` binding; see that handler's
        // own comment for why a plain binding isn't trusted in this file.
        enabled: false

        // Drag handle + divider (dedup: see ToolbarDragHandleGroup's own
        // comment above for why this is a shared component rather than a
        // hand-copy).
        ToolbarDragHandleGroup {
            id: dragHandleGroup
            isVertical: bar.isVertical
            dragTarget: bar
        }

        ToolButton {
            id: boldButton
            text: "B"
            font.bold: true
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarButtonLabel {}
            checkable: true
            checked: bar.noteWindow ? bar.noteWindow.currentBold : false
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.toggleBold(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd))
            ToolbarButtonTip { id: boldButtonTip; text: "Bold" }
        }
        ToolButton {
            id: italicButton
            text: "I"
            font.italic: true
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarButtonLabel {}
            checkable: true
            checked: bar.noteWindow ? bar.noteWindow.currentItalic : false
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.toggleItalic(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd))
            ToolbarButtonTip { id: italicButtonTip; text: "Italic" }
        }
        ToolButton {
            id: strikethroughButton
            text: "S"
            font.strikeout: true
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarButtonLabel {}
            checkable: true
            checked: bar.noteWindow ? bar.noteWindow.currentStrikethrough : false
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.toggleStrikethrough(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd))
            ToolbarButtonTip { id: strikethroughButtonTip; text: "Strikethrough" }
        }
        ToolButton {
            text: "H1"
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarButtonLabel {}
            checkable: true
            checked: bar.noteWindow ? bar.noteWindow.currentHeadingLevel === 1 : false
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.setHeadingLevel(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd, 1))
            ToolbarButtonTip { text: "Heading 1" }
        }
        ToolButton {
            text: "H2"
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarButtonLabel {}
            checkable: true
            checked: bar.noteWindow ? bar.noteWindow.currentHeadingLevel === 2 : false
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.setHeadingLevel(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd, 2))
            ToolbarButtonTip { text: "Heading 2" }
        }
        ToolButton {
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarSvgIcon {
                path: "M4 10.5c-.83 0-1.5.67-1.5 1.5s.67 1.5 1.5 1.5 1.5-.67 1.5-1.5-.67-1.5-1.5-1.5zm0-6c-.83 0-1.5.67-1.5 1.5S3.17 7.5 4 7.5 5.5 6.83 5.5 6 4.83 4.5 4 4.5zm0 12c-.83 0-1.5.68-1.5 1.5s.68 1.5 1.5 1.5 1.5-.68 1.5-1.5-.67-1.5-1.5-1.5zM7 19h14v-2H7v2zm0-6h14v-2H7v2zm0-8v2h14V5H7z"
            }
            checkable: true
            checked: bar.noteWindow ? bar.noteWindow.currentBulletList : false
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.toggleBulletList(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd))
            ToolbarButtonTip { text: "Bullet List" }
        }
        ToolButton {
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarSvgIcon {
                path: "M2 17h2v.5H3v1h1v.5H2v1h3v-4H2v1zm1-9h1V4H2v1h1v3zm-1 3h1.8L2 13.1v.9h3v-1H3.2L5 10.9V10H2v1zm5-6v2h14V5H7zm0 14h14v-2H7v2zm0-6h14v-2H7v2z"
            }
            checkable: true
            checked: bar.noteWindow ? bar.noteWindow.currentNumberedList : false
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.toggleNumberedList(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd))
            ToolbarButtonTip { text: "Numbered List" }
        }
        ToolButton {
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarSvgIcon {
                path: "M22,7h-9v2h9V7z M22,15h-9v2h9V15z M5.54,11L2,7.46l1.41-1.41l2.12,2.12l4.24-4.24l1.41,1.41L5.54,11z M5.54,19L2,15.46 l1.41-1.41l2.12,2.12l4.24-4.24l1.41,1.41L5.54,19z"
            }
            checkable: true
            checked: bar.noteWindow ? bar.noteWindow.currentCheckboxList : false
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.toggleCheckboxList(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd))
            ToolbarButtonTip { text: "Checkbox List" }
        }
        // The "grid icon" (feature request): inserts a starter table when
        // clicked with the cursor outside any table (unchanged behavior),
        // and doubles as the anchor for the row/column-editing popup below
        // once the cursor IS inside a table — hovering it (or the popup
        // itself) reveals that popup instead of cluttering this main row
        // with buttons that used to render as permanent no-ops outside a
        // table.
        ToolButton {
            id: insertTableButton
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarSvgIcon {
                path: "M20 2H4c-1.1 0-2 .9-2 2v16c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2zM8 20H4v-4h4v4zm0-6H4v-4h4v4zm0-6H4V4h4v4zm6 12h-4v-4h4v4zm0-6h-4v-4h4v4zm0-6h-4V4h4v4zm6 12h-4v-4h4v4zm0-6h-4v-4h4v4zm0-6h-4V4h4v4z"
            }
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.insertTable(bar.noteWindow.editor.selectionEnd))
            ToolbarButtonTip {
                text: "Insert Table"
                suppressed: bar.noteWindow ? bar.noteWindow.currentInTable : false
            }
        }

        // Cut/copy/paste (feature request): replace the removed right-click
        // context menu (see NoteWindow.qml's mouseDragArea, which used to
        // open one on a plain right-click) — that menu's MouseArea-vs-
        // TextInput event arbitration kept proving unreliable live, so
        // exposing the same three actions as ordinary toolbar buttons is
        // simpler and just works like every other button here.
        Rectangle {
            implicitWidth: bar.isVertical ? 18 : 1
            implicitHeight: bar.isVertical ? 1 : 18
            color: Qt.rgba(1, 1, 1, 0.15)
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            Layout.leftMargin: bar.isVertical ? 0 : 4
            Layout.topMargin: bar.isVertical ? 4 : 0
        }

        ToolButton {
            id: cutButton
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarSvgIcon {
                path: "M9.64 7.64c.23-.5.36-1.05.36-1.64 0-2.21-1.79-4-4-4S2 3.79 2 6s1.79 4 4 4c.59 0 1.14-.13 1.64-.36L10 12l-2.36 2.36C7.14 14.13 6.59 14 6 14c-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4c0-.59-.13-1.14-.36-1.64L12 14l7 7h3v-1L9.64 7.64zM6 8c-1.1 0-2-.89-2-2s.9-2 2-2 2 .89 2 2-.9 2-2 2zm0 12c-1.1 0-2-.89-2-2s.9-2 2-2 2 .89 2 2-.9 2-2 2zm6-7.5c-.28 0-.5-.22-.5-.5s.22-.5.5-.5.5.22.5.5-.22.5-.5.5z"
            }
            enabled: bar.noteWindow ? bar.noteWindow.editor.selectionStart !== bar.noteWindow.editor.selectionEnd : false
            onClicked: bar.runAction(() => bar.noteWindow.editor.cut())
            ToolbarButtonTip { text: "Cut" }
        }
        ToolButton {
            id: copyButton
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarSvgIcon {
                path: "M16 1H4c-1.1 0-2 .9-2 2v14h2V3h12V1zm3 4H8c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h11c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm0 16H8V7h11v14z"
            }
            onClicked: bar.runAction(() => bar.noteWindow.performCopy())
            ToolbarButtonTip { text: "Copy" }
        }
        ToolButton {
            id: pasteButton
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarSvgIcon {
                path: "M19 2h-4.18C14.4.84 13.3 0 12 0c-1.3 0-2.4.84-2.82 2H5c-1.1 0-2 .9-2 2v16c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2zm-7 0c.55 0 1 .45 1 1s-.45 1-1 1-1-.45-1-1 .45-1 1-1zm7 18H5V4h2v3h10V4h2v16z"
            }
            enabled: bar.noteWindow ? bar.noteWindow.editor.canPaste : false
            onClicked: bar.runAction(() => bar.noteWindow.editor.paste())
            ToolbarButtonTip { text: "Paste" }
        }
    }

    // Row/column-editing popup (feature request): the five buttons that
    // used to sit permanently in the main row above — acting as silent
    // no-ops (per TextFormatting.h) whenever the cursor wasn't actually
    // inside a table — now live in their own popup, shown only while the
    // cursor IS inside a table AND the pointer is hovering either the grid
    // icon above or this popup itself (the latter so moving the mouse from
    // the button down into the popup doesn't dismiss it mid-move).
    //
    // A genuine second top-level Window, not an Item embedded in this
    // Window's own tree, for the exact same reason `bar` itself is one
    // (see its own header comment): it needs to render below the grid
    // icon, extending past this toolbar's own small fixed surface, and —
    // per the feature request — is explicitly allowed to overlap the note,
    // unlike the main bar which always keeps clear of it.
    Window {
        id: columnOptionsPopup
        title: "suopNotes-columnOptionsPopup"
        flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
        color: "transparent"
        width: columnOptionsBackground.width
        height: columnOptionsBackground.height

        // Feature request: shown automatically whenever the cursor is
        // inside a table, not just while hovering the grid icon/the popup
        // itself — the main bar no longer relocates to sit near the table
        // (see reposition()'s own comment), so this popup, positioned at
        // the table directly below, is what stands in for a table-local
        // toolbar now.
        readonly property bool wantsVisible: bar.noteWindow && bar.noteWindow.currentInTable

        // Exposed for tests only, same convention as bar's own aliases
        // above.
        readonly property alias background: columnOptionsBackground
        readonly property alias content: columnOptionsContent
        readonly property alias insertRowButton: insertRowButton
        readonly property alias deleteRowButton: deleteRowButton
        readonly property alias insertColumnButton: insertColumnButton
        readonly property alias deleteColumnButton: deleteColumnButton
        readonly property alias deleteTableButton: deleteTableButton
        readonly property alias dragArea: columnOptionsDragArea
        readonly property alias gripHandle: columnOptionsDragHandleGroup.gripHandle
        readonly property alias gripDotsGrid: columnOptionsDragHandleGroup.gripDotsGrid

        // Same "shown once at startup, content-visibility toggled from then
        // on" discipline as `bar` itself — see its own Component.onCompleted
        // comment for why repeatedly mapping/unmapping a window while a
        // note holds Wayland keyboard focus disrupts that note's own text
        // input. The seeded position is a throwaway default, same reasoning
        // as bar's own seed: repositionColumnOptionsPopup() below takes
        // over for every real placement, the instant it first becomes
        // relevant.
        visible: false
        Component.onCompleted: {
            positionManager.applyPosition("columnOptionsPopup", columnOptionsPopup.title, 100, 100, true)
            show()
            hide()
            show()
            updateBlur()
        }

        // Same real compositor blur-behind as the main bar (feature
        // request: this popup should match its styling) — see bar's own
        // updateBlur() for the reasoning. Gated on wantsVisible, not just
        // bar.noteWindow: this window stays permanently mapped (see
        // Component.onCompleted's own comment) even while the cursor
        // isn't in any table, so without this gate it stayed blurred, and
        // its dragArea below stayed draggable, at whatever position it
        // was last shown — a real, blurred, draggable window silently
        // sitting near the main bar the rest of the time, blocking clicks
        // meant for whatever was underneath (bug found live: introduced
        // the moment this popup gained its own blur/drag to begin with,
        // since neither was ever conditioned on wantsVisible).
        function updateBlur() {
            blurEffect.applyTo(columnOptionsPopup,
                wantsVisible && bar.noteWindow ? bar.noteWindow.noteAppearance.blurEnabled : false,
                columnOptionsBackground.radius)
        }
        onWidthChanged: updateBlur()
        onHeightChanged: updateBlur()

        onWantsVisibleChanged: {
            columnOptionsBackground.visible = wantsVisible
            columnOptionsContent.visible = wantsVisible
            columnOptionsContent.enabled = wantsVisible
            columnOptionsDragArea.enabled = wantsVisible
            if (wantsVisible)
                repositionColumnOptionsPopup()
            updateBlur()
        }

        // Feature request: keeps tracking the table itself as the cursor
        // moves from one table straight into another (wantsVisible alone
        // wouldn't flip in that case — it stays true throughout — so
        // onWantsVisibleChanged's own reposition call wouldn't re-run);
        // tableRect's own result can change without currentInTable itself
        // toggling.
        Connections {
            target: bar.noteWindow
            function onCurrentInTableChanged() {
                if (columnOptionsPopup.wantsVisible)
                    columnOptionsPopup.repositionColumnOptionsPopup()
            }
        }

        // Same stale-screen bugfix as bar's own onScreenChanged handler
        // above (see its comment) — repositionColumnOptionsPopup() reads
        // note.screen too (see its own body below), so it needs the same
        // late-arriving-screen re-trigger.
        Connections {
            target: bar.noteWindow
            function onScreenChanged() {
                if (columnOptionsPopup.wantsVisible)
                    columnOptionsPopup.repositionColumnOptionsPopup()
            }
        }

        // Anchors directly below the table itself (feature request: this
        // popup stands in for a table-local toolbar now that the main bar
        // no longer relocates to the table — see reposition()'s own
        // comment) — falls back to just below the grid icon on the main
        // bar if the table's own rect isn't available for some reason.
        // Either way, falls back to ABOVE the anchor instead only when
        // there isn't room below on the tracked note's own screen — the
        // popup is allowed to overlap the note itself (feature request),
        // just not run off the screen entirely.
        function repositionColumnOptionsPopup() {
            const note = bar.noteWindow
            let anchorX
            let anchorTop
            let anchorBottom

            const rect = note ? note.docHandler.tableRect(note.editor.selectionEnd) : Qt.rect(0, 0, 0, 0)
            if (note && rect.width > 0 && rect.height > 0) {
                // editor lives inside editorFlick's content item, so its
                // Flickable-driven scroll transform is already baked into
                // mapToGlobal — no separate contentY subtraction needed
                // (same reasoning bar's own former table-anchoring code
                // relied on).
                const topLeft = note.editor.mapToGlobal(rect.x, rect.y)
                anchorX = topLeft.x
                anchorTop = topLeft.y
                anchorBottom = topLeft.y + rect.height
            } else {
                const buttonTopLeft = insertTableButton.mapToGlobal(0, 0)
                anchorX = buttonTopLeft.x
                anchorTop = buttonTopLeft.y
                anchorBottom = buttonTopLeft.y + insertTableButton.height
            }

            let targetX = anchorX
            let targetY = anchorBottom + 4

            const screen = note ? note.screen : null
            if (screen && targetY + columnOptionsPopup.height > screen.virtualY + screen.height)
                targetY = anchorTop - columnOptionsPopup.height - 4

            columnOptionsPopup.x = targetX
            columnOptionsPopup.y = targetY
            positionManager.moveWindow(columnOptionsPopup.title, targetX, targetY)
        }

        Rectangle {
            id: columnOptionsBackground
            visible: false
            width: columnOptionsContent.implicitWidth + 8
            height: columnOptionsContent.implicitHeight + 8
            radius: 6
            color: background.color
            border.width: background.border.width
            border.color: background.border.color
        }

        // Same glass treatment as the main bar's own background (feature
        // request: every popup should look like it belongs to the
        // toolbar, not a plain flat rectangle).
        GlassOverlay {
            anchors.fill: columnOptionsBackground
            cornerRadius: columnOptionsBackground.radius
            visible: columnOptionsBackground.visible && (settings.chromeGlassFollowsNote
                ? (bar.noteWindow ? bar.noteWindow.noteAppearance.glassEnabled : false)
                : settings.chromeGlassEnabled)
            tintColor: bar.noteWindow ? bar.noteWindow.noteAppearance.glassTintColor : "#ffffff"
            sheenOpacity: bar.noteWindow ? bar.noteWindow.noteAppearance.glassSheenOpacity : 0.12
            ringCount: bar.noteWindow ? bar.noteWindow.noteAppearance.glassSheenStrength : 10
            sheenSize: bar.noteWindow ? bar.noteWindow.noteAppearance.glassSheenSize : 0.18
        }

        RowLayout {
            id: columnOptionsContent
            visible: false
            enabled: false
            anchors.centerIn: columnOptionsBackground
            spacing: 2

            // Same drag handle as the main bar's own (dedup: see
            // ToolbarDragHandleGroup's own comment near the top of this
            // file) — always the row-mode 2x4 dot pattern, since this
            // popup is never vertical. edgePadding is halved (see the
            // component's own edgePadding comment): columnOptionsBackground
            // below already contributes 4px of slack on this side via its
            // own "+8, centered" sizing, so only 4 more is needed here to
            // reach the same 8px total the main bar gets entirely from its
            // own edgePadding.
            ToolbarDragHandleGroup {
                id: columnOptionsDragHandleGroup
                isVertical: false
                dragTarget: columnOptionsPopup
                edgePadding: 4
            }

            ToolButton {
                id: insertRowButton
                implicitWidth: bar.buttonSize
                implicitHeight: bar.buttonSize
                background: ToolbarButtonHighlight {}
                hoverEnabled: true
                // Material Symbols (not classic Material Icons — see
                // third_party/material-icons/NOTICE.md for why), so its
                // own viewBox origin is "0 -960" rather than "0 0".
                contentItem: ToolbarSvgIcon {
                    viewBoxSize: 960
                    viewBoxOriginY: -960
                    path: "M200-560h560v-240H200v240Zm-80 400v-720h720v720H680v-80h80v-240H200v240h80v80H120Zm360-320Zm0-80v80-80Zm0 0ZM440-80v-80h-80v-80h80v-80h80v80h80v80h-80v80h-80Z"
                }
                onClicked: bar.runAction(() => bar.noteWindow.docHandler.insertTableRow(bar.noteWindow.editor.selectionEnd))
                ToolbarButtonTip { text: "Insert Row" }
            }
            ToolButton {
                id: deleteRowButton
                implicitWidth: bar.buttonSize
                implicitHeight: bar.buttonSize
                background: ToolbarButtonHighlight {}
                hoverEnabled: true
                // table_rows plus a diagonal strike — see ToolbarSvgIcon's
                // strikethrough doc comment for why (no dedicated Material
                // "delete row" glyph exists).
                contentItem: ToolbarSvgIcon {
                    path: "M21,8H3V4h18V8z M21,10H3v4h18V10z M21,16H3v4h18V16z"
                    strikethrough: true
                }
                onClicked: bar.runAction(() => bar.noteWindow.docHandler.deleteTableRow(bar.noteWindow.editor.selectionEnd))
                ToolbarButtonTip { text: "Delete Row" }
            }
            ToolButton {
                id: insertColumnButton
                implicitWidth: bar.buttonSize
                implicitHeight: bar.buttonSize
                background: ToolbarButtonHighlight {}
                hoverEnabled: true
                contentItem: ToolbarSvgIcon {
                    viewBoxSize: 960
                    viewBoxOriginY: -960
                    path: "M160-760v560h240v-560H160ZM80-120v-720h720v160h-80v-80H480v560h240v-80h80v160H80Zm400-360Zm-80 0h80-80Zm0 0Zm320 120v-80h-80v-80h80v-80h80v80h80v80h-80v80h-80Z"
                }
                onClicked: bar.runAction(() => bar.noteWindow.docHandler.insertTableColumn(bar.noteWindow.editor.selectionEnd))
                ToolbarButtonTip { text: "Insert Column" }
            }
            ToolButton {
                id: deleteColumnButton
                implicitWidth: bar.buttonSize
                implicitHeight: bar.buttonSize
                background: ToolbarButtonHighlight {}
                hoverEnabled: true
                // view_column plus a diagonal strike — same reasoning as
                // deleteRowButton above.
                contentItem: ToolbarSvgIcon {
                    path: "M14.67,5v14H9.33V5H14.67z M15.67,19H21V5h-5.33V19z M8.33,19V5H3v14H8.33z"
                    strikethrough: true
                }
                onClicked: bar.runAction(() => bar.noteWindow.docHandler.deleteTableColumn(bar.noteWindow.editor.selectionEnd))
                ToolbarButtonTip { text: "Delete Column" }
            }
            ToolButton {
                id: deleteTableButton
                implicitWidth: bar.buttonSize
                implicitHeight: bar.buttonSize
                background: ToolbarButtonHighlight {}
                hoverEnabled: true
                contentItem: ToolbarSvgIcon {
                    path: "M8 4v1.45l2 2V4h4v4h-3.45l2 2H14v1.45l2 2V10h4v4h-3.45l2 2H20v1.45l2 2V4c0-1.1-.9-2-2-2H4.55l2 2H8zm8 0h4v4h-4V4zM1.27 1.27L0 2.55l2 2V20c0 1.1.9 2 2 2h15.46l2 2 1.27-1.27L1.27 1.27zM10 12.55L11.45 14H10v-1.45zm-6-6L5.45 8H4V6.55zM8 20H4v-4h4v4zm0-6H4v-4h3.45l.55.55V14zm6 6h-4v-4h3.45l.55.54V20zm2 0v-1.46L17.46 20H16z"
                }
                onClicked: bar.runAction(() => bar.noteWindow.docHandler.deleteTable(bar.noteWindow.editor.selectionEnd))
                ToolbarButtonTip { text: "Delete Table" }
            }
        }

        // Right-click-drag moves the popup, same convention as the main
        // bar's own dragArea (feature request: this popup should match
        // its behavior) — acceptedButtons excludes Left, so a left-button
        // press keeps falling through to the buttons in
        // columnOptionsContent normally.
        MouseArea {
            id: columnOptionsDragArea
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            // Starts false and is set imperatively from
            // onWantsVisibleChanged above from then on — same reasoning
            // as bar's own dragArea: dragging this popup at whatever
            // stale position it last had, while it's not supposed to be
            // showing at all, makes no sense (see updateBlur()'s own
            // comment for the bug this — and the same gate on blur —
            // fixes).
            enabled: false
            onPressed: columnOptionsPopup.startSystemMove()
        }
    }

    // Single shared tooltip window for every ToolbarButtonTip above and
    // every column-options button below — see ToolbarButtonTip's own
    // comment for why this exists as a separate window rather than
    // rendering inside bar's own. A genuine top-level Window, shown
    // exactly once here and never hidden/shown again (same "mapping/
    // unmapping a surface disrupts hover live" reasoning as bar/
    // columnOptionsPopup's own Component.onCompleted) — "hidden" the rest
    // of the time is achieved by making its *content* invisible, not by
    // touching this window's own visible/show()/hide() ever again.
    // Sized to just its own text (background's own implicit size), so it
    // never reserves space it isn't currently using.
    Window {
        id: tooltipWindow
        title: "suopNotes-toolbarTooltip"
        flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
        color: "transparent"
        width: tooltipBackground.width
        height: tooltipBackground.height

        property string text: ""
        property bool contentVisible: false
        // Which button's tooltip is currently showing, if any — lets a
        // button whose own hover already ended avoid hiding a tooltip
        // that a DIFFERENT button has since taken over showing (a hover
        // handoff between two adjacent buttons can fire in either order).
        property var activeButton: null

        readonly property alias background: tooltipBackground

        visible: false
        Component.onCompleted: {
            // Same throwaway-seed-position reasoning as bar/
            // columnOptionsPopup's own Component.onCompleted — showTip()
            // below takes over real positioning the instant any tooltip
            // is first shown.
            positionManager.applyPosition("toolbarTooltip", tooltipWindow.title, 100, 100, true)
            show()
            hide()
            show()
        }

        function showTip(button, tipText) {
            tooltipWindow.activeButton = button
            tooltipWindow.text = tipText
            tooltipWindow.contentVisible = true
            const pos = button.mapToGlobal(0, button.height + 6)
            tooltipWindow.x = pos.x
            tooltipWindow.y = pos.y
            positionManager.moveWindow(tooltipWindow.title, pos.x, pos.y)
        }

        function hideTip() {
            tooltipWindow.contentVisible = false
            tooltipWindow.activeButton = null
        }

        // color/border mirror the main bar's own background exactly (same
        // note-backgroundColor + windowOpacity, or the same dark fallback
        // when no note is tracked) — same convention columnOptionsBackground
        // below already follows, referencing bar's `background` id directly
        // rather than re-deriving the same expression a third time.
        Rectangle {
            id: tooltipBackground
            visible: tooltipWindow.contentVisible
            width: tooltipLabel.implicitWidth + 12
            height: tooltipLabel.implicitHeight + 12
            radius: 6
            color: background.color
            border.width: background.border.width
            border.color: background.border.color
        }

        // Same glass treatment as the main bar/columnOptionsPopup's own
        // backgrounds (feature request: tooltips should match the rest of
        // the toolbar's chrome) — a sibling of tooltipBackground, not a
        // child of it, so it paints on top of the flat background color
        // but still below tooltipLabel (same layering columnOptionsPopup's
        // own GlassOverlay/columnOptionsContent already use).
        GlassOverlay {
            anchors.fill: tooltipBackground
            cornerRadius: tooltipBackground.radius
            visible: tooltipBackground.visible && (settings.chromeGlassFollowsNote
                ? (bar.noteWindow ? bar.noteWindow.noteAppearance.glassEnabled : false)
                : settings.chromeGlassEnabled)
            tintColor: bar.noteWindow ? bar.noteWindow.noteAppearance.glassTintColor : "#ffffff"
            sheenOpacity: bar.noteWindow ? bar.noteWindow.noteAppearance.glassSheenOpacity : 0.12
            ringCount: bar.noteWindow ? bar.noteWindow.noteAppearance.glassSheenStrength : 10
            sheenSize: bar.noteWindow ? bar.noteWindow.noteAppearance.glassSheenSize : 0.18
        }

        Text {
            id: tooltipLabel
            anchors.centerIn: tooltipBackground
            visible: tooltipWindow.contentVisible
            text: tooltipWindow.text
            // Same source as every button's own ToolbarButtonLabel, not a
            // fixed white — tooltipBackground now inherits the tracked
            // note's own (possibly light) backgroundColor above, so a
            // fixed white label would stop contrasting against it.
            color: bar.noteWindow ? bar.noteWindow.noteAppearance.fontColor : "#ffffff"
            font.pixelSize: 11
        }
    }

    // Right-click-drag moves the bar, the same convention as NoteWindow's
    // own mouseDragArea — acceptedButtons excludes Left, so a left-button
    // press isn't a hit here at all and keeps searching underneath,
    // reaching the buttons in `content` normally (same fallthrough
    // mechanism mouseDragArea's own comment documents), while a
    // right-button press anywhere on the bar (including over the grip,
    // the buttons, or the gaps between them) starts the move.
    MouseArea {
        id: dragArea
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        // Starts false (matching noteWindow's own null default) and is
        // set imperatively from onNoteWindowChanged above from then on —
        // dragging an invisible (no note tracked) bar makes no sense.
        enabled: false
        onPressed: bar.startSystemMove()
    }
}
