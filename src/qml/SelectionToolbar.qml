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
ChromeWindow {
    id: bar

    // The frameless map-once/park ritual, the "keep above" rule seeding and the
    // window flags all live in ChromeWindow now; this file (and its three nested
    // popups below) keep only what is their own — the buttons, placement, blur,
    // and visibility. ruleId is each window's KWin rule-group key (and MUST stay
    // registered in PositionEventService::pseudoNoteIds).
    ruleId: "selectionToolbar"

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

    // Same wantsVisible convention columnOptionsPopup/spellingSuggestionsPopup
    // each have their own copy of below — whether this permanently-mapped
    // window (see its own visible: false / Component.onCompleted comment)
    // should currently be showing anything at all. Drives its own
    // width/height (see below) the same way it drives content.visible in
    // onNoteWindowChanged.
    readonly property bool wantsVisible: noteWindow !== null

    // Whether the pointer is currently anywhere over the toolbar itself —
    // see NoteWindow.qml's own `hovered` alias for why Main.qml treats this
    // as a keep-alive signal for toolbar visibility alongside `active`.
    readonly property alias hovered: barHoverHandler.hovered
    HoverHandler { id: barHoverHandler }

    // Whether the tracked note's code mode is currently suppressing markdown
    // formatting — the same note-side property that disables NoteWindow's own
    // formatting shortcuts, read here so the toolbar and the keyboard can't
    // disagree about whether this note formats markdown.
    //
    // The formatting buttons below are DISABLED (dimmed) for this, never
    // hidden, and that is deliberate twice over. NoteContextMenu.qml states the
    // UX half: a surface whose controls appear and disappear never lets the
    // user build muscle memory for where anything is. The mechanical half is
    // local to this file — barLength is derived from the button count, so
    // hiding buttons resizes this window live, and a resize of a mapped window
    // here is a real client<->compositor negotiation with a whole
    // self-correcting size guard (see enforceSize) already devoted to the ways
    // it goes wrong. Dimming costs one binding and races nothing. That matters
    // most for THIS flag specifically: code mode toggles while the very same
    // note stays tracked, so dimming/undimming can happen repeatedly under a
    // stationary pointer. Contrast taskListNote just below, which can only
    // change when the tracked note itself changes.
    //
    // False when no note is tracked, which changes nothing on its own: the
    // whole GridLayout is already disabled in that case (see content.enabled).
    readonly property bool formattingSuppressed: bar.noteWindow
        ? bar.noteWindow.codeModeSuppressesAutoFormatting
        : false

    // True while the tracked note is the one aggregated TickTick task note.
    // Its text is not free-form markdown at all: it is a machine-rendered flat
    // list of open task titles, one checkbox line each (TickTickTaskList::
    // render), and NoteWindow.qml re-asserts every line as a checkbox after
    // each edit (DocumentHandler::repairCheckboxList). So the block-structure
    // buttons are hidden from it outright — headings, bullet/numbered/checkbox
    // lists, tables and images are each either undone on the spot by that
    // repair or flattened into checkbox lines and pushed to TickTick as
    // literal markup in a task title. A button that can only ever no-op or
    // corrupt is worse than no button; same call NoteHud makes for its own
    // new-task glyph.
    //
    // Hidden rather than dimmed, unlike formattingSuppressed above, precisely
    // because this cannot flip under a stationary pointer: a note either IS
    // the task note for its whole life or never is, so the only way this
    // changes is bar.noteWindow retargeting — which already re-anchors and
    // repositions the whole window (see onNoteWindowChanged). The resize the
    // comment above warns about therefore rides along with a reposition that
    // was happening anyway, instead of being a new event of its own.
    //
    // False when no note is tracked, and also whenever the module isn't built
    // — NoteWindow.isTickTickTaskNote is itself false in that case (it null-
    // checks `tickTick`), so nothing here needs to know the module exists.
    readonly property bool taskListNote: bar.noteWindow
        ? bar.noteWindow.isTickTickTaskNote
        : false

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
    readonly property alias heading1Button: heading1Button
    readonly property alias heading2Button: heading2Button
    readonly property alias bulletListButton: bulletListButton
    readonly property alias numberedListButton: numberedListButton
    readonly property alias checkboxListButton: checkboxListButton
    readonly property alias insertTableButton: insertTableButton
    readonly property alias insertImageButton: insertImageButton
    readonly property alias columnOptionsPopup: columnOptionsPopup
    readonly property alias spellingSuggestionsPopup: spellingSuggestionsPopup
    readonly property alias addToDictionaryButton: addToDictionaryButton
    readonly property alias noSuggestionsLabel: noSuggestionsLabel
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
    // How many buttons content actually lays out right now. Ten normally; on
    // the TickTick task note only Bold/Italic/Strikethrough survive — see
    // taskListNote for which seven go and why. Stated here as a number rather
    // than counted from content's children because barLength below has to be
    // knowable without measuring content (see its own comment).
    readonly property int taskListNoteButtonCount: 3
    readonly property int fullButtonCount: 10
    readonly property int buttonCount: bar.taskListNote ? bar.taskListNoteButtonCount
                                                        : bar.fullButtonCount
    // gripHandle (9) + 1 divider (1, inside the drag-handle group before
    // the formatting/table buttons) + buttonCount buttons * buttonSize +
    // (buttonCount + 1) gaps * spacing (2 each), plus 22px of breathing room
    // so nothing sits flush against background's rounded corners. A window a
    // few px narrower than its actual content silently clips the rightmost
    // buttons' hit-test region outside the real window surface (confirmed
    // historically as the explanation for reported missing tooltips and
    // unreliable clicks on the last button(s) in the row), so this stays
    // comfortably above content's implicit width.
    //
    // Spelled as the arithmetic rather than the 374 constant it used to be
    // (340 before the Insert Image button was added, then bumped by hand):
    // the button count is no longer fixed, so a hand-maintained constant would
    // need a second value for the task note and both would drift. The formula
    // reproduces 374 exactly at ten buttons, and 136 at three.
    //
    // Stays even at every count, which is load-bearing: reposition()'s own
    // centering math divides this by 2, and window positioning rounds to whole
    // pixels — an odd barLength surfaced a half-pixel mismatch against that
    // rounding (confirmed empirically), which an even value sidesteps entirely
    // rather than chasing down exactly where the rounding happens. Every term
    // above is even (9 + 1 + 22 = 32), so the sum is too regardless of count.
    readonly property int barLength:
        32 + bar.buttonCount * bar.buttonSize + (bar.buttonCount + 1) * 2

    // The grip + dividers always stay in the toolbar's first line/column
    // regardless of button count — only the buttons themselves wrap once
    // there are more than maxButtonsBeforeWrap (feature request: "wrap
    // past N icons"). Kept comfortably above the current 10-button count so
    // the next button or two added doesn't immediately need this revisited.
    // Deliberately not much higher than that, though: the wrap path reflows
    // overflow buttons
    // into a second row/column, but barThickness/barLength below are
    // still single-line constants that don't grow to fit a second line —
    // wrapping has never actually been exercised (button count has always
    // stayed under this line), so a genuinely wrapped toolbar would render
    // clipped. Grow barThickness/barLength to a real multi-line size
    // alongside this if button count ever needs to cross it for real.
    readonly property int maxButtonsBeforeWrap: 15
    // gripHandle + its divider (both bundled into the one drag-handle-group
    // child that leads the layout). Counted a touch generously — the actual
    // non-button grid child count is just that single group — but an
    // over-count only widens the pre-wrap allowance, which is harmless
    // (wrapping has never been exercised; see maxButtonsBeforeWrap), whereas
    // an under-count would wrap prematurely.
    readonly property int nonButtonChildCount: 2

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
    // normal window — nothing races, nothing gets swallowed. (Qt.Window
    // DoesNotAcceptFocus is absent from ChromeWindow's flags for exactly this
    // reason — it was found to swallow the pointer click itself here.)

    // wantsVisible-gated, not just isVertical directly: this window stays
    // permanently mapped with WindowStaysOnTopHint (see
    // Component.onCompleted's own comment below) even while no note is
    // tracked, so without a gate here it stayed a full barLength x
    // barThickness hit target — sitting wherever it was last positioned,
    // silently eating clicks meant for whatever was underneath even though
    // its content had gone invisible (bug found live).
    //
    // NOT gated on wantsVisible anymore, though (bug found live: a 1px
    // popup elsewhere in this file that persisted no matter what — see
    // spellingSuggestionsPopup's own width/height comment for the full
    // trace-confirmed story): shrinking to 1x1 on every hide, then back to
    // real size on every show, forces a genuine client<->compositor
    // Wayland resize negotiation each time, not just a QML property write
    // — two such negotiations close together can race, with a stale
    // belated "you're 1x1 now" acknowledgment landing after a fresh
    // "you're your real size again" one and silently clobbering the
    // window's actual on-screen geometry. ChromeWindow.park() below, called
    // from onNoteWindowChanged, already fully solves the original
    // "blocks clicks after closing" problem on its own — a window sitting
    // off every connected screen can't overlap real content regardless of
    // its size — so shrinking was never load-bearing for that fix, just an
    // extra step that turned out to actively cause a worse bug. Width/
    // height now stay at their real size unconditionally; only position
    // changes on hide.
    // The single source of truth for how big this window must be right now,
    // named rather than inlined into width/height below so the size guard
    // (see enforceSize()) can re-assert exactly the same expression instead
    // of a hand-copied second version of it that could drift.
    readonly property int wantedWidth: isVertical ? barThickness : barLength
    readonly property int wantedHeight: isVertical ? barLength : barThickness

    width: wantedWidth
    height: wantedHeight

    // The map-once ritual (seed the off-screen "keep above" rule, show/hide/show
    // so KWin re-evaluates it, block input) now lives in ChromeWindow — see
    // there for the full trace-confirmed reasoning behind every step (mapping a
    // surface while a note holds Wayland focus disrupts its text input, so this
    // window maps once at startup and "hides" by parking its content off-screen).
    // This file only adds applying blur once the surface exists, which
    // ChromeWindow's mapped() fires at the right point in that ritual.
    onMapped: bar.updateBlur()

    // Parking a window off-screen (how each of these four permanently-mapped
    // windows "hides") is ChromeWindow.park() now — each window parks itself via
    // its own park(), instead of the one shared bar.parkOffscreen(win) this used
    // to be. See ChromeWindow for why it is load-bearing for VISIBILITY only,
    // never input correctness: it is racy (moveWindow is async, completions
    // arrive out of order, and it no-ops if KWin scripting is unreachable), so
    // each window ALSO carries a windowInputMask.setAcceptsInput() hook tied to
    // its own content-visibility flag. The mask stops clicks, the park stops
    // pixels.

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
    onWidthChanged: {
        enforceSize()
        updateBlur()
    }
    onHeightChanged: {
        enforceSize()
        updateBlur()
    }

    // Second line of defense for the orientation-clipping bug
    // repositionDebounce's own comment describes in full. That debounce
    // removes the known TRIGGER (two resize negotiations back-to-back); this
    // catches the damage if a stale configure/ack ever clobbers this
    // window's real geometry anyway — from a cause not yet identified, or a
    // second reposition legitimately arriving later (a note dragged to
    // another monitor, say). Without it there is no self-correction at all:
    // barThickness/barLength are fixed constants rather than derived from
    // content's implicit size (see their own comment for why that would be
    // circular), so a clipped surface stays clipped indefinitely.
    //
    // This is observable at the QML level despite being a platform-level
    // clobber: the trace behind spellingSuggestionsPopup's own width/height
    // comment recorded `width` ITSELF dropping to 1 while the QML that feeds
    // it never changed — i.e. the platform writes the property, so
    // onWidthChanged above genuinely fires for it.
    //
    // Qt.binding(), NOT a plain `width = wantedWidth`: a bare imperative
    // assignment to a property that currently has a binding DESTROYS that
    // binding, which would leave width permanently frozen at whatever this
    // guard last wrote and stop it ever tracking isVertical again — turning
    // a transient clipped surface into a permanent one, i.e. a worse version
    // of the bug being fixed. Re-installing an equivalent binding restores
    // the correct value AND keeps the property live for the next flip.
    //
    // Converges rather than recursing: the corrective assignment re-fires
    // onWidthChanged, but by then width already equals wantedWidth, so the
    // guard makes no further write and the chain stops. The !== check is
    // what makes that true — it must stay.
    function enforceSize() {
        if (width !== wantedWidth)
            width = Qt.binding(() => bar.wantedWidth)
        if (height !== wantedHeight)
            height = Qt.binding(() => bar.wantedHeight)
    }

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
            spellingSuggestionsPopup.updateBlur()
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
    // Re-anchors the bar to the tracked note whenever that note's geometry
    // actually changes — a drag that lands it somewhere else, or a resize.
    // Without this the bar stayed at wherever the note *used* to be, and
    // only unfocusing/refocusing (which re-runs onNoteWindowChanged) put it
    // back — the same "nothing else re-reads this" gap as onScreenChanged
    // just above, for the note's position rather than its screen.
    //
    // noteX/noteY (model-backed), NOT noteWindow.x/y: per NoteWindow.qml's
    // architecture notes, plain Window.x/y are never updated after a real
    // compositor-driven drag on Wayland, so they'd never fire here at all.
    // That indirection is also what makes this a drag-*end* trigger for
    // free rather than a live follow: noteX/noteY only change when
    // PositionEventService's KWin script reports interactiveMoveResizeFinished
    // and that lands in Settings, which is exactly once per completed drag.
    // Live-following a compositor-driven move isn't available to us — the
    // client is told nothing until the gesture ends (see PositionEventService.h).
    //
    // Resize is the opposite: noteWidth/noteHeight are client-side and do
    // update continuously, so these fire on every pixel of a resize drag.
    // repositionDebounce absorbs that — it's one-shot and restarted here,
    // so it only fires once the resize goes quiet for 50ms, keeping this to
    // one KWin round-trip per gesture rather than one per frame.
    Connections {
        target: bar.noteWindow
        function onScreenChanged() {
            if (bar.noteWindow)
                repositionDebounce.restart()
        }
        function onNoteXChanged() {
            if (bar.noteWindow)
                repositionDebounce.restart()
        }
        function onNoteYChanged() {
            if (bar.noteWindow)
                repositionDebounce.restart()
        }
        function onNoteWidthChanged() {
            if (bar.noteWindow)
                repositionDebounce.restart()
        }
        function onNoteHeightChanged() {
            if (bar.noteWindow)
                repositionDebounce.restart()
        }
    }

    // Coalesces the two reposition() calls the lag documented just above
    // routinely produces — onNoteWindowChanged fires one immediately with a
    // STALE screen, then onScreenChanged fires another once the real
    // wl_surface enter/leave lands — into a single call against settled
    // state.
    //
    // Bug found live ("toolbar/icon layout orientation mismatch, only one
    // icon visible in the middle"; "unable to consistently replicate"), and
    // the intermittency is the tell. reposition() picks the note's edge by
    // clamping against screen.virtualX/width (see its edge-priority chain),
    // so a stale screen readily picks a DIFFERENT edge than the settled one
    // — e.g. vertical=true, then vertical=false a few ms later. Each flip
    // of isVertical forces a real 436x36 <-> 36x436 client<->compositor
    // resize negotiation, so two flips back-to-back means two negotiations
    // racing: exactly the hazard spellingSuggestionsPopup's own width/height
    // comment documents as trace-confirmed on this platform (a belated,
    // stale ack from the earlier one landing AFTER the fresher one and
    // silently overwriting the window's real on-screen geometry). Lose that
    // race and the surface stays at the OLD orientation's size while
    // content.flow — pure client-side QML, applied instantly — has already
    // reflowed to the new one. A 436px-wide row inside a 36px-wide surface,
    // centered via content's anchors.centerIn, overflows equally off both
    // ends and clips: one icon left visible in the middle. barThickness/
    // barLength are fixed constants deliberately (see their comment), so
    // nothing ever recomputes the surface back — it stays clipped until
    // something else happens to trigger a fresh reposition.
    //
    // Debouncing removes the back-to-back pair at the source rather than
    // trying to correct after the fact. A ONE-SHOT timer restarted by real
    // events (never `repeat: true`, which this project bans outright) is
    // event-driven, not polling, and costs nothing while idle — the same
    // pattern, for the same reason, as NoteWindow.qml's positionSaveDebounce.
    // 50ms is comfortably longer than the observed gap between the two
    // calls, while staying well inside the ~60-100ms KWin round-trip that
    // positionManager.moveWindow() needs afterward anyway (see
    // PositionManager::moveWindow), so it adds no perceptible latency to
    // where the bar actually lands.
    Timer {
        id: repositionDebounce
        interval: 50
        repeat: false
        onTriggered: bar.reposition()
    }

    // Recomputes the default position when the tracked note *changes*
    // (including going from/to null). The Connections block above covers
    // the other case — the still-tracked note's own geometry moving under
    // the bar. Between the two, a position the user chose by dragging the
    // bar itself (see dragArea below) survives only until the next of
    // either happens, at which point the bar re-anchors to the note.
    onNoteWindowChanged: {
        // Computed fresh here, not read from the wantsVisible property:
        // wantsVisible and this handler are both listeners on the same
        // noteWindowChanged signal, and QML doesn't guarantee wantsVisible's
        // own binding has already re-evaluated by the time this handler
        // runs (bindings vs. on-handlers on the same signal race) — same
        // reasoning insertTableButton's own dragArea.enabled comment below
        // already gives for not trusting a plain declarative binding here.
        // wantsVisible itself stays fine for width/height above, which are
        // read declaratively rather than from inside another signal
        // handler racing the same change.
        const tracking = (noteWindow !== null)
        background.visible = tracking
        content.visible = tracking
        content.enabled = tracking
        dragArea.enabled = tracking
        if (tracking) {
            // Debounced, not called straight through — see
            // repositionDebounce's own comment for the orientation-clipping
            // bug that the immediate call (racing onScreenChanged's own,
            // moments later) causes.
            repositionDebounce.restart()
        } else {
            // Dropped, not left pending: a reposition landing after the bar
            // has already been parked would drag it back on-screen for a
            // note that's no longer tracked. reposition() bails on a null
            // noteWindow anyway, but stopping here keeps that a belt-and-
            // braces rather than the only thing standing between a park and
            // an unwanted move.
            repositionDebounce.stop()
            bar.park()
        }
        updateBlur()
        columnOptionsPopup.updateBlur()
        spellingSuggestionsPopup.updateBlur()
        // Precaches every ALREADY-misspelled word's popup size for the
        // newly-tracked note too — see spellingSuggestionsPopup's own
        // precacheAllCurrentSuggestionSizes() comment. Without this, a note
        // focused for the first time this session would only start
        // precaching from its NEXT edit onward (misspelledWordRangesChanged
        // firing), leaving whatever was already misspelled when the note
        // opened stuck on the slow, uncached path for its first tap.
        if (tracking)
            spellingSuggestionsPopup.precacheAllCurrentSuggestionSizes()
        // Bug found live ("switching between misspelled words that are on
        // different notes causes the position to not update to the new
        // note, though the suggestions do... inconsistent, rarely works"):
        // spellingSuggestionsPopup.wantsVisible depends on
        // bar.noteWindow.currentMisspelledWord, so its own
        // onWantsVisibleChanged only fires on a false->true (or true->false)
        // EDGE. If the newly-focused note already has a misspelled word
        // flagged by the time noteWindow retargets here — a real race
        // between window activation and tap-handling order (see
        // NoteWindow.qml's TapHandler, which sets currentMisspelledWord) —
        // wantsVisible reads true both immediately before AND after this
        // reassignment (the old note's word, then the new note's), so it
        // never toggles, and the popup silently keeps showing at the OLD
        // note's position while only its suggestion list (bound straight
        // through to noteWindow, no edge-detection involved) updates to
        // match the new word. This explicit, unconditional check — not
        // gated on any edge — catches exactly that case; it's a harmless
        // no-op duplicate on the normal false->true path, since
        // beginSpellingSuggestionsTransition() just re-issues the same
        // hide/reposition/reveal cycle onWantsVisibleChanged already
        // started.
        if (tracking && noteWindow.currentMisspelledWord !== "")
            spellingSuggestionsPopup.beginSpellingSuggestionsTransition()
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

        // Keeps this window's input region in lockstep with whether it's
        // actually showing anything — see ChromeWindow.park()'s own comment for
        // why the offscreen park can't be trusted to do this job, and
        // WindowInputMask's for the Wayland mechanics.
        //
        // Hooked to `visible` here rather than called from each of the
        // handlers that flip it: this Rectangle's own visibility IS the
        // file's existing "is this window really showing content" flag (the
        // same one updateBlur() keys off), so one hook on it can't drift out
        // of sync with a flip site someone forgets to update later.
        onVisibleChanged: windowInputMask.setAcceptsInput(bar, background.visible)

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
    ChromeGlass {
        anchors.fill: background
        cornerRadius: background.radius
        noteWindow: bar.noteWindow
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
        // (feature request), while the drag-handle group (grip + divider) —
        // always the first child — stays on the toolbar's first line/column
        // regardless (see nonButtonChildCount's own comment). -1 tells
        // GridLayout to compute that dimension automatically from the other.
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
            enabled: !bar.formattingSuppressed
            onClicked: bar.runAction(() => bar.noteWindow.applyInlineToggle(r => bar.noteWindow.docHandler.toggleBold(r.start, r.end)))
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
            enabled: !bar.formattingSuppressed
            onClicked: bar.runAction(() => bar.noteWindow.applyInlineToggle(r => bar.noteWindow.docHandler.toggleItalic(r.start, r.end)))
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
            enabled: !bar.formattingSuppressed
            onClicked: bar.runAction(() => bar.noteWindow.applyInlineToggle(r => bar.noteWindow.docHandler.toggleStrikethrough(r.start, r.end)))
            ToolbarButtonTip { id: strikethroughButtonTip; text: "Strikethrough" }
        }
        // Every button from here down to insertImageButton is absent on the
        // TickTick task note — see taskListNote for why, and buttonCount,
        // which has to agree with how many of them go.
        ToolButton {
            id: heading1Button
            text: "H1"
            visible: !bar.taskListNote
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarButtonLabel {}
            checkable: true
            checked: bar.noteWindow ? bar.noteWindow.currentHeadingLevel === 1 : false
            enabled: !bar.formattingSuppressed
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.setHeadingLevel(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd, 1))
            ToolbarButtonTip { text: "Heading 1" }
        }
        ToolButton {
            id: heading2Button
            text: "H2"
            visible: !bar.taskListNote
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarButtonLabel {}
            checkable: true
            checked: bar.noteWindow ? bar.noteWindow.currentHeadingLevel === 2 : false
            enabled: !bar.formattingSuppressed
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.setHeadingLevel(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd, 2))
            ToolbarButtonTip { text: "Heading 2" }
        }
        ToolButton {
            id: bulletListButton
            visible: !bar.taskListNote
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
            enabled: !bar.formattingSuppressed
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.toggleBulletList(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd))
            ToolbarButtonTip { text: "Bullet List" }
        }
        ToolButton {
            id: numberedListButton
            visible: !bar.taskListNote
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
            enabled: !bar.formattingSuppressed
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.toggleNumberedList(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd))
            ToolbarButtonTip { text: "Numbered List" }
        }
        ToolButton {
            id: checkboxListButton
            visible: !bar.taskListNote
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
            enabled: !bar.formattingSuppressed
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
            visible: !bar.taskListNote
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarSvgIcon {
                path: "M20 2H4c-1.1 0-2 .9-2 2v16c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2zM8 20H4v-4h4v4zm0-6H4v-4h4v4zm0-6H4V4h4v4zm6 12h-4v-4h4v4zm0-6h-4v-4h4v4zm0-6h-4V4h4v4zm6 12h-4v-4h4v4zm0-6h-4v-4h4v4zm0-6h-4V4h4v4z"
            }
            enabled: !bar.formattingSuppressed
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.insertTable(bar.noteWindow.editor.selectionEnd))
            ToolbarButtonTip {
                text: "Insert Table"
                suppressed: bar.noteWindow ? bar.noteWindow.currentInTable : false
            }
        }

        // Insert an empty image placeholder (feature request): drops "![]()" on
        // its own line at the cursor, which renders as a Browse placeholder the
        // user then fills in (or drag-and-drop an image file straight onto the
        // note instead — see NoteWindow.qml's DropArea). Material "image" glyph.
        ToolButton {
            id: insertImageButton
            visible: !bar.taskListNote
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            hoverEnabled: true
            contentItem: ToolbarSvgIcon {
                path: "M21 19V5c0-1.1-.9-2-2-2H5c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2zM8.5 13.5l2.5 3.01L14.5 12l4.5 6H5l3.5-4.5z"
            }
            enabled: !bar.formattingSuppressed
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.insertImage(bar.noteWindow.editor.selectionEnd))
            ToolbarButtonTip { text: "Insert Image" }
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
    ChromeWindow {
        id: columnOptionsPopup
        title: "suopNotes-columnOptionsPopup"
        ruleId: "columnOptionsPopup"

        // wantsVisible-gated, not just columnOptionsBackground's own
        // width/height directly: this window stays permanently mapped with
        // WindowStaysOnTopHint (see Component.onCompleted's own comment
        // below), and the background's implicit size doesn't collapse to 0
        // just because it goes invisible. Without this gate, the popup kept
        // sitting on top of the note at its last anchored position after
        // leaving the table, silently eating clicks meant for the note
        // underneath even though nothing was visibly there anymore (bug
        // found live — same root cause as spellingSuggestionsPopup's own
        // width/height below, and the reason updateBlur()/dragArea.enabled
        // alone, right below, weren't a complete fix: they stopped the
        // popup from being visibly blurred/draggable while unwanted, but
        // not from still occupying screen space and intercepting clicks).
        // NOT gated on wantsVisible anymore (bug found live: a 1px popup
        // elsewhere in this file that persisted no matter what — see
        // bar's own width/height comment and spellingSuggestionsPopup's
        // own, trace-confirmed, for the full story): shrinking to 1x1 on
        // every hide, then back to real size on every show, forces a
        // genuine client<->compositor Wayland resize negotiation each
        // time, and two such negotiations close together can race, with a
        // stale belated "you're 1x1 now" acknowledgment silently
        // clobbering a fresher "you're real size again" one.
        // onWantsVisibleChanged below already calls ChromeWindow.park(),
        // which alone fully solves the original "blocks clicks after
        // closing" problem — a window sitting off every connected screen
        // can't overlap real content regardless of its size. Width/height
        // now stay at their real content size unconditionally; only
        // position changes on hide.
        width: isFinite(columnOptionsBackground.width) ? columnOptionsBackground.width : 80
        height: isFinite(columnOptionsBackground.height) ? columnOptionsBackground.height : 32

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

        // Same "shown once at startup, content-visibility toggled from then on"
        // discipline as `bar` itself, now provided by ChromeWindow's map-once
        // ritual (see there). This popup only adds applying blur once the surface
        // exists — repositionColumnOptionsPopup() below takes over real placement
        // the instant it first becomes relevant.
        onMapped: columnOptionsPopup.updateBlur()

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
            else
                columnOptionsPopup.park()
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
            // Same input-region hook, same reasoning, as bar's own
            // background above — see it and ChromeWindow.park() for why this
            // window can't rely on being parked off-screen to stop eating
            // clicks while it isn't wanted.
            onVisibleChanged: windowInputMask.setAcceptsInput(columnOptionsPopup, columnOptionsBackground.visible)
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
        ChromeGlass {
            anchors.fill: columnOptionsBackground
            cornerRadius: columnOptionsBackground.radius
            noteWindow: bar.noteWindow
            contentShowing: columnOptionsBackground.visible
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

    // Spelling-suggestion popup (feature request): shown when the user taps
    // a misspelled word flagged by the spellcheck highlighter — NoteWindow's
    // own TapHandler sets currentMisspelledWord/currentSpellingSuggestions/
    // misspelledWordStart/misspelledWordEnd on the tracked note when that
    // happens (see NoteWindow.qml), and clears currentMisspelledWord again
    // once nothing is relevant to show. Offers a short list of replacement
    // words, anchored below the tapped word itself.
    //
    // A genuine third top-level Window, not an Item embedded in this
    // Window's own tree, for the same reason `bar`/columnOptionsPopup are
    // (see their own header comments): it needs to render outside this
    // toolbar's own small fixed surface, anchored to an arbitrary point
    // inside the note rather than to the toolbar itself.
    //
    // Deliberately simpler than columnOptionsPopup: no drag handle/grip
    // (this is an ephemeral suggestion list, not something worth letting the
    // user drag around) — but DOES get the same real compositor blur-behind
    // now (feature request: every popup should match the toolbar's own
    // styling), wired up the same wantsVisible-gated way columnOptionsPopup's
    // own updateBlur() already is (see its comment for the bug that gating
    // avoids: a still-mapped, blurred window sitting around after "closing").
    ChromeWindow {
        id: spellingSuggestionsPopup
        title: "suopNotes-spellingSuggestionsPopup"
        ruleId: "spellingSuggestionsPopup"

        // wantsVisible-gated, not just spellingSuggestionsBackground's own
        // width/height directly: this window stays permanently mapped with
        // WindowStaysOnTopHint (see Component.onCompleted's own comment
        // below), and the background's implicit size doesn't collapse to 0
        // just because it goes invisible. Without this gate, the popup kept
        // sitting on top of the note at its last anchored position after
        // "closing," silently eating clicks meant for the note underneath
        // even though nothing was visibly there anymore (bug found live).
        // Shrinking to 1x1 (NOT 0x0 — bug found live: a real xdg-shell
        // Wayland surface rejects a 0-width/height window geometry commit
        // as a protocol error and kills the whole Wayland connection,
        // crashing the app; see bar's own width/height comment above for
        // the full story).
        //
        // NOT gated on wantsVisible at all anymore (bug found live: "1px
        // popup" persisting even with the isFinite+Math.max floor below in
        // place) — shrinking to 1x1 on every hide, then back to real size
        // on every show, meant EVERY hide/show cycle forced a genuine
        // client<->compositor Wayland resize negotiation (request,
        // configure, ack), not just a QML property write. Two such
        // negotiations close together (hide, then show again moments
        // later — routine once a word's popup can reopen for the same or
        // a different word) could race: a stale, belated "you're 1x1 now"
        // acknowledgment from the PRIOR hide landing after the fresh
        // "you're back to your real size" one, silently overwriting the
        // window's actual on-screen geometry back to 1x1 — with NO
        // corresponding change to spellingSuggestionsBackground.width
        // (confirmed live via trace logging: width dropped to 1 while
        // wantsVisible stayed true and background.width never changed),
        // meaning this was a real platform-level race, not a QML binding
        // bug. ChromeWindow.park() below already fully solves the
        // original "blocks clicks after closing" problem on its own — a
        // window sitting off every connected screen can't overlap real
        // content regardless of its size — so shrinking was never load-
        // bearing for that fix to begin with, just an extra (and, it turns
        // out, actively harmful) belt-and-suspenders that introduced its
        // own worse bug. Width/height now track content size
        // unconditionally; only position changes on hide.
        width: isFinite(spellingSuggestionsBackground.width) ? spellingSuggestionsBackground.width : 80
        height: isFinite(spellingSuggestionsBackground.height) ? spellingSuggestionsBackground.height : 32

        // "" means "nothing to show" (see NoteWindow.qml's own
        // currentMisspelledWord doc comment) — mirrors columnOptionsPopup's
        // own wantsVisible, just gated on the misspelled-word property
        // instead of currentInTable.
        readonly property bool wantsVisible: bar.noteWindow && bar.noteWindow.currentMisspelledWord !== ""

        // Exposed for tests only, same convention as bar's/
        // columnOptionsPopup's own aliases above.
        readonly property alias background: spellingSuggestionsBackground
        readonly property alias content: spellingSuggestionsContent
        readonly property alias repeater: suggestionsRepeater

        // Same "shown once at startup, content-visibility toggled from then on"
        // discipline as `bar`/columnOptionsPopup, now provided by ChromeWindow's
        // map-once ritual (see there). This popup only adds applying blur once
        // the surface exists — repositionSpellingSuggestionsPopup() below takes
        // over real placement the instant it first becomes relevant.
        onMapped: spellingSuggestionsPopup.updateBlur()

        // Same real compositor blur-behind as columnOptionsPopup's own
        // (feature request) — see its updateBlur() for the reasoning
        // behind gating on wantsVisible, not just bar.noteWindow.
        //
        // Gated on spellingSuggestionsBackground.visible here, NOT
        // wantsVisible directly (bug found live: "the blur is visible
        // before the actual popup shows up, creating another flash") —
        // wantsVisible flips true the INSTANT a word is tapped, well
        // before content reveal is confirmed (see
        // spellingSuggestionsRevealIfReady's own comment), but the
        // compositor blur-behind region is a real, immediately-visible
        // effect applied to the window's current geometry regardless of
        // whether spellingSuggestionsBackground itself is painting
        // anything — enabling it on wantsVisible alone put a blurred
        // patch on screen at the popup's stale, not-yet-confirmed
        // position before the real content ever appeared. Tying it to the
        // same visible flag the reveal/hide logic itself flips means blur
        // only ever turns on at the exact moment real content does too.
        function updateBlur() {
            blurEffect.applyTo(spellingSuggestionsPopup,
                spellingSuggestionsBackground.visible && bar.noteWindow ? bar.noteWindow.noteAppearance.blurEnabled : false,
                spellingSuggestionsBackground.radius)
        }

        // Bug found live ("becomes visible before the position changes,
        // leading to bad UX"): revealing content synchronously here, the
        // instant wantsVisible flips true, meant the popup painted at
        // whatever position it happened to be sitting at already (its
        // off-screen park spot, or the seed) — repositionSpellingSuggestionsPopup()
        // below only REQUESTS the real position; positionManager.moveWindow()
        // actually applies it via a slow (~60-100ms observed), asynchronous
        // KWin script call (see PositionManager::moveWindow), so the
        // content was visibly popping in at the wrong spot and then
        // jumping once the move eventually landed.
        //
        // A fixed debounce delay used to paper over this, but a delay is
        // just a guess at how long the move will take — too short and the
        // same bug resurfaces, too long and it reads as sluggish. Instead,
        // this waits for actual CONFIRMATION that the position landed:
        // positionManager.windowMoved fires once moveWindow()'s background
        // thread genuinely finishes (see PositionManager::moveWindow), and
        // reveal only happens once every move issued so far
        // (spellingSuggestionsMovesIssued) has a matching completion
        // (spellingSuggestionsMovesCompleted) — a COUNT comparison, not an
        // identity check, because moveWindow calls for the same window
        // have no ordering guarantee on completion (see its own comment),
        // so "the latest move's own completion arrived" can't be detected
        // directly, but "as many completions as issuances" implies it has.
        // Hiding stays immediate (nothing to wait for when tucking
        // something away).
        property int spellingSuggestionsMovesIssued: 0
        property int spellingSuggestionsMovesCompleted: 0

        // Bug found live ("sometimes the popup will be cut off — only 2
        // options show up... sometimes cut off but the space is reserved
        // [the blur shows where the third would be]... sometimes it works
        // normally"): position confirmation alone says nothing about
        // whether spellingSuggestionsContent's own SIZE has finished
        // settling — the suggestion-list Repeater rebuild that
        // spellingSuggestionsResettleDebounce's own comment documents (real
        // content, then a wobble, then its real final size again) can still
        // be in progress at the moment the position confirmation arrives,
        // especially switching straight from one note's word to a
        // DIFFERENT note's — reveal used to fire the instant position
        // landed regardless, painting whatever partial size the content
        // happened to be at that instant. False the moment a transition
        // begins (and again on every width/height wobble in between);
        // flips true only once spellingSuggestionsResettleDebounce fires
        // with no further size change in between — see that Timer's own
        // trigger below.
        property bool spellingSuggestionsSizeSettled: false

        // Feature request ("the popup takes a bit of time to show up —
        // precache its position and size"): the suggestion STRINGS are
        // already precached the instant a word settles as misspelled (see
        // DocumentHandler::cachedSuggestionsFor's own comment), but the
        // popup's own on-screen SIZE isn't something C++ can compute at
        // all — it depends on QML/Qt Quick Controls font metrics for each
        // suggestion button's text plus this file's own padding/spacing.
        // Precaching that too, ahead of any tap, means
        // repositionSpellingSuggestionsPopup() can target the popup's real
        // final width/height on its very FIRST move request instead of
        // whatever size the popup happened to be at already (leftover from
        // the previous word, or a bare placeholder) — closing the gap that
        // used to need a SECOND, sequential KWin round-trip (first
        // confirming a wrong-size position, then correcting it once
        // spellingSuggestionsResettleDebounce caught up) down to just the
        // one round-trip most word-processors' spellcheck popups manage.
        //
        // Keyed by the suggestion list itself (joined into one string), not
        // the misspelled word — the popup's size depends only on what it
        // ends up DISPLAYING (each suggestion button's text, plus the
        // always-present "Add to dictionary" button), so two different
        // misspelled words that happen to produce the same suggestion list
        // share one cache entry. Plain JS object, not a QML property with
        // real change notification: nothing needs to react to a cache
        // entry appearing, only to READ it synchronously at the moment a
        // transition begins (see beginSpellingSuggestionsTransition below).
        property var suggestionsSizeCache: ({})

        // Bug found live ("TypeError: Cannot call method 'join' of
        // undefined"): `|| []`, not a bare suggestions.join(...) —
        // bar.noteWindow.currentSpellingSuggestions (predictedPopupSize's
        // own call below) can read back undefined rather than its declared
        // `property var currentSpellingSuggestions: []` default if the
        // tracked NoteWindow's underlying QObject is mid-destruction (the
        // note was just closed) when this runs — Qt Quick hands back
        // undefined for property access on a since-deleted QObject rather
        // than throwing. sizeMeasureQueue.shift() (see
        // sizeMeasureSettleDebounce's own onTriggered below) can likewise
        // hand back undefined if ever called against an empty queue.
        // Treating either as "no suggestions" is correct regardless of
        // which one actually happened here — there's nothing meaningful
        // left to key a popup size cache entry off of.
        function suggestionsSizeCacheKey(suggestions) {
            return (suggestions || []).join("\u0001")
        }

        // Returns the precached {width, height} for the CURRENTLY tracked
        // note's current suggestion list, or null if nothing's cached for
        // it yet (a brand new suggestion list this session hasn't seen
        // before — falls back to the normal live-measurement wait).
        function predictedPopupSize() {
            if (!bar.noteWindow)
                return null
            const key = suggestionsSizeCacheKey(bar.noteWindow.currentSpellingSuggestions)
            return key in suggestionsSizeCache ? suggestionsSizeCache[key] : null
        }

        // FIFO queue of suggestion lists still waiting to be measured.
        // Processed one at a time (see measureNextInQueue below) since
        // there's only one hidden measuring pass in flight at once — it
        // reuses a single shared Item (sizeMeasureContent below), the same
        // way spellingSuggestionsContent itself is a single shared Item for
        // the currently-displayed word.
        property var sizeMeasureQueue: []
        property bool sizeMeasureInProgress: false

        // Queues `suggestions` for background size measurement if (and
        // only if) it isn't already cached and isn't already queued — safe
        // to call for every currently-misspelled word on every
        // misspelledWordRangesChanged (see the Connections below): the
        // common case, nothing new having settled since last time, is just
        // a couple of cheap lookups.
        function precacheSuggestionsPopupSize(suggestions) {
            const key = suggestionsSizeCacheKey(suggestions)
            if (key in suggestionsSizeCache)
                return
            if (sizeMeasureQueue.some(queued => suggestionsSizeCacheKey(queued) === key))
                return
            sizeMeasureQueue.push(suggestions)
            measureNextInQueue()
        }

        function measureNextInQueue() {
            if (sizeMeasureInProgress || sizeMeasureQueue.length === 0)
                return
            sizeMeasureInProgress = true
            sizeMeasureRepeater.model = sizeMeasureQueue[0]
            sizeMeasureSettleDebounce.restart()
        }

        // Walks every currently-misspelled word in the tracked note and
        // queues its suggestion list for measurement — called once whenever
        // a NEW note gets tracked (see onNoteWindowChanged below) and again
        // every time that note's own misspelledWordRangesChanged fires
        // (see the Connections below), so newly-typed typos get their
        // popup size precached too, not just whatever was already
        // misspelled when the note was first focused.
        // Truncated to settings.spellcheckMaxSuggestions, same as
        // NoteWindow.qml's own TapHandler does before ever assigning
        // currentSpellingSuggestions (see its own comment there) — this
        // MUST match exactly, not just be "close enough": predictedPopupSize()
        // looks the cache up by bar.noteWindow.currentSpellingSuggestions,
        // the already-truncated list, so caching the full untruncated
        // suggestions() result here under a different key would make every
        // lookup a guaranteed miss, silently defeating precaching entirely.
        function precacheAllCurrentSuggestionSizes() {
            if (!bar.noteWindow)
                return
            const ranges = bar.noteWindow.docHandler.misspelledWordRanges
            for (let i = 0; i < ranges.length; i++) {
                const suggestions = bar.noteWindow.docHandler.suggestionsForWordAt(ranges[i].start)
                precacheSuggestionsPopupSize(suggestions.slice(0, settings.spellcheckMaxSuggestions))
            }
        }

        Connections {
            target: bar.noteWindow ? bar.noteWindow.docHandler : null
            function onMisspelledWordRangesChanged() {
                spellingSuggestionsPopup.precacheAllCurrentSuggestionSizes()
            }
        }

        // A structural mirror of spellingSuggestionsContent below (same
        // delegate, same padding/spacing, same always-present "Add to
        // dictionary" button and conditional "No suggestions" label) whose
        // only job is to sit off-screen and report implicit sizes for
        // suggestion lists nobody's tapped yet. visible: false, same as
        // spellingSuggestionsContent itself starts out — Qt Quick Layouts
        // still computes implicitWidth/implicitHeight for an invisible
        // item (confirmed by spellingSuggestionsBackground's own width/
        // height bindings below, which already rely on exactly that for
        // the real content before it's ever shown).
        ColumnLayout {
            id: sizeMeasureContent
            visible: false
            spacing: 2

            Repeater {
                id: sizeMeasureRepeater
                model: []
                ToolButton {
                    required property string modelData
                    text: modelData
                    implicitHeight: bar.buttonSize
                    leftPadding: 10
                    rightPadding: 10
                    contentItem: ToolbarButtonLabel {}
                }
            }
            Label {
                text: "No suggestions"
                visible: sizeMeasureRepeater.count === 0
                leftPadding: 10
                rightPadding: 10
                topPadding: 4
                bottomPadding: 4
            }
            ToolButton {
                text: "Add to dictionary"
                implicitHeight: bar.buttonSize
                leftPadding: 10
                rightPadding: 10
                contentItem: ToolbarButtonLabel {}
            }

            // Same wobble-then-settle behavior spellingSuggestionsContent's
            // own onWidthChanged/onHeightChanged already documents (a
            // Repeater model reassignment tears down/rebuilds every
            // delegate, and the Layout's implicit size genuinely dips
            // mid-rebuild before reaching its real final value) — restart
            // on every change, only commit once nothing's changed for
            // sizeMeasureSettleDebounce's own interval.
            onWidthChanged: sizeMeasureSettleDebounce.restart()
            onHeightChanged: sizeMeasureSettleDebounce.restart()
        }

        Timer {
            id: sizeMeasureSettleDebounce
            interval: 40
            onTriggered: {
                // Defense in depth alongside suggestionsSizeCacheKey's own
                // `|| []` guard: nothing to commit if a stray restart
                // (e.g. sizeMeasureContent's own onWidthChanged/
                // onHeightChanged firing after the queue's already been
                // drained) fires this with the queue empty.
                if (spellingSuggestionsPopup.sizeMeasureQueue.length === 0) {
                    spellingSuggestionsPopup.sizeMeasureInProgress = false
                    return
                }
                const suggestions = spellingSuggestionsPopup.sizeMeasureQueue.shift()
                const key = spellingSuggestionsPopup.suggestionsSizeCacheKey(suggestions)
                spellingSuggestionsPopup.suggestionsSizeCache[key] = Qt.size(
                    isFinite(sizeMeasureContent.implicitWidth) ? Math.max(sizeMeasureContent.implicitWidth + 8, 80) : 80,
                    isFinite(sizeMeasureContent.implicitHeight) ? Math.max(sizeMeasureContent.implicitHeight + 8, 32) : 32)
                spellingSuggestionsPopup.sizeMeasureInProgress = false
                spellingSuggestionsPopup.measureNextInQueue()
            }
        }

        function spellingSuggestionsRevealIfReady() {
            if (wantsVisible && spellingSuggestionsMovesCompleted >= spellingSuggestionsMovesIssued
                    && spellingSuggestionsSizeSettled) {
                spellingSuggestionsBackground.visible = true
                spellingSuggestionsContent.visible = true
                spellingSuggestionsContent.enabled = true
                spellingSuggestionsRevealFallback.stop()
                updateBlur()
            }
        }

        Connections {
            target: positionManager
            function onWindowMoved(windowTitle) {
                if (windowTitle === spellingSuggestionsPopup.title) {
                    spellingSuggestionsPopup.spellingSuggestionsMovesCompleted++
                    spellingSuggestionsPopup.spellingSuggestionsRevealIfReady()
                }
            }
        }

        // Safety net, not the primary mechanism: reveals unconditionally
        // if windowMoved somehow never arrives (e.g. KWin scripting
        // genuinely unavailable — see KWinRuleApplier::isAvailable — still
        // lets the worker thread finish and emit, but this covers any
        // other way completion could go missing) — a real popup that never
        // reveals is a worse bug than a rare, late reveal at a
        // not-yet-confirmed position. Restarted on every new move request,
        // not recurring — costs nothing once actually revealed.
        //
        // Bug found live ("still get a flash of the original position, not
        // sure what causes it"): observed KWin round-trips are usually
        // ~60-100ms (see PositionManager::moveWindow's own comment), but
        // under load — several moves queued back-to-back (a fresh show
        // plus its own resettle-debounce re-reposition, say) — a real
        // confirmation can occasionally land past the old 400ms mark. When
        // that happened, THIS fallback fired first and revealed the popup
        // still sitting at its prior, unconfirmed position, which then
        // visibly jumped once the real confirmation arrived a moment
        // later. Raised well past the observed worst case so it only ever
        // fires as a genuine last resort, not a normal-latency race.
        Timer {
            id: spellingSuggestionsRevealFallback
            interval: 1200
            onTriggered: {
                if (spellingSuggestionsPopup.wantsVisible) {
                    spellingSuggestionsBackground.visible = true
                    spellingSuggestionsContent.visible = true
                    spellingSuggestionsContent.enabled = true
                    spellingSuggestionsPopup.updateBlur()
                }
            }
        }

        // Bug found live ("doesn't transition position between two
        // misspelled words" / "stuck at the prior word until the user
        // clicks out"): wantsVisible only depends on currentMisspelledWord
        // being empty vs non-empty, so tapping misspelled word A then
        // DIRECTLY misspelled word B — both non-empty — never toggles it
        // at all, meaning onWantsVisibleChanged below (the only thing that
        // used to call repositionSpellingSuggestionsPopup() for a "show")
        // never re-fired for that transition. The popup only happened to
        // move at all when the resettle-debounce further below ALSO
        // happened to fire — which needs a real width/height change, so
        // two words whose suggestion lists produce the same popup size
        // (far more likely for two words near the same screen edge, since
        // both clamp to that same edge regardless of a few pixels of size
        // difference) left NOTHING to retrigger it, and the popup just
        // stayed stuck. Mirrors columnOptionsPopup's own
        // onCurrentInTableChanged Connections below for the exact same
        // reason (see its comment): wantsVisible alone doesn't capture
        // every case the anchor can move.
        //
        // Routed through the same hide-then-reveal-once-confirmed cycle
        // onWantsVisibleChanged's own "show" branch uses (rather than just
        // repositioning an already-visible popup in place) so a word-to-
        // word jump reveals at its new, confirmed position instead of
        // visibly sliding/flashing there while already on-screen.
        function beginSpellingSuggestionsTransition() {
            spellingSuggestionsBackground.visible = false
            spellingSuggestionsContent.visible = false
            spellingSuggestionsContent.enabled = false
            // Turns blur off immediately too (see updateBlur's own
            // comment) — matters here specifically for the word-to-word
            // path below, which calls this directly without going through
            // onWantsVisibleChanged's own trailing updateBlur() call.
            updateBlur()
            // Restarted here unconditionally, not just inside
            // repositionSpellingSuggestionsPopup() itself: if that bails
            // out early (e.g. the misspelledWordStart < 0 guard above —
            // defense in depth only, shouldn't happen given
            // NoteWindow.qml's own ordering fix, but just in case) no move
            // ever gets issued, and this safety net must still cover that
            // case so the popup can never end up permanently stuck
            // invisible.
            spellingSuggestionsRevealFallback.restart()
            // Not settled yet — this transition is about to (re)assign the
            // suggestion list, and its own content size may still be
            // mid-wobble (or, for a same-size word-to-word jump, might
            // never fire a width/height change at all) — restarted here
            // unconditionally, not left to onWidthChanged/onHeightChanged
            // alone, so a same-size jump still gets exactly one settle
            // window instead of leaving spellingSuggestionsSizeSettled
            // permanently false (see that property's own comment).
            //
            // EXCEPT when this word's popup size is already precached (see
            // predictedPopupSize's own comment) — repositionSpellingSuggestionsPopup()
            // below already targets the real final size in that case, so
            // there's nothing left to wait for; marking it settled
            // immediately lets the reveal go through as soon as the (now
            // correctly-targeted, one-shot) position confirmation lands
            // instead of ALSO waiting out spellingSuggestionsResettleDebounce.
            // The debounce below still restarts regardless, as a safety
            // net: if the real content ever settles at a genuinely
            // different size than predicted, its own onWidthChanged/
            // onHeightChanged handlers re-flip this false and this same
            // debounce corrects the position for real.
            spellingSuggestionsSizeSettled = (predictedPopupSize() !== null)
            spellingSuggestionsResettleDebounce.restart()
            repositionSpellingSuggestionsPopup()
        }

        Connections {
            target: bar.noteWindow
            function onMisspelledWordStartChanged() {
                if (spellingSuggestionsPopup.wantsVisible)
                    spellingSuggestionsPopup.beginSpellingSuggestionsTransition()
            }
        }

        onWantsVisibleChanged: {
            if (wantsVisible) {
                beginSpellingSuggestionsTransition()
            } else {
                spellingSuggestionsRevealFallback.stop()
                spellingSuggestionsBackground.visible = false
                spellingSuggestionsContent.visible = false
                spellingSuggestionsContent.enabled = false
                // Counted the same as repositionSpellingSuggestionsPopup()'s
                // own move, not a bare ChromeWindow.park() call (bug found
                // live): ChromeWindow.park() also calls positionManager.
                // moveWindow(), and that move's eventual completion ALSO
                // fires windowMoved for this window's title — the
                // Connections handler below matches purely by title, with
                // no way to tell "a move I'm gating on" apart from "some
                // other move for the same window". Leaving this
                // uncounted let that completion arrive as unearned credit
                // toward a LATER show's spellingSuggestionsMovesCompleted,
                // satisfying spellingSuggestionsRevealIfReady()'s check
                // before the NEW move had actually landed — revealing
                // content at the stale, PRIOR word's position instead of
                // the new one (this is what made tapping a correctly-spelled
                // word, which hides via this exact path, poison the next
                // real show — clicking outside every note instead cleared
                // bar.noteWindow only after Main.qml's own debounce delay,
                // long enough for any in-flight move to have already
                // settled, which is why that path alone seemed to "fix" it).
                spellingSuggestionsMovesIssued++
                spellingSuggestionsPopup.park()
            }
            updateBlur()
        }

        // Bug found live (root cause of the popup flashing into view then
        // immediately vanishing): the suggestion list is data-driven
        // (suggestionsRepeater.model tracks currentSpellingSuggestions), so
        // reassigning that model tears down and rebuilds every delegate —
        // during that churn, spellingSuggestionsContent's own implicit
        // size genuinely wobbles through several intermediate values over
        // the following tens of milliseconds before settling (observed
        // live: real content size, then a near-zero dip mid-rebuild, then
        // its real final size again). onWantsVisibleChanged's own
        // immediate repositionSpellingSuggestionsPopup() call above only
        // ever captures ONE of those sizes — whichever happened to be
        // current at that instant, sometimes the near-zero one — and
        // nothing corrected the popup's position afterward as its real,
        // final size arrived, so it could end up positioned (partly or
        // fully) off-screen once settled, since the "does it fit below
        // the screen" check never saw the size it would actually end up
        // being. Restarting this debounce on every width/height change and
        // repositioning once more when it fires means the popup's position
        // always catches up to its real, settled size.
        //
        // Debounced, not repositioned on every wobble directly: each
        // reposition call also calls positionManager.moveWindow(), which
        // spawns its own slow (~100ms), unsynchronized background thread
        // per call (see PositionManager::moveWindow) — repositioning on
        // every intermediate wobble would queue several overlapping KWin
        // script calls with no ordering guarantee between them, the same
        // class of startup race already fixed elsewhere in this file (see
        // Component.onCompleted's own comment above). A one-shot Timer
        // restarted by a real size-change event, not a recurring poll —
        // costs nothing while the popup is settled.
        Timer {
            id: spellingSuggestionsResettleDebounce
            interval: 40
            onTriggered: {
                if (spellingSuggestionsPopup.wantsVisible)
                    spellingSuggestionsPopup.repositionSpellingSuggestionsPopup()
                // No further width/height change landed in the last 40ms —
                // see spellingSuggestionsSizeSettled's own comment: this is
                // what actually allows a reveal that's still waiting on it
                // (position may already have confirmed earlier) to finally
                // go through, painting the popup's real, fully-settled size
                // instead of whatever partial size it happened to be
                // mid-wobble.
                spellingSuggestionsPopup.spellingSuggestionsSizeSettled = true
                spellingSuggestionsPopup.spellingSuggestionsRevealIfReady()
            }
        }
        onWidthChanged: {
            if (wantsVisible) {
                spellingSuggestionsSizeSettled = false
                spellingSuggestionsResettleDebounce.restart()
            }
            updateBlur()
        }
        onHeightChanged: {
            if (wantsVisible) {
                spellingSuggestionsSizeSettled = false
                spellingSuggestionsResettleDebounce.restart()
            }
            updateBlur()
        }

        // Same stale-screen bugfix as bar's/columnOptionsPopup's own
        // onScreenChanged handlers above (see bar's own comment) —
        // repositionSpellingSuggestionsPopup() reads note.screen too (see
        // its own body below), so it needs the same late-arriving-screen
        // re-trigger. (The onMisspelledWordStartChanged Connections above
        // covers the "anchor moved without wantsVisible itself changing"
        // case for jumping between two misspelled words directly.)
        Connections {
            target: bar.noteWindow
            function onScreenChanged() {
                if (spellingSuggestionsPopup.wantsVisible)
                    spellingSuggestionsPopup.repositionSpellingSuggestionsPopup()
            }
        }

        // Feature request: the popup should track the flagged word as the
        // note scrolls, not just stay frozen at its last on-screen spot —
        // NoteWindow.qml's own editorFlick.onContentYChanged already
        // clears currentMisspelledWord (hiding this popup) once the word
        // scrolls fully out of view; this covers the other half, keeping
        // the popup anchored correctly while the word is still visible but
        // has moved within the note. Guarded on wantsVisible rather than
        // assuming NoteWindow's own clear (a listener on the very same
        // contentYChanged signal) has already run first — listener order
        // between two independent objects isn't guaranteed, so this reads
        // its own current wantsVisible rather than relying on that
        // ordering; if the word IS about to go out of view, this issues
        // one harmless extra reposition moments before NoteWindow's own
        // handler hides the popup anyway.
        Connections {
            target: bar.noteWindow ? bar.noteWindow.editorFlick : null
            function onContentYChanged() {
                if (spellingSuggestionsPopup.wantsVisible)
                    spellingSuggestionsPopup.repositionSpellingSuggestionsPopup()
            }
        }

        // Anchors directly below the tapped word itself — same technique
        // repositionColumnOptionsPopup() uses for a table's own rect (see
        // its own comment), just via positionToRectangle(start) (a
        // TextArea builtin converting a text offset to a local rect)
        // instead of docHandler.tableRect(...). Falls back to ABOVE the
        // anchor only when there isn't room below on the tracked note's own
        // screen, same as the other two popups.
        function repositionSpellingSuggestionsPopup() {
            const note = bar.noteWindow
            if (!note)
                return

            // Defense in depth, not the primary fix (see NoteWindow.qml's
            // own tap-to-suggest TapHandler comment for that — it now
            // assigns currentMisspelledWord LAST specifically so this
            // always runs with a real, already-current
            // misspelledWordStart): bails out rather than calling
            // positionToRectangle(-1), which logs a live "QTextCursor::
            // setPosition: Position '-1' out of range" warning and hands
            // back a garbage rect, silently mispositioning the popup —
            // exactly the bug this guard is here in case any other call
            // site ever gets the ordering wrong again.
            if (note.misspelledWordStart < 0)
                return

            const rect = note.editor.positionToRectangle(note.misspelledWordStart)
            const topLeft = note.editor.mapToGlobal(rect.x, rect.y)
            const anchorTop = topLeft.y
            const anchorBottom = topLeft.y + rect.height

            // Feature request: use the PRECACHED size for this word's own
            // suggestion list when one's available (see
            // precacheAllCurrentSuggestionSizes' own comment), rather than
            // spellingSuggestionsPopup.width/height directly — right as a
            // transition begins, those two are still whatever the
            // PREVIOUS word's popup measured (or the 80x32 placeholder),
            // not this word's real size yet; that only arrives
            // asynchronously once spellingSuggestionsContent's own Repeater
            // rebuilds and settles (see spellingSuggestionsResettleDebounce).
            // Targeting the real size on this, the FIRST move request for
            // this word, means the below/above and left/right clamping
            // below is already correct — the debounce-triggered reposition
            // that used to be needed to CORRECT a wrong-size guess becomes
            // a no-op confirmation instead of a second, sequential KWin
            // round-trip.
            const predicted = predictedPopupSize()
            const popupWidth = predicted ? predicted.width : spellingSuggestionsPopup.width
            const popupHeight = predicted ? predicted.height : spellingSuggestionsPopup.height

            let targetX = topLeft.x
            let targetY = anchorBottom + 4

            const screen = note.screen
            if (screen && targetY + popupHeight > screen.virtualY + screen.height)
                targetY = anchorTop - popupHeight - 4

            // X-axis clamp (bug found live: a word right at the screen's
            // left/right edge), mirroring bar's own reposition()'s
            // clampedHorizontalX — targetX used to be the anchor's raw x
            // with no clamping at all, so a word near either edge could
            // request a position with the popup's own width extending
            // past that edge.
            if (screen)
                targetX = Math.max(screen.virtualX, Math.min(targetX, screen.virtualX + screen.width - popupWidth))

            spellingSuggestionsPopup.x = targetX
            spellingSuggestionsPopup.y = targetY
            spellingSuggestionsMovesIssued++
            positionManager.moveWindow(spellingSuggestionsPopup.title, targetX, targetY)
        }

        Rectangle {
            id: spellingSuggestionsBackground
            visible: false
            // Same input-region hook, same reasoning, as bar's own
            // background above (see ChromeWindow.park()). This window's reveal
            // is deliberately deferred until its position/size are confirmed
            // (see spellingSuggestionsRevealIfReady), so hooking `visible`
            // rather than wantsVisible also means it only starts accepting
            // clicks once it's genuinely settled where the user can see it.
            onVisibleChanged: windowInputMask.setAcceptsInput(spellingSuggestionsPopup, spellingSuggestionsBackground.visible)
            // Math.max floor, not a bare implicitWidth/Height passthrough
            // (bug found live: "sometimes a 1px-wide popup"): suggestionsRepeater's
            // model is data-driven (currentSpellingSuggestions), so every
            // reassignment tears down and rebuilds its delegates —
            // spellingSuggestionsContent's own implicit size genuinely
            // dips to a near-zero value mid-rebuild before settling (see
            // the debounced re-reposition Timer above, which fixes the
            // resulting stale POSITION but not a stale SIZE if the window
            // happens to render during that exact dip). A floor well below
            // any real suggestion popup's natural size means a mid-rebuild
            // dip can never visually squish the popup, regardless of
            // exactly when Qt Quick Layouts settles.
            //
            // isFinite-guarded, not a bare Math.max (bug found live: the
            // 1px-wide popup persisted even with the floor above in place)
            // — Math.max(NaN, 80) evaluates to NaN in JavaScript, not 80,
            // because NaN compares false against everything including
            // itself; a mid-rebuild layout pass that momentarily produces
            // an invalid (NaN) implicitWidth/Height, not just a near-zero
            // one, silently poisoned the floor entirely, and Qt's own
            // handling of an assigned NaN window dimension is what actually
            // produced the visible 1px sliver. Falling straight to the
            // floor for any non-finite value closes that gap.
            width: isFinite(spellingSuggestionsContent.implicitWidth) ? Math.max(spellingSuggestionsContent.implicitWidth + 8, 80) : 80
            height: isFinite(spellingSuggestionsContent.implicitHeight) ? Math.max(spellingSuggestionsContent.implicitHeight + 8, 32) : 32
            radius: 6
            color: background.color
            border.width: background.border.width
            border.color: background.border.color
        }

        // Same glass treatment as the main bar's/columnOptionsPopup's own
        // backgrounds (feature request: every popup should look like it
        // belongs to the toolbar, not a plain flat rectangle).
        ChromeGlass {
            anchors.fill: spellingSuggestionsBackground
            cornerRadius: spellingSuggestionsBackground.radius
            noteWindow: bar.noteWindow
            contentShowing: spellingSuggestionsBackground.visible
        }

        // A stacked column of suggestion buttons reads more naturally than
        // a row for a short word list (up to ~5 short entries) — the first
        // Repeater in this file, since there's no existing dynamic-content
        // popup to mirror (every other popup here has a fixed, hand-written
        // set of buttons).
        ColumnLayout {
            id: spellingSuggestionsContent
            visible: false
            enabled: false
            anchors.centerIn: spellingSuggestionsBackground
            spacing: 2

            Repeater {
                id: suggestionsRepeater
                model: bar.noteWindow ? bar.noteWindow.currentSpellingSuggestions : []

                ToolButton {
                    id: suggestionButton
                    required property string modelData
                    text: modelData
                    implicitHeight: bar.buttonSize
                    leftPadding: 10
                    rightPadding: 10
                    // Layout.fillWidth, NOT Layout.alignment (bug found
                    // live: "the suggestions' targets are only as large as
                    // the text"): an aligned button keeps its own implicit,
                    // text-derived width, so a short suggestion sitting in
                    // a column sized by a much longer one left most of its
                    // own row dead — the hover highlight stopped at the end
                    // of the word and a click just past it hit nothing at
                    // all, even though the popup visibly reserved that
                    // space. Filling makes every button span the column's
                    // full width, so the whole reserved row hovers and
                    // clicks as one target. Text stays flush left via the
                    // contentItem's own AlignLeft below, which is what the
                    // alignment was really there for.
                    Layout.fillWidth: true
                    background: ToolbarButtonHighlight {}
                    hoverEnabled: true
                    contentItem: ToolbarButtonLabel { horizontalAlignment: Text.AlignLeft }
                    // Replaces the misspelled word with this suggestion,
                    // then immediately clears currentMisspelledWord so the
                    // popup closes right away — doesn't depend on ordering
                    // against NoteWindow's own onTextChanged handler (which
                    // would also eventually clear it as a side effect of the
                    // text actually changing), just does it explicitly here
                    // for a more immediate, certain close.
                    onClicked: bar.runAction(() => {
                        bar.noteWindow.docHandler.replaceRange(bar.noteWindow.misspelledWordStart, bar.noteWindow.misspelledWordEnd, suggestionButton.modelData)
                        bar.noteWindow.currentMisspelledWord = ""
                    })
                }
            }

            // Bug found live: a misspelled word can have zero suggestions
            // (e.g. too garbled for the dictionary to find any close match)
            // — the popup used to just show an empty Repeater in that case,
            // reading as "nothing happened" even though it WAS open. This
            // makes that state explicit instead.
            Label {
                id: noSuggestionsLabel
                text: "No suggestions"
                visible: suggestionsRepeater.count === 0
                color: bar.noteWindow ? bar.noteWindow.noteAppearance.fontColor : "#ffffff"
                opacity: 0.6
                Layout.alignment: Qt.AlignLeft
                leftPadding: 10
                rightPadding: 10
                topPadding: 4
                bottomPadding: 4
            }

            // "Add to dictionary" (feature request): the misspelled word
            // currently selected via tap becomes permanently exempt from
            // spellcheck, app-wide, regardless of which backend/language is
            // active — see UserDictionaryFilteringBackend. Only THIS note's
            // highlighter is forced to re-check immediately
            // (rehighlightSpellcheck()); other already-open notes pick up
            // the change on their own next edit, a deliberate scope cut
            // (see DocumentHandler::rehighlightSpellcheck's own comment).
            ToolButton {
                id: addToDictionaryButton
                text: "Add to dictionary"
                implicitHeight: bar.buttonSize
                leftPadding: 10
                rightPadding: 10
                // Same full-row target as each suggestion button above —
                // see that Layout.fillWidth's own comment.
                Layout.fillWidth: true
                background: ToolbarButtonHighlight {}
                hoverEnabled: true
                contentItem: ToolbarButtonLabel { horizontalAlignment: Text.AlignLeft }
                onClicked: bar.runAction(() => {
                    settings.addUserDictionaryWord(bar.noteWindow.currentMisspelledWord)
                    bar.noteWindow.docHandler.rehighlightSpellcheck()
                    bar.noteWindow.currentMisspelledWord = ""
                })
            }
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
    ChromeWindow {
        id: tooltipWindow
        title: "suopNotes-toolbarTooltip"
        ruleId: "toolbarTooltip"

        // NOT gated on contentVisible anymore (bug found live: a 1px popup
        // elsewhere in this file that persisted no matter what — see
        // bar's own width/height comment and spellingSuggestionsPopup's
        // own, trace-confirmed, for the full story): this window stays
        // permanently mapped with WindowStaysOnTopHint (see
        // Component.onCompleted's own comment below), and shrinking it to
        // 1x1 on every hide, then back to real size on every show — which
        // for a tooltip that toggles on every hover in/out happens VERY
        // often — forces a genuine client<->compositor Wayland resize
        // negotiation each time, with two close together able to race and
        // leave the window stuck at a stale size. hideTip() below already
        // calls ChromeWindow.park(), which alone fully solves the original
        // "blocks clicks after hiding" problem — a window sitting off
        // every connected screen can't overlap real content regardless of
        // its size. Width/height now stay at their real text size
        // unconditionally; only position changes on hide.
        width: isFinite(tooltipBackground.width) ? tooltipBackground.width : 40
        height: isFinite(tooltipBackground.height) ? tooltipBackground.height : 20

        // Re-apply the blur-behind whenever the window resizes: the tooltip's
        // size changes with its text (a different button's tip is a different
        // width), and blur is applied to the window's current geometry.
        onWidthChanged: updateBlur()
        onHeightChanged: updateBlur()

        property string text: ""
        property bool contentVisible: false
        // Which button's tooltip is currently showing, if any — lets a
        // button whose own hover already ended avoid hiding a tooltip
        // that a DIFFERENT button has since taken over showing (a hover
        // handoff between two adjacent buttons can fire in either order).
        property var activeButton: null

        readonly property alias background: tooltipBackground

        // The map-once ritual now lives in ChromeWindow (see there). showTip()
        // below takes over real positioning the instant any tooltip is first
        // shown; this only adds applying blur once the surface exists.
        onMapped: tooltipWindow.updateBlur()

        // Blur-behind, matching the rest of the toolbar chrome (feature
        // request: the tooltip should read as glass too, not just carry the
        // fake sheen the GlassOverlay below paints). Gated on contentVisible —
        // the tooltip's real "is anything showing" flag, which tooltipBackground
        // binds its own visibility to — rather than applied unconditionally, so
        // the blur is present exactly while the tip is, and follows the tracked
        // note's own blur setting just as bar/columnOptionsPopup/
        // spellingSuggestionsPopup do (no tracked note ⇒ no blur, same as them).
        // Re-applied per show/hide below rather than bound, so it always uses
        // the tip's freshly-set position and the note's current setting.
        function updateBlur() {
            blurEffect.applyTo(tooltipWindow,
                tooltipWindow.contentVisible && bar.noteWindow
                    ? bar.noteWindow.noteAppearance.blurEnabled
                    : false,
                tooltipBackground.radius)
        }

        function showTip(button, tipText) {
            tooltipWindow.activeButton = button
            tooltipWindow.text = tipText
            tooltipWindow.contentVisible = true
            const pos = button.mapToGlobal(0, button.height + 6)
            tooltipWindow.x = pos.x
            tooltipWindow.y = pos.y
            positionManager.moveWindow(tooltipWindow.title, pos.x, pos.y)
            updateBlur()
        }

        function hideTip() {
            tooltipWindow.contentVisible = false
            tooltipWindow.activeButton = null
            tooltipWindow.park()
            updateBlur()
        }

        // color/border mirror the main bar's own background exactly (same
        // note-backgroundColor + windowOpacity, or the same dark fallback
        // when no note is tracked) — same convention columnOptionsBackground
        // below already follows, referencing bar's `background` id directly
        // rather than re-deriving the same expression a third time.
        Rectangle {
            id: tooltipBackground
            visible: tooltipWindow.contentVisible
            // Same input-region hook, same reasoning, as bar's own
            // background above (see ChromeWindow.park()) — doubly worth having
            // here, since a tooltip window toggles on every single hover
            // in/out, giving the racy park by far the most chances to lose.
            onVisibleChanged: windowInputMask.setAcceptsInput(tooltipWindow, tooltipBackground.visible)
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
        ChromeGlass {
            anchors.fill: tooltipBackground
            cornerRadius: tooltipBackground.radius
            noteWindow: bar.noteWindow
            contentShowing: tooltipBackground.visible
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
