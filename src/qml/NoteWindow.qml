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

    // Find/replace requests from this note, wired up by Main.qml to the single
    // app-wide FindReplacePanel window.
    //
    // Signals rather than a direct call, because a note has no handle on the
    // panel and deliberately isn't given one: the chrome windows' data flow is
    // strictly one-way — Main.qml pushes the focused note INTO the panel/
    // toolbar, and they read out of it (see SelectionToolbar.qml's noteWindow).
    // A back-reference here would make that a cycle. This mirrors how NoteHud
    // already asks for things it cannot do itself (NoteHud.searchRequested,
    // which this window turns straight into findRequested below).
    //
    // withReplace distinguishes Ctrl+H from Ctrl+F; findCloseRequested is
    // Escape's, and is emitted whether or not the panel is actually open (the
    // note has no way to know, and close() no-ops when it isn't).
    signal findRequested(bool withReplace)
    signal findCloseRequested()

    // A stationary right-click in this note wants the app-wide context menu
    // opened at that point, in SCREEN coordinates (already resolved through
    // noteOrigin() below — a note cannot be trusted to know its own position
    // via Window.x/y, see the position comment further down).
    //
    // A signal, not a direct call, for exactly the reason findRequested above
    // is one: the menu is a single app-wide chrome window Main.qml owns, a
    // note has no handle on it and deliberately isn't given one, and a
    // back-reference would turn the chrome's strictly one-way data flow into
    // a cycle.
    //
    // Emitted by mouseDragArea's own onReleased, which is the one place that
    // knows the right-button gesture ended without becoming a window drag.
    signal contextMenuRequested(int screenX, int screenY)

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

    // Spellcheck tap-to-suggest (feature): populated by the spellcheck
    // TapHandler below when a tap lands on a flagged word; SelectionToolbar's
    // own suggestion popup reads these to know what to show/replace. Empty
    // string means "no misspelled word currently selected via tap" — the
    // popup hides whenever this is empty.
    property string currentMisspelledWord: ""
    property var currentSpellingSuggestions: []
    property int misspelledWordStart: -1
    property int misspelledWordEnd: -1

    // Re-polls every currentXxx property above from the editor's real
    // selection and the document's own format at that selection. Called
    // whenever either could have changed: the cursor/selection moves (see
    // editor's onSelectionStartChanged/onSelectionEndChanged), or a
    // formatting call mutates the document without moving the selection at
    // all (see docHandler's onFormattingChanged — e.g. clicking Bold on a
    // held selection).
    // Runs an inline toggle and puts the selection back over the same text.
    // Without the reselect, the marker inserted at the selection's end lands
    // INSIDE it ("bold me**"), which both looks wrong and makes the toolbar's
    // own bold/italic indicator read false right after it was turned on.
    function applyInlineToggle(fn) {
        const result = fn({start: editor.selectionStart, end: editor.selectionEnd})
        if (result)
            editor.select(result.start, result.end)
        refreshFormatState()
    }

    // The checkbox glyph the pointer is genuinely ON TOP OF, in editor
    // coordinates — or an empty rect for anywhere else. Drives both the hover
    // highlight and the pointing-hand cursor, so the two can never disagree
    // about whether there's a checkbox under the pointer.
    //
    // The containment test is the whole point, and it is not redundant with
    // checkboxRectAt()'s own position check. TextEdit::positionAt() never
    // fails: it CLAMPS, returning the nearest character position for a point
    // that is nowhere near any text. So a pointer in the blank space above the
    // editor (mouseDragArea covers the HUD strip too, which maps to a negative
    // y here) resolves to position 0 — landing inside the first line's marker
    // and lighting up a checkbox the pointer isn't within a hundred pixels of.
    // Asking the rect whether it actually contains the point is what turns
    // "which character is nearest" back into "is the pointer on the glyph".
    //
    // editor has zero padding (asserted in tst_NoteWindow), so its item
    // coordinates and the document coordinates checkboxRectAt returns are the
    // same space and are compared directly.
    function checkboxRectUnderPointer(xInEditor, yInEditor) {
        const rect = docHandler.checkboxRectAt(editor.positionAt(xInEditor, yInEditor))
        if (rect.width <= 0 || rect.height <= 0)
            return Qt.rect(0, 0, 0, 0)
        const inside = xInEditor >= rect.x && xInEditor < rect.x + rect.width
                    && yInEditor >= rect.y && yInEditor < rect.y + rect.height
        return inside ? rect : Qt.rect(0, 0, 0, 0)
    }

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
    readonly property alias touchDragHandler: touchDragHandler
    readonly property alias editor: editor
    readonly property alias editorFlick: editorFlick
    readonly property alias noteHud: noteHud
    readonly property alias copyToast: copyToast
    readonly property alias checkboxHighlight: checkboxHighlight
    readonly property alias noteAppearance: noteAppearance
    readonly property alias docHandler: docHandler
    readonly property alias misspelledWordRepeater: misspelledWordRepeater
    readonly property alias scrollIndicator: scrollIndicator
    readonly property alias scrollHandle: scrollHandle
    readonly property alias mainFrame: mainFrame
    readonly property alias lineNumberGutter: lineNumberGutter

    // Code mode (feature request): this note renders as code — a monospaced
    // font (resolved in NoteAppearance::fontFamily, not here) and the
    // line-number gutter below. Resolves the note's own override against the
    // global default; see NoteAppearance::codeModeEnabled.
    readonly property bool codeModeEnabled: noteAppearance.codeModeEnabled

    // The three things code mode SWITCHES OFF, each gated by its own toggle so
    // a user can re-enable any of them for their code notes. Read off
    // noteAppearance rather than settings, so each resolves this note's own
    // override against the global default exactly the way codeModeEnabled
    // above does — "what does code mode do" gets a global answer that an
    // individual note may disagree with.
    //
    // Derived here, once, rather than at each of the ~15 use sites below: the
    // whole point of the pairing is that every consumer agrees on what code
    // mode means, and a repeated `codeModeEnabled && noteAppearance.codeModeX`
    // expression is exactly the kind of thing that drifts when one site is
    // updated and another is missed. Each reads false whenever code mode is
    // off, so every consumer is a plain "is this suppressed" check.
    readonly property bool codeModeSuppressesMarkdown: noteWindow.codeModeEnabled
        && noteAppearance.codeModeDisablesMarkdownHighlighting
    readonly property bool codeModeSuppressesAutoFormatting: noteWindow.codeModeEnabled
        && noteAppearance.codeModeDisablesAutoFormatting
    readonly property bool codeModeSuppressesSpellcheck: noteWindow.codeModeEnabled
        && noteAppearance.codeModeDisablesSpellcheck

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

    // How far the duplicate spawns from its source, on both axes. Small and
    // constant, in the spirit of Component.onCompleted's own `index * 40`
    // cascade: enough that the copy reads as a separate window rather than a
    // repaint of the original, without flinging it across the screen.
    readonly property int duplicateOffset: 30

    // This note's top-left in SCREEN coordinates — the one correct way to
    // turn anything measured inside this window into a screen position.
    //
    // Prefers noteX/noteY (model-backed, and the only value updated after a
    // compositor-driven drag — see the position comment below) and falls back
    // to this window's own x/y, which is the sole record of where a
    // never-dragged note sits. The pair is tested together, matching
    // Component.onCompleted's own hasSavedPosition convention.
    //
    // Deliberately NOT Item.mapToGlobal(), which is the obvious-looking
    // alternative and is wrong here: mapToGlobal resolves through
    // QWindow::position(), which on Wayland is never updated after a
    // compositor-driven move (the protocol simply doesn't tell clients where
    // they are) — so it silently returns coordinates from wherever the note
    // was when it opened, for any note the user has ever dragged.
    function noteOrigin() {
        const hasKnownPosition = (noteX !== 0 || noteY !== 0)
        return Qt.point(hasKnownPosition ? noteX : noteWindow.x,
                        hasKnownPosition ? noteY : noteWindow.y)
    }

    // Duplicate (feature request): creates a genuinely independent note — its
    // own id, own file, own session state — that merely starts out with this
    // note's content. Nothing is shared afterward; editing either is a normal
    // edit of a normal note.
    //
    // The content goes through createNote() rather than a save after the fact
    // because the new note's window reads its file the moment it opens, while
    // saveNoteContent() is debounced — see NoteStore::createNote for the full
    // reasoning.
    //
    // The position is seeded through openNoteAt() rather than moved after the
    // window exists because a mapped Wayland toplevel can't be placed by
    // assigning x/y at all; placement happens via the KWin rule the new
    // delegate writes during its own Component.onCompleted, so the position
    // must already be in Settings by then.
    function duplicateNoteAt(screenX, screenY) {
        const newId = noteStore.createNote(docHandler.toMarkdown())
        openNotesModel.openNoteAt(newId, screenX, screenY)
    }

    // Duplicate at the conventional diagonal offset from this note — what the
    // HUD's own duplicate glyph does. The context menu instead duplicates at
    // the point the menu was opened, which is why the position is a parameter
    // of duplicateNoteAt() above rather than baked in here.
    function duplicateNote() {
        const origin = noteOrigin()
        duplicateNoteAt(origin.x + duplicateOffset, origin.y + duplicateOffset)
    }

    // Feature request: Copy — both the toolbar button and Ctrl+C, see
    // editor's own Keys.onPressed below — copies the whole note when
    // nothing is selected, rather than silently doing nothing, and briefly
    // shows copyToast either way (a copy with no visible change, the
    // no-selection case especially, otherwise gives no feedback that
    // anything happened at all).
    function performCopy() {
        if (editor.selectionStart === editor.selectionEnd) {
            const savedPosition = editor.cursorPosition
            editor.selectAll()
            editor.copy()
            editor.deselect()
            editor.cursorPosition = savedPosition
        } else {
            editor.copy()
        }
        copyToast.show()
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
        //
        // All nine are disabled in code mode (subject to the same
        // codeModeDisablesAutoFormatting toggle that gates DocumentHandler's
        // own as-you-type rewrites, so the two can never disagree about
        // whether this note formats markdown). A DISABLED Shortcut does not
        // consume its sequence — it stops matching entirely — so Ctrl+B in a
        // code note falls through to the editor rather than being swallowed
        // as a silent no-op. That also frees the sequence for whatever else
        // wants it, which is the correct behavior for a mode that says this
        // note is not markdown.
        //
        // Ctrl+Y / Ctrl+F / Ctrl+H below are deliberately NOT gated: undo,
        // find and replace are not markdown formatting and are, if anything,
        // more useful in a code note.
        Shortcut {
            sequence: settings.shortcutBold
            enabled: !noteWindow.codeModeSuppressesAutoFormatting
            onActivated: noteWindow.applyInlineToggle(r => docHandler.toggleBold(r.start, r.end))
        }
        Shortcut {
            sequence: settings.shortcutItalic
            enabled: !noteWindow.codeModeSuppressesAutoFormatting
            onActivated: noteWindow.applyInlineToggle(r => docHandler.toggleItalic(r.start, r.end))
        }
        Shortcut {
            sequence: settings.shortcutStrikethrough
            enabled: !noteWindow.codeModeSuppressesAutoFormatting
            onActivated: noteWindow.applyInlineToggle(r => docHandler.toggleStrikethrough(r.start, r.end))
        }
        Shortcut {
            sequence: settings.shortcutHeading1
            enabled: !noteWindow.codeModeSuppressesAutoFormatting
            onActivated: docHandler.setHeadingLevel(editor.selectionStart, editor.selectionEnd, 1)
        }
        Shortcut {
            sequence: settings.shortcutHeading2
            enabled: !noteWindow.codeModeSuppressesAutoFormatting
            onActivated: docHandler.setHeadingLevel(editor.selectionStart, editor.selectionEnd, 2)
        }
        Shortcut {
            sequence: settings.shortcutParagraph
            enabled: !noteWindow.codeModeSuppressesAutoFormatting
            onActivated: docHandler.setHeadingLevel(editor.selectionStart, editor.selectionEnd, 0)
        }
        Shortcut {
            sequence: settings.shortcutBulletList
            enabled: !noteWindow.codeModeSuppressesAutoFormatting
            onActivated: docHandler.toggleBulletList(editor.selectionStart, editor.selectionEnd)
        }
        Shortcut {
            sequence: settings.shortcutNumberedList
            enabled: !noteWindow.codeModeSuppressesAutoFormatting
            onActivated: docHandler.toggleNumberedList(editor.selectionStart, editor.selectionEnd)
        }
        Shortcut {
            sequence: settings.shortcutInsertTable
            enabled: !noteWindow.codeModeSuppressesAutoFormatting
            onActivated: docHandler.insertTable(editor.selectionEnd)
        }

        // Ctrl+Y as an extra redo binding: TextArea's built-in undo/redo
        // already responds to Qt's default StandardKey.Redo (Ctrl+Shift+Z
        // on this platform), but Ctrl+Y is the redo convention users
        // coming from Windows/other editors expect, so it's added
        // alongside rather than in place of the default.
        Shortcut {
            sequence: "Ctrl+Y"
            onActivated: editor.redo()
        }

        // Find/replace (feature request) — hardcoded like Ctrl+Y above
        // rather than user-configurable, matching the near-universal
        // editor convention for these two. Qt.WindowShortcut (the default
        // context) scopes each to its own note window, so several open
        // notes never fight over one press — which is exactly what makes
        // "the focused note is the one the panel searches" work, given the
        // panel itself is a single app-wide window.
        //
        // These only ASK (see findRequested's own comment): the panel lives
        // in Main.qml, and a note cannot reach it.
        Shortcut {
            sequence: "Ctrl+F"
            onActivated: noteWindow.findRequested(false)
        }
        Shortcut {
            sequence: "Ctrl+H"
            onActivated: noteWindow.findRequested(true)
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
            // "Scroll past end" (feature request): extends contentHeight
            // far enough past the real text that the scrollRange clamps
            // below (wheel handler, scrollbar drag, middle-click
            // autoscroll — each computed as contentHeight - height) can
            // carry the *last line* all the way to the top of the note,
            // not just its bottom. positionToRectangle(length) reads that
            // last line's own top-left y straight off the laid-out text —
            // exact regardless of font metrics/line height — added to
            // `height` gives a contentHeight whose remainder above `height`
            // is exactly that y. Only applied once real content actually
            // overflows the viewport (editor.contentHeight > height);
            // short notes keep the old editor.height value (itself already
            // floored to `height` — see editor.height's own comment below)
            // so nothing scrolls when there's nothing to scroll past.
            contentHeight: editor.contentHeight > height
                ? editor.positionToRectangle(editor.length).y + height
                : editor.height
            // Feature request/bugfix: a left-mouse-drag over the note used
            // to compete with (and sometimes win over) TextInput's own
            // drag-text-selection — Flickable's childMouseEventFilter can
            // steal the grab mid-drag before TextInput calls
            // setKeepMouseGrab(true), a well-known Flickable-vs-TextInput
            // arbitration race — which meant dragging to select text could
            // silently scroll the note instead. Disabling `interactive`
            // removes Flickable from that race entirely; wheel scrolling
            // (which Qt also gates behind this same flag — confirmed
            // empirically, a synthetic wheel event produced no contentY
            // change at all otherwise) is restored manually below via
            // wheelHandler instead.
            interactive: false

            // Keeps the caret on-screen when it moves off the visible edge —
            // whether from typing past the bottom or from arrow-key/Home/End
            // navigation. editor sits at content-coordinate y=0 inside
            // editorFlick (no ancestor between them offsets y), so
            // editor.cursorRectangle's y can be compared directly against
            // contentY/height without any coordinate mapping.
            //
            // Only y is guaranteed: code mode's line-number gutter shifts
            // editorDragFilter's x (see lineNumberGutter.gutterWidth), so
            // editor's content x is the gutter width, not 0. Nothing here
            // reads x — this scrolls vertically only (contentWidth: width) —
            // but a future x-axis comparison against contentX would need the
            // offset that this one deliberately doesn't.
            function ensureCursorVisible(rect) {
                if (rect.y < contentY)
                    contentY = rect.y
                else if (rect.y + rect.height > contentY + height)
                    contentY = rect.y + rect.height - height
            }

            // Feature request: the spelling-suggestion popup (see
            // SelectionToolbar.qml's own spellingSuggestionsPopup) is
            // anchored to one specific word's on-screen rect — if the note
            // scrolls enough that the flagged word itself scrolls out of
            // the visible viewport, that anchor no longer points at
            // anything the user can see, so this clears
            // currentMisspelledWord (the same property the tap-to-suggest
            // TapHandler above already uses to close the popup on a normal
            // click) the moment that happens, rather than leaving the
            // popup pinned to text that's no longer on screen. Same
            // content-coordinate comparison ensureCursorVisible above
            // already uses — no mapping needed, editor sits at (0,0)
            // inside editorFlick's own content.
            onContentYChanged: {
                if (noteWindow.currentMisspelledWord === "")
                    return
                const rect = editor.positionToRectangle(noteWindow.misspelledWordStart)
                if (rect.y + rect.height < contentY || rect.y > contentY + height)
                    noteWindow.currentMisspelledWord = ""
            }

            // Restores mouse-wheel scrolling now that `interactive` above is
            // false (see its own comment). acceptedButtons: Qt.NoButton
            // means press/release/click all fall straight through to
            // editor and the other MouseAreas beneath it untouched (same
            // fallthrough convention mouseDragArea's own acceptedButtons
            // uses) — this exists purely to catch wheel events, which
            // aren't gated by acceptedButtons at all.
            //
            // A plain MouseArea, not WheelHandler (used here originally):
            // switched after wheel scrolling was confirmed still dead live
            // even with the pixelDelta/angleDelta fallback below in place,
            // while every other real-hardware interaction in this file
            // (mouseDragArea, resizeBorders) already goes through MouseArea
            // successfully — hedges against a possible WheelHandler-
            // specific platform quirk rather than continuing to chase
            // WheelHandler's own event delivery blind.
            //
            // Bugfix: a plain mouse wheel reports its delta via
            // angleDelta (eighths of a degree, typically ±120 per notch);
            // a touchpad or a "smooth"/high-resolution wheel instead
            // reports pixelDelta (real pixels) with angleDelta left at
            // (0, 0) — confirmed live on Wayland that only reading
            // angleDelta left scrolling completely dead for exactly that
            // input class, since 0 was silently subtracted every time.
            // pixelDelta, when present, is already a real pixel amount, so
            // it's used directly in preference to angleDelta.
            //
            // angleDelta itself is scaled down rather than used as pixels
            // directly (previous behavior): its raw 120-per-notch, taken
            // literally as 120px, scrolled noticeably further per click
            // than KDE/GTK/Qt Widgets do by default — they all resolve one
            // notch to QApplication::wheelScrollLines() (3) lines of text.
            // Dividing by 120 first gives the notch count regardless of the
            // wheel's own resolution (a high-res wheel reports several
            // smaller deltas per physical click instead of one ±120 — this
            // still scales proportionally), then that's converted to pixels
            // via the note's own font size so the tick stays proportional
            // to whatever text size the note is actually showing.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                onWheel: (event) => {
                    const scrollRange = Math.max(0, editorFlick.contentHeight - editorFlick.height)
                    const lineHeight = noteAppearance.fontSize * 1.2
                    const delta = event.pixelDelta.y !== 0
                        ? event.pixelDelta.y
                        : (event.angleDelta.y / 120) * 3 * lineHeight
                    editorFlick.contentY = Math.max(0, Math.min(scrollRange,
                        editorFlick.contentY - delta))
                }
            }

            // Line-number gutter (code mode). A sibling of editorDragFilter
            // inside editorFlick's CONTENT, not a child of mainFrame, and
            // that placement is the whole trick: content coordinates scroll
            // with the text for free, so this needs no contentY tracking and
            // no repositioning on scroll — which is also the only shape that
            // obeys the repo's no-polling rule without a single timer.
            //
            // It reserves horizontal space by SHIFTING editorDragFilter (and
            // therefore editor) right by its own width — see gutterWidth
            // below — rather than by overlaying the text.
            Item {
                id: lineNumberGutter

                // Zero-width when off, so every consumer of gutterWidth is a
                // plain arithmetic term with no special case: a note that is
                // not in code mode lays out exactly as it did before this
                // existed, byte for byte.
                readonly property real gutterWidth: noteWindow.codeModeEnabled
                    ? digitMetrics.width + leftPadding + rightPadding
                    : 0
                readonly property real leftPadding: 2
                // Separates the numbers from the first character of code.
                // Generous on purpose: a number butted against the text reads
                // as part of it, which is exactly what a gutter must not do.
                readonly property real rightPadding: 8

                // Document positions where each logical line starts — index i
                // is line i+1's start. Derived from the buffer's own text, so
                // it re-derives whenever the text changes and cannot drift out
                // of sync with what is rendered.
                //
                // A logical (block) line, NOT a visual row: a wrapped line
                // gets ONE number, at the row where it begins, which is what
                // every code editor does — numbering wrapped continuation rows
                // would make the gutter disagree with every compiler error
                // message the user is trying to match against.
                readonly property var lineStarts: {
                    // Short-circuit before touching `text`, so an ordinary
                    // note never scans its buffer on a keystroke for a gutter
                    // it isn't showing. Reading editor.text below is what
                    // subscribes this binding to every edit — returning first
                    // means an ordinary note doesn't subscribe at all, rather
                    // than subscribing and then discarding the result.
                    if (!noteWindow.codeModeEnabled)
                        return []

                    // Depend on `text` explicitly: editor.length alone would
                    // not re-evaluate this when an edit leaves the length
                    // unchanged (typing over a selection of the same size,
                    // replacing a newline with a character).
                    const text = editor.text
                    const starts = [0]
                    for (let i = 0; i < text.length; i++) {
                        if (text.charCodeAt(i) === 10 /* \n */)
                            starts.push(i + 1)
                    }
                    return starts
                }

                // The numbers deliberately do NOT inherit the note's family:
                // they are chrome, not content, and a proportional family
                // would make them jitter horizontally line to line. Size and
                // color still track the note so the gutter scales with the
                // text it is numbering.
                readonly property font numberFont: Qt.font({
                    family: "monospace",
                    pixelSize: noteAppearance.fontSize
                })

                // Measures the widest number the gutter will actually show, in
                // the gutter's own font, instead of guessing from a per-digit
                // constant — a monospace font is not guaranteed here (the user
                // can override this note's family, see NoteAppearance::
                // fontFamily) and a proportional font's digits are not the
                // width of its "0".
                //
                // Sized to the digits the note ACTUALLY has, with no minimum:
                // the delegates right-align inside this width, so reserving a
                // two-digit box for a 9-line note would sit every number one
                // whole digit clear of the left edge — read as a stray indent
                // rather than as room for a tenth line that doesn't exist. The
                // gutter therefore does widen as a note crosses 9 to 10 lines;
                // that one-off shift is the accepted cost of not indenting
                // every short note forever.
                //
                // Every reference to the gutter below is QUALIFIED
                // (lineNumberGutter.x), never bare, and that is required
                // rather than stylistic: an unqualified name inside a nested
                // object resolves against that object, the ids in scope, and
                // the COMPONENT ROOT (noteWindow) — an enclosing non-root
                // object's properties are not in the chain at all. A bare
                // `numberFont` here therefore threw "numberFont is not
                // defined" at runtime, which no test caught: the QML suite
                // never instantiated a note in code mode, and the app itself
                // exits 255 with an EMPTY log under the wayland platform, so
                // this only surfaced from an offscreen run.
                TextMetrics {
                    id: digitMetrics
                    font: lineNumberGutter.numberFont
                    text: "0".repeat(String(lineNumberGutter.lineStarts.length).length)
                }

                x: 0
                y: 0
                width: gutterWidth
                height: editor.height
                visible: noteWindow.codeModeEnabled

                Repeater {
                    // An INT model (the line count), not the lineStarts array
                    // itself: a Repeater rebuilds every delegate whenever its
                    // model identity changes, and lineStarts is a fresh array
                    // on every keystroke — so binding the array directly would
                    // tear down and rebuild every number in the note for each
                    // character typed. With a count, delegates persist and
                    // only their own y binding re-evaluates. This file has
                    // been bitten by exactly this before; see DocumentHandler
                    // ::emitListMarkerRangesChangedIfNeeded's comment for the
                    // same lesson learned the hard way on the marker overlay.
                    model: noteWindow.codeModeEnabled ? lineNumberGutter.lineStarts.length : 0

                    delegate: Text {
                        required property int index

                        // positionToRectangle() reads the y straight off the
                        // laid-out text, so a number tracks its line through
                        // wrapping, font changes and reflow without this file
                        // knowing anything about line heights. Guarded against
                        // a transient index past the end: lineStarts and the
                        // Repeater's own count update on separate passes, and
                        // a delete that removes lines can evaluate this
                        // between the two.
                        readonly property real lineY: {
                            const starts = lineNumberGutter.lineStarts
                            if (index >= starts.length)
                                return 0
                            return editor.positionToRectangle(starts[index]).y
                        }

                        x: lineNumberGutter.leftPadding
                        y: lineY
                        width: digitMetrics.width
                        horizontalAlignment: Text.AlignRight
                        text: index + 1
                        font: lineNumberGutter.numberFont
                        // Dimmed against the note's own font color rather than
                        // a fixed grey: the gutter has to stay legible on both
                        // a light and a dark note, and the note's font color is
                        // the only thing here that is already known to contrast
                        // with its background. 0.4 is the same dimming the
                        // context menu uses for a disabled row.
                        color: Qt.rgba(noteAppearance.fontColor.r,
                                       noteAppearance.fontColor.g,
                                       noteAppearance.fontColor.b,
                                       0.4)
                    }
                }
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
                // Shifted right to clear the gutter (0 when it is off, so
                // this is a no-op for an ordinary note). x, never y: the
                // editor-sits-at-content-(0,0) invariant ensureCursorVisible
                // and onContentYChanged both rely on is specifically about y
                // — see editorFlick's own comment above, which says so.
                x: lineNumberGutter.gutterWidth
                width: editorFlick.width - lineNumberGutter.gutterWidth
                height: editor.height

                TextArea {
                    id: editor

                    property bool ready: false

                    width: editorFlick.width - lineNumberGutter.gutterWidth
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
                    // The buffer holds literal markdown source and saves
                    // byte-for-byte what was typed; MarkdownHighlighter (owned
                    // by docHandler, attached to this TextArea's own
                    // textDocument) paints the formatting by scanning that
                    // text. MarkdownText mode did the opposite — it parsed
                    // markdown into a block/list/table object model and
                    // regenerated the text on save, which could not tell one
                    // blank line between paragraphs from none.
                    textFormat: TextEdit.PlainText
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

                    // A table's columns are aligned by stretching each cell to
                    // a width measured in the document's font (see
                    // MarkdownHighlighter::highlightTableRow). Changing the
                    // font emits no contentsChange, so nothing would otherwise
                    // re-run that measurement: every table would stay stretched
                    // to the OLD font's metrics until the next keystroke.
                    // Restyling on a font change is what keeps them aligned.
                    onFontChanged: docHandler.rehighlightMarkdown()

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

                    // A tap that lands inside a bullet's/checkbox's marker
                    // places the caret at the item's text instead, where the
                    // tap visibly aimed (bug found live: "the text cursor is
                    // inside of the checkbox, causing any content typed to
                    // break it"). The marker is invisible text under a drawn
                    // glyph, so EVERY position in it renders at that glyph's
                    // own left or right edge — measured on a fresh "- [ ] ",
                    // the box spans x=0..14.69 while its label starts at
                    // x=21.94, so every tap left of x≈20 (the box itself, and
                    // the gap beside it where an empty item's label looks like
                    // it starts) resolved into the marker. Typing there split
                    // it — "- [ b] " — and the checkbox silently vanished.
                    //
                    // Runs as its own tap handler rather than inside the
                    // toggle one above: these are separate rules that happen
                    // to share a gesture (the toggle only fires on the box,
                    // this fires on the whole marker, bullets included), and
                    // TapHandler's passive grab lets both observe the same tap
                    // without competing — the same reason the spell-check
                    // handler below is also its own.
                    //
                    // Deliberately a TAP, not a press: a press-drag is a
                    // selection, and a user dragging across a marker means to
                    // select it (to copy or delete the item wholesale), which
                    // snapping would silently prevent. TapHandler never fires
                    // for a drag, so that keeps working untouched.
                    // Deferred, and that is load-bearing rather than tidiness:
                    // a pointer handler sees the release BEFORE the item's own
                    // event handling, and TextArea then places the caret from
                    // the click itself — so assigning cursorPosition directly
                    // here is silently overwritten a moment later (measured:
                    // the caret stayed at 2, the raw position of a click on the
                    // box, instead of the 6 assigned). Qt.callLater runs it
                    // once the whole event has been delivered, still within the
                    // same frame, so the caret never renders in the marker.
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: (eventPoint) => {
                            const position = editor.positionAt(eventPoint.position.x, eventPoint.position.y)
                            const labelStart = docHandler.listLabelPositionAt(position)
                            if (labelStart !== position)
                                Qt.callLater(() => editor.cursorPosition = labelStart)
                        }
                    }

                    // Tap-to-suggest (feature request): tapping a misspelled (underlined)
                    // word selects it and surfaces suggestions via the properties above, which
                    // SelectionToolbar's own popup displays. Tapping a word that ISN'T
                    // misspelled (or tapping whitespace/a list marker) just clears any
                    // previously-shown suggestions — same non-exclusive pattern as the
                    // checkbox TapHandler above, so normal click-to-place-cursor still works
                    // unmodified either way.
                    //
                    // Checks isWordMisspelledAt() to decide whether to show the popup at all —
                    // NOT suggestions.length (bug found live: a genuinely misspelled word can
                    // still have zero suggestions, e.g. it's too garbled for the dictionary to
                    // find any close match; treating "no suggestions" as "not misspelled" made
                    // such words silently show no popup on tap, even though they were still
                    // correctly underlined). The popup still opens in that case — with an empty
                    // suggestion list, just the "Add to dictionary" action — rather than hiding
                    // entirely.
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: (eventPoint) => {
                            const position = editor.positionAt(eventPoint.position.x, eventPoint.position.y)
                            if (!docHandler.isWordMisspelledAt(position)) {
                                noteWindow.currentMisspelledWord = ""
                                return
                            }
                            const word = docHandler.wordAt(position)
                            editor.select(word.start, word.end)
                            // currentMisspelledWord is assigned LAST, not
                            // first — bug found live: SelectionToolbar's
                            // spellingSuggestionsPopup.wantsVisible depends
                            // only on currentMisspelledWord, so assigning it
                            // first used to synchronously trigger that
                            // popup's reposition-and-show logic immediately,
                            // while misspelledWordStart/End here still held
                            // their PREVIOUS value (or -1, on the very first
                            // tap of a session — hence a live
                            // "QTextCursor::setPosition: Position '-1' out
                            // of range" warning) and
                            // currentSpellingSuggestions still held the
                            // previous word's list. The popup positioned
                            // itself from that stale/garbage data — often
                            // well outside the note's visible area — with
                            // nothing left afterward to re-trigger a correct
                            // reposition once the real values landed a few
                            // lines below, so it flashed briefly at its old
                            // position and then snapped to the wrong one.
                            // Setting every dependency first means the
                            // popup only ever reacts once, with fully
                            // correct data already in place.
                            //
                            // Capped at settings.spellcheckMaxSuggestions
                            // (feature request: some words return 10+
                            // candidates, making the popup unwieldy) —
                            // truncated here, once, rather than in the popup
                            // itself, so every consumer of
                            // currentSpellingSuggestions sees the already-
                            // bounded list.
                            noteWindow.currentSpellingSuggestions =
                                docHandler.suggestionsForWordAt(position).slice(0, settings.spellcheckMaxSuggestions)
                            noteWindow.misspelledWordStart = word.start
                            noteWindow.misspelledWordEnd = word.end
                            noteWindow.currentMisspelledWord = word.text
                        }
                    }

                    // On-hover highlight over the checkbox glyph itself
                    // (feature request: "real checkboxes with on hover
                    // animations") — reuses the same InteractableHighlight
                    // every other bit of interactable chrome in this app
                    // uses (toolbar buttons, NoteHud glyphs, the
                    // scrollbar), so this reads as a real, clickable widget
                    // rather than plain text. docHandler.checkboxRectAt
                    // returns an empty rect off a checkbox glyph, which
                    // naturally collapses the highlight to nothing.
                    //
                    // Driven from mouseDragArea's own onPositionChanged/
                    // onExited below, NOT a HoverHandler declared here —
                    // confirmed live (and reproduced in the offscreen test
                    // harness) that mouseDragArea, sitting on top of editor
                    // in z-order with its own hoverEnabled: true, claims
                    // hover for this whole area first; a HoverHandler on an
                    // item underneath it never sees a single hover event.
                    InteractableHighlight {
                        id: checkboxHighlight
                        property rect glyphRect: Qt.rect(0, 0, 0, 0)
                        x: glyphRect.x
                        y: glyphRect.y
                        width: glyphRect.width
                        height: glyphRect.height
                        radius: 3
                        active: glyphRect.width > 0
                    }

                    DocumentHandler {
                        id: docHandler
                        document: editor.textDocument
                        // Code mode's spellcheck suppression rides on the
                        // SAME property the global preference uses, rather
                        // than a second gate inside DocumentHandler: "off
                        // because this note is code" and "off because you
                        // turned it off" are the same state as far as the
                        // highlighter is concerned, and one property means
                        // there is no way for two gates to disagree.
                        //
                        // Deliberately does not touch settings.spellcheckEnabled
                        // itself — that is the user's app-wide preference, and
                        // a note being code must not rewrite it for every
                        // other note.
                        spellcheckEnabled: settings.spellcheckEnabled
                            && !noteWindow.codeModeSuppressesSpellcheck
                        markdownDecorationEnabled: !noteWindow.codeModeSuppressesMarkdown
                        autoFormattingEnabled: !noteWindow.codeModeSuppressesAutoFormatting
                        spellcheckLanguage: settings.spellcheckLanguage
                        userDictionaryWords: settings.userDictionaryWords
                        // Controls how long a word must sit unedited before
                        // it's flagged as misspelled — see DocumentHandler
                        // ::spellcheckDebounceMs's own comment for the
                        // pending-edit mechanism this drives. Replaced an
                        // earlier cursor/selection-position-based exclusion
                        // attempt (bug found live: "the current word that
                        // has a popup currently loses the underline when the
                        // popup is on" — tapping a word to open its
                        // suggestion popup calls editor.select(), landing
                        // the cursor at the word's own end, indistinguishable
                        // by POSITION alone from still mid-typing it); this
                        // tracks the actual edit instead, so a tapped/
                        // selected word — no text changed — is simply never
                        // excluded in the first place.
                        spellcheckDebounceMs: settings.spellcheckDebounceMs
                        // Catches formatting changes that don't move the
                        // selection at all — e.g. clicking Bold on a held
                        // selection (persistentSelection keeps selectionStart/
                        // End exactly where they were) — see
                        // refreshFormatState()'s own comment above.
                        onFormattingChanged: noteWindow.refreshFormatState()
                        // Keeps misspelledWordOverlay's ListModel in sync
                        // via targeted updates rather than handing it a
                        // fresh QVariantList directly — see that model's
                        // own comment for why (avoids a full underline
                        // delegate rebuild on every keystroke).
                        onMisspelledWordRangesChanged: misspelledWordOverlay.syncMisspelledWordsModel()
                        // Same deal for the bullet/checkbox glyph overlay —
                        // see listMarkerOverlay's own model comment.
                        onListMarkerRangesChanged: listMarkerOverlay.syncListMarkersModel()
                        // And for the table grid — see tableGridOverlay.
                        onTableRangesChanged: tableGridOverlay.syncTablesModel()

                        // Debounced + atomic on NoteStore's side (~300ms after
                        // the last keystroke) — instant-feeling without a
                        // disk write per keystroke. Used to be TextArea's own
                        // onTextChanged, gated by comparing its `text`
                        // property against a last-seen value to filter out
                        // SpellCheckHighlighter's spurious setFormat()
                        // re-fires — but that property is Qt's built-in
                        // MarkdownText serialization, which collapses blank
                        // lines/trailing newlines, so a bare Enter keystroke
                        // could produce the exact same string as before and
                        // get mistaken for a no-op (bug found live: a
                        // newline wasn't saved until some later, non-newline
                        // edit happened to change that lossy string too).
                        // contentEdited() is emitted from C++ off the
                        // document's own real-edit-vs-formatting-only
                        // distinction instead (see its own doc comment),
                        // so it can't be fooled that way.
                        onContentEdited: {
                            if (!editor.ready)
                                return

                            noteWindow.currentMisspelledWord = ""
                            // Auto-list shorthand (feature request): typing
                            // "- "/"* "/"1. " at the start of an otherwise-
                            // plain line converts it into a real list,
                            // consuming the typed marker. This mutates the
                            // document, which re-enters onContentEdited
                            // recursively — harmless and self-terminating,
                            // since the shorthand text is already gone by
                            // the second pass, so the regex simply stops
                            // matching (and a no-op attempt never touches
                            // the document, so it never re-fires
                            // contentEdited at all).
                            docHandler.tryAutoConvertListShorthand(editor.cursorPosition)
                            // Keeps a numbered list's items sequential after
                            // ordinary editing that doesn't go through
                            // tryContinueList/toggleNumberedList at all —
                            // e.g. backspacing an item away or selecting one
                            // and deleting it outright (feature request).
                            docHandler.maintainListNumbering(editor.cursorPosition)
                            noteStore.saveNoteContent(noteWindow.noteId, docHandler.toMarkdown())
                        }
                    }

                    // Visual underline overlay for misspelled words (bug
                    // found live: QtQuick's TextArea does not render
                    // underline decorations applied via QSyntaxHighlighter::
                    // setFormat() at all — confirmed empirically via
                    // grabToImage + pixel inspection: a plain SingleUnderline
                    // format produced zero colored pixels, with or without
                    // Text.NativeRendering, while background/foreground color
                    // from the SAME kind of format rendered correctly. See
                    // DocumentHandler::misspelledWordRanges' own doc comment.
                    // A child of `editor` (not a sibling) specifically so it
                    // shares editor's own local coordinate space — the same
                    // space editor.positionToRectangle() already returns
                    // rects in — and therefore scrolls/repositions in
                    // lockstep with the text automatically, no extra
                    // scroll-tracking needed. Purely decorative: a plain Item
                    // has no input handling by default, so it never competes
                    // with the TapHandler/MouseAreas above for clicks.
                    Item {
                        id: misspelledWordOverlay
                        anchors.fill: parent
                        z: 1

                        // Bug found live ("every letter typed seems to
                        // recalculate the spellcheck, causing the underline
                        // to flicker" — persisted even after excluding the
                        // in-progress word and adding DocumentHandler's own
                        // per-block cache): docHandler.misspelledWordRanges
                        // reports ABSOLUTE document positions, so typing or
                        // deleting anywhere before a misspelled word
                        // genuinely shifts that word's own start/end by the
                        // edit's size — correct, not a bug, and true for
                        // EVERY misspelled word after the edit point, not
                        // just the one being typed. Feeding that plain
                        // QVariantList straight to a Repeater's `model` used
                        // to mean a single keystroke near the top of a note
                        // with several typos further down destroyed and
                        // recreated ALL of their underline delegates at
                        // once, every keystroke — visible as the whole
                        // note's underlines flickering, not just the word
                        // under the cursor. A ListModel kept in sync via
                        // targeted set()/append()/remove() calls (see
                        // syncMisspelledWordsModel() below) instead lets the
                        // Repeater reuse each delegate whose entry merely
                        // shifted position: only its bound start/end
                        // properties change, so its Canvas node persists and
                        // simply repositions, with nothing torn down.
                        ListModel {
                            id: misspelledWordsModel
                        }

                        // Diffs docHandler.misspelledWordRanges against the
                        // model's current contents by index (misspelled
                        // words don't reorder relative to each other, so
                        // index-based matching is exactly right for a pure
                        // shift; the rare case of a word being newly
                        // inserted or removed from the middle of the list
                        // just means everything after that point gets a
                        // set() too, which is still a plain property update,
                        // never a destroy/recreate).
                        function syncMisspelledWordsModel() {
                            const ranges = docHandler.misspelledWordRanges
                            const n = Math.min(misspelledWordsModel.count, ranges.length)
                            for (let i = 0; i < n; i++) {
                                const existing = misspelledWordsModel.get(i)
                                if (existing.start !== ranges[i].start || existing.end !== ranges[i].end)
                                    misspelledWordsModel.set(i, ranges[i])
                            }
                            while (misspelledWordsModel.count < ranges.length)
                                misspelledWordsModel.append(ranges[misspelledWordsModel.count])
                            while (misspelledWordsModel.count > ranges.length)
                                misspelledWordsModel.remove(misspelledWordsModel.count - 1)
                        }

                        Repeater {
                            id: misspelledWordRepeater
                            model: misspelledWordsModel

                            delegate: Canvas {
                                id: underlineCanvas
                                required property int start
                                required property int end

                                // Feature request: a solid, straight,
                                // configurable underline replacing the
                                // previous fixed wavy red squiggle. Mirrored
                                // into plain properties (rather than reading
                                // settings.* directly inside onPaint) purely
                                // so their own onChanged handlers can force
                                // a repaint — Canvas only repaints reactively
                                // when something IT explicitly triggers
                                // (onWidthChanged/onHeightChanged below, or
                                // these two handlers directly) changes; a
                                // Settings change on its own wouldn't
                                // otherwise be noticed.
                                property color underlineColor: settings.spellcheckUnderlineColor
                                property real underlineWidth: settings.spellcheckUnderlineWidth
                                onUnderlineColorChanged: requestPaint()
                                onUnderlineWidthChanged: requestPaint()

                                // Bug found live ("deleting text before a
                                // misspelled word leaves its underline
                                // drawn in the wrong place", then again as
                                // "adding text before an underline breaks
                                // it... inconsistent", reproduced in a
                                // multi-paragraph note): positionToRectangle()
                                // depends on the editor's actual rendered
                                // TEXT LAYOUT, which settles ASYNCHRONOUSLY
                                // — a frame or more after start/end update —
                                // so recomputing the instant start/end (or
                                // editor.width/contentWidth/contentHeight)
                                // change, via a plain reactive binding, can
                                // observe layout mid-settle and capture a
                                // transiently wrong (sometimes degenerate,
                                // zero-size) rect.
                                //
                                // recomputeWordRect() below is called
                                // IMPERATIVELY instead of being a binding —
                                // x/y/width/height are only ever ASSIGNED,
                                // never bound to a live expression — so a
                                // start/end change on its own does nothing
                                // to them until settleDebounce below
                                // actually fires and confirms layout has
                                // caught up. That's what fixes a second bug
                                // found live once the FIRST fix above
                                // landed ("a flash — the line disappears
                                // for a tiny bit before reappearing"): the
                                // old plain-binding version recomputed
                                // TWICE per edit (once immediately, reading
                                // stale layout and briefly going invisible;
                                // once more when settleDebounce's
                                // recomputeGeneration bump forced a second
                                // pass) — every edit visibly flickered the
                                // underline off and back on. Never
                                // recomputing until settleDebounce fires
                                // means the delegate just stays at its
                                // LAST KNOWN GOOD position/size the whole
                                // time in between: no intermediate state is
                                // ever painted, so nothing flashes.
                                function recomputeWordRect() {
                                    if (!editor.ready)
                                        return
                                    const startRect = editor.positionToRectangle(start)
                                    const endRect = editor.positionToRectangle(end)
                                    // Multi-line misspelled words shouldn't
                                    // happen in practice (words don't wrap
                                    // mid-word) — if one somehow does,
                                    // leave the last good rect in place
                                    // rather than draw a garbled cross-line
                                    // shape.
                                    if (startRect.y !== endRect.y)
                                        return
                                    // Qualified with the item's own id
                                    // (underlineCanvas.x, not a bare x)
                                    // deliberately — bug found live
                                    // (delegate.x read back as JS
                                    // `undefined` after an edit, in the
                                    // real AOT-compiled build only): the
                                    // qmlsc/qmlcachegen AOT compiler this
                                    // project's build already uses for
                                    // every .qml file (see the build's own
                                    // "Generating .rcc/qmlcache/..." step)
                                    // apparently resolves an unqualified
                                    // `x = ...` assignment inside a named
                                    // JS function differently than the
                                    // interpreted engine would — qualifying
                                    // every one of these against
                                    // underlineCanvas explicitly removes
                                    // any ambiguity for it to get wrong.
                                    underlineCanvas.x = startRect.x
                                    underlineCanvas.y = startRect.y
                                    underlineCanvas.width = Math.max(1, endRect.x - startRect.x)
                                    underlineCanvas.height = startRect.height
                                }

                                // EVERY trigger below — a fresh delegate's
                                // own creation, start/end changing, a
                                // resize, or any reflow that changes the
                                // document's own rendered bounds — routes
                                // through this ONE debounce, never calling
                                // recomputeWordRect() directly. Bug found
                                // live even after the imperative rewrite
                                // above: an EARLIER version of this file
                                // called recomputeWordRect() eagerly,
                                // straight from editor's
                                // onWidthChanged/onContentWidthChanged/
                                // onContentHeightChanged (reasoning: a
                                // resize's own contentWidth/contentHeight
                                // signal fires only once its layout is
                                // already final, so there's nothing to wait
                                // for) — but a REGULAR EDIT can ALSO change
                                // contentWidth/contentHeight as a side
                                // effect (e.g. the edited line becoming the
                                // document's new widest line), and THAT
                                // path has no such guarantee: it's racing
                                // the exact same not-yet-settled
                                // positionToRectangle() this whole redesign
                                // exists to wait out. Confirmed live: that
                                // eager path could read a stale rect and
                                // assign it immediately, reintroducing the
                                // "sometimes correct, sometimes not"
                                // flash/wrong-position bug from a second
                                // angle. Funneling every trigger through
                                // the same 32ms debounce costs nothing
                                // perceptible (resize responsiveness is
                                // still effectively instant to a human) and
                                // removes every remaining race — there is
                                // now exactly one code path that ever
                                // assigns x/y/width/height.
                                Timer {
                                    id: settleDebounce
                                    // A couple of frames' worth — comfortably
                                    // past Qt Quick's own async text-layout
                                    // catch-up for a single edit, confirmed
                                    // live against the multi-paragraph repro
                                    // referenced above.
                                    interval: 32
                                    onTriggered: underlineCanvas.recomputeWordRect()
                                }
                                onStartChanged: settleDebounce.restart()
                                onEndChanged: settleDebounce.restart()
                                Component.onCompleted: settleDebounce.restart()

                                Connections {
                                    target: editor
                                    function onWidthChanged() { settleDebounce.restart() }
                                    function onContentWidthChanged() { settleDebounce.restart() }
                                    function onContentHeightChanged() { settleDebounce.restart() }
                                }

                                visible: width > 1 && height > 0

                                // x/y alone don't need a repaint — onPaint
                                // draws in this Canvas's own LOCAL
                                // coordinate space (0,0 to width,height);
                                // a pure position change just moves the
                                // already-painted bitmap via the scene
                                // graph, same as any other Item.
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    if (width <= 1 || height <= 0)
                                        return
                                    ctx.strokeStyle = underlineColor
                                    ctx.lineWidth = underlineWidth
                                    ctx.beginPath()
                                    // lineWidth / 2 keeps the stroke's own
                                    // rendered pixels inside the delegate's
                                    // bounds rather than being centered
                                    // right on its bottom edge and half
                                    // clipped — matters more now that the
                                    // width is user-configurable (up to 8px)
                                    // instead of a fixed ~1px hairline.
                                    const baseline = height - underlineWidth / 2
                                    ctx.moveTo(0, baseline)
                                    ctx.lineTo(width, baseline)
                                    ctx.stroke()
                                }
                            }
                        }
                    }

                    // Draws the real bullets and checkboxes (feature request:
                    // "checkboxes should look like real checkboxes",
                    // "bulleted lists should look like real bullets").
                    //
                    // A QSyntaxHighlighter can only restyle characters — it
                    // cannot swap a "-" for a "•". So MarkdownHighlighter
                    // makes each marker's reserved slot transparent while
                    // keeping its width (see its reservedMarkerFormat) and
                    // this overlay paints the actual control into that slot.
                    // The buffer keeps the literal "- [x]" the user typed, so
                    // the file still saves byte-for-byte — the glyph exists
                    // only on screen, which is the whole point of the
                    // plain-text rewrite.
                    //
                    // Same shape, same constraints and the same hard-won
                    // fixes as misspelledWordOverlay above: a child of
                    // `editor` so it inherits editor's coordinate space and
                    // scrolls with the text for free, a ListModel synced by
                    // targeted updates so delegates are never needlessly
                    // rebuilt, and one debounced imperative recompute rather
                    // than a reactive binding on positionToRectangle().
                    Item {
                        id: listMarkerOverlay
                        anchors.fill: parent
                        z: 1

                        ListModel {
                            id: listMarkersModel
                        }

                        // See syncMisspelledWordsModel() above — identical
                        // reasoning: these are ABSOLUTE document positions, so
                        // typing anywhere above a list shifts every marker
                        // below it, and handing the raw QVariantList to the
                        // Repeater would destroy and rebuild every bullet in
                        // the note on those keystrokes.
                        function syncListMarkersModel() {
                            const ranges = docHandler.listMarkerRanges
                            const n = Math.min(listMarkersModel.count, ranges.length)
                            for (let i = 0; i < n; i++) {
                                const existing = listMarkersModel.get(i)
                                const incoming = ranges[i]
                                if (existing.start !== incoming.start || existing.end !== incoming.end
                                        || existing.kind !== incoming.kind
                                        || existing.checked !== incoming.checked)
                                    listMarkersModel.set(i, incoming)
                            }
                            while (listMarkersModel.count < ranges.length)
                                listMarkersModel.append(ranges[listMarkersModel.count])
                            while (listMarkersModel.count > ranges.length)
                                listMarkersModel.remove(listMarkersModel.count - 1)
                        }

                        Repeater {
                            id: listMarkerRepeater
                            model: listMarkersModel

                            delegate: Item {
                                id: markerGlyph
                                required property int start
                                required property int end
                                required property string kind
                                required property bool checked

                                // Byte-for-byte the mechanism underlineCanvas
                                // above documents at length: positionToRectangle()
                                // reflects a text layout that settles a frame or
                                // more AFTER start/end change, so x/y/width/height
                                // are only ever ASSIGNED from this one function,
                                // never bound to a live expression, and every
                                // trigger routes through the single debounce
                                // below. Read that comment before changing any of
                                // this — the failure modes (a glyph at a stale
                                // position, or a flash as it recomputes twice per
                                // edit) are subtle and were both found live.
                                function recomputeGlyphRect() {
                                    if (!editor.ready)
                                        return
                                    const startRect = editor.positionToRectangle(start)
                                    const endRect = editor.positionToRectangle(end)
                                    if (startRect.y !== endRect.y)
                                        return
                                    markerGlyph.x = startRect.x
                                    markerGlyph.y = startRect.y
                                    markerGlyph.width = Math.max(1, endRect.x - startRect.x)
                                    markerGlyph.height = startRect.height
                                }

                                Timer {
                                    id: glyphSettleDebounce
                                    interval: 32
                                    onTriggered: markerGlyph.recomputeGlyphRect()
                                }
                                onStartChanged: glyphSettleDebounce.restart()
                                onEndChanged: glyphSettleDebounce.restart()
                                Component.onCompleted: glyphSettleDebounce.restart()

                                Connections {
                                    target: editor
                                    function onWidthChanged() { glyphSettleDebounce.restart() }
                                    function onContentWidthChanged() { glyphSettleDebounce.restart() }
                                    function onContentHeightChanged() { glyphSettleDebounce.restart() }
                                }

                                visible: width > 1 && height > 0

                                // Sized off the LINE HEIGHT rather than the
                                // reserved slot's width, so both controls scale
                                // with the note's font instead of with whatever
                                // the current font happens to make a "-" or a
                                // "[" measure. Centered on the slot, which lets
                                // them overhang it slightly — harmless, because
                                // what sits on either side is the marker's own
                                // collapsed, invisible characters.
                                readonly property real glyphSize: Math.max(4, markerGlyph.height * 0.52)

                                // The checkbox alone is drawn larger than the
                                // shared glyphSize (feature request: "the
                                // display for checkboxes should be a little
                                // bigger"). Scaled here rather than by raising
                                // glyphSize itself, which a bullet's dot also
                                // derives from — nobody asked for bigger
                                // bullets, and a list's dots are already the
                                // right weight next to their text.
                                //
                                // Paired with kCheckboxSlotPercent in
                                // MarkdownHighlighter.cpp, which reserves the
                                // column this is centred in: that slot is
                                // proportionally wider than the bullet's by
                                // enough to fit this. Read that constant's
                                // comment before retuning either.
                                //
                                // Clamped to the slot's own width because the
                                // two are measured in different units and only
                                // AGREE for a font the pair was tuned against:
                                // the slot is a multiple of the "[" character's
                                // advance, while this is a fraction of the line
                                // height, and their ratio moves with whatever
                                // (user-configurable, per note) font the note is
                                // set to. A narrow-bracketed font would
                                // otherwise draw a box wider than its column —
                                // and since the slot starts at the editor's left
                                // edge, the overflow isn't an overhang, it's the
                                // box's left half clipped away (the exact defect
                                // that reserved slot was introduced to fix). A
                                // box that gives up a fraction of a pixel on
                                // such a font still looks like a checkbox; half
                                // a box does not.
                                readonly property real checkboxSize:
                                    Math.max(4, Math.min(markerGlyph.height * 0.62, markerGlyph.width))

                                // A real bullet: a filled dot in the text's own
                                // color, where the literal "-" would have been.
                                Rectangle {
                                    visible: markerGlyph.kind === "bullet"
                                    width: markerGlyph.glyphSize * 0.62
                                    height: width
                                    radius: width / 2
                                    anchors.centerIn: parent
                                    color: editor.color
                                }

                                // A real checkbox: a rounded outline that fills
                                // in and shows a tick when checked. Both colors
                                // are derived from the note's OWN font and
                                // background (which the user can override per
                                // note), rather than a fixed accent — a
                                // hardcoded blue would be invisible on a blue
                                // note.
                                Rectangle {
                                    id: checkboxBox
                                    visible: markerGlyph.kind === "checkbox"
                                    width: markerGlyph.checkboxSize
                                    height: width
                                    radius: Math.max(2, width * 0.22)
                                    anchors.centerIn: parent
                                    color: markerGlyph.checked ? editor.color : "transparent"
                                    border.width: Math.max(1, width * 0.1)
                                    border.color: markerGlyph.checked
                                        ? editor.color
                                        : Qt.rgba(editor.color.r, editor.color.g, editor.color.b, 0.55)

                                    // The tick. Drawn as two rotated bars rather
                                    // than a "✓" text glyph so it can't depend on
                                    // the note's user-chosen font actually having
                                    // one.
                                    Item {
                                        anchors.centerIn: parent
                                        width: checkboxBox.width * 0.62
                                        height: checkboxBox.height * 0.62
                                        visible: markerGlyph.checked
                                        opacity: 0.95

                                        Rectangle {
                                            width: parent.width * 0.42
                                            height: Math.max(1, checkboxBox.width * 0.12)
                                            radius: height / 2
                                            color: noteAppearance.backgroundColor
                                            x: parent.width * 0.02
                                            y: parent.height * 0.58
                                            transformOrigin: Item.Left
                                            rotation: 45
                                        }
                                        Rectangle {
                                            width: parent.width * 0.78
                                            height: Math.max(1, checkboxBox.width * 0.12)
                                            radius: height / 2
                                            color: noteAppearance.backgroundColor
                                            x: parent.width * 0.24
                                            y: parent.height * 0.72
                                            transformOrigin: Item.Left
                                            rotation: -45
                                        }
                                    }

                                    Behavior on color {
                                        ColorAnimation { duration: 90 }
                                    }
                                }
                            }
                        }
                    }

                    // Draws the grid over pipe tables (feature request:
                    // "tables don't display properly (at all, they just show
                    // the raw text)").
                    //
                    // The division of labour: MarkdownHighlighter collapses the
                    // pipes and stretches each cell so a column sits at one
                    // exact x in every row, and this draws the lines. Neither
                    // touches the buffer — a hand-typed ragged table renders as
                    // a neat grid and still saves byte-for-byte as typed, which
                    // is why the alignment is done with character advances
                    // rather than by padding the markdown itself.
                    //
                    // Same overlay constraints as the two above it: a child of
                    // `editor` for its coordinate space, a ListModel synced by
                    // targeted updates, and one debounced imperative recompute.
                    Item {
                        id: tableGridOverlay
                        anchors.fill: parent
                        z: 0

                        ListModel {
                            id: tablesModel
                        }

                        // A table's column/row positions are ARRAYS, and
                        // ListModel cannot hold one: it silently converts a
                        // nested JS array role into another ListModel (
                        // confirmed — the delegate received an object with
                        // .count and .agent instead of a list, so .length was
                        // undefined, no lines were computed, and the grid just
                        // never appeared). dynamicRoles: true does NOT change
                        // that. Carrying the arrays across as JSON strings and
                        // parsing them in the delegate sidesteps the conversion
                        // entirely, and keeps the C++ side reporting an
                        // ordinary list.
                        function tableEntry(table) {
                            return {
                                "tableTop": table.tableTop,
                                "tableBottom": table.tableBottom,
                                "headerRule": table.headerRule,
                                "columnBoundariesJson": JSON.stringify(table.columnBoundaries),
                                "rowStartsJson": JSON.stringify(table.rowStarts)
                            }
                        }

                        // Same reasoning as syncMisspelledWordsModel(): these
                        // are absolute document positions, so typing anywhere
                        // above (or inside) a table shifts them, and handing the
                        // raw QVariantList to the Repeater would tear down and
                        // rebuild the whole grid on every keystroke.
                        function syncTablesModel() {
                            const tables = docHandler.tableRanges
                            const n = Math.min(tablesModel.count, tables.length)
                            for (let i = 0; i < n; i++)
                                tablesModel.set(i, tableEntry(tables[i]))
                            while (tablesModel.count < tables.length)
                                tablesModel.append(tableEntry(tables[tablesModel.count]))
                            while (tablesModel.count > tables.length)
                                tablesModel.remove(tablesModel.count - 1)
                        }

                        Repeater {
                            model: tablesModel

                            delegate: Item {
                                id: tableGrid
                                required property string columnBoundariesJson
                                required property string rowStartsJson
                                required property int tableTop
                                required property int tableBottom
                                required property int headerRule

                                // Resolved pixel geometry, assigned only by
                                // recomputeGrid() below.
                                property var columnXs: []
                                property var rowYs: []
                                property real ruleY: -1
                                property real lineThickness: 1

                                readonly property color lineColor:
                                    Qt.rgba(editor.color.r, editor.color.g, editor.color.b, 0.28)

                                // Imperative + debounced for exactly the reason
                                // underlineCanvas documents at length:
                                // positionToRectangle() reflects a text layout
                                // that settles a frame or more after the
                                // positions change, so reading it from a
                                // reactive binding can capture a half-settled
                                // (sometimes degenerate) rect.
                                function recomputeGrid() {
                                    if (!editor.ready)
                                        return
                                    const topRect = editor.positionToRectangle(tableGrid.tableTop)
                                    const bottomRect = editor.positionToRectangle(tableGrid.tableBottom)
                                    if (bottomRect.y + bottomRect.height <= topRect.y)
                                        return

                                    tableGrid.x = 0
                                    tableGrid.y = topRect.y
                                    tableGrid.width = editor.width
                                    tableGrid.height = (bottomRect.y + bottomRect.height) - topRect.y

                                    const boundaries = JSON.parse(tableGrid.columnBoundariesJson)
                                    const xs = []
                                    for (let i = 0; i < boundaries.length; i++)
                                        xs.push(editor.positionToRectangle(boundaries[i]).x)
                                    tableGrid.columnXs = xs

                                    // Row lines sit at the TOP of each rendered
                                    // row; the table's bottom edge is added as
                                    // one extra line at full height.
                                    const starts = JSON.parse(tableGrid.rowStartsJson)
                                    const ys = []
                                    for (let i = 0; i < starts.length; i++)
                                        ys.push(editor.positionToRectangle(starts[i]).y - topRect.y)
                                    ys.push(tableGrid.height)
                                    tableGrid.rowYs = ys

                                    tableGrid.ruleY =
                                        editor.positionToRectangle(tableGrid.headerRule).y - topRect.y
                                }

                                Timer {
                                    id: gridSettleDebounce
                                    interval: 32
                                    onTriggered: tableGrid.recomputeGrid()
                                }
                                onTableTopChanged: gridSettleDebounce.restart()
                                onTableBottomChanged: gridSettleDebounce.restart()
                                onColumnBoundariesJsonChanged: gridSettleDebounce.restart()
                                onRowStartsJsonChanged: gridSettleDebounce.restart()
                                Component.onCompleted: gridSettleDebounce.restart()

                                Connections {
                                    target: editor
                                    function onWidthChanged() { gridSettleDebounce.restart() }
                                    function onContentWidthChanged() { gridSettleDebounce.restart() }
                                    function onContentHeightChanged() { gridSettleDebounce.restart() }
                                }

                                visible: tableGrid.height > 0 && tableGrid.columnXs.length > 1

                                // Column separators, including the table's own
                                // left and right edges (the header row's outer
                                // pipes are boundaries too).
                                Repeater {
                                    model: tableGrid.columnXs
                                    delegate: Rectangle {
                                        required property real modelData
                                        x: modelData
                                        y: 0
                                        width: tableGrid.lineThickness
                                        height: tableGrid.height
                                        color: tableGrid.lineColor
                                    }
                                }

                                // Row separators, plus the bottom edge.
                                Repeater {
                                    model: tableGrid.rowYs
                                    delegate: Rectangle {
                                        required property real modelData
                                        x: tableGrid.columnXs.length > 0 ? tableGrid.columnXs[0] : 0
                                        y: modelData
                                        width: tableGrid.columnXs.length > 1
                                            ? tableGrid.columnXs[tableGrid.columnXs.length - 1] - tableGrid.columnXs[0]
                                              + tableGrid.lineThickness
                                            : 0
                                        height: tableGrid.lineThickness
                                        color: tableGrid.lineColor
                                    }
                                }

                                // The header rule, drawn into the sliver of
                                // space the collapsed "| --- |" row leaves
                                // behind. Brighter than the other lines so the
                                // header reads as a header.
                                Rectangle {
                                    visible: tableGrid.ruleY >= 0
                                    x: tableGrid.columnXs.length > 0 ? tableGrid.columnXs[0] : 0
                                    y: tableGrid.ruleY
                                    width: tableGrid.columnXs.length > 1
                                        ? tableGrid.columnXs[tableGrid.columnXs.length - 1] - tableGrid.columnXs[0]
                                          + tableGrid.lineThickness
                                        : 0
                                    height: Math.max(1, tableGrid.lineThickness)
                                    color: Qt.rgba(editor.color.r, editor.color.g, editor.color.b, 0.6)
                                }
                            }
                        }
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

                    // Live preview: markdown punctuation ("**", "#", "`")
                    // collapses to zero width everywhere except the line the
                    // cursor is actually on, where it's revealed so it can be
                    // edited. -1 means "no line" and collapses every marker in
                    // the note.
                    //
                    // Gated on hasBeenEdited — "has this editor EVER held
                    // focus" — rather than on the live activeFocus, and the
                    // difference matters twice:
                    //
                    //  - A freshly opened note has never been touched, so it
                    //    reads as finished prose with nothing revealed, rather
                    //    than exposing whatever punctuation happens to sit on
                    //    line 0 (where the cursor starts).
                    //  - The selection toolbar is a SEPARATE WINDOW that takes
                    //    OS focus when clicked (see persistentSelection above,
                    //    which exists for that same reason). On live
                    //    activeFocus, clicking Bold would drop focus, collapse
                    //    the cursor line's markers and reflow the text the user
                    //    is aiming at — mid-click. Sticky state keeps the line
                    //    revealed across that, exactly as persistentSelection
                    //    keeps the selection.
                    //
                    // Safe to call on every cursor move, including the many
                    // that stay on one line: DocumentHandler resolves the
                    // position to a block number and the highlighter no-ops
                    // unless that block actually changed (see
                    // MarkdownHighlighter::setCursorBlockNumber).
                    property bool hasBeenEdited: false
                    function syncMarkerReveal() {
                        docHandler.setCursorPosition(hasBeenEdited ? cursorPosition : -1)
                    }
                    onCursorPositionChanged: syncMarkerReveal()
                    onActiveFocusChanged: {
                        if (activeFocus)
                            hasBeenEdited = true
                        syncMarkerReveal()
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

                    // Tab inserts four spaces (feature request) instead of
                    // TextArea's own default of a literal tab character —
                    // consuming the event here is what stops that default
                    // from ALSO firing afterward. Routed through
                    // docHandler.insertTabSpaces(), NOT this TextArea's own
                    // insert() method — confirmed empirically that insert()
                    // silently drops a pure-whitespace string under
                    // textFormat: TextEdit.MarkdownText (it live-
                    // renormalizes the text on every edit and treats
                    // insignificant whitespace as CommonMark noise to
                    // strip); a direct QTextCursor mutation bypasses that
                    // renormalization, same as every other formatting
                    // operation in this app already does.
                    Keys.onTabPressed: (event) => {
                        docHandler.insertTabSpaces(selectionStart, selectionEnd)
                        event.accepted = true
                    }

                    // Ctrl+C (feature request): routes through
                    // noteWindow.performCopy() instead of TextArea's own
                    // built-in Copy handling, so Ctrl+C gets the same
                    // whole-note-when-nothing-selected fallback and toast as
                    // the toolbar's Copy button — consuming the event here
                    // is what stops the built-in handling from ALSO firing
                    // afterward (same convention as Tab above).
                    Keys.onPressed: (event) => {
                        if (event.matches(StandardKey.Copy)) {
                            noteWindow.performCopy()
                            event.accepted = true
                        }
                    }

                    // Up on the topmost (visual, word-wrap-aware) line of the
                    // document has nowhere else to go, same for Down on the
                    // bottommost — rather than leaving the cursor stuck at
                    // its current column, jump it to that line's start/end,
                    // same target Home/End would resolve to. positionAt() at
                    // x=0/width on the cursor's own row mirrors
                    // belowContentArea's reprojectedPosition() below, which
                    // relies on the same nearest-x hit-testing to land on a
                    // line boundary.
                    //
                    // "Is the cursor on the topmost/bottommost line" is
                    // answered by asking whether the document's own first (0)
                    // / last (length) position sits on a different row than
                    // the cursor. Both of the two previous answers to that
                    // question are bugs this codebase has already shipped:
                    //
                    // - Comparing cursorRectangle against contentHeight, a
                    //   document-wide aggregate (reported: Down on a short,
                    //   single-visual-line bottom list item landed on the
                    //   line ABOVE instead of staying on its own; wrapped
                    //   items were unaffected). A marker glyph and the
                    //   plain-text cursor glyph beside it can lay out with
                    //   different line heights, so cursorRectangle's bottom
                    //   can disagree with contentHeight by a pixel or more
                    //   even on the true last line — but only on a marker's
                    //   own (first) line, which is why a wrapped item's last
                    //   sub-line (plain text throughout) never showed it.
                    //
                    // - Probing one pixel above/below the cursor's own row
                    //   (reported: Up on the first line of a paragraph
                    //   landed at that paragraph's start and stuck there —
                    //   every further Up repeated it, so the paragraph above
                    //   was unreachable). Paragraphs are laid out with a gap
                    //   between them (block margins, ~6px), and Qt resolves
                    //   a hit-test landing in that gap to the start of the
                    //   block BELOW it — i.e. back onto the cursor's own
                    //   row, which reads as "nothing above" mid-document.
                    //   Down never showed this: the gap resolving downward
                    //   is the direction Down wanted anyway.
                    //
                    // Comparing row against row keeps both sides on the same
                    // self-consistent metric — cursorRectangle and
                    // positionToRectangle share one code path, so two
                    // positions on one visual line always report an
                    // identical y — and never hit-tests a point outside a
                    // laid-out row, so neither quirk above can resurface.
                    //
                    // event.accepted defaults to true for these per-key
                    // Keys.onXxxPressed handlers (unlike the general
                    // Keys.onPressed) — off the boundary line it must be
                    // explicitly cleared, or every ordinary Up/Down press
                    // gets swallowed here and never reaches TextEdit's own
                    // default line-to-line cursor movement underneath.
                    // cursorRectangle.y is the cursor's TOP edge, which sits
                    // exactly on the boundary between this visual line and the
                    // one above — so positionAt() at that y can resolve to the
                    // line above instead of the cursor's own (bug found live:
                    // Down on a last line directly beneath a blank line jumped
                    // UP into the blank line rather than to its own end).
                    // Probing the cursor's vertical CENTRE is unambiguous: it
                    // can only ever fall inside the cursor's own line.
                    function cursorLineY() {
                        return cursorRectangle.y + cursorRectangle.height / 2
                    }
                    Keys.onUpPressed: (event) => {
                        if (positionToRectangle(0).y < cursorRectangle.y) {
                            event.accepted = false
                            return
                        }
                        moveCursorToBoundary(positionAt(0, cursorLineY()), event)
                    }
                    Keys.onDownPressed: (event) => {
                        if (positionToRectangle(length).y > cursorRectangle.y) {
                            event.accepted = false
                            return
                        }
                        moveCursorToBoundary(positionAt(width, cursorLineY()), event)
                    }

                    // Shared tail of the two boundary jumps above. Shift+Up /
                    // Shift+Down have to keep extending the selection they're
                    // in the middle of, so the jump can't just assign
                    // cursorPosition — that drops the anchor and collapses
                    // any selection (reported: Shift+Down on the bottom line
                    // threw the selection away and dumped the cursor at the
                    // note's bottom-right). moveCursorSelection() keeps the
                    // existing anchor, and starts a fresh selection from the
                    // cursor when there's nothing selected yet, which is
                    // exactly what Shift+End would have done on that line.
                    function moveCursorToBoundary(target, event) {
                        if (event.modifiers & Qt.ShiftModifier)
                            moveCursorSelection(target, TextEdit.SelectCharacters)
                        else
                            cursorPosition = target
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
            // the last line instead.
            //
            // Bugfix: height used to be editor.height - editor.contentHeight
            // (zero once real content already filled/exceeded the
            // viewport, "no fill space left to click into") — true before
            // "scroll past end" existed (see editorFlick.contentHeight's
            // own comment above), but that feature deliberately extends
            // editorFlick.contentHeight past editor.height so the last
            // line can reach the note's top, which opens up exactly that
            // "fill space" again once scrolled down: real dead space below
            // the text, just reachable only after scrolling. Sizing off
            // editorFlick.contentHeight instead covers that too — for a
            // short note (no scroll-past-end applied at all) the two are
            // identical, so this changes nothing there.
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

                // Tracks editorDragFilter's own x rather than sitting at 0,
                // which keeps reprojectedPosition()'s "both share the same
                // parent and x" assumption below literally true once code
                // mode's gutter shifts the editor right. Left at 0 it would
                // hand positionAt() an x offset by the gutter width, so a
                // click below the text would land the caret a few characters
                // off — and only in code mode.
                x: editorDragFilter.x
                y: editor.contentHeight
                width: editorFlick.width - lineNumberGutter.gutterWidth
                height: Math.max(0, editorFlick.contentHeight - editor.contentHeight)

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

        // Subtle scroll indicator/scrollbar (feature request): shown only
        // when the note's content overflows the visible area, tracking
        // position via editorFlick's own visibleArea (Flickable already
        // computes the normalized position/extent — no manual
        // contentY/contentHeight math needed). A hand-rolled Rectangle,
        // not QtQuick.Controls' ScrollBar — everything else in this file
        // (HUD, resize handles) is hand-rolled to control its exact look,
        // and a QQC2 control would pull in a platform style whose default
        // chrome wouldn't match or take the glass treatment below
        // consistently. Draggable (see scrollDrag below) — distinct from
        // dragging the note's own text content to scroll, which stays
        // disabled (see editorFlick.interactive's own comment) since that
        // conflicts with dragging to select text; the scrollbar handle
        // itself has no such conflict.
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
                //
                // Feature request: dragging the scrollbar handle itself
                // (distinct from dragging the note's own text content,
                // which stays disabled via editorFlick.interactive —
                // clarified live after an earlier, over-broad removal here
                // took out both) should move the scroll position, same as
                // any ordinary scrollbar.
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
            // A checkbox is a control you click, so the pointer says so
            // (feature request) — the same PointingHandCursor every other
            // clickable in the app uses. It reads checkboxHighlight.active
            // rather than re-hit-testing, so the cursor and the hover
            // highlight are driven by one answer and always agree: wherever
            // the box lights up, the hand shows, and clicking there really
            // does toggle (see checkboxRectUnderPointer). Ranked BELOW the
            // armed-autoscroll shapes, which own the cursor outright while
            // a scroll gesture is live, and above the ordinary I-beam.
            // The I-beam is additionally scoped to the right of code mode's
            // line-number gutter: the gutter is not editable text and an
            // I-beam over it would promise a caret that clicking there cannot
            // produce. Compared in this MouseArea's own coordinates, which is
            // why the gutter's x is mapped through mainFrame rather than used
            // raw — the gutter lives in editorFlick's scrolling content, this
            // area fills mainFrame, and the two origins differ by
            // editorFlick's margins. gutterWidth is 0 outside code mode, so
            // this collapses to the original editorFlick.x boundary there.
            cursorShape: middleScrollArmed
                ? (Math.abs(middleScrollCurrentY - middleScrollOriginY) > middleScrollDeadzone
                       ? Qt.SizeVerCursor : Qt.SizeAllCursor)
                : (checkboxHighlight.active
                       ? Qt.PointingHandCursor
                       : (mouseY >= editorFlick.y
                          && mouseX >= editorFlick.x + lineNumberGutter.gutterWidth
                              ? Qt.IBeamCursor : Qt.ArrowCursor))

            // The right button does two jobs, told apart by MOVEMENT alone
            // (feature request): drag the note if the pointer travels more
            // than dragThreshold while held, open the context menu if it
            // doesn't. Both are the standard convention for their own
            // gesture, and they can share the button precisely because a
            // stationary press and a drag are distinguishable before either
            // has to commit.
            //
            // That split only works because startSystemMove() is DEFERRED
            // until the threshold is crossed, rather than fired on press —
            // and it was already written that way, for its own reason:
            // startSystemMove() hands the whole gesture to the compositor the
            // instant it's called and the client never reliably gets its own
            // onReleased/onCanceled back afterward (see the position comment
            // near the top of this file). A press-time startSystemMove()
            // would swallow every stationary right-click's release too, so
            // there would be no click left to open a menu with.
            //
            // The flip side of that same fact is what makes onReleased's
            // check trustworthy: once a drag really has started, the release
            // belongs to the compositor and never arrives here at all.
            // rightDragStarted is still tested there rather than relying on
            // that — a gesture this file is not the sole owner of is not one
            // to make silent assumptions about.
            property real pressX: 0
            property real pressY: 0
            property bool rightDragStarted: false
            // Whether the in-flight right-button gesture is still eligible to
            // become a context-menu click. Distinct from !rightDragStarted,
            // and not redundant with it: a right-click whose only job was to
            // DISARM middle-click autoscroll (see onPressed) returns early
            // without starting a menu gesture at all, while rightDragStarted
            // still reads false from whatever gesture preceded it — so
            // !rightDragStarted alone would pop a menu out of the very click
            // the user meant as a cancel.
            property bool rightClickCandidate: false
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
                    rightClickCandidate = false
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
                    rightClickCandidate = true
                }
            }
            onReleased: (mouse) => {
                if (mouse.button === Qt.MiddleButton) {
                    middleButtonHeld = false
                    if (middleScrollDraggedWhileHeld)
                        disarmMiddleScroll()
                    return
                }
                // A right-button release that reaches this file at all is a
                // release the compositor never took — i.e. one that never
                // became a window drag. See the gesture comment above for why
                // rightClickCandidate, not !rightDragStarted, is the test.
                //
                // Reported in SCREEN coordinates: this MouseArea fills
                // mainFrame, which fills the window's own content item, so
                // mouse.x/y here ARE window coordinates and need only the
                // note's own origin added. noteOrigin() is what makes that
                // survive a drag — see its comment for why mapToGlobal()
                // would not.
                if (mouse.button === Qt.RightButton && rightClickCandidate) {
                    rightClickCandidate = false
                    const origin = noteWindow.noteOrigin()
                    noteWindow.contextMenuRequested(origin.x + mouse.x, origin.y + mouse.y)
                }
            }
            onPositionChanged: (mouse) => {
                middleScrollCurrentY = mapToItem(editorFlick, mouse.x, mouse.y).y

                // Checkbox hover highlight (see checkboxHighlight's own
                // comment): this MouseArea is the one that actually
                // receives hover for the whole editor area, so it's what
                // has to drive the highlight rather than a handler on
                // editor itself.
                const posInEditor = mapToItem(editor, mouse.x, mouse.y)
                checkboxHighlight.glyphRect =
                    noteWindow.checkboxRectUnderPointer(posInEditor.x, posInEditor.y)

                if (middleButtonHeld && !middleScrollDraggedWhileHeld
                        && Math.abs(middleScrollCurrentY - middleScrollOriginY) > dragThreshold) {
                    middleScrollDraggedWhileHeld = true
                }
                if (middleScrollArmed || rightDragStarted || !(mouse.buttons & Qt.RightButton))
                    return
                if (Math.abs(mouse.x - pressX) > dragThreshold || Math.abs(mouse.y - pressY) > dragThreshold) {
                    rightDragStarted = true
                    // This gesture is a drag now and can never go back to
                    // being a menu click — cleared here rather than relying
                    // on the release never arriving (which is true, but is
                    // the compositor's behavior to guarantee, not ours).
                    rightClickCandidate = false
                    noteWindow.startSystemMove()
                }
            }
            onExited: checkboxHighlight.glyphRect = Qt.rect(0, 0, 0, 0)
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
        // modal-ish pointer mode — and otherwise closes the find/replace
        // panel.
        //
        // The panel is its own window and owns Escape while IT has focus
        // (Qt.WindowShortcut is scoped per window, so this one is not even a
        // candidate for a key typed into the panel). This branch is what
        // covers Escape while focus is in the NOTE — most obviously having
        // just clicked into the text to look at a match (bug found live:
        // Escape did nothing at all there, since only the panel's own fields
        // were listening).
        //
        // Autoscroll wins when both are live: it's a modal-ish pointer mode
        // the user is actively in, and it can only be disarmed by Escape or
        // another click, whereas the panel also has a visible close button.
        Shortcut {
            sequence: "Escape"
            onActivated: {
                if (mouseDragArea.middleScrollArmed)
                    mouseDragArea.disarmMiddleScroll()
                else
                    noteWindow.findCloseRequested()
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
            onDuplicateRequested: noteWindow.duplicateNote()
            onSearchRequested: noteWindow.findRequested(false)
            onCloseRequested: noteWindow.deleteNote()
        }

        // Small glass-styled "Copied" confirmation (feature request): shown
        // briefly by performCopy() above, whether triggered from the
        // toolbar's Copy button or Ctrl+C — needed most for the
        // nothing-selected case (copies the whole note silently otherwise,
        // with no other visible change at all to confirm anything
        // happened), but shown for every copy for consistency. z above
        // noteHud (3): must stay readable even while the HUD is showing.
        Rectangle {
            id: copyToast
            z: 4
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 10
            width: copyToastLabel.implicitWidth + 20
            height: copyToastLabel.implicitHeight + 10
            radius: 8
            color: Qt.rgba(0, 0, 0, 0.6)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.18)
            opacity: 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 150 } }

            function show() {
                copyToast.opacity = 1
                copyToastHideTimer.restart()
            }

            // One-shot, restarted by each real copy (never a recurring
            // background poll): fires once, 900ms after the most recent
            // show(), to fade the toast back out.
            Timer {
                id: copyToastHideTimer
                interval: 900
                onTriggered: copyToast.opacity = 0
            }

            GlassOverlay {
                anchors.fill: parent
                cornerRadius: parent.radius
                tintColor: noteAppearance.glassTintColor
                sheenOpacity: noteAppearance.glassSheenOpacity
                ringCount: noteAppearance.glassSheenStrength
                sheenSize: noteAppearance.glassSheenSize
            }

            Text {
                id: copyToastLabel
                anchors.centerIn: parent
                text: "Copied"
                color: "#ffffff"
                font.pixelSize: 12
            }
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
