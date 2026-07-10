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
