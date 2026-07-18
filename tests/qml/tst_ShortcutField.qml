import QtQml
import QtQuick
import QtTest
import SuopNotesUi

// ShortcutField turns a key press into a QKeySequence-parsable string like
// "Ctrl+Alt+T". These tests document that translation directly (keyToString,
// sequenceStringFor, isModifierOnly are pure and callable on the component)
// and then the click-to-record flow end to end. Nothing here writes to
// `sequence` — the component only ever emits recorded(); the host persists it.
Item {
    id: root
    width: 200
    height: 120

    ShortcutField {
        id: field
    }

    SignalSpy {
        id: recordedSpy
        target: field
        signalName: "recorded"
    }

    TestCase {
        name: "ShortcutField"
        when: windowShown

        function init() {
            field.sequence = ""
            field.listening = false
            recordedSpy.clear()
        }

        // --- keyToString: how each individual key becomes its token ---

        function test_letter_keys_map_to_their_uppercase_character() {
            compare(field.keyToString(Qt.Key_A), "A")
            compare(field.keyToString(Qt.Key_T), "T")
            compare(field.keyToString(Qt.Key_Z), "Z")
        }

        function test_digit_keys_map_to_their_character() {
            compare(field.keyToString(Qt.Key_0), "0")
            compare(field.keyToString(Qt.Key_7), "7")
            compare(field.keyToString(Qt.Key_9), "9")
        }

        function test_function_keys_map_to_F_plus_their_number() {
            compare(field.keyToString(Qt.Key_F1), "F1")
            compare(field.keyToString(Qt.Key_F5), "F5")
            compare(field.keyToString(Qt.Key_F12), "F12")
        }

        function test_named_keys_use_their_QKeySequence_spelling() {
            compare(field.keyToString(Qt.Key_Escape), "Esc")
            compare(field.keyToString(Qt.Key_Tab), "Tab")
            compare(field.keyToString(Qt.Key_Backspace), "Backspace")
            compare(field.keyToString(Qt.Key_Return), "Return")
            // Enter (keypad) and Return produce the same token on purpose.
            compare(field.keyToString(Qt.Key_Enter), "Return")
            compare(field.keyToString(Qt.Key_Space), "Space")
            compare(field.keyToString(Qt.Key_Delete), "Delete")
        }

        function test_punctuation_keys_map_to_their_symbol() {
            compare(field.keyToString(Qt.Key_Comma), ",")
            compare(field.keyToString(Qt.Key_Period), ".")
            compare(field.keyToString(Qt.Key_Minus), "-")
            compare(field.keyToString(Qt.Key_Slash), "/")
            compare(field.keyToString(Qt.Key_Backslash), "\\")
        }

        function test_plus_and_equal_both_map_to_plus() {
            // Documenting a known ambiguity: "Shift+=" and a keypad "+" are
            // indistinguishable in the produced token — both become "+".
            compare(field.keyToString(Qt.Key_Plus), "+")
            compare(field.keyToString(Qt.Key_Equal), "+")
        }

        function test_a_key_the_field_cannot_spell_yields_an_empty_string() {
            compare(field.keyToString(Qt.Key_Home), "")
        }

        // --- isModifierOnly ---

        function test_the_four_modifier_keys_are_recognised_as_modifier_only() {
            verify(field.isModifierOnly(Qt.Key_Control))
            verify(field.isModifierOnly(Qt.Key_Shift))
            verify(field.isModifierOnly(Qt.Key_Alt))
            verify(field.isModifierOnly(Qt.Key_Meta))
        }

        function test_an_ordinary_key_is_not_modifier_only() {
            verify(!field.isModifierOnly(Qt.Key_A))
            verify(!field.isModifierOnly(Qt.Key_F1))
        }

        // --- sequenceStringFor: modifiers + key assembled into one token ---

        function test_modifiers_are_emitted_in_ctrl_alt_shift_meta_order() {
            var seq = field.sequenceStringFor({
                modifiers: Qt.ShiftModifier | Qt.MetaModifier | Qt.ControlModifier | Qt.AltModifier,
                key: Qt.Key_T
            })
            compare(seq, "Ctrl+Alt+Shift+Meta+T")
        }

        function test_a_single_modifier_plus_a_key() {
            compare(field.sequenceStringFor({ modifiers: Qt.ControlModifier, key: Qt.Key_B }), "Ctrl+B")
        }

        function test_a_bare_key_with_no_modifiers_is_allowed() {
            // Function keys are usable on their own, so no-modifier is valid.
            compare(field.sequenceStringFor({ modifiers: Qt.NoModifier, key: Qt.Key_F5 }), "F5")
        }

        function test_an_unspellable_key_produces_no_sequence_even_with_modifiers() {
            compare(field.sequenceStringFor({ modifiers: Qt.ControlModifier, key: Qt.Key_Home }), "")
        }

        // --- click-to-record flow ---

        function test_clicking_the_field_enters_listening_mode() {
            mouseClick(field)
            verify(field.listening)
        }

        function test_pressing_a_combination_records_it_and_stops_listening() {
            mouseClick(field)
            verify(field.listening)
            keyClick(Qt.Key_T, Qt.ControlModifier)
            compare(recordedSpy.count, 1)
            compare(recordedSpy.signalArguments[0][0], "Ctrl+T")
            verify(!field.listening)
        }

        function test_a_lone_modifier_press_waits_for_a_real_key() {
            mouseClick(field)
            keyClick(Qt.Key_Control)
            compare(recordedSpy.count, 0)
            verify(field.listening) // still armed, waiting for a non-modifier
        }

        function test_escape_cancels_recording_without_emitting() {
            mouseClick(field)
            keyClick(Qt.Key_Escape)
            compare(recordedSpy.count, 0)
            verify(!field.listening)
        }

        function test_key_presses_are_ignored_when_not_listening() {
            keyClick(Qt.Key_T, Qt.ControlModifier)
            compare(recordedSpy.count, 0)
        }

        function test_losing_focus_leaves_listening_mode() {
            mouseClick(field)
            verify(field.listening)
            field.focus = false
            verify(!field.listening)
        }
    }
}
