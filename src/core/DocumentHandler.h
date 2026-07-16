#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QRectF>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class MarkdownHighlighter;
class SpellCheckerBackend;
class SpellCheckHighlighter;
class UserDictionaryFilteringBackend;

// Thin QML-facing wrapper: extracts a QTextCursor over the TextArea's
// underlying QTextDocument and delegates to the pure, independently-tested
// TextFormatting functions. Also owns markdown load/save, mirroring the
// exact QTextDocument::toMarkdown()/setMarkdown() mechanism verified in the
// project's WYSIWYG spike, rather than relying on unverified assumptions
// about TextArea's own text-property serialization.
class DocumentHandler : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(bool spellcheckEnabled READ spellcheckEnabled WRITE setSpellcheckEnabled NOTIFY spellcheckEnabledChanged)
    // Code mode's two document-level gates (feature request). Both default to
    // true — an ordinary note is a markdown note — and NoteWindow.qml binds
    // them to `codeMode && settings.codeModeDisablesX`, so this class never
    // needs to know what code mode is; it only knows what is switched off.
    //
    // markdownDecorationEnabled forwards straight to MarkdownHighlighter (see
    // its own setMarkdownDecorationEnabled for what it does and does not
    // cover — find/replace highlighting keeps working either way).
    Q_PROPERTY(bool markdownDecorationEnabled READ markdownDecorationEnabled WRITE setMarkdownDecorationEnabled NOTIFY markdownDecorationEnabledChanged)
    // autoFormattingEnabled gates the four behaviors that REWRITE the buffer
    // on their own as the user types — list continuation, list shorthand
    // conversion, numbering maintenance, and heading reset on Return. Nothing
    // the user explicitly asks for (the toolbar, the shortcuts) is gated here;
    // those are QML's own call, and NoteWindow.qml gates them from the same
    // binding so the two can't disagree.
    Q_PROPERTY(bool autoFormattingEnabled READ autoFormattingEnabled WRITE setAutoFormattingEnabled NOTIFY autoFormattingEnabledChanged)
    Q_PROPERTY(QString spellcheckLanguage READ spellcheckLanguage WRITE setSpellcheckLanguage NOTIFY spellcheckLanguageChanged)
    Q_PROPERTY(QStringList userDictionaryWords READ userDictionaryWords WRITE setUserDictionaryWords NOTIFY userDictionaryWordsChanged)
    // Every currently-misspelled word's range in the document, as a list of
    // {"start": int, "end": int} maps. Exists because QtQuick's TextArea
    // does NOT visually render underline decorations applied via
    // QSyntaxHighlighter::setFormat() (confirmed empirically: background/
    // foreground color from a highlighter format render fine, but
    // SpellCheckUnderline — or even a plain SingleUnderline — never paints
    // any pixels, with or without Text.NativeRendering) — a real QtQuick
    // limitation, not a bug in SpellCheckHighlighter's own format-setting
    // logic (unit-tested and confirmed correct at the QTextLayout data
    // level). NoteWindow.qml draws the actual visible underline itself,
    // as a decorative overlay positioned via editor.positionToRectangle()
    // for each range this property reports, kept an ordinary read-only
    // QML-reactive property (not the highlighter's own applied formats)
    // specifically so that overlay can bind directly and redraw on change.
    Q_PROPERTY(QVariantList misspelledWordRanges READ misspelledWordRanges NOTIFY misspelledWordRangesChanged)
    // How long (ms) a block of text must sit unedited before the word at
    // the most recent edit position is allowed back into
    // misspelledWordRanges — mirrored from Settings::spellcheckDebounceMs.
    // See the pending-edit tracking this drives (setDocument()'s
    // contentsChange connection, m_pendingEditActive/m_pendingEditPosition)
    // for the mechanism itself; this property only controls its timing.
    Q_PROPERTY(int spellcheckDebounceMs READ spellcheckDebounceMs WRITE setSpellcheckDebounceMs NOTIFY spellcheckDebounceMsChanged)
    // Every bullet and checkbox marker in the document, as a list of
    // {"start": int, "end": int, "kind": "bullet"|"checkbox", "checked": bool}
    // maps. MarkdownHighlighter renders those markers' literal characters
    // transparent while keeping their width (see its reservedMarkerFormat),
    // and NoteWindow.qml draws a real bullet/checkbox into the reserved slot
    // for each range reported here — the same "C++ reports ranges, QML draws
    // the decoration" split misspelledWordRanges above already uses, and for
    // the same underlying reason: a QSyntaxHighlighter can only restyle
    // characters, it can't replace one with a different glyph.
    Q_PROPERTY(QVariantList listMarkerRanges READ listMarkerRanges NOTIFY listMarkerRangesChanged)
    // Every pipe table in the document, as the geometry needed to draw a grid
    // over it — a list of maps:
    //   "columnBoundaries": [int]  document positions of the header row's pipes
    //   "rowStarts":        [int]  document position of each rendered row's start
    //   "top"/"bottom":     int    positions on the first and last rendered rows
    // All of them are POSITIONS, not pixels: NoteWindow.qml turns each into a
    // rect with editor.positionToRectangle(), the same way every other overlay
    // here works, so the grid tracks scrolling and reflow for free.
    //
    // The rows are already aligned into columns by MarkdownHighlighter (which
    // stretches each cell so a column is one exact x in every row) — that is
    // what makes a single set of boundaries taken from the header row correct
    // for the whole table. The delimiter row is excluded from rowStarts: it is
    // collapsed to a sliver and is where the header rule gets drawn instead.
    Q_PROPERTY(QVariantList tableRanges READ tableRanges NOTIFY tableRangesChanged)

    // Whole-line images (feature request): each entry is {start, end, src, alt}
    // in document positions (start/end bracket the "![alt](src)" line's text),
    // for NoteWindow.qml to draw the picture into the tall reserved band
    // MarkdownHighlighter leaves for it — or, when src is empty, a "Browse…"
    // placeholder. Same Repeater-driving, emit-only-on-real-change discipline as
    // listMarkerRanges above (see emitImageRangesChangedIfNeeded).
    Q_PROPERTY(QVariantList imageRanges READ imageRanges NOTIFY imageRangesChanged)
