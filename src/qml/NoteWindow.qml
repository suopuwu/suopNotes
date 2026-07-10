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

    // True only while Component.onCompleted is restoring this window's
    // already-persisted width/height — see there for why persistSize()
    // must not fire on those two specific assignments.
    property bool restoringGeometry: false

    // Whether the pointer is currently anywhere over this note — a plain
    // mouse-presence signal, independent of OS-level window activation.
    // Main.qml's toolbar-tracking logic uses this as a second, more direct
    // "the user is still right here" signal alongside `active`, so a brief
    // compositor-level activation blip doesn't hide the toolbar out from
    // under someone who's still visibly working in/around the note.
    readonly property alias hovered: noteHoverHandler.hovered

    // Live "is this format active at the current cursor/selection" state
    // (feature request: the toolbar should show, not just apply, formatting
    // — e.g. the Bold button should look pressed when the selection is
    // already bold). Plain properties refreshed imperatively by
    // refreshFormatState() below, not declarative bindings on
    // docHandler.isBold()/etc — those calls only re-run when a QML property
    // they *read* changes, and nothing here reads editor.selectionStart/End
    // on every keystroke or toolbar click, so a plain binding would go stale
    // exactly when a selection is held and a toolbar button is clicked
    // (selectionStart/End don't move, only the format underneath them does).
    property bool currentBold: false
    property bool currentItalic: false
    property bool currentStrikethrough: false
    property int currentHeadingLevel: 0
    property bool currentBulletList: false
    property bool currentNumberedList: false
    property bool currentCheckboxList: false
    // Drives whether the toolbar's row/column-editing popup can show at all
    // (feature request) — those buttons are only ever meaningful with the
    // cursor actually inside a table.
    property bool currentInTable: false

    // Re-polls every currentXxx property above from the editor's real
    // selection and the document's own format at that selection. Called
    // whenever either could have changed: the cursor/selection moves (see
    // editor's onSelectionStartChanged/onSelectionEndChanged), or a
    // formatting call mutates the document without moving the selection at
    // all (see docHandler's onFormattingChanged — e.g. clicking Bold on a
    // held selection).
    function refreshFormatState() {
        const start = editor.selectionStart
        const end = editor.selectionEnd
        currentBold = docHandler.isBold(start, end)
        currentItalic = docHandler.isItalic(start, end)
        currentStrikethrough = docHandler.isStrikethrough(start, end)
        currentHeadingLevel = docHandler.headingLevel(start, end)
        currentBulletList = docHandler.isBulletList(start, end)
        currentNumberedList = docHandler.isNumberedList(start, end)
        currentCheckboxList = docHandler.isCheckboxList(start, end)
        currentInTable = docHandler.isInTable(end)
    }

    // Exposed for tests only (structural verification of drag-gesture
    // config); not used by app code outside this file.
    readonly property alias mouseDragArea: mouseDragArea
    readonly property alias editorContextMenu: editorContextMenu
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
        // restoringGeometry (cleared below) suppresses persistSize() for
        // these two assignments — this is what was causing a real,
        // reported bug: assigning width here fires onWidthChanged
        // synchronously, which used to call persistSize() with the CORRECT
        // width but
        // height still at its 260 default (the next line hasn't run yet),
        // clobbering the real saved height in Settings. That write
        // round-trips synchronously back through OpenNotesModel's
        // dataChanged into this exact delegate's live-bound noteHeight
        // property, corrupting it to 260 *before* the next line ever reads
        // it — so `noteWindow.height = noteHeight` then reapplies the
        // already-corrupted value instead of the real one, and since
        // window.height was already 260, no onHeightChanged fires to
        // self-correct. Net effect: height silently reverted to 260 on
        // every single restart, while width (assigned first, before any
        // corruption occurs) always restored correctly — restoring
        // already-persisted values has nothing to persist anyway.
        restoringGeometry = true
        noteWindow.width = noteWidth
        noteWindow.height = noteHeight
        restoringGeometry = false

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

    // Deliberately does not pass this window's own x/y — see
    // OpenNotesModel::updateSize's doc comment for why that used to
    // silently revert a note's saved position on every resize after a drag.
    function persistSize() {
        openNotesModel.updateSize(noteId, width, height)
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

    onWidthChanged: { updateBlur(); if (!restoringGeometry) persistSize() }
    onHeightChanged: { updateBlur(); if (!restoringGeometry) persistSize() }

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
        // windowOpacity is folded into the fill's own alpha (rather than
        // Rectangle.opacity) so it only thins the background: Item.opacity
        // is a group opacity that also multiplies border.color and every
        // child (glass overlay, text), which isn't what "background
        // opacity" is supposed to mean.
        color: Qt.rgba(noteAppearance.backgroundColor.r, noteAppearance.backgroundColor.g,
                        noteAppearance.backgroundColor.b,
                        noteAppearance.backgroundColor.a * noteAppearance.windowOpacity)
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

        // Configurable formatting shortcuts (feature request): sequences
        // come from Settings (rebindable via the tray Settings dialog's
        // Keyboard Shortcuts section — see SettingsDialog.qml/
        // ShortcutField.qml) rather than being hardcoded here, so a
        // conflicting binding or non-QWERTY layout doesn't require editing
        // the app itself. Qt's default Shortcut context (Qt.WindowShortcut)
        // already scopes each of these to this specific NoteWindow, so
        // multiple open notes never fight over the same key press.
        Shortcut {
            sequence: settings.shortcutBold
            onActivated: docHandler.toggleBold(editor.selectionStart, editor.selectionEnd)
        }
        Shortcut {
            sequence: settings.shortcutItalic
            onActivated: docHandler.toggleItalic(editor.selectionStart, editor.selectionEnd)
        }
        Shortcut {
            sequence: settings.shortcutStrikethrough
            onActivated: docHandler.toggleStrikethrough(editor.selectionStart, editor.selectionEnd)
        }
        Shortcut {
            sequence: settings.shortcutHeading1
            onActivated: docHandler.setHeadingLevel(editor.selectionStart, editor.selectionEnd, 1)
        }
        Shortcut {
            sequence: settings.shortcutHeading2
            onActivated: docHandler.setHeadingLevel(editor.selectionStart, editor.selectionEnd, 2)
        }
        Shortcut {
            sequence: settings.shortcutParagraph
            onActivated: docHandler.setHeadingLevel(editor.selectionStart, editor.selectionEnd, 0)
        }
        Shortcut {
            sequence: settings.shortcutBulletList
            onActivated: docHandler.toggleBulletList(editor.selectionStart, editor.selectionEnd)
        }
        Shortcut {
            sequence: settings.shortcutNumberedList
            onActivated: docHandler.toggleNumberedList(editor.selectionStart, editor.selectionEnd)
        }
        Shortcut {
            sequence: settings.shortcutInsertTable
            onActivated: docHandler.insertTable(editor.selectionEnd)
        }

        Flickable {
            id: editorFlick
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 8
            // Text fills the whole note, right up under noteHud's
            // top-right footprint (feature request) — deliberately not
            // reserving a top-margin gap for it (that just wasted screen
            // space while the note isn't hovered). noteHud is z: 3, above
            // both editorFlick and scrollIndicator (anchored to its top),
            // so while the note IS hovered and the HUD is showing, its
            // pin/gear/close icons take priority over clicks in that corner
            // — including the scrollbar handle, if it happens to be
            // scrolled near the top. Accepted tradeoff: an occasionally
            // obscured scrollbar corner beats permanent dead space.
            clip: true
            contentWidth: width
            contentHeight: editor.height

            // Keeps the caret on-screen when it moves off the visible edge —
            // whether from typing past the bottom or from arrow-key/Home/End
            // navigation. editor sits at content-coordinate (0,0) inside
            // editorFlick (no offsetting ancestor between them), so
            // editor.cursorRectangle can be compared directly against
            // contentY/height without any coordinate mapping.
            function ensureCursorVisible(rect) {
                if (rect.y < contentY)
                    contentY = rect.y
                else if (rect.y + rect.height > contentY + height)
                    contentY = rect.y + rect.height - height
            }

            // Sole purpose: gives EditorDragFilter below a place in the tree
            // where it's `editor`'s direct parent and nothing else's — see
            // EditorDragFilter's own class comment for why that's required
            // (childMouseEventFilter only ever sees events headed to this
            // wrapper's own children, so it can't affect the below-content
            // MouseArea, a sibling of this wrapper rather than a descendant
            // of it).
            EditorDragFilter {
                id: editorDragFilter
                width: editorFlick.width
                height: editor.height

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
                    // Regression found live (popup toolbar feature): with
                    // this left at its default (false), the selection was
                    // cleared the instant this TextArea lost active focus
                    // — which now happens on every legitimate click of the
                    // toolbar's own buttons, since that window accepts OS
                    // focus (see SelectionToolbar.qml's flags comment).
                    // Each button reads editor.selectionStart/selectionEnd
                    // to know what to format; by the time the click was
                    // actually handled, the selection had already collapsed
                    // to nothing, so nothing ever got formatted. true keeps
                    // the selection intact (and visible) across a focus
                    // change to another window, exactly this app's own use
                    // case.
                    persistentSelection: true
                    color: noteAppearance.fontColor
                    // highlightColor is picked without an alpha slider (its
                    // own ColorDialog has none, matching backgroundColor's);
                    // highlightOpacity is the independently adjustable knob
                    // that actually controls translucency, same split as
                    // backgroundColor/windowOpacity.
                    selectionColor: Qt.rgba(noteAppearance.highlightColor.r, noteAppearance.highlightColor.g,
                                             noteAppearance.highlightColor.b, noteAppearance.highlightOpacity)
                    selectedTextColor: noteAppearance.fontColor
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

                    // Click-to-toggle checkboxes (feature request): a
                    // non-exclusive tap handler sitting alongside editor's
                    // own click-to-place-cursor handling (TapHandler's
                    // default passive grab doesn't compete with it, so both
                    // fire independently — the cursor still lands wherever
                    // was clicked either way). tryToggleCheckboxAt itself
                    // only actually toggles when the position hit-tests
                    // against the item's own checkbox glyph, not its text.
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: (eventPoint) => {
                            const position = editor.positionAt(eventPoint.position.x, eventPoint.position.y)
                            docHandler.tryToggleCheckboxAt(position)
                        }
                    }

                    DocumentHandler {
                        id: docHandler
                        document: editor.textDocument
                        // Catches formatting changes that don't move the
                        // selection at all — e.g. clicking Bold on a held
                        // selection (persistentSelection keeps selectionStart/
                        // End exactly where they were) — see
                        // refreshFormatState()'s own comment above.
                        onFormattingChanged: noteWindow.refreshFormatState()
                    }

                    Component.onCompleted: {
                        docHandler.setMarkdown(noteStore.loadNoteContent(noteWindow.noteId))
                        // setMarkdown() rebuilds the whole document through a
                        // cursor that starts at position 0; since Qt cursors
                        // aren't anchored against insertions at their own
                        // position by default, this editor's own cursor rides
                        // along to the very end of the newly-inserted text.
                        // That drags the view down with it via
                        // onCursorRectangleChanged below — so every note would
                        // open scrolled to the bottom instead of the top.
                        // Snapping back to 0 here (before the view has had a
                        // chance to react) is what makes notes open at the top.
                        cursorPosition = 0
                        ready = true
                        noteWindow.refreshFormatState()
                    }

                    // Covers the other half: the cursor/selection itself
                    // moving into (or out of) differently-formatted text,
                    // with no formatting call involved at all.
                    onSelectionStartChanged: noteWindow.refreshFormatState()
                    onSelectionEndChanged: noteWindow.refreshFormatState()

                    // Debounced + atomic on NoteStore's side (~300ms after the
                    // last keystroke) — instant-feeling without a disk write per
                    // keystroke.
                    onTextChanged: {
                        if (!ready)
                            return
                        // Auto-list shorthand (feature request): typing
                        // "- "/"* "/"1. " at the start of an otherwise-plain
                        // line converts it into a real list, consuming the
                        // typed marker. This mutates the document, which
                        // re-enters onTextChanged recursively — harmless and
                        // self-terminating, since the shorthand text is
                        // already gone by the second pass, so the regex
                        // simply stops matching.
                        docHandler.tryAutoConvertListShorthand(cursorPosition)
                        noteStore.saveNoteContent(noteWindow.noteId, docHandler.toMarkdown())
                    }

                    onCursorRectangleChanged: editorFlick.ensureCursorVisible(cursorRectangle)

                    // Enter/Return on a heading line breaks out of the heading
                    // (see DocumentHandler::tryResetHeadingOnReturn) so the
                    // next line starts as a plain paragraph, not a
                    // continuation of the heading style. Otherwise, Enter
                    // inside a list item continues the list (or, on an empty
                    // item, exits back to a plain paragraph) — see
                    // DocumentHandler::tryContinueList. Falls through to
                    // TextArea's own default Return handling (a plain
                    // paragraph break) whenever neither applies.
                    Keys.onReturnPressed: (event) => {
                        if (docHandler.tryResetHeadingOnReturn(cursorPosition))
                            event.accepted = true
                        else if (docHandler.tryContinueList(cursorPosition))
                            event.accepted = true
                        else
                            event.accepted = false
                    }
                    Keys.onEnterPressed: (event) => {
                        if (docHandler.tryResetHeadingOnReturn(cursorPosition))
                            event.accepted = true
                        else if (docHandler.tryContinueList(cursorPosition))
                            event.accepted = true
                        else
                            event.accepted = false
                    }

                    // Up on the topmost (visual, word-wrap-aware) line has
                    // nowhere else to go, same for Down on the bottommost —
                    // rather than leaving the cursor stuck at its current
                    // column, jump it to that line's start/end, same target
                    // Home/End would resolve to. positionAt() at x=0/width
                    // on the cursor's own row mirrors belowContentArea's
                    // reprojectedPosition() above, which relies on the same
                    // nearest-x hit-testing to land on a line boundary.
                    // cursorRectangle is in the same coordinate space as
                    // contentHeight (both TextEdit-native, unaffected by the
                    // Control's own padding) — see ensureCursorVisible's
                    // comment above for why that direct comparison is safe.
                    //
                    // event.accepted defaults to true for these per-key
                    // Keys.onXxxPressed handlers (unlike the general
                    // Keys.onPressed) — off the boundary line it must be
                    // explicitly cleared, or every ordinary Up/Down press
                    // gets swallowed here and never reaches TextEdit's own
                    // default line-to-line cursor movement underneath.
                    Keys.onUpPressed: (event) => {
                        if (cursorRectangle.y < 1) {
                            cursorPosition = positionAt(0, cursorRectangle.y)
                        } else {
                            event.accepted = false
                        }
                    }
                    Keys.onDownPressed: (event) => {
                        if (cursorRectangle.y + cursorRectangle.height > contentHeight - 1) {
                            cursorPosition = positionAt(width, cursorRectangle.y)
                        } else {
                            event.accepted = false
                        }
                    }
                }
            }

            // Clicking or drag-selecting below the real text (in the fill
            // space that makes editor cover the whole note — see the height
            // binding above) should resolve to the last line, at whichever
            // character is nearest horizontally — not unconditionally at
            // the very end of the document. Qt's own hit-testing for a y
            // past the bottom of the laid-out text just returns the
            // document's last position outright, ignoring x, so a bare
            // click/drag on editor down there always jumps to the true end
            // regardless of where you clicked horizontally. This MouseArea
            // sits after `editor` (topmost sibling wins, same convention as
            // mouseDragArea below) but only over the strip below editor's
            // real content — reprojecting onto the bottom edge of that
            // content before hit-testing gets a proper nearest-x lookup on
            // the last line instead. Zero height (so no hit at all) once
            // real content already fills or exceeds the viewport, since
            // there's no fill space left to click into.
            //
            // Handles the full press/drag/release cycle itself (rather than
            // a plain onClicked) so a drag that *starts* down here — in
            // either direction, extending up into real text or staying
            // below it — turns into a real selection, not just a final
            // cursor placement. This mirrors editor's own selectByMouse
            // dragging but doesn't share its limitation: since this
            // MouseArea holds its own press-grab for the whole gesture, it
            // keeps receiving move events regardless of where the pointer
            // strays (including back up into real content), unlike
            // editor's internal drag-selection, which silently stops
            // updating once a drag that started on IT crosses below its
            // own content — the reason EditorDragFilter (wrapping `editor`
            // above) exists to correct for that, separately, for drags
            // that start the other way around.
            MouseArea {
                id: belowContentArea

                property int dragAnchor: -1

                x: 0
                y: editor.contentHeight
                width: editorFlick.width
                height: Math.max(0, editor.height - editor.contentHeight)

                // mouse.y is relative to this area's own origin, which
                // itself sits editor.contentHeight below editor's origin
                // (both share the same parent and x) — add that back to
                // get an editor-relative y, then clamp into real content so
                // positionAt() resolves a genuine nearest-x character
                // instead of Qt's raw out-of-bounds behavior described
                // above. A y that's already within real content (dragged
                // up off the top of this area, into the text) passes
                // through unclamped, for a normal accurate hit-test there.
                function reprojectedPosition(mouseX, mouseY) {
                    const editorY = mouseY + editor.contentHeight
                    const clampedY = Math.min(Math.max(editorY, 0), Math.max(0, editor.contentHeight - 1))
                    return editor.positionAt(mouseX, clampedY)
                }

                onPressed: (mouse) => {
                    editor.forceActiveFocus()
                    dragAnchor = reprojectedPosition(mouse.x, mouse.y)
                    editor.cursorPosition = dragAnchor
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    editor.select(dragAnchor, reprojectedPosition(mouse.x, mouse.y))
                }
            }
        }

        // Subtle scroll indicator (feature request): shown only when the
        // note's content overflows the visible area, tracking position via
        // editorFlick's own visibleArea (Flickable already computes the
        // normalized position/extent — no manual contentY/contentHeight
        // math needed). Deliberately a plain non-interactive Rectangle, not
        // QtQuick.Controls' ScrollBar — everything else in this file
        // (HUD, resize handles) is hand-rolled to control its exact look,
        // and a QQC2 control would pull in a platform style whose default
        // chrome wouldn't match or take the glass treatment below
        // consistently.
        Item {
            id: scrollIndicator
            // Above mouseDragArea (z: 1, declared below), matching
            // resizeBorders' own tier (2): without this, mouseDragArea's
            // IBeam/Arrow cursor logic — not this MouseArea's own
            // PointingHandCursor — would win cursor resolution here, and a
            // drag press would fall through to mouseDragArea instead of
            // starting the scroll drag.
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

            // Whether the scrollbar should render its glass material
            // (feature request): by default mirrors the note's own glass
            // setting, same as before; settings.chromeGlassFollowsNote lets
            // that be pinned to a fixed settings.chromeGlassEnabled value
            // instead, independent of this particular note.
            readonly property bool chromeGlassActive: settings.chromeGlassFollowsNote
                ? noteAppearance.glassEnabled
                : settings.chromeGlassEnabled

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
                color: scrollIndicator.chromeGlassActive ? Qt.rgba(0, 0, 0, 0.28) : noteAppearance.fontColor
                opacity: scrollIndicator.chromeGlassActive
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

            // Glass-textured when scrollIndicator.chromeGlassActive (default:
            // mirrors the note's own glass mode; settings.chromeGlassFollowsNote
            // — feature request — can pin it to a fixed on/off instead), using
            // the same appearance properties mainFrame's and NoteHud's own
            // GlassOverlay instances use, so the scrollbar reads as the
            // same material rather than a plain flat bar.
            GlassOverlay {
                anchors.fill: scrollHandle
                cornerRadius: scrollHandle.radius
                visible: scrollIndicator.chromeGlassActive
                tintColor: noteAppearance.glassTintColor
                sheenOpacity: noteAppearance.glassSheenOpacity
                ringCount: noteAppearance.glassSheenStrength
                sheenSize: noteAppearance.glassSheenSize
            }

            // Configurable hover/press highlight box (feature request) —
            // same InteractableHighlight the popup toolbar's buttons and
            // NoteHud's glyphs use, so the scrollbar's own hover feedback
            // is tunable from the same Settings knobs instead of the
            // ad hoc opacity bump above.
            InteractableHighlight {
                anchors.fill: scrollHandle
                radius: scrollHandle.radius
                active: scrollDrag.pressed || scrollHoverHandler.hovered
            }
        }

        // Right-click-drag (mouse) and tap-drag (touch) move the window via
        // the compositor. This is the only mechanism that works on Wayland —
        // clients can't set their own position directly (see project notes).
        // Left click is reserved for editing, so the mouse gesture is scoped
        // to the right button (plus middle, for the autoscroll feature
        // below — see acceptedButtons' own comment).
        //
        // Mouse case uses a MouseArea, not a DragHandler, and sits on top of
        // the editor (z above it) rather than as an ancestor: TextArea's
        // underlying TextInput grabs the mouse exclusively on press for any
        // button, and a DragHandler attached to an ancestor only ever gets a
        // passive grab in that case — a passive grab can't be promoted over
        // a descendant's exclusive one, so it never activates. A MouseArea
        // sidesteps this because it participates in Qt Quick's legacy
        // top-most-hit-test: since acceptedButtons excludes Left (outside
        // the brief armed-autoscroll window — see below), a left-button
        // press isn't a hit here at all and keeps searching underneath
        // (reaching the editor normally), while a right/middle-button press
        // *is* a hit here — intercepted before it ever reaches TextInput
        // underneath, so no competing exclusive grab exists to fight over.
        // (A bare Item with only a PointerHandler doesn't get this
        // fallthrough: unmatched events don't drop to siblings behind it,
        // only bubble to its own ancestors.)
        // Sitting on top (z above the editor) also makes mouseDragArea the
        // one QQuickItem the platform cursor is resolved against here — Qt
        // Quick's cursor lookup walks up from the topmost hit item's own
        // ancestor chain, not sideways into covered siblings, so TextArea's
        // automatic IBeam cursor underneath never becomes visible on its
        // own. Reflecting it explicitly (whenever the pointer is over
        // editorFlick — its real text or the dead space below the last
        // line, both) is what makes the cursor look right again; over the
        // thin top margin above it, the default arrow stands.
        MouseArea {
            id: mouseDragArea
            anchors.fill: parent
            z: 1
            // Left is included ONLY while middle-click autoscroll is armed
            // (feature request) — Chrome's own convention is that clicking
            // ANY button cancels an armed autoscroll rather than also
            // performing that button's normal action, so this area briefly
            // steals left-button hit-testing too for exactly that one
            // click, then immediately falls back to excluding it again so
            // normal left-click editing is completely unaffected the rest
            // of the time.
            acceptedButtons: middleScrollArmed
                ? (Qt.RightButton | Qt.MiddleButton | Qt.LeftButton)
                : (Qt.RightButton | Qt.MiddleButton)
            hoverEnabled: true
            // Chrome-style scroll-mode cursor (feature request) while
            // autoscroll is armed, taking priority over the ordinary
            // I-beam/arrow split below. Qt's built-in cursor shapes have no
            // distinct single-direction up/down pair (only SizeVerCursor, a
            // bidirectional vertical arrow, and SizeAllCursor, a four-way
            // one) — that's the closest available "makes sense given the
            // direction" without shipping custom cursor bitmap assets:
            // SizeAllCursor right at the origin (nothing to scroll yet, any
            // direction is still on the table), SizeVerCursor once the
            // pointer has actually moved far enough to be scrolling —
            // matching the same deadzone the Timer below uses to decide
            // whether it's scrolling at all, so the cursor changes at
            // exactly the moment real scrolling starts.
            cursorShape: middleScrollArmed
                ? (Math.abs(middleScrollCurrentY - middleScrollOriginY) > middleScrollDeadzone
                       ? Qt.SizeVerCursor : Qt.SizeAllCursor)
                : (mouseY >= editorFlick.y ? Qt.IBeamCursor : Qt.ArrowCursor)

            // Right-click now has two outcomes — start a window move, or
            // open the copy/paste context menu (feature request) — decided
            // by whether the press actually moves before release, since
            // startSystemMove() hands the whole gesture to the compositor
            // the instant it's called (see project notes on Wayland
            // interactive move: the client never reliably gets its own
            // onReleased/onCanceled back afterward). Deferring the call
            // until a small movement threshold is exceeded, instead of
            // firing it unconditionally on press like before, is what makes
            // "click" and "drag" distinguishable at all: a plain click that
            // never crosses the threshold never calls startSystemMove(), so
            // this MouseArea keeps its normal grab and onClicked fires
            // exactly like any ordinary click.
            property real pressX: 0
            property real pressY: 0
            property bool rightDragStarted: false
            readonly property int dragThreshold: 4

            // Middle-click autoscroll (feature request, Chrome-style):
            // middle-click arms it and records the origin Y; the pointer
            // then free-scrolls the note continuously in whichever
            // direction it's moved from that origin, at a speed scaled by
            // the distance. Two ways to end up scrolling, both supported:
            // a quick click arms it, then the pointer free-scrolls with no
            // button held (see hoverEnabled above: onPositionChanged fires
            // on plain hover too) until a further click (any button; see
            // acceptedButtons above) or Escape disarms it again — OR the
            // button can just be held down and dragged, scrolling exactly
            // the same way while held, and releasing it (once it's
            // actually been dragged) disarms immediately, without needing
            // a second click. middleScrollDraggedWhileHeld is what tells
            // those two gestures apart at release time: a plain click that
            // never moved leaves it false, so release does nothing and the
            // click-to-arm/free-scroll convention is untouched.
            property bool middleScrollArmed: false
            property real middleScrollOriginY: 0
            property real middleScrollCurrentY: 0
            property bool middleButtonHeld: false
            property bool middleScrollDraggedWhileHeld: false
            readonly property int middleScrollDeadzone: 8

            function armMiddleScroll(mouseX, mouseY) {
                middleScrollArmed = true
                middleScrollOriginY = mapToItem(editorFlick, mouseX, mouseY).y
                middleScrollCurrentY = middleScrollOriginY
                middleScrollDraggedWhileHeld = false
                middleScrollTimer.start()
            }
            function disarmMiddleScroll() {
                middleScrollArmed = false
                middleButtonHeld = false
                middleScrollDraggedWhileHeld = false
                middleScrollTimer.stop()
            }

            onPressed: (mouse) => {
                if (middleScrollArmed) {
                    disarmMiddleScroll()
                    mouse.accepted = true
                    return
                }
                if (mouse.button === Qt.MiddleButton) {
                    armMiddleScroll(mouse.x, mouse.y)
                    middleButtonHeld = true
                    mouse.accepted = true
                    return
                }
                if (mouse.button === Qt.RightButton) {
                    pressX = mouse.x
                    pressY = mouse.y
                    rightDragStarted = false
                }
            }
            onReleased: (mouse) => {
                if (mouse.button === Qt.MiddleButton) {
                    middleButtonHeld = false
                    if (middleScrollDraggedWhileHeld)
                        disarmMiddleScroll()
                }
            }
            onPositionChanged: (mouse) => {
                middleScrollCurrentY = mapToItem(editorFlick, mouse.x, mouse.y).y
                if (middleButtonHeld && !middleScrollDraggedWhileHeld
                        && Math.abs(middleScrollCurrentY - middleScrollOriginY) > dragThreshold) {
                    middleScrollDraggedWhileHeld = true
                }
                if (middleScrollArmed || rightDragStarted || !(mouse.buttons & Qt.RightButton))
                    return
                if (Math.abs(mouse.x - pressX) > dragThreshold || Math.abs(mouse.y - pressY) > dragThreshold) {
                    rightDragStarted = true
                    noteWindow.startSystemMove()
                }
            }
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton && !rightDragStarted)
                    editorContextMenu.popup(mouse.x, mouse.y)
            }

            // ~60fps: smooth enough for a scroll animation, cheap enough to
            // not matter. Armed only between an explicit middle-click and
            // an explicit disarm (another click or Escape) — never runs
            // otherwise, so this is a continuous foreground tick bound to
            // an active, user-initiated interaction (the same category as
            // editorFlick's own built-in flick/deceleration animation),
            // not a background poll of idle/external state.
            Timer {
                id: middleScrollTimer
                interval: 16
                repeat: true
                onTriggered: {
                    const dy = mouseDragArea.middleScrollCurrentY - mouseDragArea.middleScrollOriginY
                    if (Math.abs(dy) <= mouseDragArea.middleScrollDeadzone)
                        return
                    const beyondDeadzone = dy - Math.sign(dy) * mouseDragArea.middleScrollDeadzone
                    // No speed ceiling (feature request) — quadratic in the
                    // distance past the deadzone, uncapped, so dragging far
                    // enough from the origin keeps getting faster instead of
                    // plateauing; editorFlick.contentY's own clamp below is
                    // still what stops it from overshooting past either end.
                    const speed = Math.sign(beyondDeadzone) * (beyondDeadzone * beyondDeadzone * 0.002)
                    const maxContentY = Math.max(0, editorFlick.contentHeight - editorFlick.height)
                    editorFlick.contentY = Math.max(0, Math.min(maxContentY, editorFlick.contentY + speed))
                }
            }
        }

        // Escape cancels an armed middle-click autoscroll without needing
        // another click — standard convention for backing out of an armed,
        // modal-ish pointer mode.
        Shortcut {
            sequence: "Escape"
            onActivated: if (mouseDragArea.middleScrollArmed) mouseDragArea.disarmMiddleScroll()
        }

        // Right-click copy/paste menu (feature request): opened by
        // mouseDragArea's onClicked above instead of QtQuick Controls'
        // built-in ContextMenu (disabled on editor above) specifically
        // because that built-in menu would fight the drag area for the
        // same right-button press — see the disabled ContextMenu.menu's own
        // comment.
        Menu {
            id: editorContextMenu

            // Exposed for tests only, same convention as mouseDragArea's
            // own alias above.
            readonly property alias cutItem: cutItem
            readonly property alias copyItem: copyItem
            readonly property alias pasteItem: pasteItem
            readonly property alias selectAllItem: selectAllItem

            MenuItem {
                id: cutItem
                text: "Cut"
                enabled: editor.selectionStart !== editor.selectionEnd
                onTriggered: { editor.cut(); editor.forceActiveFocus() }
            }
            MenuItem {
                id: copyItem
                text: "Copy"
                enabled: editor.selectionStart !== editor.selectionEnd
                onTriggered: { editor.copy(); editor.forceActiveFocus() }
            }
            MenuItem {
                id: pasteItem
                text: "Paste"
                enabled: editor.canPaste
                onTriggered: { editor.paste(); editor.forceActiveFocus() }
            }
            MenuSeparator {}
            MenuItem {
                id: selectAllItem
                text: "Select All"
                enabled: editor.length > 0
                onTriggered: { editor.selectAll(); editor.forceActiveFocus() }
            }
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
        // editor/HUD/resize borders underneath.
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
