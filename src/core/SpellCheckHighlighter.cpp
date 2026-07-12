#include "SpellCheckHighlighter.h"

#include "SpellChecker.h"
#include "TextFormatting.h"

#include <QHash>
#include <QRegularExpression>
#include <QTextCharFormat>

namespace
{

// Hand-rolled list markers this editor writes as literal block-leading text
// (see TextFormatting.h's own comment on toggleNumberedList/
// toggleCheckboxList for why these are real text rather than QTextList
// decoration). TextFormatting.cpp keeps its own copies of the checkbox
// glyphs and the numbered-marker pattern, but both are file-local (anonymous
// namespace, no header declaration) — nothing public exposes them, so they
// are re-declared here rather than shared.
const QRegularExpression &numberedMarkerPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d+\\.\\s+)"));
    return pattern;
}

const QRegularExpression &checkboxMarkerPattern()
{
    // "☐ " / "☑ " — the literal Unicode ballot-box glyphs, not ASCII.
    static const QRegularExpression pattern(QStringLiteral("^(\\x{2610}\\s|\\x{2611}\\s)"));
    return pattern;
}

// Word-like tokens: runs of Unicode letters plus internal apostrophes
// ("don't", "it's"). Punctuation (markdown or otherwise) simply isn't
// captured, so stray "*"/"#"/"["/"]" characters never leak into a token.
const QRegularExpression &wordPattern()
{
    static const QRegularExpression pattern(QStringLiteral("[\\p{L}']+"),
                                              QRegularExpression::UseUnicodePropertiesOption);
    return pattern;
}

bool isPurelyNumeric(const QString &token)
{
    if (token.isEmpty())
        return false;
    for (const QChar &ch : token) {
        if (!ch.isDigit())
            return false;
    }
    return true;
}

}

SpellCheckHighlighter::SpellCheckHighlighter(QTextDocument *document, SpellCheckerBackend *backend)
    : QSyntaxHighlighter(static_cast<QObject *>(nullptr)), m_backend(backend)
{
    // See the header comment on this constructor: intentionally NOT using
    // QSyntaxHighlighter(document), which would make `document` this
    // object's QObject parent and race DocumentHandler's own unique_ptr
    // ownership of it. setDocument() attaches for highlighting purposes
    // only, without taking a parent/ownership stake.
    setDocument(document);
}

void SpellCheckHighlighter::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    // Whether turning off (clear existing underlines) or back on (flag
    // already-present text, not just future edits), a full rehighlight is
    // what makes the visible state match m_active immediately.
    rehighlight();
}

bool SpellCheckHighlighter::isActive() const
{
    return m_active;
}

QList<QPair<int, int>> SpellCheckHighlighter::wordRangesInBlock(const QTextBlock &block)
{
    QList<QPair<int, int>> ranges;
    const QString text = block.text();

    int searchStart = 0;
    if (TextFormatting::isHandRolledListBlock(block)) {
        QRegularExpressionMatch match = numberedMarkerPattern().match(text);
        if (!match.hasMatch())
            match = checkboxMarkerPattern().match(text);
        if (match.hasMatch())
            searchStart = match.capturedLength(1);
    }

    QRegularExpressionMatchIterator it = wordPattern().globalMatch(text, searchStart);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString word = match.captured();
        if (word.isEmpty() || isPurelyNumeric(word))
            continue;
        ranges.append(qMakePair(match.capturedStart(), match.capturedLength()));
    }
    return ranges;
}

void SpellCheckHighlighter::highlightBlock(const QString &text)
{
    if (!m_active || !m_backend || !m_backend->isValid())
        return;

    QTextCharFormat misspelledFormat;
    misspelledFormat.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    misspelledFormat.setUnderlineColor(Qt::red);

    // Caches each distinct token's misspelled/not result for the duration of
    // this single highlightBlock pass, so a word repeated within one block
    // (e.g. "the the") only costs one isMisspelled() dictionary lookup.
    QHash<QString, bool> misspelledByWord;
    const QList<QPair<int, int>> ranges = wordRangesInBlock(currentBlock());
    for (const auto &range : ranges) {
        const QString word = text.mid(range.first, range.second);

        auto cached = misspelledByWord.find(word);
        if (cached == misspelledByWord.end())
            cached = misspelledByWord.insert(word, m_backend->isMisspelled(word));

        if (cached.value())
            setFormat(range.first, range.second, misspelledFormat);
    }
}
