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
    }
}
