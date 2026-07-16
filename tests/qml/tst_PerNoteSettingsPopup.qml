import QtQml
import QtQuick
import QtQuick.Templates
import QtTest
import SuopNotesUi

Item {
    id: root

    NoteAppearance {
        id: appearance
        noteId: "font-family-combo-test-note"
    }

    PerNoteSettingsPopup {
        id: popup
        noteAppearance: appearance
    }

    // Assigned imperatively from root's own scope (not a `settings: settings`
    // binding on the NoteAppearance itself) — same self-shadowing trap
    // documented in NoteWindow.qml: a bare `settings` inside NoteAppearance's
    // own scope resolves to its own property of the same name.
    Component.onCompleted: appearance.settings = settings

    TestCase {
        name: "PerNoteSettingsPopup"
        when: windowShown

        function initTestCase() {
            popup.visible = true
        }

        function init() {
            appearance.clearFontFamilyOverride()
            settings.fontFamily = ""
            popup.fontFamilyOverrideCheckBox.checked = false

            appearance.clearCodeModeEnabledOverride()
            settings.codeModeEnabled = false
            // Same reason as fontFamilyOverrideCheckBox above: a genuine
            // toggle severs this box's `checked: ...hasCodeModeEnabledOverride`
            // binding for good, so its state has to be reset by hand between
            // tests rather than left to the binding.
            popup.codeModeOverrideCheckBox.checked = false

            appearance.clearCodeModeDisablesMarkdownHighlightingOverride()
            appearance.clearCodeModeDisablesAutoFormattingOverride()
            appearance.clearCodeModeDisablesSpellcheckOverride()
            settings.codeModeDisablesMarkdownHighlighting = true
            settings.codeModeDisablesAutoFormatting = true
            settings.codeModeDisablesSpellcheck = true
            // Only the OVERRIDE boxes are reset here, never the value boxes
            // beside them — resetting a value box would sever its
            // `checked: ...codeModeDisablesX` binding on the very first init,
            // before any test ever got to see it track the resolved value.
            popup.codeModeDisablesMarkdownOverrideCheckBox.checked = false
            popup.codeModeDisablesAutoFormattingOverrideCheckBox.checked = false
            popup.codeModeDisablesSpellcheckOverrideCheckBox.checked = false
        }

        function cleanup() {
            appearance.clearCodeModeEnabledOverride()
            settings.codeModeEnabled = false
            appearance.clearCodeModeDisablesMarkdownHighlightingOverride()
            appearance.clearCodeModeDisablesAutoFormattingOverride()
            appearance.clearCodeModeDisablesSpellcheckOverride()
            settings.codeModeDisablesMarkdownHighlighting = true
            settings.codeModeDisablesAutoFormatting = true
            settings.codeModeDisablesSpellcheck = true
        }

        function toggleViaKeyboard(checkBox) {
            checkBox.forceActiveFocus()
            // Same settling wait as toggleFontFamilyOverrideCheckBoxViaKeyboard
            // below — see its comment.
            wait(50)
            keyClick(Qt.Key_Space)
        }

        // AbstractButton.toggled() (which drives the actual override
        // set/clear logic in PerNoteSettingsPopup.qml) is only emitted for
        // genuine interactive toggles — not by setting `checked` directly
        // and not by calling toggle() — so the checkbox must actually be
        // exercised via a real key press to test that logic honestly.
        function toggleFontFamilyOverrideCheckBoxViaKeyboard() {
            popup.fontFamilyOverrideCheckBox.forceActiveFocus()
            // Lets window activation/focus settle before the key is sent —
            // without this, the very first keyboard interaction in the whole
            // test file can race the popup window becoming active and the
            // key event is dropped.
            wait(50)
            keyClick(Qt.Key_Space)
        }

        function test_font_family_combo_lists_every_installed_system_font() {
            compare(popup.fontFamilyCombo.model.length, Qt.fontFamilies().length)
        }

        function test_combo_is_disabled_until_override_is_checked() {
            compare(popup.fontFamilyCombo.enabled, false)
            toggleFontFamilyOverrideCheckBoxViaKeyboard()
            compare(popup.fontFamilyCombo.enabled, true)
        }

        function test_checking_the_override_seeds_it_from_the_current_family() {
            var families = Qt.fontFamilies()
            settings.fontFamily = families[1]
            toggleFontFamilyOverrideCheckBoxViaKeyboard()
            compare(appearance.hasFontFamilyOverride, true)
            compare(appearance.fontFamily, families[1])
        }

        function test_selecting_a_family_sets_a_per_note_override() {
            var families = Qt.fontFamilies()
            toggleFontFamilyOverrideCheckBoxViaKeyboard()
            popup.fontFamilyCombo.currentIndex = 0
            popup.fontFamilyCombo.activated(0)
            compare(appearance.hasFontFamilyOverride, true)
            compare(appearance.fontFamily, families[0])
        }

        function test_unchecking_override_reverts_to_the_global_family() {
            var families = Qt.fontFamilies()
            settings.fontFamily = families[1]
            toggleFontFamilyOverrideCheckBoxViaKeyboard()
            popup.fontFamilyCombo.currentIndex = 0
            popup.fontFamilyCombo.activated(0)
            toggleFontFamilyOverrideCheckBoxViaKeyboard()
            compare(appearance.hasFontFamilyOverride, false)
            compare(appearance.fontFamily, families[1])
        }

        function test_closed_box_previews_the_selected_family_in_its_own_font() {
            var families = Qt.fontFamilies()
            settings.fontFamily = families[2]
            compare(popup.fontFamilyCombo.font.family, families[2])
        }

        // ------------------------------------------------------------------
        // Code mode (feature request)
        //
        // Same two-checkbox idiom as every other override on this popup:
        // "Override code mode" decides whether THIS note answers the question
        // itself, and "Enabled" is its answer — greyed out until the override
        // is on, since otherwise the note just follows the global default.
        // ------------------------------------------------------------------

        function test_the_enabled_checkbox_is_greyed_out_until_the_code_mode_override_is_ticked() {
            compare(popup.codeModeEnabledCheckBox.enabled, false,
                    "with no override, this note follows the global default and there is nothing to answer")

            toggleViaKeyboard(popup.codeModeOverrideCheckBox)
            compare(popup.codeModeEnabledCheckBox.enabled, true)
        }

        // One test, not two: a genuine toggle of the Enabled box severs its
        // `checked: ...codeModeEnabled` binding for the rest of this popup's
        // life, so "ticking the override leaves the display alone" must be
        // asserted before anything ever toggles that box — same ordering
        // hazard tst_SettingsDialog.qml's own spinbox test documents.
        function test_ticking_the_override_seeds_it_from_the_current_value_without_changing_what_is_shown() {
            compare(settings.codeModeEnabled, false, "precondition: the global default is off")
            compare(appearance.codeModeEnabled, false, "so this note resolves to not-code-mode")
            compare(popup.codeModeEnabledCheckBox.checked, false, "and the popup shows that")

            toggleViaKeyboard(popup.codeModeOverrideCheckBox)

            compare(appearance.hasCodeModeEnabledOverride, true,
                    "the note now answers the question itself...")
            compare(appearance.codeModeEnabled, false,
                    "...but with the same answer it was already resolving to — taking control of a " +
                    "setting must not change it")
            compare(popup.codeModeEnabledCheckBox.checked, false,
                    "so nothing the user is looking at changes either")

            // And now that the override is in place, the Enabled box is what
            // actually turns this note into a code note.
            toggleViaKeyboard(popup.codeModeEnabledCheckBox)

            compare(appearance.codeModeEnabled, true)
            compare(settings.codeModeEnabled, false,
                    "one note becoming a code note must not touch the global default")
        }

        function test_ticking_the_override_while_the_global_default_is_on_seeds_it_on() {
            settings.codeModeEnabled = true
            compare(appearance.codeModeEnabled, true, "precondition: the note resolves to code mode")

            toggleViaKeyboard(popup.codeModeOverrideCheckBox)

            compare(appearance.hasCodeModeEnabledOverride, true)
            compare(appearance.codeModeEnabled, true,
                    "the override should be seeded from the value the note already had, not from false")

            // Proves the note is now genuinely pinned to its own answer: the
            // global default going away no longer moves it.
            settings.codeModeEnabled = false
            compare(appearance.codeModeEnabled, true,
                    "an overridden note keeps its own answer when the global default changes")
        }

        function test_unticking_the_override_hands_the_note_back_to_the_global_default() {
            settings.codeModeEnabled = true
            toggleViaKeyboard(popup.codeModeOverrideCheckBox)
            appearance.setCodeModeEnabledOverride(false)
            compare(appearance.codeModeEnabled, false,
                    "precondition: this note overrides the global default and opts out of code mode")

            toggleViaKeyboard(popup.codeModeOverrideCheckBox)

            compare(appearance.hasCodeModeEnabledOverride, false)
            compare(appearance.codeModeEnabled, true,
                    "dropping the override should follow the global default again")
        }

        // ------------------------------------------------------------------
        // What code mode switches off, per note
        //
        // The other half of code mode: not whether the note IS code, but what
        // being code does to it. Each of the three suppressions carries its own
        // Override box, so a note can be code and still keep, say, spellcheck
        // while every other code note loses it.
        //
        // Every test below asserts on `appearance` rather than on what the
        // value boxes show, except where it says otherwise: a genuine toggle
        // severs a value box's `checked` binding for the rest of this popup's
        // life, and these tests should document the resolution rules rather
        // than depend on which of them ran first.
        // ------------------------------------------------------------------

        function test_a_code_notes_suppression_boxes_are_greyed_out_until_their_own_override_is_ticked() {
            compare(popup.codeModeDisablesMarkdownCheckBox.enabled, false,
                    "with no override, this note follows the global answer and has nothing to say")
            compare(popup.codeModeDisablesAutoFormattingCheckBox.enabled, false)
            compare(popup.codeModeDisablesSpellcheckCheckBox.enabled, false)

            toggleViaKeyboard(popup.codeModeDisablesMarkdownOverrideCheckBox)

            compare(popup.codeModeDisablesMarkdownCheckBox.enabled, true)
            compare(popup.codeModeDisablesAutoFormattingCheckBox.enabled, false,
                    "each suppression has its own override — ticking one must not enable the others")
            compare(popup.codeModeDisablesSpellcheckCheckBox.enabled, false)
        }

        // These deliberately do NOT depend on the code mode override: a note
        // that is code because the GLOBAL default says so still gets to say
        // what that means for it, and that is the common case.
        function test_a_notes_suppressions_are_settable_without_it_overriding_code_mode_itself() {
            compare(appearance.hasCodeModeEnabledOverride, false,
                    "precondition: this note has no code mode override of its own")

            toggleViaKeyboard(popup.codeModeDisablesSpellcheckOverrideCheckBox)

            compare(appearance.hasCodeModeDisablesSpellcheckOverride, true,
                    "the suppression override stands on its own...")
            compare(appearance.hasCodeModeEnabledOverride, false,
                    "...without dragging a code mode override along with it")
        }

        function test_overriding_one_suppression_leaves_the_other_two_following_the_global_defaults() {
            toggleViaKeyboard(popup.codeModeDisablesSpellcheckOverrideCheckBox)
            toggleViaKeyboard(popup.codeModeDisablesSpellcheckCheckBox)

            compare(appearance.codeModeDisablesSpellcheck, false,
                    "this note's code mode should now keep spellcheck")
            compare(settings.codeModeDisablesSpellcheck, true,
                    "one note keeping spellcheck must not touch the global answer")

            compare(appearance.hasCodeModeDisablesMarkdownHighlightingOverride, false,
                    "the other two suppressions should be untouched")
            compare(appearance.hasCodeModeDisablesAutoFormattingOverride, false)
            compare(appearance.codeModeDisablesMarkdownHighlighting, true)
            compare(appearance.codeModeDisablesAutoFormatting, true)
        }

        function test_ticking_a_suppression_override_seeds_it_from_the_global_it_was_following() {
            settings.codeModeDisablesAutoFormatting = false
            compare(appearance.codeModeDisablesAutoFormatting, false,
                    "precondition: the note follows the global answer of 'keep auto-formatting'")

            toggleViaKeyboard(popup.codeModeDisablesAutoFormattingOverrideCheckBox)

            compare(appearance.hasCodeModeDisablesAutoFormattingOverride, true,
                    "the note now answers for itself...")
            compare(appearance.codeModeDisablesAutoFormatting, false,
                    "...with the answer it was already resolving to, not with the struct's own " +
                    "default — taking control of a setting must not change it")

            // Proves it is genuinely pinned now: the global moving no longer
            // moves the note.
            settings.codeModeDisablesAutoFormatting = true
            compare(appearance.codeModeDisablesAutoFormatting, false,
                    "an overridden suppression keeps its answer when the global one changes")
        }

        function test_unticking_a_suppression_override_hands_it_back_to_the_global_default() {
            settings.codeModeDisablesMarkdownHighlighting = false
            toggleViaKeyboard(popup.codeModeDisablesMarkdownOverrideCheckBox)
            appearance.setCodeModeDisablesMarkdownHighlightingOverride(true)
            compare(appearance.codeModeDisablesMarkdownHighlighting, true,
                    "precondition: this note overrides the global answer and does suppress markdown")

            toggleViaKeyboard(popup.codeModeDisablesMarkdownOverrideCheckBox)

            compare(appearance.hasCodeModeDisablesMarkdownHighlightingOverride, false)
            compare(appearance.codeModeDisablesMarkdownHighlighting, false,
                    "dropping the override should follow the global answer again")
        }

        function test_each_dropdown_entry_renders_in_its_own_font() {
            var families = Qt.fontFamilies()
            var item = popup.fontFamilyCombo.delegate.createObject(null, {
                modelData: families[3],
                index: 3
            })
            compare(item.font.family, families[3])
            item.destroy()
        }
    }
}
