#include "NoteAppearance.h"

NoteAppearance::NoteAppearance(QObject *parent)
    : QObject(parent)
{
}

Settings *NoteAppearance::settings() const
{
    return m_settings;
}

void NoteAppearance::setSettings(Settings *settings)
{
    if (m_settings == settings)
        return;

    if (m_settings)
        m_settings->disconnect(this);

    m_settings = settings;

    if (m_settings) {
        connect(m_settings, &Settings::windowRadiusChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::backgroundColorChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::windowOpacityChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::blurEnabledChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::glassEnabledChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::glassSheenOpacityChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::glassSheenStrengthChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::glassSheenSizeChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::glassTintColorChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::borderWidthChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::borderColorChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::scrollbarWidthChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::fontSizeChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::fontColorChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::fontFamilyChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::highlightColorChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::highlightOpacityChanged, this, &NoteAppearance::resync);
        connect(m_settings, &Settings::noteOverrideChanged, this, [this](const QString &noteId) {
            if (noteId == m_noteId)
                resync();
        });
    }

    emit settingsChanged();
    resync();
}

QString NoteAppearance::noteId() const
{
    return m_noteId;
}

void NoteAppearance::setNoteId(const QString &noteId)
{
    if (m_noteId == noteId)
        return;
    m_noteId = noteId;
    emit noteIdChanged();
    resync();
}

int NoteAppearance::windowRadius() const
{
    return m_override.hasWindowRadius ? m_override.windowRadius
                                       : (m_settings ? m_settings->windowRadius() : m_override.windowRadius);
}

bool NoteAppearance::hasWindowRadiusOverride() const
{
    return m_override.hasWindowRadius;
}

QColor NoteAppearance::backgroundColor() const
{
    return m_override.hasBackgroundColor
        ? m_override.backgroundColor
        : (m_settings ? m_settings->backgroundColor() : m_override.backgroundColor);
}

bool NoteAppearance::hasBackgroundColorOverride() const
{
    return m_override.hasBackgroundColor;
}

qreal NoteAppearance::windowOpacity() const
{
    return m_override.hasWindowOpacity
        ? m_override.windowOpacity
        : (m_settings ? m_settings->windowOpacity() : m_override.windowOpacity);
}

bool NoteAppearance::hasWindowOpacityOverride() const
{
    return m_override.hasWindowOpacity;
}

bool NoteAppearance::blurEnabled() const
{
    return m_override.hasBlurEnabled ? m_override.blurEnabled
                                      : (m_settings ? m_settings->blurEnabled() : m_override.blurEnabled);
}

bool NoteAppearance::hasBlurEnabledOverride() const
{
    return m_override.hasBlurEnabled;
}

bool NoteAppearance::glassEnabled() const
{
    return m_override.hasGlassEnabled ? m_override.glassEnabled
                                       : (m_settings ? m_settings->glassEnabled() : m_override.glassEnabled);
}

bool NoteAppearance::hasGlassEnabledOverride() const
{
    return m_override.hasGlassEnabled;
}

qreal NoteAppearance::glassSheenOpacity() const
{
    return m_override.hasGlassSheenOpacity
        ? m_override.glassSheenOpacity
        : (m_settings ? m_settings->glassSheenOpacity() : m_override.glassSheenOpacity);
}

bool NoteAppearance::hasGlassSheenOpacityOverride() const
{
    return m_override.hasGlassSheenOpacity;
}

qreal NoteAppearance::glassSheenStrength() const
{
    return m_override.hasGlassSheenStrength
        ? m_override.glassSheenStrength
        : (m_settings ? m_settings->glassSheenStrength() : m_override.glassSheenStrength);
}

bool NoteAppearance::hasGlassSheenStrengthOverride() const
{
    return m_override.hasGlassSheenStrength;
}

qreal NoteAppearance::glassSheenSize() const
{
    return m_override.hasGlassSheenSize
        ? m_override.glassSheenSize
        : (m_settings ? m_settings->glassSheenSize() : m_override.glassSheenSize);
}

bool NoteAppearance::hasGlassSheenSizeOverride() const
{
    return m_override.hasGlassSheenSize;
}

QColor NoteAppearance::glassTintColor() const
{
    return m_override.hasGlassTintColor
        ? m_override.glassTintColor
        : (m_settings ? m_settings->glassTintColor() : m_override.glassTintColor);
}

bool NoteAppearance::hasGlassTintColorOverride() const
{
    return m_override.hasGlassTintColor;
}

qreal NoteAppearance::borderWidth() const
{
    return m_override.hasBorderWidth ? m_override.borderWidth
                                      : (m_settings ? m_settings->borderWidth() : m_override.borderWidth);
}

