#include "MarkdownImage.h"

#include <QRegularExpression>

namespace
{
// Group 1 is the whole image span (excluding surrounding whitespace); group 2
// is the alt text; group 3 is the src. Either of alt/src may be empty.
const QRegularExpression &imagePattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^\\s*(!\\[([^\\]\\n]*)\\]\\(([^)\\n]*)\\))\\s*$"));
    return pattern;
}
}

namespace MarkdownImage
{

Match parse(const QString &line)
{
    Match match;
    const QRegularExpressionMatch m = imagePattern().match(line);
    if (!m.hasMatch())
        return match;

    match.isValid = true;
    match.spanStart = m.capturedStart(1);
    match.spanLength = m.capturedLength(1);
    match.alt = m.captured(2);
    match.src = m.captured(3);
    match.srcStart = m.capturedStart(3);
    match.srcLength = m.capturedLength(3);
    return match;
}

}
