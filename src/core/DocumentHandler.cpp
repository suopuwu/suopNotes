#include "DocumentHandler.h"
#include "MarkdownSerialization.h"
#include "SpellCheckHighlighter.h"
#include "SpellChecker.h"
#include "TextFormatting.h"

#include <QAbstractTextDocumentLayout>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>

namespace
{
// RAII helper for DocumentHandler::m_handlingListMutation — see that
// member's own comment for why this guard exists at all.
//
// Also wraps the whole call in a single QTextDocument edit block (bugfix):
// every list-mutating entry point below used to issue many separate,
// unbatched QTextCursor edits (one insertText/mergeBlockFormat per block
// TextFormatting::renumberRun touches, for instance) as a reaction to a
// single keystroke. Each one became its own entry on Qt's native undo
// stack, so repeatedly pressing Ctrl+Z inside a list mostly just replayed
// those auto-formatting micro-edits at the same handful of block
// positions instead of stepping back through real edit history — and
// QTextDocument::contentsChanged() (which QML's TextArea.onTextChanged
// listens to, and which re-enters these very functions) is deferred and
// coalesced to a single emission at endEditBlock() rather than firing
// once per edit, so this also cuts down on how often that reentrant path
// runs. Only begins/ends a block when this guard is actually the
// outermost (non-reentrant) one — a reentrant call returns immediately
// without editing anything, so there is nothing for it to batch.
class ListMutationGuard
{
public:
    ListMutationGuard(bool &flag, QTextDocument *document)
        : m_flag(flag)
        , m_reentrant(flag)
    {
        m_flag = true;
        if (!m_reentrant && document) {
            m_editBlockCursor = QTextCursor(document);
            m_editBlockCursor.beginEditBlock();
        }
    }
    ~ListMutationGuard()
    {
        if (!m_reentrant && !m_editBlockCursor.isNull())
            m_editBlockCursor.endEditBlock();
        m_flag = m_reentrant;
    }
    // True if a list-mutating call was ALREADY in progress when this guard
    // was constructed — callers bail out immediately in that case.
    bool reentrant() const { return m_reentrant; }

private:
    bool &m_flag;
    bool m_reentrant;
    // beginEditBlock()/endEditBlock() are QTextCursor methods (not
    // QTextDocument's, despite grouping the whole document's undo stack) —
    // this cursor exists purely to call them, never to edit through.
    QTextCursor m_editBlockCursor;
};

// Per-block cache backing computeMisspelledWordRanges() (bug found live:
// "every letter typed seems to recalculate the spellcheck... performance
// intensive"). Without this, every keystroke re-ran isMisspelled() against
// EVERY word in the ENTIRE document, not just the block that actually
// changed — fine for a short note, wasteful (and, per that report, visibly
// costly) for a long one. Attached to each QTextBlock via setUserData(),
// which is exactly what block-level caches like this are for (Qt deletes it
// automatically when the block itself is removed/merged away).
//
// block.revision() (confirmed empirically, not just assumed: see the
// verification this comment refers to in the PR/commit history) only
// increments when a block's own TEXT actually changes — not on
// formatting-only operations (e.g. QSyntaxHighlighter::setFormat) — so
// comparing it against the revision cached here is a reliable, cheap
// (single int compare) way to know "has this specific block's text changed
// since I last checked its words against the dictionary" without needing to
// track edit positions/deltas ourselves.
class MisspelledBlockCache : public QTextBlockUserData
{
public:
    int revision = -1;
    // Word ranges (relative to the block's own start) that are genuinely
    // misspelled — a subset of SpellCheckHighlighter::wordRangesInBlock's
    // full token list, already filtered through isMisspelled() so rebuilding
    // the absolute-position result list below is just pointer/int
    // arithmetic, no further dictionary lookups.
    QList<QPair<int, int>> misspelledRanges;
};
}

DocumentHandler::DocumentHandler(QObject *parent)
    : QObject(parent)
{
    m_spellCheckerBackend = std::make_unique<UserDictionaryFilteringBackend>(makeSpellCheckerBackend(QString()));
}

