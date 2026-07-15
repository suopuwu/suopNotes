#include "SpellCheckHighlighter.h"

#include "MarkdownList.h"
#include "SpellChecker.h"

#include <QHash>
#include <QRegularExpression>
#include <QTextCharFormat>

namespace
{

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

    // Skip any list marker so its own characters are never offered up as a
    // misspelled word. Delegated to MarkdownList rather than re-matched here:
    // this file used to keep private copies of the marker patterns, which is
    // one more place for them to disagree with TextFormatting about where a
    // marker ends — and a disagreement shows up as a marker underlined as a
    // typo.
    const int searchStart = MarkdownList::parse(text).length;

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
