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

qreal slotPercentForKind(Kind kind)
{
    switch (kind) {
    case Kind::Checkbox:
        return kCheckboxSlotPercent;
    case Kind::Bullet:
        return kBulletSlotPercent;
    case Kind::Numbered:
    case Kind::None:
        break;
    }
    return 0.0;
}

Marker parse(const QString &line)
{
    Marker marker;

    if (const QRegularExpressionMatch match = checkboxPattern().match(line); match.hasMatch()) {
        marker.kind = Kind::Checkbox;
        marker.length = match.capturedLength();
        marker.checked = match.captured(1) != QStringLiteral(" ");
        // The "[" immediately before the captured state character. Derived
        // from the match rather than hard-coded to index 2 so it stays correct
        // for every bullet spelling the pattern accepts. See Marker::glyphStart
        // on why the bracket and not the state character itself.
        marker.glyphStart = match.capturedStart(1) - 1;
        marker.glyphLength = 1;
        return marker;
    }

    if (const QRegularExpressionMatch match = bulletPattern().match(line); match.hasMatch()) {
        marker.kind = Kind::Bullet;
        marker.length = match.capturedLength();
        marker.glyphStart = 0;
        marker.glyphLength = 1; // the "-" itself
        return marker;
    }

    if (const QRegularExpressionMatch match = numberedPattern().match(line); match.hasMatch()) {
        marker.kind = Kind::Numbered;
        marker.length = match.capturedLength();
        marker.number = match.captured(1).toInt();
        // Left at 0/0: "1." stays real text, so there is no reserved slot and
        // nothing gets drawn over it.
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