// Defined here (not defaulted inline in the header) because
// std::unique_ptr<SpellCheckerBackend>/<SpellCheckHighlighter> only need
// complete types at the point the destructor's generated code actually runs
// — which is here, where both real headers are included.
DocumentHandler::~DocumentHandler() = default;

QQuickTextDocument *DocumentHandler::document() const
{
    return m_document;
}

void DocumentHandler::setDocument(QQuickTextDocument *document)
{
    if (m_document == document)
        return;
    m_document = document;
    emit documentChanged();

    if (document && document->textDocument()) {
        m_highlighter = std::make_unique<SpellCheckHighlighter>(document->textDocument(), m_spellCheckerBackend.get());
        m_highlighter->setActive(m_spellcheckEnabled);
        // Real text edits are the one trigger for misspelledWordRangesChanged
        // that isn't already covered by an explicit setter/invokable below —
        // connect once per document so the underline overlay knows to
        // recompute after every edit, not just after settings changes. Routed
        // through the change-detecting helper (not a direct emit) — see its
        // own comment for why.
        connect(document->textDocument(), &QTextDocument::contentsChanged, this,
                &DocumentHandler::emitMisspelledWordRangesChangedIfNeeded);
    } else {
        m_highlighter.reset();
    }
    m_lastMisspelledWordRanges = computeMisspelledWordRanges();
    emit misspelledWordRangesChanged();
}

bool DocumentHandler::spellcheckEnabled() const
{
    return m_spellcheckEnabled;
}

void DocumentHandler::setSpellcheckEnabled(bool enabled)
{
    if (m_spellcheckEnabled == enabled)
        return;
    m_spellcheckEnabled = enabled;
    if (m_highlighter)
        m_highlighter->setActive(enabled);
    emit spellcheckEnabledChanged();
    emitMisspelledWordRangesChangedIfNeeded();
}

QString DocumentHandler::spellcheckLanguage() const
{
    return m_spellcheckLanguage;
}

void DocumentHandler::setSpellcheckLanguage(const QString &language)
{
    if (m_spellcheckLanguage == language)
        return;
    m_spellcheckLanguage = language;
    if (m_spellCheckerBackend)
        m_spellCheckerBackend->setInner(makeSpellCheckerBackend(language));
    if (m_highlighter)
        m_highlighter->rehighlight();
    // The per-block cache in computeMisspelledWordRanges() keys staleness
    // off block.revision(), which only moves on a real text edit — a
    // language swap changes what isMisspelled() itself returns for the
    // SAME unchanged text, so every cached block must be forced to
    // recompute even though none of them actually changed.
    invalidateAllMisspelledBlockCaches();
    emit spellcheckLanguageChanged();
    emitMisspelledWordRangesChangedIfNeeded();
}

QStringList DocumentHandler::userDictionaryWords() const
{
    return m_userDictionaryWords;
}

void DocumentHandler::setUserDictionaryWords(const QStringList &words)
{
    if (m_userDictionaryWords == words)
        return;
    m_userDictionaryWords = words;
    if (m_spellCheckerBackend)
        m_spellCheckerBackend->setUserWords(words);
    if (m_highlighter)
        m_highlighter->rehighlight();
    // Same reasoning as setSpellcheckLanguage's own invalidation call above:
    // adding/removing a personal-dictionary word changes isMisspelled() for
    // unchanged text.
    invalidateAllMisspelledBlockCaches();
    emit userDictionaryWordsChanged();
    emitMisspelledWordRangesChangedIfNeeded();
}

int DocumentHandler::cursorPosition() const
{
    return m_cursorPosition;
}

void DocumentHandler::setCursorPosition(int position)
{
    if (m_cursorPosition == position)
        return;
    m_cursorPosition = position;
    emit cursorPositionChanged();
    // A cursor move can change which word is excluded from checking (see
    // this property's own header comment) without any text actually having
    // changed — e.g. clicking out of the word just typed, or arrowing into
    // a different one. Routed through the change-detecting helper so
    // ordinary cursor movement that doesn't cross into/out of any word
    // (the common case: moving within already-checked text) doesn't force
    // a needless Repeater rebuild.
    emitMisspelledWordRangesChangedIfNeeded();
}