bool NoteAppearance::hasBorderWidthOverride() const
{
    return m_override.hasBorderWidth;
}

QColor NoteAppearance::borderColor() const
{
    return m_override.hasBorderColor ? m_override.borderColor
                                      : (m_settings ? m_settings->borderColor() : m_override.borderColor);
}

bool NoteAppearance::hasBorderColorOverride() const
{
    return m_override.hasBorderColor;
}

qreal NoteAppearance::scrollbarWidth() const
{
    return m_override.hasScrollbarWidth
        ? m_override.scrollbarWidth
        : (m_settings ? m_settings->scrollbarWidth() : m_override.scrollbarWidth);
}

bool NoteAppearance::hasScrollbarWidthOverride() const
{
    return m_override.hasScrollbarWidth;
}

int NoteAppearance::fontSize() const
{
    return m_override.hasFontSize ? m_override.fontSize
                                   : (m_settings ? m_settings->fontSize() : m_override.fontSize);
}

bool NoteAppearance::hasFontSizeOverride() const
{
    return m_override.hasFontSize;
}

QColor NoteAppearance::fontColor() const
{
    return m_override.hasFontColor ? m_override.fontColor
                                    : (m_settings ? m_settings->fontColor() : m_override.fontColor);
}

bool NoteAppearance::hasFontColorOverride() const
{
    return m_override.hasFontColor;
}

QString NoteAppearance::fontFamily() const
{
    return m_override.hasFontFamily ? m_override.fontFamily
                                     : (m_settings ? m_settings->fontFamily() : m_override.fontFamily);
}

bool NoteAppearance::hasFontFamilyOverride() const
{
    return m_override.hasFontFamily;
}

QColor NoteAppearance::highlightColor() const
{
    return m_override.hasHighlightColor
        ? m_override.highlightColor
        : (m_settings ? m_settings->highlightColor() : m_override.highlightColor);
}

bool NoteAppearance::hasHighlightColorOverride() const
{
    return m_override.hasHighlightColor;
}

qreal NoteAppearance::highlightOpacity() const
{
    return m_override.hasHighlightOpacity
        ? m_override.highlightOpacity
        : (m_settings ? m_settings->highlightOpacity() : m_override.highlightOpacity);
}

bool NoteAppearance::hasHighlightOpacityOverride() const
{
    return m_override.hasHighlightOpacity;
}

void NoteAppearance::resync()
{
    m_override = m_settings && !m_noteId.isEmpty() ? m_settings->noteOverride(m_noteId)
                                                     : Settings::NoteAppearanceOverride();

    emit windowRadiusChanged();
    emit backgroundColorChanged();
    emit windowOpacityChanged();
    emit blurEnabledChanged();
    emit glassEnabledChanged();
    emit glassSheenOpacityChanged();
    emit glassSheenStrengthChanged();
    emit glassSheenSizeChanged();
    emit glassTintColorChanged();
    emit borderWidthChanged();
    emit borderColorChanged();
    emit scrollbarWidthChanged();
    emit fontSizeChanged();
    emit fontColorChanged();
    emit fontFamilyChanged();
    emit highlightColorChanged();
    emit highlightOpacityChanged();
}

void NoteAppearance::writeOverride(const std::function<void(Settings::NoteAppearanceOverride &)> &mutate)
{
    if (!m_settings || m_noteId.isEmpty())
        return;

    Settings::NoteAppearanceOverride override = m_settings->noteOverride(m_noteId);
    mutate(override);
    // Settings::setNoteOverride emits noteOverrideChanged, which our own
    // connection (setSettings) routes back into resync() — no need to
    // update m_override or emit our own signals here too.
    m_settings->setNoteOverride(m_noteId, override);
}

void NoteAppearance::setWindowRadiusOverride(int radius)
{
    writeOverride([radius](Settings::NoteAppearanceOverride &o) {
        o.hasWindowRadius = true;
        o.windowRadius = radius;
    });
}

void NoteAppearance::clearWindowRadiusOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasWindowRadius = false; });
}

void NoteAppearance::setBackgroundColorOverride(const QColor &color)
{
    writeOverride([color](Settings::NoteAppearanceOverride &o) {
        o.hasBackgroundColor = true;
        o.backgroundColor = color;
    });
}

void NoteAppearance::clearBackgroundColorOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasBackgroundColor = false; });
}

void NoteAppearance::setWindowOpacityOverride(qreal opacity)
{
    writeOverride([opacity](Settings::NoteAppearanceOverride &o) {
        o.hasWindowOpacity = true;
        o.windowOpacity = opacity;
    });
}