public:
    explicit DocumentHandler(QObject *parent = nullptr);
    ~DocumentHandler();

    QQuickTextDocument *document() const;
    void setDocument(QQuickTextDocument *document);

    bool spellcheckEnabled() const;
    void setSpellcheckEnabled(bool enabled);

    bool markdownDecorationEnabled() const;
    void setMarkdownDecorationEnabled(bool enabled);

    bool autoFormattingEnabled() const;
    void setAutoFormattingEnabled(bool enabled);

    QString spellcheckLanguage() const;
    void setSpellcheckLanguage(const QString &language);

    QStringList userDictionaryWords() const;
    void setUserDictionaryWords(const QStringList &words);

    QVariantList misspelledWordRanges() const;

    QVariantList listMarkerRanges() const;

    QVariantList tableRanges() const;

    QVariantList imageRanges() const;

    // Re-runs the markdown pass over the whole document. Needed because table
    // column alignment is computed from the document's font metrics (see
    // MarkdownHighlighter::highlightTableRow), and a font change emits no
    // contentsChange — so without this, changing a note's font or size would
    // leave every table stretched to the OLD font's measurements until the
    // next keystroke happened to restyle it.
    Q_INVOKABLE void rehighlightMarkdown();

    int spellcheckDebounceMs() const;
    void setSpellcheckDebounceMs(int ms);

    // Tells the markdown highlighter which line the cursor is on, so that
    // line — and only that line — shows its raw markdown punctuation
    // (Obsidian-style live preview; see MarkdownHighlighter::
    // setCursorBlockNumber). Called from QML on every cursor move, which is
    // cheap: the highlighter no-ops unless the *block* actually changed.
    //
    // Pass -1 when the editor has no cursor to speak of (it lost focus), which
    // collapses every marker in the document.
    Q_INVOKABLE void setCursorPosition(int position);

    // Resolves `position` to the word range (start/end/text) it falls inside,
    // using the exact same tokenization SpellCheckHighlighter uses to decide
    // what gets underlined — so a tapped position always resolves to
    // precisely the range that would/wouldn't be flagged, no drift between
    // the two. Returns an empty map (no keys) if position isn't inside any
    // word (whitespace, punctuation, or a hand-rolled list marker glyph).
    Q_INVOKABLE QVariantMap wordAt(int position) const;
    // Whether the word at `position` is actually misspelled — false if
    // there's no word there, there's no working spellcheck backend, or the
    // word is correctly spelled. Callers deciding whether to show a
    // "misspelled" UI at all (e.g. the tap-to-suggest popup) should check
    // THIS, not merely "did suggestionsForWordAt return something" — a
    // genuinely misspelled word can still have zero suggestions (e.g. it's
    // too garbled for the dictionary to find any close match), and treating
    // that the same as "not misspelled" would silently hide it.
    Q_INVOKABLE bool isWordMisspelledAt(int position) const;
    // Spelling suggestions for the word at `position`, or an empty list if
    // there's no word there, no working spellcheck backend, the word isn't
    // actually misspelled, or it's misspelled but the backend found no
    // close match. Use isWordMisspelledAt() above to tell "not misspelled"
    // apart from "misspelled with no suggestions" — this method alone can't.
    Q_INVOKABLE QStringList suggestionsForWordAt(int position) const;
    // Replaces the exact [start, end) range (e.g. from wordAt()) with
    // `replacement` — a single atomic cursor edit, same shape as the other
    // simple mutators below.
    Q_INVOKABLE void replaceRange(int start, int end, const QString &replacement);

    // Find/replace, delegating to the pure TextSearch functions the same way
    // the formatting toolbar delegates to TextFormatting. Every occurrence of
    // `pattern` in the document, as a list of {"start": int, "end": int}
    // maps (same shape as misspelledWordRanges) — literal unless useRegex,
    // case-insensitive unless caseSensitive; empty/invalid patterns just
    // yield an empty list.
    Q_INVOKABLE QVariantList searchMatches(const QString &pattern, bool useRegex, bool caseSensitive);
    // Whether the pattern compiles at all — drives the search field's error
    // state. Only a malformed regex is invalid; literal and empty patterns
    // always pass (see TextSearch::patternValid).
    Q_INVOKABLE bool searchPatternValid(const QString &pattern, bool useRegex);
    // Replaces one match from searchMatches. Regex mode expands $1/$2
    // capture references and $$ in the replacement, which is why the pattern
    // rides along: the captures are re-derived by matching it at [start,
    // end). Returns the inserted replacement's own {"start", "end"} so the
    // UI can reselect it, or an empty map if nothing was replaced (stale
    // range, invalid pattern). One cursor edit = one undo step, and the edit
    // flows through contentsChange like typing does, so it autosaves.
    Q_INVOKABLE QVariantMap replaceSearchMatch(int start, int end, const QString &pattern,
                                               const QString &replacement, bool useRegex,
                                               bool caseSensitive);
    // Replaces every match and returns how many. Exactly ONE undo step no
    // matter the count — TextSearch::replaceAll batches the whole loop in a
    // single edit block, coalescing contentsChange to one emission. Must not
    // be called from inside another edit block.
    Q_INVOKABLE int replaceAllMatches(const QString &pattern, const QString &replacement,
                                      bool useRegex, bool caseSensitive);
    // Paints every match the find/replace panel is showing, so the user can see
    // where else their term appears instead of only the one match the editor's
    // selection is sitting on. `matches` is a searchMatches() list straight
    // back; `currentIndex` is which entry the panel's counter is pointing at
    // (-1 for none), and gets a stronger highlight than the rest.
    //
    // The panel must push a fresh list after anything that moves the matches
    // (a re-search, a replace, stepping to the next match) and call
    // clearSearchHighlight() when it closes or stops tracking this note —
    // nothing here observes the document on its own. See MarkdownHighlighter::
    // setSearchMatches() for why the highlighting lives in the markdown
    // highlighter, and for how stale positions between an edit and the panel's
    // re-search behave.
    Q_INVOKABLE void setSearchHighlight(const QVariantList &matches, int currentIndex);
    Q_INVOKABLE void clearSearchHighlight();
    // What setSearchHighlight() last applied, in the same {"start", "end"} map
    // shape searchMatches() returns. Exposed for the QML test suite, which
    // otherwise has no way to see this state at all: the highlight lands in a
    // QTextLayout char format, which QML cannot read back.
    Q_INVOKABLE QVariantList searchHighlightRanges() const;
    Q_INVOKABLE int searchHighlightCurrentIndex() const;
    // Forces an immediate re-check of the current document against the active
    // spellcheck backend (e.g. right after the user adds a word to their
    // personal dictionary from the suggestion popup, so the underline under
    // THIS note's occurrence of that word disappears immediately rather than
    // waiting for the next edit). Other already-open notes pick up the change
    // on their own next edit/rehighlight — not propagated cross-document here,
    // a deliberate scope cut to avoid a cross-window pub/sub mechanism nobody
    // asked for.
    Q_INVOKABLE void rehighlightSpellcheck();

    Q_INVOKABLE QString toMarkdown() const;
    Q_INVOKABLE void setMarkdown(const QString &markdown);

    // Each returns the toggled content's resulting range as
    // {"start": int, "end": int}, so the caller can reselect the same TEXT.
    // Qt's own selection adjustment can't be relied on here: the marker
    // inserted at the selection's END gets swallowed into it while the one at
    // its start merely shifts it, so "bold me" comes back as "bold me**" —
    // visibly wrong, and it makes isBold() report false on the very selection
    // that was just emboldened. See SelectionToolbar.qml's applyInlineToggle().
    Q_INVOKABLE QVariantMap toggleBold(int selectionStart, int selectionEnd);
    Q_INVOKABLE QVariantMap toggleItalic(int selectionStart, int selectionEnd);
    Q_INVOKABLE QVariantMap toggleStrikethrough(int selectionStart, int selectionEnd);
    Q_INVOKABLE void toggleBulletList(int selectionStart, int selectionEnd);
    Q_INVOKABLE void toggleNumberedList(int selectionStart, int selectionEnd);
    Q_INVOKABLE void toggleCheckboxList(int selectionStart, int selectionEnd);
    Q_INVOKABLE void setHeadingLevel(int selectionStart, int selectionEnd, int level);
    // Tab (feature request): four literal spaces at `position`, replacing
    // any selection.
    Q_INVOKABLE void insertTabSpaces(int selectionStart, int selectionEnd);
    Q_INVOKABLE void insertTable(int position);
    Q_INVOKABLE void insertTableRow(int position);
    Q_INVOKABLE void deleteTableRow(int position);
    Q_INVOKABLE void insertTableColumn(int position);
    Q_INVOKABLE void deleteTableColumn(int position);
    Q_INVOKABLE void deleteTable(int position);

    // Read-only state queries backing the toolbar's "is this active at the
    // current selection/cursor" indicator (e.g. a highlighted Bold button
    // when the selection is already bold). Each mirrors the exact cursor
    // state its matching toggle*/setHeadingLevel call above reads, via
    // TextFormatting's own read-only counterparts.
    Q_INVOKABLE bool isBold(int selectionStart, int selectionEnd) const;
    Q_INVOKABLE bool isItalic(int selectionStart, int selectionEnd) const;
    Q_INVOKABLE bool isStrikethrough(int selectionStart, int selectionEnd) const;
    Q_INVOKABLE int headingLevel(int selectionStart, int selectionEnd) const;
    Q_INVOKABLE bool isBulletList(int selectionStart, int selectionEnd) const;
    Q_INVOKABLE bool isNumberedList(int selectionStart, int selectionEnd) const;
    Q_INVOKABLE bool isCheckboxList(int selectionStart, int selectionEnd) const;
    Q_INVOKABLE bool isCheckboxChecked(int selectionStart, int selectionEnd) const;
    // Click-to-toggle (feature request): flips the checkbox at `position` —
    // succeeds only if position falls within that item's own checkbox
    // glyph (not elsewhere in its text, where a click should just place
    // the cursor normally). Returns true if a toggle actually happened.
    Q_INVOKABLE bool tryToggleCheckboxAt(int position);
    // Where a click resolved to `position` should actually leave the caret:
    // the item's own text start when `position` fell inside a bullet's or
    // checkbox's (invisible, glyph-covered) marker, and `position` itself
    // otherwise. See TextFormatting::listLabelPosition for why a click that
    // looks like it landed on the box resolves inside the marker at all, and
    // why typing there destroys the checkbox.
    Q_INVOKABLE int listLabelPositionAt(int position) const;
    // Drives whether the toolbar's row/column-editing buttons show at all
    // (feature request) — they used to render permanently, acting as
    // silent no-ops (per TextFormatting.h) whenever the cursor wasn't
    // actually inside a table.
    Q_INVOKABLE bool isInTable(int position) const;
    // The table's own bounding rect containing `position`, in the SAME
    // document-layout coordinate space QML's own cursorRectangle already
    // uses (so callers combine it with editorFlick.contentY/mapToGlobal
    // exactly the same way ensureCursorVisible does) — feature request:
    // anchoring the toolbar to the table itself, not just the whole note,
    // whenever the cursor is inside one. An empty (null) QRectF when the
    // position isn't actually inside a table.
    Q_INVOKABLE QRectF tableRect(int position) const;
    // The checkbox glyph's own bounding rect (same document-layout
    // coordinate space as tableRect above) for the block containing
    // `position`, only if `position` itself falls within that glyph —
    // feature request: lets QML draw a hover highlight over real checkbox
    // items. An empty QRectF otherwise (not a checkbox item, or hovering
    // past the glyph into its label text).
    Q_INVOKABLE QRectF checkboxRectAt(int position) const;

    // The URL of the [text](url) link whose visible text contains `position`,
    // or an empty string if `position` isn't on a link. Drives middle-click-to-
    // open and the hover tooltip in NoteWindow.qml (feature request: clickable
    // links). Scans only the block containing `position`, so it's cheap enough
    // to call on every hover move. The clickable region is the visible text
    // (what renders as the link), NOT the collapsed "](url)" punctuation.
    Q_INVOKABLE QString linkUrlAt(int position) const;

    // Inserts an empty image placeholder "![]()" on its own line at `position`
    // (feature request: the toolbar's insert-image button) — it renders as a
    // Browse placeholder the user then fills in. Adds surrounding newlines only
    // as needed so the image always ends up alone on its line (block images
    // only render when nothing else shares the line).
    Q_INVOKABLE void insertImage(int position);
    // Sets the src of the image on the line containing `position` to
    // `relativePath` (feature request: after the Browse dialog picks a file).
    // No-op if that line isn't an image. Done as one undoable edit.
    Q_INVOKABLE void setImageSourceAt(int position, const QString &relativePath);

    // Enter/Return inside a list item (bullet or numbered): continues the
    // list with a new item, or — if the current item is empty — exits the
    // list back to a plain paragraph, mirroring standard word-processor
    // behavior. Returns false (does nothing) when the cursor isn't in a
    // list at all, so QML's key handler knows to fall through to the
    // TextArea's own default Return handling.
    Q_INVOKABLE bool tryContinueList(int cursorPosition);

    // Called after every keystroke: detects "- "/"* " or "1. " typed at the
    // start of an otherwise-plain line and converts it into a real bullet/
    // numbered list, consuming the typed marker text. This is the one
    // exception to the "never live-convert typed markdown" rule the toolbar
    // formatting otherwise follows (see TextFormatting.h) — list shorthand
    // is universal enough (Notion, Word, GitHub's own editor) that users
    // expect it to just work. Returns true if a conversion happened.
    Q_INVOKABLE bool tryAutoConvertListShorthand(int cursorPosition);

    // Also called after every keystroke (feature request: "auto detect when a
    // url ... is typed ... transform them into markdown syntax"). When a
    // word-terminating space is typed right after a bare URL — a full one, a
    // "www." one, or a scheme-less "example.com" — the URL is wrapped into
    // "[url](url)" markdown, adding "https://" to the link target if the typed
    // text lacked a scheme. From then on it renders and middle-click-opens like
    // any other link. Returns true if a conversion happened. Gated on
    // auto-formatting (off in code mode, same as the list shorthand above).
    Q_INVOKABLE bool tryAutoLinkUrl(int cursorPosition);

    // Also called after every keystroke: re-derives the cursor's own
    // numbered-list run (sequential numbers + shared marker width) if it's
    // sitting on one. Ordinary text editing — backspacing an item away,
    // selecting one and deleting it — merges/removes blocks through Qt's
    // own default handling, not through tryContinueList/toggleNumberedList,
    // and would otherwise leave the surviving items out of sequence.
    Q_INVOKABLE void maintainListNumbering(int cursorPosition);

    // Enter/Return on a heading line: inserts the new block (inheriting the
    // heading's format the way QTextCursor::insertBlock() always does), then
    // immediately clears its heading level back to a plain paragraph — a
    // heading is a one-line label, not a style that should propagate to
    // whatever the user types next. Returns false (does nothing) when the
    // cursor isn't on a heading line, so QML's key handler falls through to
    // TextArea's own default Return handling, same convention as
    // tryContinueList above.
    Q_INVOKABLE bool tryResetHeadingOnReturn(int cursorPosition);