QVariantList DocumentHandler::misspelledWordRanges() const
{
    return computeMisspelledWordRanges();
}

QVariantList DocumentHandler::computeMisspelledWordRanges() const
{
    QVariantList result;
    if (!m_document || !m_document->textDocument() || !m_spellCheckerBackend || !m_spellCheckerBackend->isValid())
        return result;

    QTextDocument *doc = m_document->textDocument();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        // Reuses the previous pass's dictionary lookups for this block if
        // its text hasn't changed since (see MisspelledBlockCache's own
        // comment) — only blocks actually touched by the most recent edit
        // ever call isMisspelled() again. A cache miss (nullptr, or a stale
        // revision) means this exact block's own words, not the whole
        // document.
        auto *cache = static_cast<MisspelledBlockCache *>(block.userData());
        if (!cache || cache->revision != block.revision()) {
            const bool isNewCache = !cache;
            if (isNewCache)
                cache = new MisspelledBlockCache();

            const QString blockText = block.text();
            const QList<QPair<int, int>> ranges = SpellCheckHighlighter::wordRangesInBlock(block);
            cache->misspelledRanges.clear();
            for (const auto &range : ranges) {
                const QString word = blockText.mid(range.first, range.second);
                if (m_spellCheckerBackend->isMisspelled(word))
                    cache->misspelledRanges.append(range);
            }
            cache->revision = block.revision();

            if (isNewCache)
                block.setUserData(cache); // transfers ownership to the block
        }

        for (const auto &range : cache->misspelledRanges) {
            const int start = block.position() + range.first;
            const int end = start + range.second;
            // Skip whatever word the cursor is currently sitting inside —
            // see cursorPosition's own header comment for why: it's the
            // word actively being typed/edited, so a partial token
            // shouldn't be flagged as a misspelling-in-progress. Checked
            // here (against the cache's raw result), not baked into the
            // cache itself, since the cursor moving in and out of a word
            // must not require re-running isMisspelled() against it.
            if (m_cursorPosition >= start && m_cursorPosition <= end)
                continue;
            QVariantMap entry;
            entry.insert(QStringLiteral("start"), start);
            entry.insert(QStringLiteral("end"), end);
            result.append(entry);
        }
    }
    return result;
}

void DocumentHandler::emitMisspelledWordRangesChangedIfNeeded()
{
    const QVariantList current = computeMisspelledWordRanges();
    if (current == m_lastMisspelledWordRanges)
        return;
    m_lastMisspelledWordRanges = current;
    emit misspelledWordRangesChanged();
}

void DocumentHandler::invalidateAllMisspelledBlockCaches()
{
    if (!m_document || !m_document->textDocument())
        return;

    // Simplest correct invalidation: bump every cache's stored revision
    // to something block.revision() can never legitimately equal next,
    // rather than deleting/reallocating each MisspelledBlockCache — the
    // very next computeMisspelledWordRanges() call already treats a
    // revision mismatch as "recompute this block", so this reuses that
    // exact path instead of duplicating it.
    QTextDocument *doc = m_document->textDocument();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        if (auto *cache = static_cast<MisspelledBlockCache *>(block.userData()))
            cache->revision = -1;
    }
}

void DocumentHandler::rehighlightSpellcheck()
{
    if (m_highlighter)
        m_highlighter->rehighlight();
    // Same reasoning as setSpellcheckLanguage's own invalidation call above
    // — this is an explicit "the dictionary's answer for some word may have
    // changed" signal (e.g. right after settings.addUserDictionaryWord()),
    // not a text edit, so the per-block cache's revision-based staleness
    // check alone wouldn't catch it.
    invalidateAllMisspelledBlockCaches();
    emitMisspelledWordRangesChangedIfNeeded();
}