void NoteAppearance::clearWindowOpacityOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasWindowOpacity = false; });
}

void NoteAppearance::setBlurEnabledOverride(bool enabled)
{
    writeOverride([enabled](Settings::NoteAppearanceOverride &o) {
        o.hasBlurEnabled = true;
        o.blurEnabled = enabled;
    });
}

void NoteAppearance::clearBlurEnabledOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasBlurEnabled = false; });
}

void NoteAppearance::setGlassEnabledOverride(bool enabled)
{
    writeOverride([enabled](Settings::NoteAppearanceOverride &o) {
        o.hasGlassEnabled = true;
        o.glassEnabled = enabled;
    });
}

void NoteAppearance::clearGlassEnabledOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasGlassEnabled = false; });
}

void NoteAppearance::setGlassSheenOpacityOverride(qreal opacity)
{
    writeOverride([opacity](Settings::NoteAppearanceOverride &o) {
        o.hasGlassSheenOpacity = true;
        o.glassSheenOpacity = opacity;
    });
}

void NoteAppearance::clearGlassSheenOpacityOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasGlassSheenOpacity = false; });
}

void NoteAppearance::setGlassSheenStrengthOverride(qreal strength)
{
    writeOverride([strength](Settings::NoteAppearanceOverride &o) {
        o.hasGlassSheenStrength = true;
        o.glassSheenStrength = strength;
    });
}

void NoteAppearance::clearGlassSheenStrengthOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasGlassSheenStrength = false; });
}

void NoteAppearance::setGlassSheenSizeOverride(qreal size)
{
    writeOverride([size](Settings::NoteAppearanceOverride &o) {
        o.hasGlassSheenSize = true;
        o.glassSheenSize = size;
    });
}

void NoteAppearance::clearGlassSheenSizeOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasGlassSheenSize = false; });
}

void NoteAppearance::setGlassTintColorOverride(const QColor &color)
{
    writeOverride([color](Settings::NoteAppearanceOverride &o) {
        o.hasGlassTintColor = true;
        o.glassTintColor = color;
    });
}

void NoteAppearance::clearGlassTintColorOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasGlassTintColor = false; });
}

void NoteAppearance::setBorderWidthOverride(qreal width)
{
    writeOverride([width](Settings::NoteAppearanceOverride &o) {
        o.hasBorderWidth = true;
        o.borderWidth = width;
    });
}

void NoteAppearance::clearBorderWidthOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasBorderWidth = false; });
}

void NoteAppearance::setBorderColorOverride(const QColor &color)
{
    writeOverride([color](Settings::NoteAppearanceOverride &o) {
        o.hasBorderColor = true;
        o.borderColor = color;
    });
}

void NoteAppearance::clearBorderColorOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasBorderColor = false; });
}

void NoteAppearance::setScrollbarWidthOverride(qreal width)
{
    writeOverride([width](Settings::NoteAppearanceOverride &o) {
        o.hasScrollbarWidth = true;
        o.scrollbarWidth = width;
    });
}

void NoteAppearance::clearScrollbarWidthOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasScrollbarWidth = false; });
}

void NoteAppearance::setFontSizeOverride(int size)
{
    writeOverride([size](Settings::NoteAppearanceOverride &o) {
        o.hasFontSize = true;
        o.fontSize = size;
    });
}

void NoteAppearance::clearFontSizeOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasFontSize = false; });
}

void NoteAppearance::setFontColorOverride(const QColor &color)
{
    writeOverride([color](Settings::NoteAppearanceOverride &o) {
        o.hasFontColor = true;
        o.fontColor = color;
    });
}

void NoteAppearance::clearFontColorOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasFontColor = false; });
}

void NoteAppearance::setFontFamilyOverride(const QString &family)
{
    writeOverride([family](Settings::NoteAppearanceOverride &o) {
        o.hasFontFamily = true;
        o.fontFamily = family;
    });
}

void NoteAppearance::clearFontFamilyOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasFontFamily = false; });
}

void NoteAppearance::setHighlightColorOverride(const QColor &color)
{
    writeOverride([color](Settings::NoteAppearanceOverride &o) {
        o.hasHighlightColor = true;
        o.highlightColor = color;
    });
}

void NoteAppearance::clearHighlightColorOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasHighlightColor = false; });
}

void NoteAppearance::setHighlightOpacityOverride(qreal opacity)
{
    writeOverride([opacity](Settings::NoteAppearanceOverride &o) {
        o.hasHighlightOpacity = true;
        o.highlightOpacity = opacity;
    });
}

void NoteAppearance::clearHighlightOpacityOverride()
{
    writeOverride([](Settings::NoteAppearanceOverride &o) { o.hasHighlightOpacity = false; });
}