signals:
    void documentChanged();
    void spellcheckEnabledChanged();
    void markdownDecorationEnabledChanged();
    void autoFormattingEnabledChanged();
    void spellcheckLanguageChanged();
    void userDictionaryWordsChanged();
    // Emitted whenever the set of misspelled words in the document may have
    // changed — after every real text edit, and after anything that changes
    // which words count as misspelled at all (spellcheckEnabled/Language/
    // userDictionaryWords, or an explicit rehighlightSpellcheck() call).
    // NoteWindow.qml's underline overlay listens to this to know when to
    // recompute (see misspelledWordRanges' own doc comment for why this
    // property exists at all).
    void misspelledWordRangesChanged();
    // Emitted when the set of bullet/checkbox markers may have changed — after
    // any real text edit, and after a document swap. Like
    // misspelledWordRangesChanged above this is only emitted when the value
    // genuinely differs, so typing inside a list item's text doesn't rebuild
    // the glyph overlay's delegates on every keystroke.
    void listMarkerRangesChanged();
    void imageRangesChanged();
    // Emitted when the set of tables, or their shape, may have changed — same
    // only-on-real-change discipline as the two above.
    void tableRangesChanged();
    void spellcheckDebounceMsChanged();
    // Emitted after any call above that can change the format active at the
    // cursor/selection (the toggles, setHeadingLevel, and the two list-aware
    // Enter/autoconvert helpers when they actually mutate something) — lets
    // QML re-poll the isXxx()/headingLevel() queries above even when the
    // selection itself hasn't moved, e.g. right after clicking Bold with a
    // selection still held (see SelectionToolbar.qml's runAction()).
    void formattingChanged();
    // Emitted from onContentsChange for a genuine insert/delete — never for
    // a pure formatting-only pass (SpellCheckHighlighter's setFormat() calls
    // report charsRemoved == charsAdded == 0, same check this signal
    // reuses from the spellcheck debounce below). Exists so QML's save
    // trigger doesn't have to detect "did a real edit happen" by comparing
    // TextArea's own `text` property against its last-seen value — that
    // property is Qt's built-in MarkdownText serialization of the document,
    // which collapses blank lines/trailing newlines, so two document states
    // differing only by a bare Enter keystroke can serialize to the
    // identical string and be mistaken for a no-op (bug found live: a
    // newline wasn't saved until some later, non-newline edit happened to
    // change that lossy string too). This signal is driven off the
    // document's real edit metadata instead, so it can't be fooled that way.
    void contentEdited();

