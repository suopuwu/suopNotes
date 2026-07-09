import QtQuick
import QtQuick.Controls
import Qt.labs.platform as Platform

// A normal, framed top-level window (not a Popup — this app has no main
// window to host one in, and unlike note windows this genuinely benefits
// from native chrome/a taskbar entry while open).
ApplicationWindow {
    id: root

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

    ScrollView {
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: availableWidth

        Column {
            width: parent.width
            spacing: 12

            Column {
                width: parent.width
                spacing: 4
                Label { text: "Window radius: " + Math.round(radiusSlider.value) }
                Slider {
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
                Slider {
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
                text: "Enable glass effect"
                checked: settings.glassEnabled
                onToggled: settings.glassEnabled = checked
            }

            Column {
                width: parent.width
                spacing: 4
                Label { text: "Glass sheen opacity: " + glassSheenOpacitySlider.value.toFixed(2) }
                Slider {
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
                Slider {
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
                Slider {
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
                    Slider {
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
                Slider {
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
                Slider {
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
                    from: 8
                    to: 48
                    value: settings.fontSize
                    onValueModified: settings.fontSize = value
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
                Slider {
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

            Button {
                text: "Close"
                onClicked: root.close()
            }
        }
    }
}
