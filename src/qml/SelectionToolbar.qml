import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
        active: parent.hovered || parent.down
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

    // Scope the toolbar acts on — the currently-focused NoteWindow, or null
    // when no note has focus. Sourced from Main.qml, the one place that
    // observes every NoteWindow's own activeChanged.
    property var noteWindow: null

    // Exposed for tests only (structural/behavioral verification), not used
    // by app code outside this file — same convention NoteWindow.qml's own
    // mouseDragArea/editor aliases follow.
    readonly property alias dragArea: dragArea
    readonly property alias boldButton: boldButton
    readonly property alias background: background
    readonly property alias content: content
    readonly property alias gripDotsGrid: gripDotsGrid

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
    // gripHandle (9) + divider (1) + 9 buttons * buttonSize (288) + 10
    // gaps * spacing (20), plus a little breathing room so nothing sits
    // flush against background's rounded corners.
    readonly property int barLength: 330

    // The grip + divider always stay in the toolbar's first line/column
    // regardless of button count — only the buttons themselves wrap once
    // there are more than 10 (feature request: "wrap at ten icons").
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

        const noteX = note.noteX
        const noteY = note.noteY
        const noteW = note.noteWidth
        const noteH = note.noteHeight

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
        // wraps once there are more than 10 *buttons* (feature request),
        // while the grip+divider — always the first two children — stay
        // on the toolbar's first line/column regardless (see
        // nonButtonChildCount's own comment). -1 tells GridLayout to
        // compute that dimension automatically from the other one.
        flow: bar.isVertical ? GridLayout.TopToBottom : GridLayout.LeftToRight
        columns: bar.isVertical ? -1 : (10 + bar.nonButtonChildCount)
        rows: bar.isVertical ? (10 + bar.nonButtonChildCount) : -1
        rowSpacing: 2
        columnSpacing: 2
        // Disables every button at once whenever there's no tracked note,
        // rather than guarding each onClicked individually. Starts false
        // (matching noteWindow's own null default) and is set imperatively
        // from onNoteWindowChanged above from then on — not a declarative
        // `enabled: bar.noteWindow !== null` binding; see that handler's
        // own comment for why a plain binding isn't trusted in this file.
        enabled: false

        // Drag handle (feature request): an explicit 2x4 (row mode) / 4x2
        // (column mode) grid of dots, not a single Unicode glyph — a
        // rotated braille character ("⣿") rendered as a near-illegible
        // sliver in column mode (font hinting/anti-aliasing doesn't
        // preserve its dot pattern under arbitrary rotation).
        //
        // Regression found live: the original implementation used a `Grid`
        // positioner (columns bound to 2/4, letting it auto-compute rows)
        // to lay out the 8 dots. That logic checked out perfectly in the
        // offscreen Quick Test harness (verified via a diagnostic dump of
        // every dot's real x/y — 2 clean rows of 4, correctly spaced), but
        // only ever rendered ONE row's worth of dots in the real windowed
        // app — confirmed live via screenshot, same "works offscreen, not
        // in the real compositor" split as the button-label bug just above
        // this file's ToolbarButtonLabel comment. Root cause not
        // identified (suspected Grid-positioner-specific), so rather than
        // keep chasing an environment-specific Grid quirk, this computes
        // each dot's x/y explicitly from its own Repeater index — no
        // positioner involved at all, nothing left to behave differently
        // between environments.
        Item {
            id: gripHandle
            implicitWidth: bar.isVertical ? 21 : 9
            implicitHeight: bar.isVertical ? 9 : 21
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter

            Item {
                id: gripDotsGrid
                anchors.centerIn: parent
                width: bar.isVertical ? 21 : 9
                height: bar.isVertical ? 9 : 21
                readonly property int columns: bar.isVertical ? 4 : 2

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
            // rest of the bar/NoteWindow's own notes use). dragArea below
            // still covers right-click-drag anywhere on the bar including
            // here, for consistency with that convention; this is
            // additive, not a replacement.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.OpenHandCursor
                onPressed: bar.startSystemMove()
            }
        }

        // Rotates to match orientation: a vertical rule under a
        // horizontal (row) layout, a horizontal rule under a vertical
        // (column) one.
        Rectangle {
            width: bar.isVertical ? 18 : 1
            height: bar.isVertical ? 1 : 18
            color: Qt.rgba(1, 1, 1, 0.15)
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
        }

        ToolButton {
            id: boldButton
            text: "B"
            font.bold: true
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            contentItem: ToolbarButtonLabel {}
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.toggleBold(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd))
        }
        ToolButton {
            text: "I"
            font.italic: true
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            contentItem: ToolbarButtonLabel {}
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.toggleItalic(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd))
        }
        ToolButton {
            text: "S"
            font.strikeout: true
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            contentItem: ToolbarButtonLabel {}
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.toggleStrikethrough(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd))
        }
        ToolButton {
            text: "H1"
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            contentItem: ToolbarButtonLabel {}
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.setHeadingLevel(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd, 1))
        }
        ToolButton {
            text: "H2"
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            contentItem: ToolbarButtonLabel {}
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.setHeadingLevel(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd, 2))
        }
        ToolButton {
            text: "¶"
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            contentItem: ToolbarButtonLabel {}
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.setHeadingLevel(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd, 0))
        }
        ToolButton {
            text: "•"
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            contentItem: ToolbarButtonLabel {}
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.toggleBulletList(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd))
        }
        ToolButton {
            text: "1."
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            contentItem: ToolbarButtonLabel {}
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.toggleNumberedList(bar.noteWindow.editor.selectionStart, bar.noteWindow.editor.selectionEnd))
        }
        ToolButton {
            text: "▦"
            implicitWidth: bar.buttonSize
            implicitHeight: bar.buttonSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            background: ToolbarButtonHighlight {}
            contentItem: ToolbarButtonLabel {}
            onClicked: bar.runAction(() => bar.noteWindow.docHandler.insertTable(bar.noteWindow.editor.selectionEnd))
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