std::unique_ptr<SpellCheckerBackend> DocumentHandler::makeSpellCheckerBackend(const QString &language) const
{
    std::unique_ptr<SpellCheckerBackend> bundled;
#if defined(SUOPNOTES_HAVE_HUNSPELL) && defined(SUOPNOTES_HAVE_BUNDLED_DICTIONARY)
    bundled = std::make_unique<HunspellSpellCheckerBackend>(
        QStringLiteral(SUOPNOTES_BUNDLED_DICT_AFF_PATH), QStringLiteral(SUOPNOTES_BUNDLED_DICT_DIC_PATH));
#endif

    if (language == QStringLiteral("bundled:en_US"))
        return bundled;

#ifdef SUOPNOTES_HAVE_SONNET
    auto sonnet = std::make_unique<SonnetSpellCheckerBackend>(language);
    if (bundled)
        return std::make_unique<FallbackSpellCheckerBackend>(std::move(sonnet), std::move(bundled));
    return sonnet;
#else
    return bundled;
#endif
}

QVariantMap DocumentHandler::wordAt(int position) const
{
    if (!m_document || !m_document->textDocument())
        return QVariantMap();

    const QTextBlock block = m_document->textDocument()->findBlock(position);
    if (!block.isValid())
        return QVariantMap();

    const QList<QPair<int, int>> ranges = SpellCheckHighlighter::wordRangesInBlock(block);
    for (const auto &range : ranges) {
        const int start = block.position() + range.first;
        const int end = start + range.second;
        // Inclusive on `end` (bug found live: "with the specific string
        // 'x selected', selecting the right side of the letter x doesn't
        // trigger the spellchecker"): editor.positionAt() resolves a click
        // on the right half of a glyph to the cursor position immediately
        // AFTER that character. For a single-character word like "x"
        // (range [0, 1)), that's position 1 — which satisfies neither half
        // of an exclusive `position < end` check, since it's the word's own
        // end boundary, not a genuinely different word. Every word (not
        // just single-character ones) has this exact same dead zone at its
        // last character's right half; a one-letter word just means the
        // dead zone covers HALF of the entire word instead of a sliver of
        // its last letter, which is why it was the one that got noticed.
        // Two adjacent words can never collide over this shared boundary
        // point — real tokenization always has at least one non-letter
        // character (space/punctuation) between them, so word B's own
        // start is never equal to word A's end.
        if (position >= start && position <= end) {
            QVariantMap result;
            result.insert(QStringLiteral("start"), start);
            result.insert(QStringLiteral("end"), end);
            result.insert(QStringLiteral("text"), block.text().mid(range.first, range.second));
            return result;
        }
    }
    return QVariantMap();
}

bool DocumentHandler::isWordMisspelledAt(int position) const
{
    if (!m_spellCheckerBackend || !m_spellCheckerBackend->isValid())
        return false;

    const QVariantMap word = wordAt(position);
    if (word.isEmpty())
        return false;

    return m_spellCheckerBackend->isMisspelled(word.value(QStringLiteral("text")).toString());
}

QStringList DocumentHandler::suggestionsForWordAt(int position) const
{
    if (!isWordMisspelledAt(position))
        return QStringList();

    const QString text = wordAt(position).value(QStringLiteral("text")).toString();
    return m_spellCheckerBackend->suggestions(text);
}

void DocumentHandler::replaceRange(int start, int end, const QString &replacement)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.insertText(replacement);
}

QString DocumentHandler::toMarkdown() const
{
    if (!m_document || !m_document->textDocument())
        return QString();
    return MarkdownSerialization::toMarkdownPreservingBlankLines(m_document->textDocument());
}

void DocumentHandler::setMarkdown(const QString &markdown)
{
    if (!m_document || !m_document->textDocument())
        return;
    MarkdownSerialization::setMarkdownPreservingBlankLines(m_document->textDocument(), markdown);
}

QTextCursor DocumentHandler::cursorFor(int selectionStart, int selectionEnd) const
{
    if (!m_document || !m_document->textDocument())
        return QTextCursor();

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(selectionStart);
    cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    return cursor;
}

void DocumentHandler::toggleBold(int selectionStart, int selectionEnd)
{
    TextFormatting::toggleBold(cursorFor(selectionStart, selectionEnd));
    emit formattingChanged();
}

void DocumentHandler::toggleItalic(int selectionStart, int selectionEnd)
{
    TextFormatting::toggleItalic(cursorFor(selectionStart, selectionEnd));
    emit formattingChanged();
}

