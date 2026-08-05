import QtQml
import QtQuick

// Keeps the caret out of a list item's marker text.
//
// A bullet's "- " and a checkbox's "- [ ] " are literal document characters
// that MarkdownHighlighter collapses to zero width and draws a glyph over (see
// MarkdownList::Marker::glyphStart). Every position inside one therefore
// renders at that glyph's own left or right edge, so walking through a
// checkbox with Left is six keypresses in which the caret visibly does not
// move — reported as "the user can use the arrow keys to move into the
// checkboxes, but the display doesn't change or anything".
//
// A separate object rather than more clauses inside NoteWindow's editor
// because it is one self-contained rule, and because it is OFF by default: an
// ordinary note leaves the caret free to enter a marker, since that text is
// the user's own markdown to edit. Only the TickTick task note turns it on —
// there the marker is furniture the sync layer owns, not content.
//
// Two halves, because "never inside a marker" is not one rule:
//  - handleKeyPress() owns the keys whose correct target is BACKWARDS. Left at
//    an item's label start means "leave this line"; snapping it forward would
//    pin the caret to the label and make the line above unreachable. Home
//    means the item's text, not the line's first character.
//  - snapCaretOutOfMarker() catches everything else — Right off the end of the
//    line above, Up/Down landing at a column left of the label, Ctrl+Home, a
//    programmatic assignment — by moving the caret FORWARDS to the label.
QtObject {
    id: guard

    // The TextArea whose caret is guarded, and the DocumentHandler wrapping
    // its document. Inert until both are set.
    property var editor: null
    property var docHandler: null

    // Off by default: this changes what the arrow keys do, so a caller has to
    // ask for it.
    property bool enabled: false

    readonly property bool ready:
        guard.enabled && guard.editor !== null && guard.docHandler !== null

    // Where the caret's own line's text begins — past its "- [ ] ", or the
    // line's start when it has no marker at all.
    function labelStart(position) {
        return guard.docHandler.listLabelStartAt(position)
    }

    // The end of the line above `position`, or -1 when there is none. The
    // newline terminating the previous line sits at exactly that index, so the
    // index IS the caret slot after that line's last character.
    function previousLineEnd(position) {
        return guard.editor.text.lastIndexOf("\n", position - 1)
    }

    // Inside a marker == strictly before the point this line's own text starts.
    function isInsideMarker(position) {
        return position < guard.labelStart(position)
    }

    // Extends the selection instead of assigning when Shift is held — same
    // convention and same reason as NoteWindow's own moveCursorToBoundary:
    // assigning cursorPosition drops the anchor and throws away a selection
    // the user is in the middle of making.
    function moveCaret(target, event) {
        if (event.modifiers & Qt.ShiftModifier)
            guard.editor.moveCursorSelection(target, TextEdit.SelectCharacters)
        else
            guard.editor.cursorPosition = target
    }

    // True when it consumed the key. Called from NoteWindow's Keys.onPressed,
    // which Qt delivers before TextArea's own default handling, so consuming
    // here is what stops that default from also running.
    function handleKeyPress(event) {
        if (!guard.ready)
            return false

        const position = guard.editor.cursorPosition
        const label = guard.labelStart(position)

        if (event.key === Qt.Key_Left && position <= label) {
            // Everything leftwards from here is marker. Skip the whole thing
            // in one step and land on the line above. Ctrl+Left is included
            // deliberately: a word-left jump from the label lands on "[" or
            // "]" — inside the marker — and the forward snap would bounce it
            // straight back, leaving the line above unreachable by word too.
            const above = guard.previousLineEnd(position)
            if (above >= 0)
                guard.moveCaret(above, event)
            else if (position < label)
                guard.editor.cursorPosition = label
            // On the first line there is nowhere above to go, so the key is
            // swallowed rather than left to walk into the marker.
            return true
        }

        if (event.key === Qt.Key_Home && !(event.modifiers & Qt.ControlModifier)
                && position > label) {
            // Home means the start of what the user can read and type, which
            // in an item is its label. Ctrl+Home (document start) falls
            // through on purpose: it lands in line 0's marker and
            // snapCaretOutOfMarker moves it to that line's label.
            guard.moveCaret(label, event)
            return true
        }

        return false
    }

    // The forward half. Called from NoteWindow's onCursorPositionChanged, so
    // it runs after every caret move however it was made.
    //
    // Skipped while there is a selection: assigning cursorPosition collapses
    // one, and a drag across a marker is a deliberate selection of it — the
    // same carve-out the click-snap TapHandler in NoteWindow already makes.
    //
    // Re-entrant by construction and harmless: the assignment re-emits
    // cursorPositionChanged, and the second pass finds the caret at the label
    // and does nothing.
    function snapCaretOutOfMarker() {
        if (!guard.ready || guard.editor.selectedText.length > 0)
            return
        const position = guard.editor.cursorPosition
        if (guard.isInsideMarker(position))
            guard.editor.cursorPosition = guard.labelStart(position)
    }
}
