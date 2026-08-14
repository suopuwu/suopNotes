import QtQuick

// TickTick "synced N ago" footer (Decision, drain 57a85c8d, modification
// 1/2) — the deliberate, unambiguous refresh gesture for the aggregated task
// note. Styled and behaved as a CONTROL, not a passive status label, or
// nobody discovers it is clickable: an underline on hover and a
// pointing-hand cursor, same convention NoteHud's own glyphs use.
//
// Unlike NoteWindow's focus-driven refresh, clicking this text is NEVER
// suppressed — not by focus, not by a pending edit, not by the debounce that
// gates the implicit path. It is the one place a refresh is always honored
// immediately, which is the whole point of having it: arriving at the note
// might be "just glancing", but a click on text that says "synced 4m ago"
// can only mean "refresh".
//
// Owned by the module: this file only exists in builds that compile TickTick
// (see the guarded qt_target_qml_sources in src/CMakeLists.txt), and
// NoteWindow.qml reaches it through a Loader by relative URL whose `active`
// already implies the module is present.
//
// _now is re-stamped on real events only — a pull finishing, a push being
// confirmed, this window gaining OS focus (NoteWindow calls bumpNow()), and
// the note being hovered — NEVER on a repeating Timer. See CLAUDE.md: a
// ticking "N ago" label would be exactly the second sanctioned-timer that
// file explicitly forbids. Between those events the text is allowed to read
// slightly stale (e.g. "synced 1m ago" for several minutes) rather than pay
// for a clock nobody asked for — and since hovering is itself one of the
// re-stamp events, glancing at the label is what keeps it fresh.
Text {
    id: footer
    objectName: "tickTickSyncFooter"

    // The NoteWindow hosting this footer, set by the Loader that creates it.
    // Everything the footer needs from the note — isTickTickTaskNote, the
    // relative-time label, the appearance palette — funnels through this one
    // reference, so the component needs no other knowledge of the window.
    property var noteWindow: null

    visible: noteWindow !== null && noteWindow.isTickTickTaskNote
    font.pixelSize: 11
    font.underline: footerHover.hovered
    color: noteWindow !== null
        ? Qt.rgba(noteWindow.noteAppearance.fontColor.r,
                  noteWindow.noteAppearance.fontColor.g,
                  noteWindow.noteAppearance.fontColor.b, 0.65)
        : "transparent"

    // Bumped on the events described above; reading it (rather than
    // `new Date()` directly in the text binding) is what makes this
    // property, and not a timer, the thing that drives a refresh.
    property var _now: new Date()

    function bumpNow() {
        footer._now = new Date()
    }

    text: (noteWindow !== null && noteWindow.isTickTickTaskNote)
        ? noteWindow.tickTickSyncLabel(footer._now)
        : ""

    // The "note being hovered" re-stamp event from the header comment:
    // noteWindow.hovered is the whole-note mouse-presence alias the HUD
    // reveal already runs on, so glancing at the note — not just at this
    // tiny label — is enough to bring the text up to date. Without this the
    // label could sit on "synced just now" indefinitely: the hourly pull
    // both re-stamps _now and resets lastSyncTime, so the only readings it
    // ever produced while idle were fresh ones.
    Connections {
        target: footer.noteWindow
        function onHoveredChanged() {
            if (footer.noteWindow.hovered)
                footer.bumpNow()
        }
    }

    Connections {
        target: tickTick !== null ? tickTick.sync : null
        function onPullFinished() { footer.bumpNow() }
        // A push is a sync too (see TickTickSync::lastSyncTime): these are
        // the moments the user's own typing is confirmed delivered.
        // lastSyncTime changing already re-evaluates the text binding by
        // itself — bumping _now alongside it is what keeps the two halves of
        // the subtraction measured from the same instant, so the answer is
        // "just now" rather than "however stale _now was".
        function onPushFinished() { footer.bumpNow() }
        function onTaskCompleted() { footer.bumpNow() }
    }
    onVisibleChanged: if (visible) bumpNow()

    HoverHandler {
        id: footerHover
        cursorShape: Qt.PointingHandCursor
        onHoveredChanged: if (hovered) footer.bumpNow()
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: if (tickTick !== null) tickTick.sync.pullNow()
    }
}
