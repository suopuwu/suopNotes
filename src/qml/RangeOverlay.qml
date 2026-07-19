import QtQuick

// Shared backing for NoteWindow's range-driven overlays — the misspelled-word
// underlines, the drawn list markers, and the inline images. Each is fed a
// QVariantList of {start, end, …} ranges from DocumentHandler (absolute document
// positions) and draws one delegate per range through its own Repeater bound to
// this overlay's `model`.
//
// This owns the one piece those overlays hand-copied identically: a ListModel
// kept in sync with the ranges by a TARGETED per-index diff (set/append/remove),
// rather than by reassigning the model wholesale. That distinction is
// load-bearing, not cosmetic (bug found live): because the positions are
// absolute, an edit anywhere shifts the start/end of every range after it —
// correct, but handing that raw list to a Repeater's `model` destroyed and
// rebuilt EVERY delegate on every keystroke (the whole note's underlines/bullets
// visibly flickering). The diff instead updates only the bound properties of a
// delegate whose entry merely moved, so its node persists and simply
// repositions, with nothing torn down.
//
// The host keeps its own Repeater + delegate — including whatever async-layout
// settle timing the delegate needs to place itself against
// editor.positionToRectangle — and just binds `model: <thisOverlay>.model`. (The
// table-grid overlay is deliberately NOT built on this: its entries aren't a
// flat per-item start/end list, so it keeps its own sync.)
Item {
    id: overlay

    // Bind to docHandler.<something>Ranges. Every key of each entry is compared
    // when deciding whether a delegate's row actually changed.
    property var ranges: []

    // The host binds its Repeater to this.
    readonly property alias model: rangeModel

    ListModel { id: rangeModel }

    function sync() {
        const incoming = overlay.ranges
        const shared = Math.min(rangeModel.count, incoming.length)
        for (let i = 0; i < shared; i++) {
            const existing = rangeModel.get(i)
            let differs = false
            for (const key in incoming[i]) {
                if (existing[key] !== incoming[i][key]) {
                    differs = true
                    break
                }
            }
            // set() updates the existing delegate's bound properties in place —
            // no destroy/recreate — which is the whole point; the guard just
            // avoids even that when nothing about this row changed.
            if (differs)
                rangeModel.set(i, incoming[i])
        }
        while (rangeModel.count < incoming.length)
            rangeModel.append(incoming[rangeModel.count])
        while (rangeModel.count > incoming.length)
            rangeModel.remove(rangeModel.count - 1)
    }

    onRangesChanged: overlay.sync()
    Component.onCompleted: overlay.sync()
}
