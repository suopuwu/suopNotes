#include "NoteFilename.h"

#include <QRegularExpression>
#include <QStringList>

namespace NoteFilename
{

namespace
{
constexpr int kMaxLength = 60;

QString firstNonEmptyLine(const QString &markdownContent)
{
    const QStringList lines = markdownContent.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            return trimmed;
    }
    return QString();
}

// Strips a single leading list/blockquote marker ("- ", "* ", "42. ", "> ")
// so plain-text notes don't produce filenames starting with punctuation
// that's awkward on a command line (e.g. a leading '-' being read as a flag).
QString stripLeadingMarker(const QString &text)
{
    static const QRegularExpression markerPattern(QStringLiteral("^\\s*(?:[-*+>]|\\d+\\.)\\s+"));
    QString result = text;
    result.remove(markerPattern);
    return result;
}

QString sanitize(const QString &text)
{
    QString result = text;

    // Characters invalid (or risky) in filenames across the filesystems
    // this app is likely to touch, plus raw control characters.
    static const QRegularExpression invalidChars(QStringLiteral("[/\\\\:*?\"<>|\\x00-\\x1F]"));
    result.remove(invalidChars);

    // Collapse whitespace left behind by removed characters/markdown syntax.
    result = result.simplified();

    // A leading dot would make the file look hidden; a leading hyphen reads
    // as a command-line flag to careless shell usage. Trailing dots/spaces
    // are invalid, or get silently stripped, on some filesystems.
    while (!result.isEmpty() && (result.front() == QLatin1Char('.') || result.front() == QLatin1Char('-')))
        result.remove(0, 1);
    while (!result.isEmpty() && (result.back() == QLatin1Char('.') || result.back() == QLatin1Char(' ')))
        result.chop(1);

    return result.trimmed();
}

QString truncateToWordBoundary(const QString &text, int maxLength)
{
    if (text.length() <= maxLength)
        return text;

    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString result;
    for (const QString &word : words) {
        const QString candidate = result.isEmpty() ? word : result + QLatin1Char(' ') + word;
        if (candidate.length() > maxLength)
            break;
        result = candidate;
    }

    // A single word longer than the whole limit: hard-truncate rather than
    // produce an empty filename.
    if (result.isEmpty())
        result = text.left(maxLength);

    return result;
}

}

QString deriveBaseName(const QString &markdownContent)
{
    QString title = stripLeadingMarker(firstNonEmptyLine(markdownContent));

    title = sanitize(title);
    title = truncateToWordBoundary(title, kMaxLength);

    if (title.isEmpty())
        title = QStringLiteral("Untitled");

    return title;
}

}