private:
    // Re-runs both highlighters in the one order that works — spellcheck
    // first, markdown LAST. See its definition for why the order is not a
    // style choice.
    void rehighlightAll();

    QTextCursor cursorFor(int selectionStart, int selectionEnd) const;

    // Builds a fresh backend for `language` (empty = system default via Sonnet;
    // the literal "bundled:en_US" explicitly forces the bundled dictionary,
    // skipping Sonnet entirely — used by the language picker's own special
    // "Bundled" entry). Sonnet (if available and not explicitly bypassed) is
    // tried first via a FallbackSpellCheckerBackend, falling back to the
    // bundled Hunspell dictionary (if the CMake fetch succeeded on this build)
    // when Sonnet reports no working dictionary for that language. Returns a
    // possibly-null unique_ptr if nothing is available at all (e.g. built
    // without Sonnet AND without a fetched bundled dictionary) — callers must
    // tolerate that; UserDictionaryFilteringBackend already does.
    std::unique_ptr<SpellCheckerBackend> makeSpellCheckerBackend(const QString &language) const;

    // Shared implementation behind the misspelledWordRanges Q_PROPERTY
    // getter: pulled out so callers that mutate something which can affect
    // the result (a real edit, a settings change) can also recompute it
    // themselves and compare against m_lastMisspelledWordRanges — see
    // emitMisspelledWordRangesChangedIfNeeded()'s own comment for why.
    QVariantList computeMisspelledWordRanges() const;
    // Shared implementation behind the listMarkerRanges Q_PROPERTY getter,
    // pulled out for the same reason computeMisspelledWordRanges above is:
    // so an edit can recompute and compare against m_lastListMarkerRanges
    // instead of emitting unconditionally. See
    // emitListMarkerRangesChangedIfNeeded().
    QVariantList computeListMarkerRanges() const;
    // Recomputes listMarkerRanges and emits only on a real change. Mirrors
    // emitMisspelledWordRangesChangedIfNeeded() below, and exists for the
    // identical reason: the property drives a QML Repeater, so an
    // unconditional emit on every keystroke would tear down and rebuild every
    // bullet/checkbox glyph in the note just because the user typed a letter
    // into one list item's label.
    void emitListMarkerRangesChangedIfNeeded();
    QVariantList computeTableRanges() const;
    void emitTableRangesChangedIfNeeded();
    QVariantList computeImageRanges() const;
    void emitImageRangesChangedIfNeeded();
    // Recomputes misspelledWordRanges and only emits misspelledWordRangesChanged
    // if the result actually differs from the last-emitted value. Every
    // trigger that used to emit misspelledWordRangesChanged unconditionally
    // (a keystroke, a settings change) now routes through this instead —
    // bug found live: with an unconditional emit, EVERY keystroke
    // re-triggered NoteWindow.qml's underline-overlay Repeater (whose model
    // is this property), tearing down and rebuilding every Canvas delegate
    // even for keystrokes that didn't change which words are misspelled at
    // all (the common case: typing inside a word that's excluded from
    // checking anyway — see m_pendingEditActive's own comment — while every
    // OTHER word's misspelled status is unchanged). That constant rebuild
    // was both wasteful and a direct cause of the underline's visible
    // flicker while typing.
    void emitMisspelledWordRangesChangedIfNeeded();
    // Forces every block's cached misspelled-word data (see the
    // MisspelledBlockCache class in the .cpp) to be recomputed on the next
    // computeMisspelledWordRanges() call, even though none of the blocks'
    // own text actually changed — needed whenever what counts as
    // "misspelled" itself changes instead (spellcheckLanguage,
    // userDictionaryWords, rehighlightSpellcheck()), since the cache's
    // normal staleness check (block.revision()) can't see that kind of
    // change at all. Also drops the cached suggestion lists (see
    // m_suggestionsCache's own comment) — a word's cached suggestions can
    // go stale for the exact same reasons its cached misspelled/not
    // verdict can.
    void invalidateAllMisspelledBlockCaches();
    // Returns backend->suggestions(word), computing it once and reusing the
    // result on every later call for the SAME word — feature request:
    // precache each misspelled word's suggestions as it settles (see
    // onContentsChange's own comment) rather than only computing them the
    // instant the user taps a word, which could otherwise make the
    // suggestion popup visibly wait on a real dictionary lookup (fuzzy
    // edit-distance search, not a cheap operation) before it could even
    // start opening.
    QStringList cachedSuggestionsFor(const QString &word) const;

    QPointer<QQuickTextDocument> m_document;
    QVariantList m_lastMisspelledWordRanges;
    QVariantList m_lastListMarkerRanges;
    QVariantList m_lastTableRanges;
    QVariantList m_lastImageRanges;
    int m_spellcheckDebounceMs = 500;
    // Bug found live ("the current word that has a popup currently loses
    // the underline when the popup is on" — the previous cursor/selection-
    // position-based exclusion attempt): tracks the actual EDIT itself, not
    // where the cursor merely sits — tapping a word to open its suggestion
    // popup calls editor.select(), which moves the cursor but edits
    // nothing, so it never touches these at all, and the word stays
    // flagged (and thus underlined) for as long as its popup is open.
    // m_pendingEditPosition is the position immediately after the most
    // recent real text edit (see onContentsChange); m_pendingEditActive is
    // true for m_spellcheckDebounceMs after that edit, during which the
    // word AT that position is excluded from misspelledWordRanges — the
    // standard "don't flag what's still being actively typed" debounce
    // most word processors use, rather than trying to infer "still being
    // typed" from cursor/selection state.
    int m_pendingEditPosition = -1;
    bool m_pendingEditActive = false;
    QTimer *m_pendingEditDebounce = nullptr;
    void onContentsChange(int from, int charsRemoved, int charsAdded);
    // Per-word (not per-position) cache: a misspelled word's suggestions
    // depend only on its own text, not where it sits, so every occurrence
    // of the same misspelled word across the whole document shares one
    // cached lookup. mutable: computeMisspelledWordRanges() and
    // suggestionsForWordAt() are both const, but this is a pure
    // memoization cache — logically read-only from either caller's
    // perspective. Cleared by invalidateAllMisspelledBlockCaches() (see its
    // own comment).
    mutable QHash<QString, QStringList> m_suggestionsCache;

    // Reentrancy guard for the hand-rolled list machinery: inserting or
    // removing a list marker's TEXT synchronously re-enters QML's
    // TextArea.onTextChanged before the outer call even returns (confirmed
    // empirically), which calls right back into tryAutoConvertListShorthand
    // and maintainListNumbering — either of which can ALSO mutate marker
    // text, cascading into further reentries and, in one confirmed case, a
    // genuine unbounded recursion. Every entry point that can touch
    // hand-rolled list markers checks and sets this on entry and clears it
    // on return (see the ListMutationGuard helper in the .cpp): the first,
    // outermost call does the real work; any reentrant call arriving while
    // it's still in progress is guaranteed redundant (the outer call will
    // finish leaving everything consistent) and just no-ops instead.
    bool m_handlingListMutation = false;
    // Reentrancy guard for tryAutoLinkUrl: its own document edit re-enters
    // onContentEdited synchronously (like the list shorthand), and without this
    // the nested call could read a mid-edit cursor position and convert again.
    bool m_handlingAutoLink = false;

    // The buffer's text as of the last maintainListNumbering() pass that did
    // any work — the gate that keeps that pass reacting to TEXT changes only.
    //
    // This is load-bearing for undo, not an optimization. maintainListNumbering
    // writes list hanging-indent (a QTextBlockFormat, i.e. undoable document
    // state), and it runs off contentEdited(), which fires for ANY non-zero
    // contentsChange. A block-format write is not the "pure formatting change"
    // that onContentsChange's charsRemoved/charsAdded check filters out:
    // measured, a mergeBlockFormat over a 8-character block reports
    // contentsChange(0, 8, 8), and so does UNDOING one. Without this gate,
    // undoing an indent write re-enters the pass, which rewrites the indent,
    // which is undoable, forever — Ctrl+Z live-locks and never reaches the
    // user's actual text. (Measured: 200+ undos with the text never reverting.)
    //
    // The pre-rewrite editor did not need this only because its reactive hook
    // was TextArea's own `text` property, which a format change cannot alter —
    // it was text-pure by construction. Driving maintenance off contentEdited()
    // instead (commit 5855742, for unrelated and good reasons) silently removed
    // that property; comparing the text explicitly restores it.
    QString m_lastMaintainedText;

    // Constructed once in the constructor as a stable UserDictionaryFilteringBackend
    // wrapper (its own inner backend may be null, degrading spellcheck to a
    // clean no-op, if built without Sonnet and without a bundled dictionary).
    // The wrapper itself never gets replaced — only its inner backend does,
    // via setInner() — so m_highlighter's raw pointer into it stays valid
    // across a spellcheckLanguage change.
    std::unique_ptr<UserDictionaryFilteringBackend> m_spellCheckerBackend;
    // (Re)constructed in setDocument() against the new document's
    // textDocument() — does not own m_spellCheckerBackend, just borrows it.
    std::unique_ptr<SpellCheckHighlighter> m_highlighter;
    // Paints markdown decoration from the buffer's literal source text — the
    // job textFormat: TextEdit.MarkdownText used to do before the editor moved
    // to a plain-text buffer. Sibling of m_highlighter above: same lifetime,
    // same document, (re)constructed alongside it in setDocument().
    std::unique_ptr<MarkdownHighlighter> m_markdownHighlighter;
    bool m_spellcheckEnabled = true;
    // Mirrors MarkdownHighlighter's own default. Held here as well as there
    // because setDocument() rebuilds the highlighter from scratch (see its
    // ctor comment) — the gate has to be re-applied to the new instance, and
    // this is what it is re-applied FROM.
    bool m_markdownDecorationEnabled = true;
    bool m_autoFormattingEnabled = true;
    QString m_spellcheckLanguage;
    QStringList m_userDictionaryWords;
};
