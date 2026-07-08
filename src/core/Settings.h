#pragma once

#include <QObject>
#include <QColor>
#include <QString>
#include <QList>

class QSettings;

// Settings persists app-wide appearance configuration and session state
// (which notes were open, and where) to disk via QSettings (INI format),
// stored under QStandardPaths::AppConfigLocation by default. Every setter
// persists synchronously (via QSettings::sync()) so changes survive an
// immediate crash and are visible to a second Settings instance pointed
// at the same file — this is deliberate: settings change infrequently,
// so there's no need for the debouncing NoteStore uses for note text.
class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString notesFolder READ notesFolder WRITE setNotesFolder NOTIFY notesFolderChanged)
    Q_PROPERTY(int windowRadius READ windowRadius WRITE setWindowRadius NOTIFY windowRadiusChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(qreal windowOpacity READ windowOpacity WRITE setWindowOpacity NOTIFY windowOpacityChanged)
    Q_PROPERTY(bool blurEnabled READ blurEnabled WRITE setBlurEnabled NOTIFY blurEnabledChanged)
    Q_PROPERTY(bool glassEnabled READ glassEnabled WRITE setGlassEnabled NOTIFY glassEnabledChanged)
    Q_PROPERTY(qreal glassSheenOpacity READ glassSheenOpacity WRITE setGlassSheenOpacity NOTIFY glassSheenOpacityChanged)
    Q_PROPERTY(qreal glassSheenStrength READ glassSheenStrength WRITE setGlassSheenStrength NOTIFY glassSheenStrengthChanged)
    Q_PROPERTY(qreal glassSheenSize READ glassSheenSize WRITE setGlassSheenSize NOTIFY glassSheenSizeChanged)
    Q_PROPERTY(QColor glassTintColor READ glassTintColor WRITE setGlassTintColor NOTIFY glassTintColorChanged)
    Q_PROPERTY(qreal borderWidth READ borderWidth WRITE setBorderWidth NOTIFY borderWidthChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY borderColorChanged)
    Q_PROPERTY(qreal scrollbarWidth READ scrollbarWidth WRITE setScrollbarWidth NOTIFY scrollbarWidthChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(QColor fontColor READ fontColor WRITE setFontColor NOTIFY fontColorChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(QColor highlightColor READ highlightColor WRITE setHighlightColor NOTIFY highlightColorChanged)
    Q_PROPERTY(qreal highlightOpacity READ highlightOpacity WRITE setHighlightOpacity NOTIFY highlightOpacityChanged)
    Q_PROPERTY(bool hudAlwaysExpanded READ hudAlwaysExpanded WRITE setHudAlwaysExpanded NOTIFY hudAlwaysExpandedChanged)

public:
    // Uses QStandardPaths::AppConfigLocation (e.g. ~/.config/suopNotes on Linux).
    explicit Settings(QObject *parent = nullptr);

    // Uses an explicit config file path, bypassing QStandardPaths entirely.
    // Intended for tests (point at a QTemporaryDir) and advanced/portable use.
    explicit Settings(const QString &configFilePath, QObject *parent = nullptr);

    ~Settings() override;

    // Per-note session state, persisted as an INI array so a note window
    // can be reopened in the same place (position is meaningful on
    // X11/Windows/macOS; on Wayland only width/height apply — see
    // suopNotes docs on window placement restoration).
    struct NoteSessionState
    {
        QString noteId;
        int x = 0;
        int y = 0;
        int width = 260;
        int height = 260;
        // Stable output/connector name (e.g. "DP-4"), not an index — indices
        // aren't stable across reboots. Empty until a position has actually
        // been captured at least once.
        QString screenName;
        // Pin state (req. 5): whether this note is Force-kept above other
        // windows via its KWin rule group. Session/runtime state, not an
        // appearance override — there's no meaningful "tray default" for it,
        // so it lives here next to geometry rather than in
        // NoteAppearanceOverride. Defaults to false: earlier builds forced
        // every note above unconditionally, but a pin toggle only makes
        // sense if the un-pinned state is normal (not-forced) stacking.
        bool alwaysOnTop = false;
    };

    // Per-note appearance override (req. 4/6/7): any field with has*==false
    // falls back to the matching global Appearance property above. Lets the
    // tray Settings dialog's values act as the default for every note, while
    // an individual note can override specific fields via its own hover
    // settings popup.
    struct NoteAppearanceOverride
    {
        bool hasWindowRadius = false;
        int windowRadius = 12;
        bool hasBackgroundColor = false;
        QColor backgroundColor;
        bool hasWindowOpacity = false;
        qreal windowOpacity = 0.9;
        bool hasBlurEnabled = false;
        bool blurEnabled = false;
        bool hasGlassEnabled = false;
        bool glassEnabled = false;
        bool hasGlassSheenOpacity = false;
        qreal glassSheenOpacity = 0.12;
        bool hasGlassSheenStrength = false;
        qreal glassSheenStrength = 10;
        bool hasGlassSheenSize = false;
        qreal glassSheenSize = 0.18;
        bool hasGlassTintColor = false;
        QColor glassTintColor;
        bool hasBorderWidth = false;
        qreal borderWidth = 0;
        bool hasBorderColor = false;
        QColor borderColor;
        bool hasScrollbarWidth = false;
        qreal scrollbarWidth = 8;
        bool hasFontSize = false;
        int fontSize = 14;
        bool hasFontColor = false;
        QColor fontColor;
        bool hasFontFamily = false;
        QString fontFamily;
        bool hasHighlightColor = false;
        QColor highlightColor;
        bool hasHighlightOpacity = false;
        qreal highlightOpacity = 0.55;
    };

    // Appearance properties
    QString notesFolder() const;
    void setNotesFolder(const QString &folder);

    int windowRadius() const;
    void setWindowRadius(int radius);

    QColor backgroundColor() const;
    void setBackgroundColor(const QColor &color);

    qreal windowOpacity() const;
    void setWindowOpacity(qreal opacity);

    bool blurEnabled() const;
    void setBlurEnabled(bool enabled);

    // Rendered translucent sheen (req. 6, ported from suopPanel) — a
    // separate mechanism from blurEnabled's real compositor blur-behind,
    // not a mutually exclusive mode of it.
    bool glassEnabled() const;
    void setGlassEnabled(bool enabled);

    // Glass sheen tuning (req. 6 follow-up): the fixed-tuning boolean toggle
    // above now has independently adjustable sheen strength/size/opacity and
    // a dedicated tint color, mirroring suopPanel's GlassOverlay knobs
    // (ringCount/sheenSize/sheenOpacity/tintColor) rather than reusing
    // backgroundColor for the tint.
    qreal glassSheenOpacity() const;
    void setGlassSheenOpacity(qreal opacity);

    qreal glassSheenStrength() const;
    void setGlassSheenStrength(qreal strength);

    qreal glassSheenSize() const;
    void setGlassSheenSize(qreal size);

    QColor glassTintColor() const;
    void setGlassTintColor(const QColor &color);

    qreal borderWidth() const;
    void setBorderWidth(qreal width);

    QColor borderColor() const;
    void setBorderColor(const QColor &color);

    // Note content scrollbar thickness (feature request) — a plain
    // Appearance property, same pattern as borderWidth, rather than
    // hardcoded in NoteWindow.qml, so it's tunable per-note like the rest
    // of the note's chrome.
    qreal scrollbarWidth() const;
    void setScrollbarWidth(qreal width);

    int fontSize() const;
    void setFontSize(int size);

    QColor fontColor() const;
    void setFontColor(const QColor &color);

    QString fontFamily() const;
    void setFontFamily(const QString &family);

    // Text selection background. Always opaque — translucency is controlled
    // separately by highlightOpacity below, the same split as
    // backgroundColor/windowOpacity.
    QColor highlightColor() const;
    void setHighlightColor(const QColor &color);

    qreal highlightOpacity() const;
    void setHighlightOpacity(qreal opacity);

    // Keeps the hover-expand note ribbon (NoteHud) permanently expanded —
    // an escape hatch for when hover-to-reveal isn't reliable/wanted (e.g.
    // touch input, or a corner overlapped by another hover target), rather
    // than a styling override, so it lives as a plain global toggle like
    // blurEnabled/glassEnabled instead of in NoteAppearanceOverride.
    bool hudAlwaysExpanded() const;
    void setHudAlwaysExpanded(bool alwaysExpanded);

    // Session state (list of open notes)
    QList<NoteSessionState> openNotes() const;
    void setOpenNotes(const QList<NoteSessionState> &notes);
    void upsertNoteState(const NoteSessionState &state);
    void removeNoteState(const QString &noteId);

    // Per-note appearance overrides. A note with no override group at all
    // returns a default-constructed NoteAppearanceOverride (every has*
    // false), which callers read as "use the global Appearance value for
    // every field."
    NoteAppearanceOverride noteOverride(const QString &noteId) const;
    void setNoteOverride(const QString &noteId, const NoteAppearanceOverride &override);
    // Removes the note's entire override group — every field reverts to
    // "use default" at once, rather than needing per-field clears. Called
    // directly from QML (NoteWindow.qml's deleteNote()), unlike its two
    // siblings above (which are only ever reached through NoteAppearance,
    // from C++) — Q_INVOKABLE accordingly.
    Q_INVOKABLE void clearNoteOverride(const QString &noteId);

signals:
    // Fired whenever a note's override group changes (set or cleared), so a
    // live NoteAppearance instance for that note knows to re-resolve.
    void noteOverrideChanged(const QString &noteId);
    void notesFolderChanged();
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
    void hudAlwaysExpandedChanged();
    void openNotesChanged();

private:
    void init();
    static QString defaultNotesFolder();
    void writeOpenNotes(const QList<NoteSessionState> &notes);

    QSettings *m_settings = nullptr;
};
