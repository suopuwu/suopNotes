#include "MarkdownLink.h"

namespace MarkdownLink
{

const QRegularExpression &pattern()
{
    // The negative lookbehind on "!" is what tells a link apart from an image.
    // The visible text (capture 1) requires at least one character; the url
    // (capture 2) may be empty for a "[]()" still being filled in.
    static const QRegularExpression p(QStringLiteral("(?<!!)\\[([^\\]\\n]+)\\]\\(([^)\\n]*)\\)"));
    return p;
}

QString urlAt(const QString &line, int positionInLine)
{
    QRegularExpressionMatchIterator it = pattern().globalMatch(line);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        // The hit region is the visible text (capture 1) — what actually renders
        // as the link — not the collapsed "](url)" punctuation.
        if (positionInLine >= match.capturedStart(1) && positionInLine < match.capturedEnd(1))
            return match.captured(2);
    }
    return QString();
}

}
