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
    readonly property alias fontFamilyCombo: fontFamilyCombo
    readonly property alias fontFamilyOverrideCheckBox: fontFamilyOverride
    // Exposed for tests only, same convention as the two above. Without
    // these, a test has to find the boxes by walking the item tree for their
    // label text — which cannot tell code mode's "Enabled" box apart from the
    // identically-labelled one next to every other override here except by
    // sibling index. Deliberately ITEM aliases, never a pass-through to
    // `.checked`: see NoteHud.qml's pinGlyphItem for the AOT staleness trap a
    // value alias hits under qmlcachegen.
    readonly property alias codeModeOverrideCheckBox: codeModeOverride
    readonly property alias codeModeEnabledCheckBox: codeModeEnabledBox
    readonly property alias codeModeDisablesMarkdownOverrideCheckBox: codeModeDisablesMarkdownOverride
    readonly property alias codeModeDisablesMarkdownCheckBox: codeModeDisablesMarkdownBox
    readonly property alias codeModeDisablesAutoFormattingOverrideCheckBox: codeModeDisablesAutoFormattingOverride
    readonly property alias codeModeDisablesAutoFormattingCheckBox: codeModeDisablesAutoFormattingBox
    readonly property alias codeModeDisablesSpellcheckOverrideCheckBox: codeModeDisablesSpellcheckOverride
    readonly property alias codeModeDisablesSpellcheckCheckBox: codeModeDisablesSpellcheckBox

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

    Platform.ColorDialog {
        id: highlightColorDialog
        currentColor: root.noteAppearance.highlightColor
        onAccepted: root.noteAppearance.setHighlightColorOverride(color)
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
                // WheelSafeSlider, not Slider: the desktop style lets the
                // wheel drag a slider's value, which in this dialog means a
                // stray scroll silently rewrites a saved setting. See
                // WheelSafeSlider.qml.
                WheelSafeSlider {
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
                WheelSafeSlider {
                    id: opacitySlider
                    width: parent.width
                    enabled: opacityOverride.checked
                    from: 0
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

            // Code mode (feature request). Same two-checkbox idiom as every
            // other override here, but grouped: code mode is the one setting on
            // this popup that changes what the note IS rather than how it looks
            // — it swaps the font, adds the line-number gutter, and switches
            // off markdown styling, the as-you-type list rewrites and
            // spellcheck.
            //
            // Those last three each get their own override too, so a note can
            // both opt into code mode AND disagree with the global answer about
            // what code mode entails (e.g. a code note that still wants
            // spellcheck). They are NOT gated on codeModeOverride: they resolve
            // independently of whether this note is currently in code mode, and
            // the common case is a note that got there via the global default
            // with no code-mode override of its own — greying them out then
            // would make them look inapplicable to exactly the note they apply
            // to. Same reasoning as the tray Settings dialog's own Code Mode
            // section, which holds the global defaults these fall back to.
            Column {
                width: parent.width
                spacing: 8

                Row {
                    spacing: 8
                    CheckBox {
                        id: codeModeOverride
                        text: "Override code mode"
                        checked: root.noteAppearance.hasCodeModeEnabledOverride
                        onToggled: checked ? root.noteAppearance.setCodeModeEnabledOverride(root.noteAppearance.codeModeEnabled)
                                           : root.noteAppearance.clearCodeModeEnabledOverride()
                    }
                    CheckBox {
                        id: codeModeEnabledBox
                        text: "Enabled"
                        enabled: codeModeOverride.checked
                        checked: root.noteAppearance.codeModeEnabled
                        onToggled: root.noteAppearance.setCodeModeEnabledOverride(checked)
                    }
                }

                Label { text: "While code mode is on, also:"; opacity: 0.7 }

                Column {
                    width: parent.width
                    spacing: 4
                    // Indented to read as belonging to the label above, the
                    // same way the tray dialog indents these three.
                    leftPadding: 12

                    Row {
                        spacing: 8
                        CheckBox {
                            id: codeModeDisablesMarkdownOverride
                            text: "Override"
                            checked: root.noteAppearance.hasCodeModeDisablesMarkdownHighlightingOverride
                            onToggled: checked ? root.noteAppearance.setCodeModeDisablesMarkdownHighlightingOverride(root.noteAppearance.codeModeDisablesMarkdownHighlighting)
                                               : root.noteAppearance.clearCodeModeDisablesMarkdownHighlightingOverride()
                        }
                        CheckBox {
                            id: codeModeDisablesMarkdownBox
                            text: "Turn off markdown styling"
                            enabled: codeModeDisablesMarkdownOverride.checked
                            checked: root.noteAppearance.codeModeDisablesMarkdownHighlighting
                            onToggled: root.noteAppearance.setCodeModeDisablesMarkdownHighlightingOverride(checked)
                        }
                    }

                    Row {
                        spacing: 8
                        CheckBox {
                            id: codeModeDisablesAutoFormattingOverride
                            text: "Override"
                            checked: root.noteAppearance.hasCodeModeDisablesAutoFormattingOverride
                            onToggled: checked ? root.noteAppearance.setCodeModeDisablesAutoFormattingOverride(root.noteAppearance.codeModeDisablesAutoFormatting)
                                               : root.noteAppearance.clearCodeModeDisablesAutoFormattingOverride()
                        }
                        CheckBox {
                            id: codeModeDisablesAutoFormattingBox
                            text: "Turn off auto-formatting"
                            enabled: codeModeDisablesAutoFormattingOverride.checked
                            checked: root.noteAppearance.codeModeDisablesAutoFormatting
                            onToggled: root.noteAppearance.setCodeModeDisablesAutoFormattingOverride(checked)
                        }
                    }

                    Row {
                        spacing: 8
                        CheckBox {
                            id: codeModeDisablesSpellcheckOverride
                            text: "Override"
                            checked: root.noteAppearance.hasCodeModeDisablesSpellcheckOverride
                            onToggled: checked ? root.noteAppearance.setCodeModeDisablesSpellcheckOverride(root.noteAppearance.codeModeDisablesSpellcheck)
                                               : root.noteAppearance.clearCodeModeDisablesSpellcheckOverride()
                        }
                        CheckBox {
                            id: codeModeDisablesSpellcheckBox
                            text: "Turn off spellcheck"
                            enabled: codeModeDisablesSpellcheckOverride.checked
                            checked: root.noteAppearance.codeModeDisablesSpellcheck
                            onToggled: root.noteAppearance.setCodeModeDisablesSpellcheckOverride(checked)
                        }
                    }
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
                WheelSafeSlider {
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
                WheelSafeSlider {
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
                WheelSafeSlider {
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
                WheelSafeSlider {
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
                WheelSafeSlider {
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
                    // The KDE desktop style (org.kde.desktop) forces
                    // wheelEnabled: true on every ComboBox and SpinBox, so a
                    // wheel passing over one would silently write a per-note
                    // override. Opting out also lets the event through to the
                    // enclosing ScrollView, so the popup scrolls instead.
                    wheelEnabled: false
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
                    id: highlightColorOverride
                    text: "Override highlight color"
                    checked: root.noteAppearance.hasHighlightColorOverride
                    onToggled: checked ? root.noteAppearance.setHighlightColorOverride(root.noteAppearance.highlightColor)
                                       : root.noteAppearance.clearHighlightColorOverride()
                }
                Rectangle {
                    width: 24
                    height: 24
                    color: root.noteAppearance.highlightColor
                    border.color: "gray"
                    enabled: highlightColorOverride.checked
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; onClicked: highlightColorDialog.open() }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                CheckBox {
                    id: highlightOpacityOverride
                    text: "Override highlight opacity"
                    checked: root.noteAppearance.hasHighlightOpacityOverride
                    onToggled: checked ? root.noteAppearance.setHighlightOpacityOverride(root.noteAppearance.highlightOpacity)
                                       : root.noteAppearance.clearHighlightOpacityOverride()
                }
                Label { text: "Highlight opacity: " + highlightOpacitySlider.value.toFixed(2); enabled: highlightOpacityOverride.checked }
                WheelSafeSlider {
                    id: highlightOpacitySlider
                    width: parent.width
                    enabled: highlightOpacityOverride.checked
                    from: 0
                    to: 1.0
                    value: root.noteAppearance.highlightOpacity
                    onMoved: root.noteAppearance.setHighlightOpacityOverride(value)
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
                ComboBox {
                    id: fontFamilyCombo
                    enabled: fontFamilyOverride.checked
                    // Same style opt-out as the font size spin box above.
                    wheelEnabled: false
                    model: Qt.fontFamilies()
                    // Previews the selected family in the closed box; each
                    // dropdown entry gets its own font via the delegate below.
                    font.family: currentText
                    currentIndex: {
                        var idx = find(root.noteAppearance.fontFamily)
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
                    onActivated: root.noteAppearance.setFontFamilyOverride(currentText)
                }
            }

            Button {
                text: "Close"
                onClicked: root.close()
            }
        }
    }
}
