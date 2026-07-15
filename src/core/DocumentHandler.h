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
public:
    explicit DocumentHandler(QObject *parent = nullptr);
    ~DocumentHandler();

    QQuickTextDocument *document() const;
    void setDocument(QQuickTextDocument *document);

    bool spellcheckEnabled() const;
    void setSpellcheckEnabled(bool enabled);

    QString spellcheckLanguage() const;
    void setSpellcheckLanguage(const QString &language);

    QStringList userDictionaryWords() const;
    void setUserDictionaryWords(const QStringList &words);

    QVariantList misspelledWordRanges() const;

    int spellcheckDebounceMs() const;
    void setSpellcheckDebounceMs(int ms);

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
    QString m_spellcheckLanguage;
    QStringList m_userDictionaryWords;
};
