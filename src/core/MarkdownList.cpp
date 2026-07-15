#include "MarkdownList.h"

#include <QRegularExpression>

namespace
{

// "- [ ] " / "* [x] " / "+ [X] " — any bullet char, either checked spelling.
// Must be tried before the bullet pattern, which would otherwise claim the
// leading "- " and report a checkbox as an ordinary bullet.
const QRegularExpression &checkboxPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^[-*+]\\s\\[([ xX])\\]\\s"));
    return pattern;
}

const QRegularExpression &bulletPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^[-*+]\\s"));
    return pattern;
}

const QRegularExpression &numberedPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d+)\\.\\s"));
    return pattern;
}

}

namespace MarkdownList
{

Marker parse(const QString &line)
{
    Marker marker;

    if (const QRegularExpressionMatch match = checkboxPattern().match(line); match.hasMatch()) {
        marker.kind = Kind::Checkbox;
        marker.length = match.capturedLength();
        marker.checked = match.captured(1) != QStringLiteral(" ");
        return marker;
    }

    if (const QRegularExpressionMatch match = bulletPattern().match(line); match.hasMatch()) {
        marker.kind = Kind::Bullet;
        marker.length = match.capturedLength();
        return marker;
    }

    if (const QRegularExpressionMatch match = numberedPattern().match(line); match.hasMatch()) {
        marker.kind = Kind::Numbered;
        marker.length = match.capturedLength();
        marker.number = match.captured(1).toInt();
        return marker;
    }

    return marker;
}

QString bulletMarker()
{
    return QStringLiteral("- ");
}

QString numberedMarker(int number)
{
    return QStringLiteral("%1. ").arg(number);
}

QString checkboxMarker(bool checked)
{
    // Literal GFM, which is also exactly what gets saved. The pre-rewrite
    // editor stored a Unicode ☐/☑ glyph in the document and translated it to
    // this syntax only at save time — the stored-vs-saved split this rewrite
    // exists to remove.
    return checked ? QStringLiteral("- [x] ") : QStringLiteral("- [ ] ");
}

}
