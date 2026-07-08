#pragma once

#include <QObject>
#include <QColor>
#include <QString>
#include <functional>

#include "Settings.h"

// QML-instantiable per-note appearance resolver (req. 4): for every
// appearance field, exposes the note's own override if one is set, else the
// matching global Settings value — so the tray Settings dialog acts as the
// default for every note, while an individual note's hover settings popup
// can override specific fields. Re-resolves whenever either the relevant
// global Settings property OR this note's own override group changes.
//
// One instance per NoteWindow (constructed in QML like DocumentHandler),
// bound via `settings`/`noteId` properties rather than passed in a
// constructor, matching the rest of this app's QML-instantiable C++ types.
class NoteAppearance : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Settings *settings READ settings WRITE setSettings NOTIFY settingsChanged)
    Q_PROPERTY(QString noteId READ noteId WRITE setNoteId NOTIFY noteIdChanged)

    Q_PROPERTY(int windowRadius READ windowRadius NOTIFY windowRadiusChanged)
    Q_PROPERTY(bool hasWindowRadiusOverride READ hasWindowRadiusOverride NOTIFY windowRadiusChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(bool hasBackgroundColorOverride READ hasBackgroundColorOverride NOTIFY backgroundColorChanged)
    Q_PROPERTY(qreal windowOpacity READ windowOpacity NOTIFY windowOpacityChanged)
    Q_PROPERTY(bool hasWindowOpacityOverride READ hasWindowOpacityOverride NOTIFY windowOpacityChanged)
    Q_PROPERTY(bool blurEnabled READ blurEnabled NOTIFY blurEnabledChanged)
    Q_PROPERTY(bool hasBlurEnabledOverride READ hasBlurEnabledOverride NOTIFY blurEnabledChanged)
    Q_PROPERTY(bool glassEnabled READ glassEnabled NOTIFY glassEnabledChanged)
    Q_PROPERTY(bool hasGlassEnabledOverride READ hasGlassEnabledOverride NOTIFY glassEnabledChanged)
    Q_PROPERTY(qreal glassSheenOpacity READ glassSheenOpacity NOTIFY glassSheenOpacityChanged)
    Q_PROPERTY(bool hasGlassSheenOpacityOverride READ hasGlassSheenOpacityOverride NOTIFY glassSheenOpacityChanged)
    Q_PROPERTY(qreal glassSheenStrength READ glassSheenStrength NOTIFY glassSheenStrengthChanged)
    Q_PROPERTY(bool hasGlassSheenStrengthOverride READ hasGlassSheenStrengthOverride NOTIFY glassSheenStrengthChanged)
    Q_PROPERTY(qreal glassSheenSize READ glassSheenSize NOTIFY glassSheenSizeChanged)
    Q_PROPERTY(bool hasGlassSheenSizeOverride READ hasGlassSheenSizeOverride NOTIFY glassSheenSizeChanged)
    Q_PROPERTY(QColor glassTintColor READ glassTintColor NOTIFY glassTintColorChanged)
    Q_PROPERTY(bool hasGlassTintColorOverride READ hasGlassTintColorOverride NOTIFY glassTintColorChanged)
    Q_PROPERTY(qreal borderWidth READ borderWidth NOTIFY borderWidthChanged)
    Q_PROPERTY(bool hasBorderWidthOverride READ hasBorderWidthOverride NOTIFY borderWidthChanged)
    Q_PROPERTY(QColor borderColor READ borderColor NOTIFY borderColorChanged)
    Q_PROPERTY(bool hasBorderColorOverride READ hasBorderColorOverride NOTIFY borderColorChanged)
    Q_PROPERTY(qreal scrollbarWidth READ scrollbarWidth NOTIFY scrollbarWidthChanged)
    Q_PROPERTY(bool hasScrollbarWidthOverride READ hasScrollbarWidthOverride NOTIFY scrollbarWidthChanged)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(bool hasFontSizeOverride READ hasFontSizeOverride NOTIFY fontSizeChanged)
    Q_PROPERTY(QColor fontColor READ fontColor NOTIFY fontColorChanged)
    Q_PROPERTY(bool hasFontColorOverride READ hasFontColorOverride NOTIFY fontColorChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(bool hasFontFamilyOverride READ hasFontFamilyOverride NOTIFY fontFamilyChanged)
    Q_PROPERTY(QColor highlightColor READ highlightColor NOTIFY highlightColorChanged)
    Q_PROPERTY(bool hasHighlightColorOverride READ hasHighlightColorOverride NOTIFY highlightColorChanged)
    Q_PROPERTY(qreal highlightOpacity READ highlightOpacity NOTIFY highlightOpacityChanged)
    Q_PROPERTY(bool hasHighlightOpacityOverride READ hasHighlightOpacityOverride NOTIFY highlightOpacityChanged)

public:
    explicit NoteAppearance(QObject *parent = nullptr);

    Settings *settings() const;
    void setSettings(Settings *settings);
    QString noteId() const;
    void setNoteId(const QString &noteId);

    int windowRadius() const;
    bool hasWindowRadiusOverride() const;
    QColor backgroundColor() const;
    bool hasBackgroundColorOverride() const;
    qreal windowOpacity() const;
    bool hasWindowOpacityOverride() const;
    bool blurEnabled() const;
    bool hasBlurEnabledOverride() const;
    bool glassEnabled() const;
    bool hasGlassEnabledOverride() const;
    qreal glassSheenOpacity() const;
    bool hasGlassSheenOpacityOverride() const;
    qreal glassSheenStrength() const;
    bool hasGlassSheenStrengthOverride() const;
    qreal glassSheenSize() const;
    bool hasGlassSheenSizeOverride() const;
    QColor glassTintColor() const;
    bool hasGlassTintColorOverride() const;
    qreal borderWidth() const;
    bool hasBorderWidthOverride() const;
    QColor borderColor() const;
    bool hasBorderColorOverride() const;
    qreal scrollbarWidth() const;
    bool hasScrollbarWidthOverride() const;
    int fontSize() const;
    bool hasFontSizeOverride() const;
    QColor fontColor() const;
    bool hasFontColorOverride() const;
    QString fontFamily() const;
    bool hasFontFamilyOverride() const;
    QColor highlightColor() const;
    bool hasHighlightColorOverride() const;
    qreal highlightOpacity() const;
    bool hasHighlightOpacityOverride() const;

    Q_INVOKABLE void setWindowRadiusOverride(int radius);
    Q_INVOKABLE void clearWindowRadiusOverride();
    Q_INVOKABLE void setBackgroundColorOverride(const QColor &color);
    Q_INVOKABLE void clearBackgroundColorOverride();
    Q_INVOKABLE void setWindowOpacityOverride(qreal opacity);
    Q_INVOKABLE void clearWindowOpacityOverride();
    Q_INVOKABLE void setBlurEnabledOverride(bool enabled);
    Q_INVOKABLE void clearBlurEnabledOverride();
    Q_INVOKABLE void setGlassEnabledOverride(bool enabled);
    Q_INVOKABLE void clearGlassEnabledOverride();
    Q_INVOKABLE void setGlassSheenOpacityOverride(qreal opacity);
    Q_INVOKABLE void clearGlassSheenOpacityOverride();
    Q_INVOKABLE void setGlassSheenStrengthOverride(qreal strength);
    Q_INVOKABLE void clearGlassSheenStrengthOverride();
    Q_INVOKABLE void setGlassSheenSizeOverride(qreal size);
    Q_INVOKABLE void clearGlassSheenSizeOverride();
    Q_INVOKABLE void setGlassTintColorOverride(const QColor &color);
    Q_INVOKABLE void clearGlassTintColorOverride();
    Q_INVOKABLE void setBorderWidthOverride(qreal width);
    Q_INVOKABLE void clearBorderWidthOverride();
    Q_INVOKABLE void setBorderColorOverride(const QColor &color);
    Q_INVOKABLE void clearBorderColorOverride();
    Q_INVOKABLE void setScrollbarWidthOverride(qreal width);
    Q_INVOKABLE void clearScrollbarWidthOverride();
    Q_INVOKABLE void setFontSizeOverride(int size);
    Q_INVOKABLE void clearFontSizeOverride();
    Q_INVOKABLE void setFontColorOverride(const QColor &color);
    Q_INVOKABLE void clearFontColorOverride();
    Q_INVOKABLE void setFontFamilyOverride(const QString &family);
    Q_INVOKABLE void clearFontFamilyOverride();
    Q_INVOKABLE void setHighlightColorOverride(const QColor &color);
    Q_INVOKABLE void clearHighlightColorOverride();
    Q_INVOKABLE void setHighlightOpacityOverride(qreal opacity);
    Q_INVOKABLE void clearHighlightOpacityOverride();

signals:
    void settingsChanged();
    void noteIdChanged();
    void windowRadiusChanged();
    void backgroundColorChanged();
    void windowOpacityChanged();
    void blurEnabledChanged();
    void glassEnabledChanged();
    void glassSheenOpacityChanged();
    void glassSheenStrengthChanged();
    void glassSheenSizeChanged();
    void glassTintColorChanged();
    void borderWidthChanged();
    void borderColorChanged();
    void scrollbarWidthChanged();
    void fontSizeChanged();
    void fontColorChanged();
    void fontFamilyChanged();
    void highlightColorChanged();
    void highlightOpacityChanged();

private:
    // Re-reads settings->noteOverride(m_noteId) and emits every field's
    // NOTIFY signal — settings/overrides change rarely (same reasoning as
    // Settings itself), so there's no value in diffing field-by-field to
    // suppress redundant emits.
    void resync();
    // Persists a single-field change back through Settings::setNoteOverride,
    // starting from the note's current full override so other fields'
    // has*/value pairs are preserved untouched.
    void writeOverride(const std::function<void(Settings::NoteAppearanceOverride &)> &mutate);

    Settings *m_settings = nullptr;
    QString m_noteId;
    Settings::NoteAppearanceOverride m_override;
};
