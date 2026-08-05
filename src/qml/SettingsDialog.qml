import QtQuick
import QtQuick.Controls
import Qt.labs.platform as Platform

// A normal, framed top-level window (not a Popup — this app has no main
// window to host one in, and unlike note windows this genuinely benefits
// from native chrome/a taskbar entry while open).
ApplicationWindow {
    id: root

    readonly property alias settingsTabBar: settingsTabBar
    readonly property alias fontFamilyCombo: fontFamilyCombo
    readonly property alias spellcheckLanguageCombo: spellcheckLanguageCombo
    readonly property alias spellcheckMaxSuggestionsSpinBox: spellcheckMaxSuggestionsSpinBox
    readonly property alias spellcheckDebounceMsSpinBox: spellcheckDebounceMsSpinBox
    readonly property alias spellcheckUnderlineWidthSlider: spellcheckUnderlineWidthSlider
    readonly property alias spellcheckUnderlineColorSwatch: spellcheckUnderlineColorSwatch
    readonly property alias newUserDictionaryWordField: newUserDictionaryWordField
    readonly property alias addUserDictionaryWordButton: addUserDictionaryWordButton
    readonly property alias userDictionaryWordsRepeater: userDictionaryWordsRepeater
    // Code Mode section, exposed for tests only like every alias above.
    readonly property alias codeModeDefaultCheckBox: codeModeDefaultCheckBox
    readonly property alias codeModeDisablesMarkdownCheckBox: codeModeDisablesMarkdownCheckBox
    readonly property alias codeModeDisablesAutoFormattingCheckBox: codeModeDisablesAutoFormattingCheckBox
    readonly property alias codeModeDisablesSpellcheckCheckBox: codeModeDisablesSpellcheckCheckBox
    readonly property alias launchOnLoginCheckBox: launchOnLoginCheckBox

    title: "suopNotes Settings"
    width: 400
    height: 520
    // Only shown via the tray menu's "Settings…" action (settingsWindow.show())
    visible: false

    // Live bindings, no Apply/Cancel step — matches Settings' own model of
    // persisting every change immediately (Step E), so this window is just
    // a view onto that state, not a separate draft.

    Platform.ColorDialog {
        id: backgroundColorDialog
        currentColor: settings.backgroundColor
        onAccepted: settings.backgroundColor = color
    }

    Platform.ColorDialog {
        id: fontColorDialog
        currentColor: settings.fontColor
        onAccepted: settings.fontColor = color
    }

    Platform.ColorDialog {
        id: highlightColorDialog
        currentColor: settings.highlightColor
        onAccepted: settings.highlightColor = color
    }

    Platform.FolderDialog {
        id: notesFolderDialog
        currentFolder: "file://" + settings.notesFolder
        onAccepted: settings.notesFolder = String(folder).replace("file://", "")
    }

    Platform.ColorDialog {
        id: borderColorDialog
        currentColor: settings.borderColor
        onAccepted: settings.borderColor = color
    }

    Platform.ColorDialog {
        id: glassTintColorDialog
        currentColor: settings.glassTintColor
        onAccepted: settings.glassTintColor = color
    }

    Platform.ColorDialog {
        id: interactableHighlightColorDialog
        currentColor: settings.interactableHighlightColor
        onAccepted: settings.interactableHighlightColor = color
    }

    Platform.ColorDialog {
        id: spellcheckUnderlineColorDialog
        currentColor: settings.spellcheckUnderlineColor
        onAccepted: settings.spellcheckUnderlineColor = color
    }

    // Folders rather than one endless column. The general settings and the
    // integrations have nothing to do with each other, and the module system
    // means integrations keep arriving — so they get their own page instead
    // of being appended to the bottom of everything else.
    header: TabBar {
        id: settingsTabBar
        TabButton { text: "General" }
        TabButton { text: "Integrations" }
    }

    // Two sibling views toggled by `visible`, not a StackLayout: same result,
    // one less level of nesting, and no QtQuick.Layouts import.
    ScrollView {
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: availableWidth
        visible: settingsTabBar.currentIndex === 0

        Column {
            width: parent.width
            spacing: 12

            Column {
                width: parent.width
                spacing: 4
                Label { text: "Window radius: " + Math.round(radiusSlider.value) }
                // WheelSafeSlider, not Slider: the desktop style lets the
                // wheel drag a slider's value, which in this dialog means a
                // stray scroll silently rewrites a saved setting. See
                // WheelSafeSlider.qml.
                WheelSafeSlider {
                    id: radiusSlider
                    width: parent.width
                    from: 0
                    to: 32
                    value: settings.windowRadius
                    onMoved: settings.windowRadius = Math.round(value)
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Label { text: "Background opacity: " + opacitySlider.value.toFixed(2) }
                WheelSafeSlider {
                    id: opacitySlider
                    width: parent.width
                    from: 0
                    to: 1.0
                    value: settings.windowOpacity
                    onMoved: settings.windowOpacity = value
                }
            }

            CheckBox {
                text: "Enable blur behind notes"
                checked: settings.blurEnabled
                onToggled: settings.blurEnabled = checked
            }

            CheckBox {
                text: "Enable spellcheck"
                checked: settings.spellcheckEnabled
                onToggled: settings.spellcheckEnabled = checked
            }

            Row {
                spacing: 8
                Label { text: "Spellcheck language:"; anchors.verticalCenter: parent.verticalCenter }
                ComboBox {
                    id: spellcheckLanguageCombo
                    // The KDE desktop style (org.kde.desktop, selected by
                    // Plasma's platform theme) forces wheelEnabled: true on
                    // every ComboBox and SpinBox, so a wheel that merely
                    // passes over one silently rewrites the setting — and
                    // these bindings persist immediately, with no Apply step
                    // to catch it. Opting back out also leaves the event
                    // unaccepted, so it reaches the enclosing ScrollView and
                    // scrolls the pane, which is what the gesture meant.
                    // Every combo and spin box in this file does the same.
                    wheelEnabled: false
                    model: settings.availableSpellcheckLanguages()
                    textRole: "name"
                    valueRole: "code"
                    currentIndex: {
                        var idx = -1
                        for (var i = 0; i < model.length; ++i) {
                            if (model[i].code === settings.spellcheckLanguage) { idx = i; break }
                        }
                        // Empty spellcheckLanguage ("") means "automatic" —
                        // no entry literally has code "", so default to
                        // index 0 (first Sonnet entry, or the bundled entry
                        // if Sonnet found nothing) when nothing matches,
                        // rather than showing a blank/invalid combo.
                        return idx >= 0 ? idx : 0
                    }
                    onActivated: settings.spellcheckLanguage = model[currentIndex].code
                }
            }

            Row {
                spacing: 8
                Label { text: "Max spelling suggestions shown:"; anchors.verticalCenter: parent.verticalCenter }
                SpinBox {
                    id: spellcheckMaxSuggestionsSpinBox
                    // Same style opt-out as spellcheckLanguageCombo above.
                    wheelEnabled: false
                    from: 0
                    to: 10
                    value: settings.spellcheckMaxSuggestions
                    onValueModified: settings.spellcheckMaxSuggestions = value
                }
            }

            Row {
                spacing: 8
                Label { text: "Spellcheck debounce (ms):"; anchors.verticalCenter: parent.verticalCenter }
                SpinBox {
                    id: spellcheckDebounceMsSpinBox
                    // Same style opt-out as spellcheckLanguageCombo above.
                    wheelEnabled: false
                    from: 0
                    to: 5000
                    stepSize: 100
                    value: settings.spellcheckDebounceMs
                    onValueModified: settings.spellcheckDebounceMs = value
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Label { text: "Underline width: " + Math.round(spellcheckUnderlineWidthSlider.value) }
                WheelSafeSlider {
                    id: spellcheckUnderlineWidthSlider
                    width: parent.width
                    from: 1
                    to: 8
                    value: settings.spellcheckUnderlineWidth
                    onMoved: settings.spellcheckUnderlineWidth = Math.round(value)
                }
            }

            Row {
                spacing: 8
                Label { text: "Underline color:"; anchors.verticalCenter: parent.verticalCenter }
                Rectangle {
                    id: spellcheckUnderlineColorSwatch
                    width: 24
                    height: 24
                    color: settings.spellcheckUnderlineColor
                    border.color: "gray"
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; onClicked: spellcheckUnderlineColorDialog.open() }
                }
            }

            Column {
                width: parent.width
                spacing: 4

                Label { text: "User dictionary (words that are never flagged):" }

                Row {
                    width: parent.width
                    spacing: 8
                    TextField {
                        id: newUserDictionaryWordField
                        width: parent.width - addUserDictionaryWordButton.width - 8
                        placeholderText: "Add a word…"
                        onAccepted: {
                            settings.addUserDictionaryWord(text)
                            text = ""
                        }
                    }
                    Button {
                        id: addUserDictionaryWordButton
                        text: "Add"
                        onClicked: {
                            settings.addUserDictionaryWord(newUserDictionaryWordField.text)
                            newUserDictionaryWordField.text = ""
                        }
                    }
                }

                Repeater {
                    id: userDictionaryWordsRepeater
                    model: settings.userDictionaryWords
                    Row {
                        spacing: 8
                        required property string modelData
                        Label { text: modelData; anchors.verticalCenter: parent.verticalCenter }
                        Button {
                            text: "Remove"
                            onClicked: settings.removeUserDictionaryWord(modelData)
                        }
                    }
                }
            }

            CheckBox {
                text: "Enable glass effect"
                checked: settings.glassEnabled
                onToggled: settings.glassEnabled = checked
            }

            Column {
                width: parent.width
                spacing: 4
                Label { text: "Glass sheen opacity: " + glassSheenOpacitySlider.value.toFixed(2) }
                WheelSafeSlider {
                    id: glassSheenOpacitySlider
                    width: parent.width
                    from: 0.0
                    to: 0.4
                    value: settings.glassSheenOpacity
                    onMoved: settings.glassSheenOpacity = value
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Label { text: "Glass sheen strength: " + Math.round(glassSheenStrengthSlider.value) }
                WheelSafeSlider {
                    id: glassSheenStrengthSlider
                    width: parent.width
                    from: 0
                    to: 100
                    value: settings.glassSheenStrength
                    onMoved: settings.glassSheenStrength = Math.round(value)
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Label { text: "Glass sheen size: " + glassSheenSizeSlider.value.toFixed(2) }
                WheelSafeSlider {
                    id: glassSheenSizeSlider
                    width: parent.width
                    from: 0.05
                    to: 0.5
                    value: settings.glassSheenSize
                    onMoved: settings.glassSheenSize = value
                }
            }

            Row {
                spacing: 8
                Label { text: "Glass color:"; anchors.verticalCenter: parent.verticalCenter }
                Rectangle {
                    width: 24
                    height: 24
                    color: settings.glassTintColor
                    border.color: "gray"
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; onClicked: glassTintColorDialog.open() }
                }
            }

            CheckBox {
                text: "Always show note controls (pin/settings/close)"
                checked: settings.hudAlwaysExpanded
                onToggled: settings.hudAlwaysExpanded = checked
            }

            CheckBox {
                text: "Show note controls beside the note (not over its text)"
                checked: settings.hudAsWindow
                onToggled: settings.hudAsWindow = checked
            }

            CheckBox {
                id: launchOnLoginCheckBox
                text: "Launch on login"
                // Hidden (not just disabled) off Linux, where there's no XDG
                // autostart directory to write to — a dead control would only
                // mislead. Its slot below also collapses the row when hidden
                // so it leaves no gap in the layout.
                visible: autostart.available
                height: visible ? implicitHeight : 0
                // Not bound to settings.* — this reflects the on-disk autostart
                // .desktop file directly (see AutostartManager), which is also
                // what KDE's own Autostart page toggles, so the two never drift.
                checked: autostart.enabled
                onToggled: autostart.enabled = checked
            }

            // Code mode (feature request). The first checkbox is the global
            // DEFAULT for every note — each note can override it from its own
            // hover settings popup, which is where a single note is turned
            // into a code note. The three below define what code mode MEANS,
            // app-wide: they are deliberately not per-note (see Settings.h),
            // since "what does code mode do" wants one answer, not one per
            // note.
            //
            // Grouped in their own section rather than sitting beside the
            // other appearance checkboxes precisely because of that split —
            // the indent and the enabled-gate below are what make it readable
            // as "these three describe the thing above".
            Column {
                width: parent.width
                spacing: 8

                Label { text: "Code Mode"; font.bold: true }

                CheckBox {
                    id: codeModeDefaultCheckBox
                    text: "Enable code mode by default"
                    checked: settings.codeModeEnabled
                    onToggled: settings.codeModeEnabled = checked
                }

                Label {
                    text: "Code mode uses a monospaced font and shows line numbers."
                    // Wraps rather than eliding: this is the one place that
                    // explains what the mode actually does, so it must stay
                    // readable at any dialog width.
                    width: parent.width
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                }

                Label { text: "While code mode is on, also:"; opacity: 0.7 }

                Column {
                    width: parent.width
                    spacing: 4
                    // Indented to read as belonging to the label above.
                    leftPadding: 12
                    // NOT gated on codeModeDefaultCheckBox: these apply to any
                    // note in code mode, including one that got there by its
                    // own per-note override while the global default is off —
                    // which is the common case, since the default ships off.
                    // Disabling them here would make them look inapplicable to
                    // exactly the notes they do apply to.

                    CheckBox {
                        id: codeModeDisablesMarkdownCheckBox
                        text: "Turn off markdown styling"
                        checked: settings.codeModeDisablesMarkdownHighlighting
                        onToggled: settings.codeModeDisablesMarkdownHighlighting = checked
                    }
                    CheckBox {
                        id: codeModeDisablesAutoFormattingCheckBox
                        text: "Turn off list and heading auto-formatting"
                        checked: settings.codeModeDisablesAutoFormatting
                        onToggled: settings.codeModeDisablesAutoFormatting = checked
                    }
                    CheckBox {
                        id: codeModeDisablesSpellcheckCheckBox
                        text: "Turn off spellcheck"
                        checked: settings.codeModeDisablesSpellcheck
                        onToggled: settings.codeModeDisablesSpellcheck = checked
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 8

                Label { text: "Interactable Highlights"; font.bold: true }

                Row {
                    spacing: 8
                    Label { text: "Highlight color:"; anchors.verticalCenter: parent.verticalCenter }
                    Rectangle {
                        width: 24
                        height: 24
                        color: settings.interactableHighlightColor
                        border.color: "gray"
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea { anchors.fill: parent; onClicked: interactableHighlightColorDialog.open() }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 4
                    Label { text: "Highlight opacity: " + interactableHighlightOpacitySlider.value.toFixed(2) }
                    WheelSafeSlider {
                        id: interactableHighlightOpacitySlider
                        width: parent.width
                        from: 0
                        to: 1.0
                        value: settings.interactableHighlightOpacity
                        onMoved: settings.interactableHighlightOpacity = value
                    }
                }

                CheckBox {
                    text: "Glass effect on highlights"
                    checked: settings.interactableHighlightGlassEnabled
                    onToggled: settings.interactableHighlightGlassEnabled = checked
                }
            }

            Column {
                width: parent.width
                spacing: 8

                Label { text: "Toolbar & Scrollbar Glass"; font.bold: true }

                CheckBox {
                    text: "Follow each note's own glass setting"
                    checked: settings.chromeGlassFollowsNote
                    onToggled: settings.chromeGlassFollowsNote = checked
                }

                CheckBox {
                    text: "Enable glass effect"
                    enabled: !settings.chromeGlassFollowsNote
                    checked: settings.chromeGlassEnabled
                    onToggled: settings.chromeGlassEnabled = checked
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Label { text: "Border width: " + Math.round(borderWidthSlider.value) }
                WheelSafeSlider {
                    id: borderWidthSlider
                    width: parent.width
                    from: 0
                    to: 12
                    value: settings.borderWidth
                    onMoved: settings.borderWidth = Math.round(value)
                }
            }

            Row {
                spacing: 8
                Label { text: "Border color:"; anchors.verticalCenter: parent.verticalCenter }
                Rectangle {
                    width: 24
                    height: 24
                    color: settings.borderColor
                    border.color: "gray"
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; onClicked: borderColorDialog.open() }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Label { text: "Scrollbar width: " + Math.round(scrollbarWidthSlider.value) }
                WheelSafeSlider {
                    id: scrollbarWidthSlider
                    width: parent.width
                    from: 0
                    to: 16
                    value: settings.scrollbarWidth
                    onMoved: settings.scrollbarWidth = Math.round(value)
                }
            }

            Row {
                spacing: 8
                Label { text: "Font size:"; anchors.verticalCenter: parent.verticalCenter }
                SpinBox {
                    // Same style opt-out as spellcheckLanguageCombo above.
                    wheelEnabled: false
                    from: 8
                    to: 48
                    value: settings.fontSize
                    onValueModified: settings.fontSize = value
                }
            }

            Row {
                spacing: 8
                Label { text: "Font family:"; anchors.verticalCenter: parent.verticalCenter }
                ComboBox {
                    id: fontFamilyCombo
                    // Same style opt-out as spellcheckLanguageCombo above.
                    wheelEnabled: false
                    model: Qt.fontFamilies()
                    // Previews the selected family in the closed box; each
                    // dropdown entry gets its own font via the delegate below.
                    font.family: currentText
                    currentIndex: {
                        var idx = find(settings.fontFamily)
                        if (idx < 0)
                            idx = find(Qt.application.font.family)
                        return idx
                    }
                    delegate: ItemDelegate {
                        required property string modelData
                        required property int index

                        width: fontFamilyCombo.width
                        text: modelData
                        font.family: modelData
                        highlighted: fontFamilyCombo.highlightedIndex === index
                    }
                    onActivated: settings.fontFamily = currentText
                }
            }

            Row {
                spacing: 8
                Label { text: "Background color:"; anchors.verticalCenter: parent.verticalCenter }
                Rectangle {
                    width: 24
                    height: 24
                    color: settings.backgroundColor
                    border.color: "gray"
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; onClicked: backgroundColorDialog.open() }
                }
            }

            Row {
                spacing: 8
                Label { text: "Font color:"; anchors.verticalCenter: parent.verticalCenter }
                Rectangle {
                    width: 24
                    height: 24
                    color: settings.fontColor
                    border.color: "gray"
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; onClicked: fontColorDialog.open() }
                }
            }

            Row {
                spacing: 8
                Label { text: "Text highlight color:"; anchors.verticalCenter: parent.verticalCenter }
                Rectangle {
                    width: 24
                    height: 24
                    color: settings.highlightColor
                    border.color: "gray"
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; onClicked: highlightColorDialog.open() }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Label { text: "Text highlight opacity: " + highlightOpacitySlider.value.toFixed(2) }
                WheelSafeSlider {
                    id: highlightOpacitySlider
                    width: parent.width
                    from: 0
                    to: 1.0
                    value: settings.highlightOpacity
                    onMoved: settings.highlightOpacity = value
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Label { text: "Notes folder:" }
                Row {
                    width: parent.width
                    spacing: 8
                    TextField {
                        id: notesFolderField
                        width: parent.width - browseButton.width - 8
                        text: settings.notesFolder
                        onEditingFinished: settings.notesFolder = text
                    }
                    Button {
                        id: browseButton
                        text: "Browse…"
                        onClicked: notesFolderDialog.open()
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 8

                Label { text: "Keyboard Shortcuts"; font.bold: true }

                Row {
                    spacing: 8
                    Label { width: 90; text: "Bold"; anchors.verticalCenter: parent.verticalCenter }
                    ShortcutField {
                        sequence: settings.shortcutBold
                        onRecorded: (newSequence) => settings.shortcutBold = newSequence
                    }
                }
                Row {
                    spacing: 8
                    Label { width: 90; text: "Italic"; anchors.verticalCenter: parent.verticalCenter }
                    ShortcutField {
                        sequence: settings.shortcutItalic
                        onRecorded: (newSequence) => settings.shortcutItalic = newSequence
                    }
                }
                Row {
                    spacing: 8
                    Label { width: 90; text: "Strikethrough"; anchors.verticalCenter: parent.verticalCenter }
                    ShortcutField {
                        sequence: settings.shortcutStrikethrough
                        onRecorded: (newSequence) => settings.shortcutStrikethrough = newSequence
                    }
                }
                Row {
                    spacing: 8
                    Label { width: 90; text: "Heading 1"; anchors.verticalCenter: parent.verticalCenter }
                    ShortcutField {
                        sequence: settings.shortcutHeading1
                        onRecorded: (newSequence) => settings.shortcutHeading1 = newSequence
                    }
                }
                Row {
                    spacing: 8
                    Label { width: 90; text: "Heading 2"; anchors.verticalCenter: parent.verticalCenter }
                    ShortcutField {
                        sequence: settings.shortcutHeading2
                        onRecorded: (newSequence) => settings.shortcutHeading2 = newSequence
                    }
                }
                Row {
                    spacing: 8
                    Label { width: 90; text: "Paragraph"; anchors.verticalCenter: parent.verticalCenter }
                    ShortcutField {
                        sequence: settings.shortcutParagraph
                        onRecorded: (newSequence) => settings.shortcutParagraph = newSequence
                    }
                }
                Row {
                    spacing: 8
                    Label { width: 90; text: "Bullet list"; anchors.verticalCenter: parent.verticalCenter }
                    ShortcutField {
                        sequence: settings.shortcutBulletList
                        onRecorded: (newSequence) => settings.shortcutBulletList = newSequence
                    }
                }
                Row {
                    spacing: 8
                    Label { width: 90; text: "Numbered list"; anchors.verticalCenter: parent.verticalCenter }
                    ShortcutField {
                        sequence: settings.shortcutNumberedList
                        onRecorded: (newSequence) => settings.shortcutNumberedList = newSequence
                    }
                }
                Row {
                    spacing: 8
                    Label { width: 90; text: "Insert table"; anchors.verticalCenter: parent.verticalCenter }
                    ShortcutField {
                        sequence: settings.shortcutInsertTable
                        onRecorded: (newSequence) => settings.shortcutInsertTable = newSequence
                    }
                }
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: availableWidth
        visible: settingsTabBar.currentIndex === 1

        Column {
            width: parent.width
            spacing: 12

            // Shown when the build has no integrations compiled in at all, so
            // the page explains itself rather than looking broken.
            Label {
                objectName: "noIntegrationsLabel"
                width: parent.width
                wrapMode: Text.WordWrap
                visible: !tickTickLoader.active
                text: "No integrations are available in this build."
            }

            // TickTick is an optional build-time module, so `tickTick` is null
            // when it was not compiled in. A Loader rather than `visible:`
            // because the bindings inside the section dereference it freely —
            // with the section merely hidden they would still evaluate and log
            // TypeErrors on every settings dialog.
            //
            // The section itself is module-owned QML, loaded by relative URL
            // rather than by type name: the file is only compiled into this
            // QML module when the TickTick module is built (see the guarded
            // qt_target_qml_sources in src/CMakeLists.txt), and `active` is
            // false in exactly the builds where the URL would not resolve.
            Loader {
                id: tickTickLoader
                objectName: "tickTickSection"
                width: parent.width
                active: tickTick !== null
                source: "TickTickSettingsSection.qml"
                onLoaded: item.dialog = root
            }

        }
    }

    // In the footer, not at the bottom of a scrolled column — closing the
    // dialog shouldn't require scrolling to the end of a page to find the
    // button.
    footer: Row {
        padding: 12
        layoutDirection: Qt.RightToLeft
        Button {
            text: "Close"
            onClicked: root.close()
        }
    }
}
