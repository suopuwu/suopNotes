import QtQml
import QtQuick
import QtQuick.Controls
import QtTest
import SuopNotesUi

// DocumentHandler is the editor's orchestration layer: it wraps a TextArea's
// QTextDocument and reports the marker/table/image ranges QML draws overlays
// for, and it owns the spellcheck pending-edit debounce. These tests exercise
// that orchestration through a real TextArea (the only way to get a
// QQuickTextDocument), keeping the deterministic range reporting separate from
// the spell-dictionary-dependent behavior, which is guarded so it never flakes
// where no dictionary is installed.
Item {
    id: root
    width: 400
    height: 300

    TextArea {
        id: editorA
        textFormat: TextEdit.PlainText
    }

    TextArea {
        id: editorB
        textFormat: TextEdit.PlainText
    }

    DocumentHandler {
        id: handler
    }

    SignalSpy {
        id: contentEditedSpy
        target: handler
        signalName: "contentEdited"
    }

    TestCase {
        name: "DocumentHandler"
        when: windowShown

        function init() {
            editorA.text = ""
            editorB.text = ""
            handler.document = null
            handler.spellcheckEnabled = false
            handler.autoFormattingEnabled = true
            handler.spellcheckDebounceMs = 300
            contentEditedSpy.clear()
        }

        // --- contentEdited means "the user changed something" ---
        //
        // QML hangs the save-and-auto-format pass off contentEdited, so what
        // this signal does and does not cover decides whether loading a note can
        // write straight back out again.

        // Counts are asserted as "at least one", never as an exact number: a
        // bulk text assignment lands as many separate document changes, so the
        // question that matters is whether an edit is reported at all.
        function test_typing_reports_a_content_edit() {
            handler.document = editorA.textDocument
            contentEditedSpy.clear()

            editorA.text = "typed by the user"

            verify(contentEditedSpy.count > 0)
        }

        function test_loading_content_does_not_report_a_content_edit() {
            handler.document = editorA.textDocument
            contentEditedSpy.clear()

            handler.setMarkdown("loaded from storage")

            // A load is not an edit. Reporting it as one makes the app save the
            // text it just loaded — invisible for a local file (identical bytes)
            // but an echo loop for content arriving from anywhere slower, and it
            // re-runs auto-format over text another writer just produced.
            compare(contentEditedSpy.count, 0)
            compare(handler.toMarkdown(), "loaded from storage")
        }

        function test_editing_after_a_load_still_reports_a_content_edit() {
            handler.document = editorA.textDocument
            handler.setMarkdown("loaded from storage")
            contentEditedSpy.clear()

            // The suppression lasts exactly as long as the load — the very next
            // real edit must be reported, or nothing would ever get saved again.
            editorA.text = "then edited"

            verify(contentEditedSpy.count > 0)
        }

        function test_a_second_load_is_still_not_an_edit() {
            handler.document = editorA.textDocument
            handler.setMarkdown("first load")
            editorA.text = "an edit in between"
            contentEditedSpy.clear()

            handler.setMarkdown("second load")

            compare(contentEditedSpy.count, 0)
        }

        // --- list marker ranges ---

        function test_a_bullet_line_reports_one_bullet_marker_range() {
            editorA.text = "- milk"
            handler.document = editorA.textDocument
            var ranges = handler.listMarkerRanges
            compare(ranges.length, 1)
            compare(ranges[0].kind, "bullet")
            compare(ranges[0].checked, false)
        }

        function test_a_checkbox_line_reports_its_checked_state() {
            editorA.text = "- [ ] todo\n- [x] done"
            handler.document = editorA.textDocument
            var ranges = handler.listMarkerRanges
            compare(ranges.length, 2)
            compare(ranges[0].kind, "checkbox")
            compare(ranges[0].checked, false)
            compare(ranges[1].kind, "checkbox")
            compare(ranges[1].checked, true)
        }

        function test_a_numbered_line_reports_no_marker_range_its_marker_stays_real_text() {
            editorA.text = "1. first\n2. second"
            handler.document = editorA.textDocument
            compare(handler.listMarkerRanges.length, 0)
        }

        // --- image ranges ---

        function test_a_whole_line_image_reports_its_span_alt_and_src() {
            editorA.text = "![a cat](cat.png)"
            handler.document = editorA.textDocument
            var images = handler.imageRanges
            compare(images.length, 1)
            compare(images[0].alt, "a cat")
            compare(images[0].src, "cat.png")
        }

        function test_a_line_with_text_around_the_image_is_not_a_whole_line_image() {
            editorA.text = "look ![a cat](cat.png) here"
            handler.document = editorA.textDocument
            compare(handler.imageRanges.length, 0)
        }

        // --- table ranges ---

        function test_a_markdown_table_reports_one_table_region() {
            editorA.text = "| a | b |\n| - | - |\n| 1 | 2 |"
            handler.document = editorA.textDocument
            var tables = handler.tableRanges
            compare(tables.length, 1)
            // header + one body row = two drawn row starts (the delimiter row
            // collapses into the header rule and is not counted).
            compare(tables[0].rowStarts.length, 2)
        }

        // --- setDocument re-derives ranges for whatever it's pointed at ---

        function test_switching_to_a_different_document_recomputes_from_the_new_one() {
            editorA.text = "- a bullet"
            handler.document = editorA.textDocument
            compare(handler.listMarkerRanges.length, 1)

            editorB.text = "just prose, no markers at all"
            handler.document = editorB.textDocument
            compare(handler.listMarkerRanges.length, 0)
        }

        function test_clearing_the_document_reports_empty_ranges_without_crashing() {
            editorA.text = "- a bullet"
            handler.document = editorA.textDocument
            compare(handler.listMarkerRanges.length, 1)

            handler.document = null
            compare(handler.listMarkerRanges.length, 0)
            compare(handler.imageRanges.length, 0)
            compare(handler.tableRanges.length, 0)
        }

        // --- #5 regression: a stale pending-edit position must not carry across
        //     a document swap and suppress the new document's own underline ---

        function test_a_stale_pending_edit_does_not_suppress_the_next_documents_underline() {
            // Keep any pending edit "fresh" for the whole test so the debounce
            // timer can't clear it out from under us between the steps.
            handler.spellcheckDebounceMs = 100000
            handler.spellcheckEnabled = true

            // Probe: with no edit in play, a freshly-assigned document flags its
            // misspelled word iff a real dictionary is available here. If not,
            // there is nothing to suppress and the regression can't be shown.
            editorB.text = "zxqwer"
            handler.document = editorB.textDocument
            if (handler.misspelledWordRanges.length === 0)
                skip("no spell dictionary available in this test environment")

            // Create a stale pending edit in document A: pointing the handler at
            // A and then typing records an edit position in A's coordinate space
            // (A's own just-typed word is correctly suppressed as a word-in-
            // progress, so A reports nothing right now).
            handler.document = editorA.textDocument
            editorA.text = "zxqwer"
            compare(handler.misspelledWordRanges.length, 0)

            // Switch to B, whose identical misspelled word sits across that same
            // stale offset. The word is a settled fact in B, so it must be
            // flagged immediately — it was not, back when setDocument left the
            // previous document's pending-edit position in place.
            handler.document = editorB.textDocument
            compare(handler.misspelledWordRanges.length, 1)
        }
    }
}
