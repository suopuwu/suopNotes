import QtQuick
import QtQuick.Controls
import Qt.labs.platform as Platform
import SuopNotesUi

// Per-note override editor (req. 4): the tray Settings dialog's values are
// the default for every note; this window lets ONE note override specific
// fields. Each field has its own "Override" checkbox — off means "use the
// default," matching NoteAppearance's own hasXOverride/clearXOverride model
// exactly, so there's no separate draft/apply step here either (same
// immediate-persist philosophy as SettingsDialog.qml).
ApplicationWindow {
    id: root

    required property NoteAppearance noteAppearance

    title: "Note Settings"
    width: 380
    height: 560
    visible: false

    Platform.ColorDialog {
        id: backgroundColorDialog
        currentColor: root.noteAppearance.backgroundColor
        onAccepted: root.noteAppearance.setBackgroundColorOverride(color)
    }

    Platform.ColorDialog {
        id: borderColorDialog
        currentColor: root.noteAppearance.borderColor
        onAccepted: root.noteAppearance.setBorderColorOverride(color)
    }

    Platform.ColorDialog {
        id: glassTintColorDialog
        currentColor: root.noteAppearance.glassTintColor
        onAccepted: root.noteAppearance.setGlassTintColorOverride(color)
    }

    Platform.ColorDialog {
        id: fontColorDialog
        currentColor: root.noteAppearance.fontColor
        onAccepted: root.noteAppearance.setFontColorOverride(color)
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: availableWidth

        Column {
            width: parent.width
            spacing: 14

            Column {
                width: parent.width
                spacing: 4
                Row {
                    width: parent.width
                    CheckBox {
                        id: radiusOverride
                        text: "Override window radius"
                        checked: root.noteAppearance.hasWindowRadiusOverride
                        onToggled: checked ? root.noteAppearance.setWindowRadiusOverride(root.noteAppearance.windowRadius)
                                           : root.noteAppearance.clearWindowRadiusOverride()
                    }
                }
                Label { text: "Radius: " + Math.round(radiusSlider.value); enabled: radiusOverride.checked }
                Slider {
                    id: radiusSlider
                    width: parent.width
                    enabled: radiusOverride.checked
                    from: 0
                    to: 32
                    value: root.noteAppearance.windowRadius
                    onMoved: root.noteAppearance.setWindowRadiusOverride(Math.round(value))
                }
            }

            Column {
                width: parent.width
                spacing: 4
                CheckBox {
                    id: opacityOverride
                    text: "Override background opacity"
                    checked: root.noteAppearance.hasWindowOpacityOverride
                    onToggled: checked ? root.noteAppearance.setWindowOpacityOverride(root.noteAppearance.windowOpacity)
                                       : root.noteAppearance.clearWindowOpacityOverride()
                }
                Label { text: "Opacity: " + opacitySlider.value.toFixed(2); enabled: opacityOverride.checked }
                Slider {
                    id: opacitySlider
                    width: parent.width
                    enabled: opacityOverride.checked
                    from: 0.1
                    to: 1.0
                    value: root.noteAppearance.windowOpacity
                    onMoved: root.noteAppearance.setWindowOpacityOverride(value)
                }
            }

            Row {
                spacing: 8
                CheckBox {
                    id: backgroundColorOverride
                    text: "Override background color"
                    checked: root.noteAppearance.hasBackgroundColorOverride
                    onToggled: checked ? root.noteAppearance.setBackgroundColorOverride(root.noteAppearance.backgroundColor)
                                       : root.noteAppearance.clearBackgroundColorOverride()
                }
                Rectangle {
                    width: 24
                    height: 24
                    color: root.noteAppearance.backgroundColor
                    border.color: "gray"
                    enabled: backgroundColorOverride.checked
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; onClicked: backgroundColorDialog.open() }
                }
            }

            Row {
                spacing: 8
                CheckBox {
                    id: blurOverride
                    text: "Override blur"
                    checked: root.noteAppearance.hasBlurEnabledOverride
                    onToggled: checked ? root.noteAppearance.setBlurEnabledOverride(root.noteAppearance.blurEnabled)
                                       : root.noteAppearance.clearBlurEnabledOverride()
                }
                CheckBox {
                    text: "Enabled"
                    enabled: blurOverride.checked
                    checked: root.noteAppearance.blurEnabled
                    onToggled: root.noteAppearance.setBlurEnabledOverride(checked)
                }
            }

            Row {
                spacing: 8
                CheckBox {
                    id: glassOverride
                    text: "Override glass effect"
                    checked: root.noteAppearance.hasGlassEnabledOverride
                    onToggled: checked ? root.noteAppearance.setGlassEnabledOverride(root.noteAppearance.glassEnabled)
                                       : root.noteAppearance.clearGlassEnabledOverride()
                }
                CheckBox {
                    text: "Enabled"
                    enabled: glassOverride.checked
                    checked: root.noteAppearance.glassEnabled
                    onToggled: root.noteAppearance.setGlassEnabledOverride(checked)
                }
            }

            Column {
                width: parent.width
                spacing: 4
                CheckBox {
                    id: glassSheenOpacityOverride
                    text: "Override glass sheen opacity"
                    checked: root.noteAppearance.hasGlassSheenOpacityOverride
                    onToggled: checked ? root.noteAppearance.setGlassSheenOpacityOverride(root.noteAppearance.glassSheenOpacity)
                                       : root.noteAppearance.clearGlassSheenOpacityOverride()
                }
                Label { text: "Sheen opacity: " + glassSheenOpacitySlider.value.toFixed(2); enabled: glassSheenOpacityOverride.checked }
                Slider {
                    id: glassSheenOpacitySlider
                    width: parent.width
                    enabled: glassSheenOpacityOverride.checked
                    from: 0.0
                    to: 0.4
                    value: root.noteAppearance.glassSheenOpacity
                    onMoved: root.noteAppearance.setGlassSheenOpacityOverride(value)
                }
            }

            Column {
                width: parent.width
                spacing: 4
                CheckBox {
                    id: glassSheenStrengthOverride
                    text: "Override glass sheen strength"
                    checked: root.noteAppearance.hasGlassSheenStrengthOverride
                    onToggled: checked ? root.noteAppearance.setGlassSheenStrengthOverride(root.noteAppearance.glassSheenStrength)
                                       : root.noteAppearance.clearGlassSheenStrengthOverride()
                }
                Label { text: "Sheen strength: " + Math.round(glassSheenStrengthSlider.value); enabled: glassSheenStrengthOverride.checked }
                Slider {
                    id: glassSheenStrengthSlider
                    width: parent.width
                    enabled: glassSheenStrengthOverride.checked
                    from: 0
                    to: 100
                    value: root.noteAppearance.glassSheenStrength
                    onMoved: root.noteAppearance.setGlassSheenStrengthOverride(Math.round(value))
                }
            }

            Column {
                width: parent.width
                spacing: 4
                CheckBox {
                    id: glassSheenSizeOverride
                    text: "Override glass sheen size"
                    checked: root.noteAppearance.hasGlassSheenSizeOverride
                    onToggled: checked ? root.noteAppearance.setGlassSheenSizeOverride(root.noteAppearance.glassSheenSize)
                                       : root.noteAppearance.clearGlassSheenSizeOverride()
                }
                Label { text: "Sheen size: " + glassSheenSizeSlider.value.toFixed(2); enabled: glassSheenSizeOverride.checked }
                Slider {
                    id: glassSheenSizeSlider
                    width: parent.width
                    enabled: glassSheenSizeOverride.checked
                    from: 0.05
                    to: 0.5
                    value: root.noteAppearance.glassSheenSize
                    onMoved: root.noteAppearance.setGlassSheenSizeOverride(value)
                }
            }

            Row {
                spacing: 8
                CheckBox {
                    id: glassTintColorOverride
                    text: "Override glass color"
                    checked: root.noteAppearance.hasGlassTintColorOverride
                    onToggled: checked ? root.noteAppearance.setGlassTintColorOverride(root.noteAppearance.glassTintColor)
                                       : root.noteAppearance.clearGlassTintColorOverride()
                }
                Rectangle {
                    width: 24
                    height: 24
                    color: root.noteAppearance.glassTintColor
                    border.color: "gray"
                    enabled: glassTintColorOverride.checked
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; onClicked: glassTintColorDialog.open() }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                CheckBox {
                    id: borderWidthOverride
                    text: "Override border width"
                    checked: root.noteAppearance.hasBorderWidthOverride
                    onToggled: checked ? root.noteAppearance.setBorderWidthOverride(root.noteAppearance.borderWidth)
                                       : root.noteAppearance.clearBorderWidthOverride()
                }
                Label { text: "Border width: " + Math.round(borderWidthSlider.value); enabled: borderWidthOverride.checked }
                Slider {
                    id: borderWidthSlider
                    width: parent.width
                    enabled: borderWidthOverride.checked
                    from: 0
                    to: 12
                    value: root.noteAppearance.borderWidth
                    onMoved: root.noteAppearance.setBorderWidthOverride(Math.round(value))
                }
            }

            Row {
                spacing: 8
                CheckBox {
                    id: borderColorOverride
                    text: "Override border color"
                    checked: root.noteAppearance.hasBorderColorOverride
                    onToggled: checked ? root.noteAppearance.setBorderColorOverride(root.noteAppearance.borderColor)
                                       : root.noteAppearance.clearBorderColorOverride()
                }
                Rectangle {
                    width: 24
                    height: 24
                    color: root.noteAppearance.borderColor
                    border.color: "gray"
                    enabled: borderColorOverride.checked
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; onClicked: borderColorDialog.open() }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                CheckBox {
                    id: scrollbarWidthOverride
                    text: "Override scrollbar width"
                    checked: root.noteAppearance.hasScrollbarWidthOverride
                    onToggled: checked ? root.noteAppearance.setScrollbarWidthOverride(root.noteAppearance.scrollbarWidth)
                                       : root.noteAppearance.clearScrollbarWidthOverride()
                }
                Label { text: "Scrollbar width: " + Math.round(scrollbarWidthSlider.value); enabled: scrollbarWidthOverride.checked }
                Slider {
                    id: scrollbarWidthSlider
                    width: parent.width
                    enabled: scrollbarWidthOverride.checked
                    from: 0
                    to: 16
                    value: root.noteAppearance.scrollbarWidth
                    onMoved: root.noteAppearance.setScrollbarWidthOverride(Math.round(value))
                }
            }

            Row {
                spacing: 8
                CheckBox {
                    id: fontSizeOverride
                    text: "Override font size"
                    checked: root.noteAppearance.hasFontSizeOverride
                    onToggled: checked ? root.noteAppearance.setFontSizeOverride(root.noteAppearance.fontSize)
                                       : root.noteAppearance.clearFontSizeOverride()
                }
                SpinBox {
                    enabled: fontSizeOverride.checked
                    from: 8
                    to: 48
                    value: root.noteAppearance.fontSize
                    onValueModified: root.noteAppearance.setFontSizeOverride(value)
                }
            }

            Row {
                spacing: 8
                CheckBox {
                    id: fontColorOverride
                    text: "Override font color"
                    checked: root.noteAppearance.hasFontColorOverride
                    onToggled: checked ? root.noteAppearance.setFontColorOverride(root.noteAppearance.fontColor)
                                       : root.noteAppearance.clearFontColorOverride()
                }
                Rectangle {
                    width: 24
                    height: 24
                    color: root.noteAppearance.fontColor
                    border.color: "gray"
                    enabled: fontColorOverride.checked
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; onClicked: fontColorDialog.open() }
                }
            }

            Row {
                spacing: 8
                CheckBox {
                    id: fontFamilyOverride
                    text: "Override font family"
                    checked: root.noteAppearance.hasFontFamilyOverride
                    onToggled: checked ? root.noteAppearance.setFontFamilyOverride(root.noteAppearance.fontFamily)
                                       : root.noteAppearance.clearFontFamilyOverride()
                }
                TextField {
                    enabled: fontFamilyOverride.checked
                    text: root.noteAppearance.fontFamily
                    onEditingFinished: root.noteAppearance.setFontFamilyOverride(text)
                }
            }

            Button {
                text: "Close"
                onClicked: root.close()
            }
        }
    }
}
