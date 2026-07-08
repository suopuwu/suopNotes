import QtQuick
import QtQuick.Controls
import QtQuick.Templates
import QtQuick.Window
import Qt5Compat.GraphicalEffects

Window {
    id: noteWindow

    required property string noteId
    required property int index

    // Supplied by OpenNotesModel — the note's last-persisted geometry.
    // (0, 0) means "never positioned" (a freshly-opened note).
    required property int noteX
    required property int noteY
    required property int noteWidth
    required property int noteHeight
    required property string screenName
    // Pin state (req. 5) — Force-kept above other windows via the same
    // per-note KWin rule group applyPosition() writes below when true.
    required property bool alwaysOnTop

    // Exposed for tests only (structural verification of drag-gesture
    // config); not used by app code outside this file.
    readonly property alias mouseDragArea: mouseDragArea
    readonly property alias touchDragHandler: touchDragHandler
    readonly property alias editor: editor
    readonly property alias editorFlick: editorFlick
    readonly property alias noteHud: noteHud
    readonly property alias noteAppearance: noteAppearance
    readonly property alias docHandler: docHandler
    readonly property alias scrollIndicator: scrollIndicator
    readonly property alias scrollHandle: scrollHandle
    readonly property alias mainFrame: mainFrame

    // Qt.Tool hides the window from the taskbar/switcher on Windows/macOS.
    // On KWin/Wayland it does not (a known Qt/KWin gap) — closed by the
    // skiptaskbar/skippager/skipswitcher force baked into the same KWin
    // rule group applyPosition() writes below.
    flags: Qt.FramelessWindowHint | Qt.Tool
    color: "transparent"
    title: "suopNotes-" + noteId

    width: 260
    height: 260

    // Stays hidden until the KWin spawn-position rule (if applicable) is
    // actually in place — "Apply Initially" only takes effect if the rule
    // exists before the window's platform surface is first created/mapped,
    // so applyPosition() must run (synchronously) before visible flips on.
    visible: false

    Component.onCompleted: {
        const hasSavedPosition = (noteX !== 0 || noteY !== 0)
        // Deterministic cascade for a never-before-positioned note, so
        // simultaneously-opened fresh notes don't perfectly overlap.
        noteWindow.x = hasSavedPosition ? noteX : (100 + index * 40)
        noteWindow.y = hasSavedPosition ? noteY : (100 + index * 40)
        noteWindow.width = noteWidth
        noteWindow.height = noteHeight

        // Assigned imperatively, not via a `settings: settings` binding on
        // NoteAppearance itself — that bare RHS "settings" would resolve to
        // NoteAppearance's OWN property of the same name (QML resolves an
        // object's own properties before the root-context property of the
        // same name), silently binding it to itself instead of the real
        // Settings instance. Here, inside noteWindow's own scope (which has
        // no property named "settings"), the identifier is unambiguous.
        noteAppearance.settings = settings

        positionManager.applyPosition(noteId, title, noteWindow.x, noteWindow.y, alwaysOnTop)

        visible = true
        updateBlur()
    }

    // Skipped when this process is quitting because a newly-launched
    // instance is replacing it (req. 2) — that instance is concurrently
    // re-applying this exact note's rule from the same persisted session,
    // and removing it here would race that write, not a real note closure.
    Component.onDestruction: if (!positionManager.suppressCleanupOnQuit) positionManager.removePosition(noteId)

    // Pin toggle (req. 5) — re-applies the KWin rule with the new flag at
    // the note's current position (KWin Force above/false either way, so
    // this reliably takes effect without needing a window recreate). Also
    // wired to a QWindow-flag fallback for non-KWin platforms.
    //
    // Uses noteX/noteY (the model-backed, PositionEventService-confirmed
    // last-known position), NOT noteWindow.x/y — per the comment below,
    // the latter is never updated after a real compositor-driven drag on
    // Wayland, so it can be stale. The flags assignment a few lines down
    // forces the native window to be destroyed and recreated (to toggle
    // WindowStaysOnTopHint), and the new window is placed via this same
    // rule's "Apply Initially" position — writing a stale x/y here would
    // make pinning silently snap the note back to wherever it was when
    // first opened.
    function setPinned(pinned) {
        openNotesModel.setAlwaysOnTop(noteId, pinned)
        positionManager.applyPosition(noteId, title, noteX, noteY, pinned)

        const wasVisible = visible
        visible = false
        flags = pinned ? (flags | Qt.WindowStaysOnTopHint) : (flags & ~Qt.WindowStaysOnTopHint)
        visible = wasVisible
    }

    function persistGeometry() {
        openNotesModel.updateGeometry(noteId, x, y, width, height, screenName)
    }

    // Delete (new requirement): permanently removes the note's .md file and
    // all its bookkeeping. closeNote() is called LAST — it triggers this
    // window's own destruction (removed from openNotesModel's list, so the
    // Instantiator drops the delegate), so nothing after it may reference
    // `this`/noteId. Component.onDestruction still fires as usual and calls
    // positionManager.removePosition(noteId), cleaning up the KWin rule —
    // no separate call needed here.
    //
    // id/cfg/model are captured into plain local variables BEFORE
    // noteStore.deleteNote() runs: that call removes the .md file and
    // synchronously emits NoteStore.noteIdsChanged, which Main.qml's
    // reconcileNotesWithDisk() (a real app instance only — not present in
    // the QML test harness) reacts to *synchronously*, on the same call
    // stack, by itself calling settings.clearNoteOverride()/
    // openNotesModel.closeNote() for this exact id — which destroys this
    // NoteWindow right then, before control ever returns here. Referencing
    // the bare `settings`/`openNotesModel` identifiers afterward would
    // re-resolve them through this (by-then-destroyed) object's QML
    // context and throw "settings is not defined" — confirmed live. Locals
    // captured beforehand are plain JS references to those (still very
    // much alive) objects, so calling through them afterward needs no
    // context lookup at all. The calls below are then harmless no-ops if
    // that race already ran them once (clearNoteOverride/closeNote are
    // both idempotent).
    function deleteNote() {
        const id = noteId
        const cfg = settings
        const model = openNotesModel

        noteStore.deleteNote(id)
        cfg.clearNoteOverride(id)
        model.closeNote(id)
    }

    // Position, unlike size, can't be read client-side on Wayland at all —
    // only the compositor knows it, and startSystemMove() hands the whole
    // gesture off to the compositor with no client-visible end event.
    // Confirmed live: mouseDragArea's onReleased/onCanceled do NOT fire
    // after a real compositor-driven move (dragged a note, watched the app's
    // own log — neither ever printed, even though the window demonstrably
    // moved). So position-saving isn't wired here at all — PositionEventService
    // (src/core/PositionEventService.h) owns it entirely, via a genuine
    // completion event KWin's own scripting API exposes on the window
    // object (interactiveMoveResizeFinished) that a persistent KWin script
    // calls back into the app over D-Bus for, straight into Settings. No
    // timer, no polling, no guessing when a drag has "probably" ended.

    // mainFrame fills the whole window, so the blur region is just the
    // window's own rect — no need to map item bounds into scene coordinates
    // (only necessary when the blurred item doesn't fill its window). Each
    // note window is blur's sole consumer, so there's no risk of the
    // last-writer-wins conflict KWindowEffects has across shared windows.
    function updateBlur() {
        blurEffect.applyTo(noteWindow, noteAppearance.blurEnabled, noteAppearance.windowRadius)
    }

    onWidthChanged: { updateBlur(); persistGeometry() }
    onHeightChanged: { updateBlur(); persistGeometry() }

    // Resolves this note's effective appearance: its own override where set
    // (req. 4), else the tray Settings dialog's global default — see
    // NoteAppearance.h. Re-resolves automatically when either changes.
    NoteAppearance {
        id: noteAppearance
        noteId: noteWindow.noteId

        onBlurEnabledChanged: noteWindow.updateBlur()
        onWindowRadiusChanged: noteWindow.updateBlur()
    }

    // Bound via noteWindow.noteAppearance (the alias above), not a bare
    // `noteAppearance: noteAppearance` — PerNoteSettingsPopup's own property
    // is ALSO named noteAppearance, so an unqualified RHS would hit the
    // exact same self-shadowing trap as the settings assignment above.
    PerNoteSettingsPopup {
        id: perNoteSettingsPopup
        noteAppearance: noteWindow.noteAppearance
    }

    Rectangle {
        id: mainFrame
        anchors.fill: parent
        radius: noteAppearance.windowRadius
        color: noteAppearance.backgroundColor
        opacity: noteAppearance.windowOpacity
        border.width: noteAppearance.borderWidth
        border.color: noteAppearance.borderColor

        layer.enabled: true
        layer.effect: OpacityMask {
            maskSource: Rectangle {
                width: mainFrame.width
                height: mainFrame.height
                radius: mainFrame.radius
            }
        }

        HoverHandler {
            id: noteHoverHandler
        }

        Row {
            id: toolbar
            // Above mouseDragArea (z: 1, declared below): without this the
            // drag area — not any button — would be the topmost item for
            // cursor-shape resolution across the whole toolbar row, hiding
            // the pointing-hand cursor set below and leaving the default
            // arrow instead (clicks already reach the buttons regardless,
            // since mouseDragArea only claims the right mouse button — see
            // mouseDragArea's own comment — but cursor resolution has no
            // equivalent per-button fallthrough).
            z: 2
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 4
            spacing: 2

            HoverHandler { cursorShape: Qt.PointingHandCursor }

            // Formatting is only ever reachable via these buttons (or their
            // matching keyboard shortcuts) — never by typing raw markdown
            // syntax, since Qt's MarkdownText mode doesn't live-convert
            // typed "**"/"#" (Spike A). That is what makes this WYSIWYG.
            ToolButton {
                text: "B"
                font.bold: true
                onClicked: docHandler.toggleBold(editor.selectionStart, editor.selectionEnd)
            }
            ToolButton {
                text: "I"
                font.italic: true
                onClicked: docHandler.toggleItalic(editor.selectionStart, editor.selectionEnd)
            }
            ToolButton {
                text: "S"
                font.strikeout: true
                onClicked: docHandler.toggleStrikethrough(editor.selectionStart, editor.selectionEnd)
            }
            ToolButton {
                text: "H1"
                onClicked: docHandler.setHeadingLevel(editor.selectionStart, editor.selectionEnd, 1)
            }
            ToolButton {
                text: "H2"
                onClicked: docHandler.setHeadingLevel(editor.selectionStart, editor.selectionEnd, 2)
            }
            ToolButton {
                text: "¶" // pilcrow — back to a normal paragraph
                onClicked: docHandler.setHeadingLevel(editor.selectionStart, editor.selectionEnd, 0)
            }
            ToolButton {
                text: "•" // bullet
                onClicked: docHandler.toggleBulletList(editor.selectionStart, editor.selectionEnd)
            }
        }

        Flickable {
            id: editorFlick
            anchors.top: toolbar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 8
            clip: true
            contentWidth: width
            contentHeight: editor.height

            TextArea {
                id: editor

                property bool ready: false

                width: editorFlick.width
                // Qt bug workaround: TextArea's own implicitHeight (the
                // default this would fall back to) does not reactively
                // recompute after docHandler.setMarkdown() mutates the
                // underlying QTextDocument directly — contentHeight tracks
                // the real document size correctly (verified: its change
                // notification fires fine), implicitHeight just never
                // re-reads it outside of edits routed through TextArea's own
                // text-editing API. Binding on contentHeight directly
                // sidesteps that, and the editorFlick.height floor is what
                // makes the editable box visually fill the whole note (not
                // just as tall as its content) instead of leaving dead space
                // below short notes.
                height: Math.max(contentHeight, editorFlick.height)
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.MarkdownText
                selectByMouse: true
                color: noteAppearance.fontColor
                font.pixelSize: noteAppearance.fontSize
                font.family: noteAppearance.fontFamily.length > 0 ? noteAppearance.fontFamily : Qt.application.font.family
                background: Item {}

                // QtQuick Controls' built-in right-click cut/copy/paste menu
                // (T.ContextMenu, Qt 6.9+) would fight the drag area below
                // for the same right-button press — right-click is reserved
                // exclusively for dragging (req. 5), so disable it. Moot in
                // practice now that the drag area sits on top and claims the
                // right button before it would ever reach the editor, but
                // still correct to disable since this control never needs it.
                ContextMenu.menu: null

                DocumentHandler {
                    id: docHandler
                    document: editor.textDocument
                }

                Component.onCompleted: {
                    docHandler.setMarkdown(noteStore.loadNoteContent(noteWindow.noteId))
                    ready = true
                }

                // Debounced + atomic on NoteStore's side (~300ms after the
                // last keystroke) — instant-feeling without a disk write per
                // keystroke.
                onTextChanged: {
                    if (ready)
                        noteStore.saveNoteContent(noteWindow.noteId, docHandler.toMarkdown())
                }
            }

            // Clicking below the real text (in the fill space that makes
            // editor cover the whole note — see the height binding above)
            // should land the cursor on the last line, at whichever
            // character is nearest the click horizontally — not
            // unconditionally at the very end of the document. Qt's own
            // hit-testing for a y past the bottom of the laid-out text just
            // returns the document's last position outright, ignoring x, so
            // a bare click on editor down there always jumps to the true
            // end regardless of where you clicked horizontally. This
            // MouseArea sits after `editor` (topmost sibling wins, same
            // convention as mouseDragArea below) but only over the strip
            // below editor's real content — reprojecting the click onto the
            // bottom edge of that content before hit-testing gets a proper
            // nearest-x lookup on the last line instead. Zero height (so no
            // hit at all) once real content already fills or exceeds the
            // viewport, since there's no fill space left to click into.
            MouseArea {
                x: 0
                y: editor.contentHeight
                width: editorFlick.width
                height: Math.max(0, editor.height - editor.contentHeight)
                onClicked: (mouse) => {
                    editor.forceActiveFocus()
                    editor.cursorPosition = editor.positionAt(mouse.x, Math.max(0, editor.contentHeight - 1))
                }
            }
        }

        // Subtle scroll indicator (feature request): shown only when the
        // note's content overflows the visible area, tracking position via
        // editorFlick's own visibleArea (Flickable already computes the
        // normalized position/extent — no manual contentY/contentHeight
        // math needed). Deliberately a plain non-interactive Rectangle, not
        // QtQuick.Controls' ScrollBar — everything else in this file
        // (toolbar, HUD, resize handles) is hand-rolled to control its
        // exact look, and a QQC2 control would pull in a platform style
        // whose default chrome wouldn't match or take the glass treatment
        // below consistently.
        Item {
            id: scrollIndicator
            // Above mouseDragArea (z: 1, declared below) for the same two
            // reasons the toolbar itself needs z: 2 above it (see toolbar's
            // own comment): so this MouseArea's PointingHandCursor wins
            // cursor resolution instead of mouseDragArea's IBeam/Arrow, and
            // so a drag press actually starts here rather than falling
            // through — matches the toolbar's tier, not resizeBorders' (2),
            // to fully own the region without competing with either.
            z: 2
            anchors.top: editorFlick.top
            anchors.bottom: editorFlick.bottom
            anchors.right: mainFrame.right
            // Right-aligned to sit just inside resizeBorders' own right-edge
            // strip (6px, flush with mainFrame.right — see resizeBorders
            // below) rather than overlapping it, regardless of how wide
            // noteAppearance.scrollbarWidth is configured.
            anchors.rightMargin: 6
            width: noteAppearance.scrollbarWidth
            visible: editorFlick.visibleArea.heightRatio < 1.0

            // Normalized 0..1 position within the *scrollable* range (i.e.
            // dividing out heightRatio), not raw visibleArea.yPosition —
            // needed so the handle's own min-height clamp below doesn't
            // throw off where it sits at the very top/bottom of the track.
            readonly property real _scrollFraction: editorFlick.visibleArea.heightRatio < 1.0
                ? editorFlick.visibleArea.yPosition / (1.0 - editorFlick.visibleArea.heightRatio)
                : 0
            readonly property real _trackRange: Math.max(0, height - scrollHandle.height)

            Rectangle {
                id: scrollHandle
                width: parent.width
                // Same curvature as the ribbon (feature request): both bind
                // straight to mainFrame.radius rather than deriving their
                // own (e.g. width / 2), so the scrollbar reads as the same
                // "family" of chrome as the HUD rather than an unrelated
                // pill shape. Rectangle.radius auto-clamps to half of
                // min(width, height), so a large window radius still just
                // rounds this thin bar toward a pill instead of overflowing.
                radius: mainFrame.radius
                // Same base "material" as the ribbon when glass mode is on
                // (feature request): NoteHud's own chip is always this exact
                // flat, mostly-transparent dark fill — GlassOverlay's sheen
                // rings/rim (below) only ever paint a band near the edge
                // (each ring is a transparent-fill Rectangle with only a
                // border stroke — see GlassOverlay.qml), so the *center* of
                // the shape is whatever's underneath showing through. With
                // the previous fontColor-tinted fill here (up to 55% opaque
                // white/etc.), that center read as a solid-ish bar instead
                // of "glass" — swapping to the ribbon's own low-alpha black
                // lets the same sheen/rim decoration read the same way on
                // both. Off glass mode keeps the original font-color-tinted
                // look (adapts to whatever text color the note uses).
                color: noteAppearance.glassEnabled ? Qt.rgba(0, 0, 0, 0.28) : noteAppearance.fontColor
                opacity: noteAppearance.glassEnabled
                    ? 1.0
                    : (scrollDrag.pressed || scrollHoverHandler.hovered ? 0.55 : 0.35)
                Behavior on opacity { NumberAnimation { duration: 120 } }
                height: Math.max(20, scrollIndicator.height * editorFlick.visibleArea.heightRatio)

                // Declaratively tracks the editor's live scroll position —
                // suspended (see `when`) while the user is actively
                // dragging, so scrollDrag's drag.target can imperatively
                // own `y` instead. A declarative binding and an imperative
                // drag write can't coexist permanently on the same
                // property: the drag's first write would otherwise sever
                // this binding for good, freezing the handle in place on
                // every later wheel/keyboard scroll instead of just this
                // one drag gesture.
                Binding on y {
                    value: scrollIndicator._trackRange * scrollIndicator._scrollFraction
                    when: !scrollDrag.drag.active
                }

                // The other half of drag-to-scroll: while dragging, feed
                // the handle's own (drag-driven) position back into the
                // editor as the user moves it, rather than only updating
                // once on release.
                onYChanged: {
                    if (!scrollDrag.drag.active)
                        return
                    const scrollRange = editorFlick.contentHeight - editorFlick.height
                    if (scrollIndicator._trackRange > 0 && scrollRange > 0)
                        editorFlick.contentY = Math.max(0, Math.min(scrollRange,
                            (y / scrollIndicator._trackRange) * scrollRange))
                }

                HoverHandler { id: scrollHoverHandler }
            }

            // Drag-to-scroll (feature request): hit area padded a few px
            // wider than the visible bar (see anchors.margins) so a thin,
            // subtle bar is still easy to grab without having to widen the
            // bar itself.
            MouseArea {
                id: scrollDrag
                anchors.fill: scrollHandle
                anchors.leftMargin: -6
                anchors.rightMargin: -6
                cursorShape: Qt.PointingHandCursor
                drag.target: scrollHandle
                drag.axis: Drag.YAxis
                drag.minimumY: 0
                drag.maximumY: scrollIndicator._trackRange
            }

            // Glass-textured when the note itself has glass mode on, using
            // the same appearance properties mainFrame's and NoteHud's own
            // GlassOverlay instances use, so the scrollbar reads as the
            // same material rather than a plain flat bar.
            GlassOverlay {
                anchors.fill: scrollHandle
                cornerRadius: scrollHandle.radius
                visible: noteAppearance.glassEnabled
                tintColor: noteAppearance.glassTintColor
                sheenOpacity: noteAppearance.glassSheenOpacity
                ringCount: noteAppearance.glassSheenStrength
                sheenSize: noteAppearance.glassSheenSize
            }
        }

        // Right-click-drag (mouse) and tap-drag (touch) move the window via
        // the compositor. This is the only mechanism that works on Wayland —
        // clients can't set their own position directly (see project notes).
        // Left click is reserved for editing, so the mouse gesture is scoped
        // to the right button only.
        //
        // Mouse case uses a MouseArea, not a DragHandler, and sits on top of
        // the editor (z above it) rather than as an ancestor: TextArea's
        // underlying TextInput grabs the mouse exclusively on press for any
        // button, and a DragHandler attached to an ancestor only ever gets a
        // passive grab in that case — a passive grab can't be promoted over
        // a descendant's exclusive one, so it never activates. A MouseArea
        // sidesteps this because it participates in Qt Quick's legacy
        // top-most-hit-test: since acceptedButtons excludes Left, a
        // left-button press isn't a hit here at all and keeps searching
        // underneath (reaching the toolbar/editor normally), while a
        // right-button press *is* a hit here — intercepted before it ever
        // reaches TextInput underneath, so no competing exclusive grab
        // exists to fight over. (A bare Item with only a PointerHandler
        // doesn't get this fallthrough: unmatched events don't drop to
        // siblings behind it, only bubble to its own ancestors.)
        // Sitting on top (z above the editor) also makes mouseDragArea the
        // one QQuickItem the platform cursor is resolved against here — Qt
        // Quick's cursor lookup walks up from the topmost hit item's own
        // ancestor chain, not sideways into covered siblings, so TextArea's
        // automatic IBeam cursor underneath never becomes visible on its
        // own. Reflecting it explicitly (whenever the pointer is over
        // editorFlick — its real text or the dead space below the last
        // line, both) is what makes the cursor look right again; over the
        // toolbar above it, the default arrow stands.
        MouseArea {
            id: mouseDragArea
            anchors.fill: parent
            z: 1
            acceptedButtons: Qt.RightButton
            hoverEnabled: true
            cursorShape: mouseY >= editorFlick.y ? Qt.IBeamCursor : Qt.ArrowCursor
            onPressed: noteWindow.startSystemMove()
        }

        DragHandler {
            id: touchDragHandler
            acceptedDevices: PointerDevice.TouchScreen
            target: null
            onActiveChanged: if (active) noteWindow.startSystemMove()
        }

        // Optional rendered sheen (req. 6, ported from suopPanel) — a
        // separate mechanism from blurEnabled's real compositor blur-behind
        // above. Sits on top of the content (declared last) but has no
        // input handling of its own, so it never blocks clicks reaching the
        // toolbar/editor/HUD/resize borders underneath.
        GlassOverlay {
            anchors.fill: parent
            cornerRadius: mainFrame.radius
            visible: noteAppearance.glassEnabled
            tintColor: noteAppearance.glassTintColor
            sheenOpacity: noteAppearance.glassSheenOpacity
            ringCount: noteAppearance.glassSheenStrength
            sheenSize: noteAppearance.glassSheenSize
        }

        // Hover-revealed HUD (req. 4/5): pin (always-on-top) + gear (opens
        // this note's own override settings). Hidden until the note itself
        // is hovered, exactly like the reference ribbon this was modeled on
        // (suopPanel's TileGridHud) hides its full icon row until hovered —
        // here the whole HUD, not just its row, gates on hover, since a
        // permanently-visible corner icon would clutter every idle note.
        //
        // z above both mouseDragArea (1) and resizeBorders (2): the HUD sits
        // in the top-right corner, the same footprint resizeBorders' own
        // top-right corner handle claims, and without a higher z here that
        // handle — plus mouseDragArea underneath it — would win hit-testing
        // for that whole area, breaking the HUD's own hover-to-expand and
        // showing a resize/move cursor over its glyphs instead of the
        // pointing-hand cursor they set for themselves.
        NoteHud {
            id: noteHud
            z: 3
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 4
            visible: noteHoverHandler.hovered
            pinned: noteWindow.alwaysOnTop
            alwaysExpanded: settings.hudAlwaysExpanded
            cornerRadius: mainFrame.radius
            glassEnabled: noteAppearance.glassEnabled
            glassTintColor: noteAppearance.glassTintColor
            glassSheenOpacity: noteAppearance.glassSheenOpacity
            glassSheenStrength: noteAppearance.glassSheenStrength
            glassSheenSize: noteAppearance.glassSheenSize
            onPinToggleRequested: noteWindow.setPinned(!noteWindow.alwaysOnTop)
            onSettingsRequested: perNoteSettingsPopup.show()
            onCloseRequested: noteWindow.deleteNote()
        }

        // Resize borders (req. 8): every edge and corner is grabbable, not
        // just a single bottom-right grip. Four thin edge strips + four
        // corner squares (ResizeHandle.qml), each claiming only its own
        // footprint with the default left button, so none of them compete
        // with mouseDragArea's whole-window right-button claim above. The
        // corner squares sit at the strips' ends and the strips are inset
        // by that same corner size, so the two never overlap — no z-order
        // tie-break needed between them.
        Item {
            id: resizeBorders
            anchors.fill: parent
            z: 2
            visible: noteHoverHandler.hovered

            readonly property int edgeThickness: 6
            readonly property int cornerSize: 12

            ResizeHandle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: resizeBorders.cornerSize
                anchors.rightMargin: resizeBorders.cornerSize
                height: resizeBorders.edgeThickness
                cursorShape: Qt.SizeVerCursor
                edges: Qt.TopEdge
                resizeTarget: noteWindow
            }
            ResizeHandle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: resizeBorders.cornerSize
                anchors.rightMargin: resizeBorders.cornerSize
                height: resizeBorders.edgeThickness
                cursorShape: Qt.SizeVerCursor
                edges: Qt.BottomEdge
                resizeTarget: noteWindow
            }
            ResizeHandle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.topMargin: resizeBorders.cornerSize
                anchors.bottomMargin: resizeBorders.cornerSize
                width: resizeBorders.edgeThickness
                cursorShape: Qt.SizeHorCursor
                edges: Qt.LeftEdge
                resizeTarget: noteWindow
            }
            ResizeHandle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.topMargin: resizeBorders.cornerSize
                anchors.bottomMargin: resizeBorders.cornerSize
                width: resizeBorders.edgeThickness
                cursorShape: Qt.SizeHorCursor
                edges: Qt.RightEdge
                resizeTarget: noteWindow
            }
            ResizeHandle {
                anchors.top: parent.top
                anchors.left: parent.left
                width: resizeBorders.cornerSize
                height: resizeBorders.cornerSize
                cursorShape: Qt.SizeFDiagCursor
                edges: Qt.TopEdge | Qt.LeftEdge
                resizeTarget: noteWindow
            }
            ResizeHandle {
                anchors.top: parent.top
                anchors.right: parent.right
                width: resizeBorders.cornerSize
                height: resizeBorders.cornerSize
                cursorShape: Qt.SizeBDiagCursor
                edges: Qt.TopEdge | Qt.RightEdge
                resizeTarget: noteWindow
            }
            ResizeHandle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                width: resizeBorders.cornerSize
                height: resizeBorders.cornerSize
                cursorShape: Qt.SizeBDiagCursor
                edges: Qt.BottomEdge | Qt.LeftEdge
                resizeTarget: noteWindow
            }
            ResizeHandle {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: resizeBorders.cornerSize
                height: resizeBorders.cornerSize
                cursorShape: Qt.SizeFDiagCursor
                edges: Qt.BottomEdge | Qt.RightEdge
                resizeTarget: noteWindow
            }
        }
    }
}
