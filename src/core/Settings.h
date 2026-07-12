#pragma once

#include <QObject>
#include <QColor>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVariantList>

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
    Q_PROPERTY(bool spellcheckEnabled READ spellcheckEnabled WRITE setSpellcheckEnabled NOTIFY spellcheckEnabledChanged)
    Q_PROPERTY(QString spellcheckLanguage READ spellcheckLanguage WRITE setSpellcheckLanguage NOTIFY spellcheckLanguageChanged)
    Q_PROPERTY(QStringList userDictionaryWords READ userDictionaryWords NOTIFY userDictionaryWordsChanged)
    Q_PROPERTY(int spellcheckMaxSuggestions READ spellcheckMaxSuggestions WRITE setSpellcheckMaxSuggestions NOTIFY spellcheckMaxSuggestionsChanged)
    Q_PROPERTY(QColor spellcheckUnderlineColor READ spellcheckUnderlineColor WRITE setSpellcheckUnderlineColor NOTIFY spellcheckUnderlineColorChanged)
    Q_PROPERTY(qreal spellcheckUnderlineWidth READ spellcheckUnderlineWidth WRITE setSpellcheckUnderlineWidth NOTIFY spellcheckUnderlineWidthChanged)
    Q_PROPERTY(int spellcheckDebounceMs READ spellcheckDebounceMs WRITE setSpellcheckDebounceMs NOTIFY spellcheckDebounceMsChanged)
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
    Q_PROPERTY(QColor interactableHighlightColor READ interactableHighlightColor WRITE setInteractableHighlightColor NOTIFY interactableHighlightColorChanged)
    Q_PROPERTY(qreal interactableHighlightOpacity READ interactableHighlightOpacity WRITE setInteractableHighlightOpacity NOTIFY interactableHighlightOpacityChanged)
    Q_PROPERTY(bool interactableHighlightGlassEnabled READ interactableHighlightGlassEnabled WRITE setInteractableHighlightGlassEnabled NOTIFY interactableHighlightGlassEnabledChanged)
    Q_PROPERTY(bool chromeGlassFollowsNote READ chromeGlassFollowsNote WRITE setChromeGlassFollowsNote NOTIFY chromeGlassFollowsNoteChanged)
    Q_PROPERTY(bool chromeGlassEnabled READ chromeGlassEnabled WRITE setChromeGlassEnabled NOTIFY chromeGlassEnabledChanged)
    Q_PROPERTY(bool hudAlwaysExpanded READ hudAlwaysExpanded WRITE setHudAlwaysExpanded NOTIFY hudAlwaysExpandedChanged)

    // Editor formatting shortcuts (feature request): each is a QKeySequence
    // string (e.g. "Ctrl+B"), rebindable via the tray Settings dialog's
    // Keyboard Shortcuts section rather than hardcoded in NoteWindow.qml, so
    // a user with a conflicting binding (or a non-QWERTY layout) can change
    // it without editing the app.
    Q_PROPERTY(QString shortcutBold READ shortcutBold WRITE setShortcutBold NOTIFY shortcutBoldChanged)
    Q_PROPERTY(QString shortcutItalic READ shortcutItalic WRITE setShortcutItalic NOTIFY shortcutItalicChanged)
    Q_PROPERTY(QString shortcutStrikethrough READ shortcutStrikethrough WRITE setShortcutStrikethrough NOTIFY shortcutStrikethroughChanged)
    Q_PROPERTY(QString shortcutHeading1 READ shortcutHeading1 WRITE setShortcutHeading1 NOTIFY shortcutHeading1Changed)
    Q_PROPERTY(QString shortcutHeading2 READ shortcutHeading2 WRITE setShortcutHeading2 NOTIFY shortcutHeading2Changed)
    Q_PROPERTY(QString shortcutParagraph READ shortcutParagraph WRITE setShortcutParagraph NOTIFY shortcutParagraphChanged)
    Q_PROPERTY(QString shortcutBulletList READ shortcutBulletList WRITE setShortcutBulletList NOTIFY shortcutBulletListChanged)
    Q_PROPERTY(QString shortcutNumberedList READ shortcutNumberedList WRITE setShortcutNumberedList NOTIFY shortcutNumberedListChanged)
    Q_PROPERTY(QString shortcutInsertTable READ shortcutInsertTable WRITE setShortcutInsertTable NOTIFY shortcutInsertTableChanged)

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

    // Whether the editor's spellcheck highlighter is active. Gated further at
    // runtime by whether a working dictionary is available (checked in the
    // highlighter itself, not here) — this property only reflects the user's
    // preference.
    bool spellcheckEnabled() const;
    void setSpellcheckEnabled(bool enabled);

    // Spellcheck dictionary language. Only two shapes of value are
    // meaningful here (Settings itself just stores whatever string it's
    // given — validation/interpretation happens in a later step's
    // language-picker UI and DocumentHandler wiring):
    //   - "" (empty, the default): automatic — defer to the system/KDE-
    //     configured default language, the same semantics
    //     Sonnet::Speller(QString()) already uses.
    //   - "bundled:en_US": explicitly use the bundled fallback dictionary
    //     regardless of what the system has.
    QString spellcheckLanguage() const;
    void setSpellcheckLanguage(const QString &language);

    // Personal/user spellcheck dictionary: words the user has explicitly
    // accepted as correct, layered on top of whatever language dictionary
    // is active. Read-only from Q_PROPERTY's perspective — mutated one
    // word at a time via the invokables below, since "add one word" /
    // "remove one word" are the only operations that make sense (unlike
    // wholesale replacement, which would lose entries a caller doesn't
    // know about).
    QStringList userDictionaryWords() const;
    Q_INVOKABLE void addUserDictionaryWord(const QString &word);
    Q_INVOKABLE void removeUserDictionaryWord(const QString &word);

    // How many spelling suggestions the tap-to-suggest popup shows at once
    // (feature request: the popup was unbounded — some misspelled words
    // return 10+ candidates, making it far taller than useful). Clamped to
    // >= 0 in the setter; 0 means "no suggestion buttons at all, just the
    // 'No suggestions'-style empty state and Add to dictionary" — a valid,
    // if unusual, user choice, not an error.
    int spellcheckMaxSuggestions() const;
    void setSpellcheckMaxSuggestions(int maxSuggestions);

    // Styling for the misspelled-word underline NoteWindow.qml's
    // misspelledWordOverlay draws (feature request: a solid, straight line
    // with a configurable color/width, replacing the previous fixed wavy
    // red squiggle). Defaults to a 3px-wide "watermelon red" line.
    QColor spellcheckUnderlineColor() const;
    void setSpellcheckUnderlineColor(const QColor &color);
    qreal spellcheckUnderlineWidth() const;
    void setSpellcheckUnderlineWidth(qreal width);

    // How long (in ms) a word must sit unedited before it's flagged as
    // misspelled (feature request: debounce the in-progress word instead of
    // excluding it by cursor/selection position — see DocumentHandler's own
    // pending-edit tracking for where this is actually applied). Defaults
    // to 500ms, the same rough grace period most word processors give you
    // before flagging what you're still actively typing.
    int spellcheckDebounceMs() const;
    void setSpellcheckDebounceMs(int ms);

    // Returns the set of selectable spellcheck languages for a picker UI:
    // every language Sonnet reports as available on this system (code ->
    // display name, e.g. "en_US" -> "American English"), PLUS one synthetic
    // entry for the app's own bundled fallback dictionary ("bundled:en_US").
    // Returns a QVariantList of QVariantMaps (each {"code": ..., "name": ...})
    // since QML consumes that shape naturally as a ListModel-compatible list.
    // Always includes the bundled entry even if Sonnet isn't compiled in or
    // finds nothing, so the picker is never empty.
    Q_INVOKABLE QVariantList availableSpellcheckLanguages() const;

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

    // Highlight box shown behind hoverable interactable controls (toolbar
    // buttons, note menu icons, scrollbar), independent of the text
    // selection highlight above.
    QColor interactableHighlightColor() const;
    void setInteractableHighlightColor(const QColor &color);

    qreal interactableHighlightOpacity() const;
    void setInteractableHighlightOpacity(qreal opacity);

    bool interactableHighlightGlassEnabled() const;
    void setInteractableHighlightGlassEnabled(bool enabled);

    // Whether the app's chrome (floating toolbar window + note scrollbar)
    // mirrors each note's own glassEnabled setting (true, the default) or
    // uses chromeGlassEnabled's fixed value instead.
    bool chromeGlassFollowsNote() const;
    void setChromeGlassFollowsNote(bool followsNote);

    bool chromeGlassEnabled() const;
    void setChromeGlassEnabled(bool enabled);

    // Keeps the hover-expand note ribbon (NoteHud) permanently expanded —
    // an escape hatch for when hover-to-reveal isn't reliable/wanted (e.g.
    // touch input, or a corner overlapped by another hover target), rather
    // than a styling override, so it lives as a plain global toggle like
    // blurEnabled/glassEnabled instead of in NoteAppearanceOverride.
    bool hudAlwaysExpanded() const;
    void setHudAlwaysExpanded(bool alwaysExpanded);

    // Editor formatting shortcuts. Each getter returns a QKeySequence-parsable
    // string; empty is never returned (falls back to the documented default),
    // so QML's Shortcut.sequence is never bound to an empty/invalid string.
    QString shortcutBold() const;
    void setShortcutBold(const QString &sequence);

    QString shortcutItalic() const;
    void setShortcutItalic(const QString &sequence);

    QString shortcutStrikethrough() const;
    void setShortcutStrikethrough(const QString &sequence);

    QString shortcutHeading1() const;
    void setShortcutHeading1(const QString &sequence);

    QString shortcutHeading2() const;
    void setShortcutHeading2(const QString &sequence);

    QString shortcutParagraph() const;
    void setShortcutParagraph(const QString &sequence);

    QString shortcutBulletList() const;
    void setShortcutBulletList(const QString &sequence);

    QString shortcutNumberedList() const;
    void setShortcutNumberedList(const QString &sequence);

    QString shortcutInsertTable() const;
    void setShortcutInsertTable(const QString &sequence);

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
    void spellcheckEnabledChanged();
    void spellcheckLanguageChanged();
    void userDictionaryWordsChanged();
    void spellcheckMaxSuggestionsChanged();
    void spellcheckUnderlineColorChanged();
    void spellcheckUnderlineWidthChanged();
    void spellcheckDebounceMsChanged();
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
    void interactableHighlightColorChanged();
    void interactableHighlightOpacityChanged();
    void interactableHighlightGlassEnabledChanged();
    void chromeGlassFollowsNoteChanged();
    void chromeGlassEnabledChanged();
    void hudAlwaysExpandedChanged();
    void openNotesChanged();
    void shortcutBoldChanged();
    void shortcutItalicChanged();
    void shortcutStrikethroughChanged();
    void shortcutHeading1Changed();
    void shortcutHeading2Changed();
    void shortcutParagraphChanged();
    void shortcutBulletListChanged();
    void shortcutNumberedListChanged();
    void shortcutInsertTableChanged();

private:
    void init();
    static QString defaultNotesFolder();
    void writeOpenNotes(const QList<NoteSessionState> &notes);

    QSettings *m_settings = nullptr;
};