void DocumentHandler::toggleStrikethrough(int selectionStart, int selectionEnd)
{
    TextFormatting::toggleStrikethrough(cursorFor(selectionStart, selectionEnd));
    emit formattingChanged();
}

void DocumentHandler::toggleBulletList(int selectionStart, int selectionEnd)
{
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return;
    TextFormatting::toggleBulletList(cursorFor(selectionStart, selectionEnd));
    emit formattingChanged();
}

void DocumentHandler::toggleNumberedList(int selectionStart, int selectionEnd)
{
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return;
    TextFormatting::toggleNumberedList(cursorFor(selectionStart, selectionEnd));
    emit formattingChanged();
}

void DocumentHandler::toggleCheckboxList(int selectionStart, int selectionEnd)
{
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return;
    TextFormatting::toggleCheckboxList(cursorFor(selectionStart, selectionEnd));
    emit formattingChanged();
}

void DocumentHandler::setHeadingLevel(int selectionStart, int selectionEnd, int level)
{
    QTextCursor cursor = cursorFor(selectionStart, selectionEnd);
    // A toggle, not a plain setter: clicking H1 on a line that's already H1
    // reverts it to a normal paragraph, matching every other toolbar toggle
    // (Bold, bullet list, ...). Callers that always want a specific level
    // unconditionally (e.g. an explicit "clear formatting" shortcut passing
    // level 0) still get exactly that level whenever the current level
    // differs from it — only the "already at the requested level" case
    // clears instead of re-applying it.
    const int currentLevel = TextFormatting::headingLevel(cursor);
    TextFormatting::setHeadingLevel(cursor, currentLevel == level ? 0 : level);
    emit formattingChanged();
}

void DocumentHandler::insertTabSpaces(int selectionStart, int selectionEnd)
{
    QTextCursor cursor = cursorFor(selectionStart, selectionEnd);
    TextFormatting::insertTabSpaces(cursor);
}

void DocumentHandler::insertTable(int position)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    TextFormatting::insertTable(cursor);
}

void DocumentHandler::insertTableRow(int position)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    TextFormatting::insertTableRow(cursor);
}

void DocumentHandler::deleteTableRow(int position)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    TextFormatting::deleteTableRow(cursor);
}

void DocumentHandler::insertTableColumn(int position)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    TextFormatting::insertTableColumn(cursor);
}

void DocumentHandler::deleteTableColumn(int position)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    TextFormatting::deleteTableColumn(cursor);
}

void DocumentHandler::deleteTable(int position)
{
    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    TextFormatting::deleteTable(cursor);
}

bool DocumentHandler::tryContinueList(int cursorPosition)
{
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return false;

    if (!m_document || !m_document->textDocument())
        return false;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(cursorPosition);

    // Numbered/checkbox items are hand-rolled (see TextFormatting.h) and
    // handled independently of the QTextList-based bullet path below.
    if (TextFormatting::isNumberedList(cursor) || TextFormatting::isCheckboxList(cursor)) {
        if (TextFormatting::isHandRolledListItemEmpty(cursor)) {
            TextFormatting::exitHandRolledList(cursor);
            return true;
        }
        return TextFormatting::continueHandRolledList(cursor);
    }

    QTextList *list = cursor.currentList();
    if (!list)
        return false;

    if (cursor.block().text().isEmpty()) {
        // An empty item is the standard "press Enter again to leave the
        // list" escape hatch — otherwise there would be no way to get back
        // to a plain paragraph without reaching for the toolbar.
        QTextBlockFormat blockFormat = cursor.blockFormat();
        blockFormat.setIndent(0);
        cursor.mergeBlockFormat(blockFormat);
        list->remove(cursor.block());
        return true;
    }

    cursor.insertBlock();
    list->add(cursor.block());
    return true;
}

