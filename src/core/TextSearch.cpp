#include "TextSearch.h"

#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocument>

namespace
{
// Both modes funnel into one compiled QRegularExpression: literal mode is
// just the escaped pattern, so the search loop, case handling and Unicode
// behavior cannot drift between the two.
QRegularExpression compiledPattern(const QString &pattern, bool useRegex, bool caseSensitive)
{
    QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
    if (!caseSensitive)
        options |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(useRegex ? pattern : QRegularExpression::escape(pattern), options);
}

// Expands $N capture references and the $$ escape against a concrete match.
// Hand-rolled because QRegularExpressionMatch exposes captured texts but no
// replacement-template expansion of its own.
QString expandReplacement(const QString &replacement, const QRegularExpressionMatch &match)
{
    QString result;
    result.reserve(replacement.size());
    for (int i = 0; i < replacement.size(); ++i) {
        const QChar ch = replacement.at(i);
        if (ch != QLatin1Char('$') || i + 1 >= replacement.size()) {
            result.append(ch);
            continue;
        }
        const QChar next = replacement.at(i + 1);
        if (next == QLatin1Char('$')) {
            // "$$" is the escape for one literal dollar sign.
            result.append(QLatin1Char('$'));
            ++i;
            continue;
        }
        if (!next.isDigit()) {
            result.append(ch);
            continue;
        }
        // $N / $NN: the longest digit run naming a real capture group wins —
        // "$12" with twelve groups is capture 12, with two groups it is
        // capture 1 followed by a literal '2'.
        int digits = 1;
        if (i + 2 < replacement.size() && replacement.at(i + 2).isDigit()
            && replacement.mid(i + 1, 2).toInt() <= match.lastCapturedIndex()) {
            digits = 2;
        }
        const int group = replacement.mid(i + 1, digits).toInt();
        if (group <= match.lastCapturedIndex()) {
            result.append(match.captured(group));
            i += digits;
        } else {
            // "$7" with no seventh group stays literal text.
            result.append(ch);
        }
    }
    return result;
}

// The anchored re-match that recovers a range's capture groups for $N
// expansion. Fails (no value) if the pattern doesn't match exactly [start,
// end) anymore — the caller's match list has gone stale against the
// document.
std::optional<QRegularExpressionMatch> matchAtRange(const QRegularExpression &re,
                                                    const QString &text, int start, int end)
{
    const QRegularExpressionMatch match =
        re.match(text, start, QRegularExpression::NormalMatch,
                 QRegularExpression::AnchorAtOffsetMatchOption);
    if (!match.hasMatch() || match.capturedEnd() != end)
        return std::nullopt;
    return match;
}
}

namespace TextSearch
{

bool patternValid(const QString &pattern, bool useRegex)
{
    if (!useRegex)
        return true;
    return QRegularExpression(pattern).isValid();
}

QList<Match> allMatches(const QString &text, const QString &pattern, bool useRegex, bool caseSensitive)
{
    QList<Match> matches;
    if (pattern.isEmpty())
        return matches;

    const QRegularExpression re = compiledPattern(pattern, useRegex, caseSensitive);
    if (!re.isValid())
        return matches;

    // globalMatch's iterator advances by one position past a zero-length
    // match instead of re-matching in place, which is what keeps degenerate
    // patterns like "x*" from looping forever.
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        matches.append({int(match.capturedStart()), int(match.capturedEnd())});
    }
    return matches;
}

std::optional<Match> replaceMatch(QTextDocument *document, int start, int end,
                                  const QString &pattern, const QString &replacement,
                                  bool useRegex, bool caseSensitive)
{
    if (!document || start < 0 || end < start)
        return std::nullopt;

    const QString text = document->toPlainText();
    if (end > text.size())
        return std::nullopt;

    QString expanded = replacement;
    if (useRegex) {
        const QRegularExpression re = compiledPattern(pattern, useRegex, caseSensitive);
        if (!re.isValid())
            return std::nullopt;
        const auto match = matchAtRange(re, text, start, end);
        if (!match)
            return std::nullopt;
        expanded = expandReplacement(replacement, *match);
    }

    QTextCursor cursor(document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.insertText(expanded);
    return Match{start, start + int(expanded.size())};
}

int replaceAll(QTextDocument *document, const QString &pattern, const QString &replacement,
               bool useRegex, bool caseSensitive)
{
    if (!document)
        return 0;

    // Match once against a snapshot, then replace back-to-front: every edit
    // only moves text AFTER earlier matches' positions, so the snapshot's
    // positions (and its capture groups, re-derived per match below) stay
    // valid for the whole loop even when the replacement contains the
    // pattern itself.
    const QString text = document->toPlainText();
    const QList<Match> matches = allMatches(text, pattern, useRegex, caseSensitive);
    if (matches.isEmpty())
        return 0;

    const QRegularExpression re = compiledPattern(pattern, useRegex, caseSensitive);

    // One edit block around the whole loop — same shape as DocumentHandler's
    // ListMutationGuard, for the same reasons: replace-all must be exactly
    // ONE native undo step (not one per occurrence), and contentsChange gets
    // deferred and coalesced to a single emission at endEditBlock(), so
    // downstream listeners (autosave, list maintenance) react once. The
    // cursor exists purely to call begin/endEditBlock, never to edit through.
    QTextCursor editBlockCursor(document);
    editBlockCursor.beginEditBlock();
    for (int i = matches.size() - 1; i >= 0; --i) {
        const Match &m = matches.at(i);
        QString expanded = replacement;
        if (useRegex) {
            const auto match = matchAtRange(re, text, m.start, m.end);
            if (match)
                expanded = expandReplacement(replacement, *match);
        }
        QTextCursor cursor(document);
        cursor.setPosition(m.start);
        cursor.setPosition(m.end, QTextCursor::KeepAnchor);
        cursor.insertText(expanded);
    }
    editBlockCursor.endEditBlock();
    return matches.size();
}
}
