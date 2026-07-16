import QtQml
import QtQuick
import QtQuick.Templates
import QtTest
import SuopNotesUi

Item {
    id: root

    SettingsDialog {
        id: dialog
    }

    TestCase {
        name: "SettingsDialog"
        when: windowShown

        function init() {
            settings.fontFamily = ""
            settings.spellcheckLanguage = ""
            settings.spellcheckMaxSuggestions = 3
        }

        // Some tests below add words to settings.userDictionaryWords (a
        // real, shared Settings instance backed by a temp file — see
        // tst_qml_main.cpp's Setup). Wipe them after every test so nothing
        // leaks into a later test in this file, or into a different qml
        // test file sharing the same Settings instance.
        function cleanup() {
            var words = settings.userDictionaryWords
            for (var i = 0; i < words.length; ++i) {
                settings.removeUserDictionaryWord(words[i])
            }
        }

        function test_font_family_combo_lists_every_installed_system_font() {
            compare(dialog.fontFamilyCombo.model.length, Qt.fontFamilies().length)
        }

        function test_combo_falls_back_to_the_application_font_when_no_family_is_set() {
            compare(dialog.fontFamilyCombo.currentText, Qt.application.font.family)
        }

        function test_selecting_a_family_updates_the_global_setting_immediately() {
            var families = Qt.fontFamilies()
            dialog.fontFamilyCombo.currentIndex = 0
            dialog.fontFamilyCombo.activated(0)
            compare(settings.fontFamily, families[0])
        }

        function test_combo_reflects_a_family_already_set_elsewhere() {
            var families = Qt.fontFamilies()
            settings.fontFamily = families[0]
            compare(dialog.fontFamilyCombo.currentText, families[0])
        }

        function test_closed_box_previews_the_selected_family_in_its_own_font() {
            var families = Qt.fontFamilies()
            settings.fontFamily = families[2]
            compare(dialog.fontFamilyCombo.font.family, families[2])
        }

        function test_each_dropdown_entry_renders_in_its_own_font() {
            var families = Qt.fontFamilies()
            var item = dialog.fontFamilyCombo.delegate.createObject(null, {
                modelData: families[3],
                index: 3
            })
            compare(item.font.family, families[3])
            item.destroy()
        }

        // availableSpellcheckLanguages() always returns at least the
        // bundled fallback entry (Settings.cpp guarantees this even when
        // Sonnet isn't compiled in or finds nothing), so the combo's model
        // is never empty.
        function test_spellcheck_language_combo_is_never_empty() {
            verify(dialog.spellcheckLanguageCombo.model.length > 0)
        }

        function test_spellcheck_language_combo_defaults_to_the_first_entry_when_language_is_automatic() {
            compare(settings.spellcheckLanguage, "")
            compare(dialog.spellcheckLanguageCombo.currentIndex, 0)
        }

        function test_selecting_a_language_updates_the_global_setting_immediately() {
            var model = dialog.spellcheckLanguageCombo.model
            verify(model.length > 0)

            dialog.spellcheckLanguageCombo.currentIndex = 0
            dialog.spellcheckLanguageCombo.activated(0)

            compare(settings.spellcheckLanguage, model[0].code)
        }

        function test_combo_reflects_a_language_already_set_elsewhere() {
            settings.spellcheckLanguage = "bundled:en_US"
            var model = dialog.spellcheckLanguageCombo.model
            var expectedIndex = -1
            for (var i = 0; i < model.length; ++i) {
                if (model[i].code === "bundled:en_US") {
                    expectedIndex = i
                    break
                }
            }
            verify(expectedIndex >= 0)
            compare(dialog.spellcheckLanguageCombo.currentIndex, expectedIndex)
        }

        // One test, not two: writing to a bound QML property (whether from
        // test code or a real click) permanently severs that binding for
        // the rest of a shared control's lifetime — the exact same
        // `value: settings.X` / `onValueModified: settings.X = value`
        // pattern this SpinBox mirrors from fontSize's own SpinBox elsewhere
        // in this file already has this property, and it's harmless in the
        // real app (nothing else ever changes spellcheckMaxSuggestions
        // except this one control) — but splitting "reflects the default"
        // and "changing it updates the setting" into separate test
        // functions would make the FIRST one fail whenever it happens to
        // run after the second (alphabetical QtQuickTest ordering isn't
        // declaration order), since by then the binding is gone. Keeping
        // both assertions in one function sidesteps that ordering hazard
        // entirely.
        function test_max_suggestions_spinbox_reflects_and_updates_the_global_setting() {
            compare(dialog.spellcheckMaxSuggestionsSpinBox.value, 3, "should reflect the default before any interaction")

            dialog.spellcheckMaxSuggestionsSpinBox.value = 5
            dialog.spellcheckMaxSuggestionsSpinBox.valueModified()
            compare(settings.spellcheckMaxSuggestions, 5, "changing the spinbox should update the global setting immediately")
        }

        // Feature request: debounce the in-progress word instead of
        // excluding it by cursor/selection position (see DocumentHandler
        // ::spellcheckDebounceMs's own comment) — same "one test, not two"
        // ordering-hazard reasoning as the spinbox test above.
        function test_debounce_spinbox_reflects_and_updates_the_global_setting() {
            compare(dialog.spellcheckDebounceMsSpinBox.value, 500, "should reflect the default before any interaction")

            dialog.spellcheckDebounceMsSpinBox.value = 800
            dialog.spellcheckDebounceMsSpinBox.valueModified()
            compare(settings.spellcheckDebounceMs, 800, "changing the spinbox should update the global setting immediately")
        }

        // Feature request: the misspelled-word underline is now a solid,
        // straight line with a configurable color/width (replacing the
        // previous fixed wavy red squiggle) — see NoteWindow.qml's own
        // misspelledWordOverlay for where these settings are actually
        // drawn from.
        function test_underline_color_swatch_reflects_the_global_setting() {
            const original = settings.spellcheckUnderlineColor
            try {
                settings.spellcheckUnderlineColor = "#123456"
                compare(dialog.spellcheckUnderlineColorSwatch.color, "#123456")
            } finally {
                settings.spellcheckUnderlineColor = original
            }
        }

        // One test, not two — same ordering-hazard reasoning as
        // test_max_suggestions_spinbox_reflects_and_updates_the_global_
        // setting's own comment above: writing to
        // spellcheckUnderlineWidthSlider.value permanently severs its
        // `value: settings.spellcheckUnderlineWidth` binding.
        function test_underline_width_slider_reflects_and_updates_the_global_setting() {
            compare(dialog.spellcheckUnderlineWidthSlider.value, 3, "should reflect the default before any interaction")

            dialog.spellcheckUnderlineWidthSlider.value = 6
            dialog.spellcheckUnderlineWidthSlider.moved()
            compare(settings.spellcheckUnderlineWidth, 6, "moving the slider should update the global setting immediately")
        }

        function test_clicking_add_appends_a_new_word_to_the_user_dictionary_and_grows_the_repeater() {
            compare(settings.userDictionaryWords, [])
            var countBefore = dialog.userDictionaryWordsRepeater.count

            dialog.newUserDictionaryWordField.text = "wobble"
            dialog.addUserDictionaryWordButton.clicked()

            compare(settings.userDictionaryWords, ["wobble"])
            compare(dialog.userDictionaryWordsRepeater.count, countBefore + 1)
            compare(dialog.newUserDictionaryWordField.text, "",
                    "the field should clear itself once the word is added")
        }

        function test_pressing_enter_in_the_word_field_also_adds_the_word() {
            compare(settings.userDictionaryWords, [])
            var countBefore = dialog.userDictionaryWordsRepeater.count

            dialog.newUserDictionaryWordField.text = "gronk"
            dialog.newUserDictionaryWordField.accepted()

            compare(settings.userDictionaryWords, ["gronk"])
            compare(dialog.userDictionaryWordsRepeater.count, countBefore + 1)
            compare(dialog.newUserDictionaryWordField.text, "",
                    "the field should clear itself once the word is added")
        }

        // ------------------------------------------------------------------
        // Code Mode section (feature request)
        //
        // The first checkbox is the global DEFAULT for every note (each note
        // can override it from its own settings popup). The three below it
        // define what code mode MEANS, app-wide.
        // ------------------------------------------------------------------

        // One test, not two — same ordering hazard the spinbox test above
        // documents: ticking the box severs its `checked: settings.
        // codeModeEnabled` binding for the rest of this control's life, so
        // "reflects the setting" has to be asserted before "writing it
        // updates the setting", in this order, in one function.
        function test_code_mode_default_checkbox_reflects_and_updates_the_global_setting() {
            var box = dialog.codeModeDefaultCheckBox

            try {
                compare(settings.codeModeEnabled, false, "precondition: code mode ships off by default")
                compare(box.checked, false, "should reflect the setting before any interaction")

                box.checked = true
                box.toggled()
                compare(settings.codeModeEnabled, true,
                        "ticking it should make every note a code note by default, immediately")

                box.checked = false
                box.toggled()
                compare(settings.codeModeEnabled, false,
                        "unticking it should turn the default back off, immediately")
            } finally {
                settings.codeModeEnabled = false
            }
        }

        // Same "one test, not two" reasoning as above, for all three boxes:
        // each assertion pair is reflect-then-write, in that order.
        function test_the_three_suppression_checkboxes_reflect_and_update_their_own_global_settings() {
            var markdownBox = dialog.codeModeDisablesMarkdownCheckBox
            var autoFormattingBox = dialog.codeModeDisablesAutoFormattingCheckBox
            var spellcheckBox = dialog.codeModeDisablesSpellcheckCheckBox

            try {
                compare(settings.codeModeDisablesMarkdownHighlighting, true, "precondition: ships on")
                compare(settings.codeModeDisablesAutoFormatting, true, "precondition: ships on")
                compare(settings.codeModeDisablesSpellcheck, true, "precondition: ships on")
                compare(markdownBox.checked, true, "should reflect its setting before any interaction")
                compare(autoFormattingBox.checked, true, "should reflect its setting before any interaction")
                compare(spellcheckBox.checked, true, "should reflect its setting before any interaction")

                markdownBox.checked = false
                markdownBox.toggled()
                compare(settings.codeModeDisablesMarkdownHighlighting, false,
                        "unticking it should let code notes keep markdown styling")
                compare(settings.codeModeDisablesAutoFormatting, true,
                        "and must not disturb the other two")
                compare(settings.codeModeDisablesSpellcheck, true)

                autoFormattingBox.checked = false
                autoFormattingBox.toggled()
                compare(settings.codeModeDisablesAutoFormatting, false)

                spellcheckBox.checked = false
                spellcheckBox.toggled()
                compare(settings.codeModeDisablesSpellcheck, false)
            } finally {
                settings.codeModeDisablesMarkdownHighlighting = true
                settings.codeModeDisablesAutoFormatting = true
                settings.codeModeDisablesSpellcheck = true
            }
        }

        // Deliberate: the three describe what code mode does for ANY note in
        // code mode — including one that got there by its own per-note
        // override while the global default is off, which is the common case
        // since the default ships off. Graying them out whenever the global
        // default is unticked would make them look inapplicable to exactly
        // the notes they do apply to.
        function test_the_suppression_checkboxes_stay_usable_while_the_global_default_is_off() {
            compare(settings.codeModeEnabled, false, "precondition: the global default is off")

            verify(dialog.codeModeDisablesMarkdownCheckBox.enabled)
            verify(dialog.codeModeDisablesAutoFormattingCheckBox.enabled)
            verify(dialog.codeModeDisablesSpellcheckCheckBox.enabled)
        }

        function test_clicking_remove_on_a_words_row_removes_it_from_the_setting_and_shrinks_the_repeater() {
            settings.addUserDictionaryWord("zaphod")
            var countBefore = dialog.userDictionaryWordsRepeater.count
            verify(countBefore > 0)

            var index = settings.userDictionaryWords.indexOf("zaphod")
            verify(index >= 0)

            // Each Repeater delegate is a Row { Label; Button }, in that
            // declaration order — see SettingsDialog.qml's
            // userDictionaryWordsRepeater delegate — so children[1] is
            // always that row's "Remove" button.
            var removeButton = dialog.userDictionaryWordsRepeater.itemAt(index).children[1]
            removeButton.clicked()

            compare(settings.userDictionaryWords.indexOf("zaphod"), -1)
            compare(dialog.userDictionaryWordsRepeater.count, countBefore - 1)
        }
    }
}