bool DocumentHandler::tryAutoConvertListShorthand(int cursorPosition)
{
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return false;

    if (!m_document || !m_document->textDocument())
        return false;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(cursorPosition);

    // Already inside a list — nothing to shorthand-convert. Numbered/
    // checkbox items are hand-rolled (no QTextList), hence the extra checks.
    if (cursor.currentList() || TextFormatting::isNumberedList(cursor) || TextFormatting::isCheckboxList(cursor))
        return false;

    const QTextBlock block = cursor.block();
    const QString textBeforeCursor = block.text().left(cursorPosition - block.position());

    static const QRegularExpression checkboxShorthand(QStringLiteral("^[-*] \\[[ xX]?\\] $"));
    static const QRegularExpression bulletShorthand(QStringLiteral("^[-*] $"));
    static const QRegularExpression numberedShorthand(QStringLiteral("^(\\d+)\\. $"));

    const bool isCheckbox = checkboxShorthand.match(textBeforeCursor).hasMatch();
    const bool isBullet = !isCheckbox && bulletShorthand.match(textBeforeCursor).hasMatch();
    const QRegularExpressionMatch numberedMatch = numberedShorthand.match(textBeforeCursor);
    const bool isNumbered = !isCheckbox && !isBullet && numberedMatch.hasMatch();
    if (!isCheckbox && !isBullet && !isNumbered)
        return false;

    QTextCursor stripCursor(m_document->textDocument());
    stripCursor.setPosition(block.position());
    stripCursor.setPosition(cursorPosition, QTextCursor::KeepAnchor);
    stripCursor.removeSelectedText();

    if (isCheckbox)
        TextFormatting::toggleCheckboxList(stripCursor);
    else if (isNumbered)
        // Preserves whatever number the user actually typed (e.g. "5. "
        // starts the list at 5, not silently resetting to 1) — Qt's own
        // auto-numbering still increments normally for every item after it.
        TextFormatting::toggleNumberedList(stripCursor, numberedMatch.captured(1).toInt());
    else
        TextFormatting::toggleBulletList(stripCursor);
    return true;
}

void DocumentHandler::maintainListNumbering(int cursorPosition)
{
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return;

    if (!m_document || !m_document->textDocument())
        return;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(cursorPosition);
    TextFormatting::maintainHandRolledListNumbering(cursor);
}

bool DocumentHandler::tryResetHeadingOnReturn(int cursorPosition)
{
    if (!m_document || !m_document->textDocument())
        return false;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(cursorPosition);

    if (TextFormatting::headingLevel(cursor) == 0)
        return false;

    cursor.insertBlock();
    TextFormatting::setHeadingLevel(cursor, 0);
    return true;
}

bool DocumentHandler::isBold(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isBold(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::isItalic(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isItalic(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::isStrikethrough(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isStrikethrough(cursorFor(selectionStart, selectionEnd));
}

int DocumentHandler::headingLevel(int selectionStart, int selectionEnd) const
{
    return TextFormatting::headingLevel(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::isBulletList(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isBulletList(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::isNumberedList(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isNumberedList(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::isCheckboxList(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isCheckboxList(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::isCheckboxChecked(int selectionStart, int selectionEnd) const
{
    return TextFormatting::isCheckboxChecked(cursorFor(selectionStart, selectionEnd));
}

bool DocumentHandler::tryToggleCheckboxAt(int position)
{
    ListMutationGuard guard(m_handlingListMutation, m_document ? m_document->textDocument() : nullptr);
    if (guard.reentrant())
        return false;

    if (!m_document || !m_document->textDocument())
        return false;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    const bool toggled = TextFormatting::tryToggleCheckboxAtPosition(cursor);
    if (toggled)
        emit formattingChanged();
    return toggled;
}

bool DocumentHandler::isInTable(int position) const
{
    if (!m_document || !m_document->textDocument())
        return false;

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    return TextFormatting::isInTable(cursor);
}

QRectF DocumentHandler::tableRect(int position) const
{
    if (!m_document || !m_document->textDocument())
        return QRectF();

    QTextDocument *document = m_document->textDocument();
    QTextCursor cursor(document);
    cursor.setPosition(position);
    QTextTable *table = cursor.currentTable();
    if (!table)
        return QRectF();

    return document->documentLayout()->frameBoundingRect(table);
}

QRectF DocumentHandler::checkboxRectAt(int position) const
{
    if (!m_document || !m_document->textDocument())
        return QRectF();

    QTextCursor cursor(m_document->textDocument());
    cursor.setPosition(position);
    return TextFormatting::checkboxMarkerRect(cursor);
}
