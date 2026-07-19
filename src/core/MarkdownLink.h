#pragma once

#include <QRegularExpression>
#include <QString>

// Inline links over literal markdown text: "[text](url)", but NOT "![alt](url)"
// (that is an image — see MarkdownImage). The url is allowed to be empty so a
// freshly-inserted "[]()" still parses as a link while it is being filled in.
//
// Its own module, like MarkdownImage, so the ONE thing that tells a link apart
// from an image — the negative lookbehind on "!" — is defined once. It is shared
// between MarkdownHighlighter (which styles links) and DocumentHandler (which
// resolves the url under the cursor): a link one recognized but the other did
// not would render clickable-looking yet do nothing, or vice versa.
namespace MarkdownLink
{

// The shared pattern. Capture 1 is the visible text (what renders as the link);
// capture 2 is the url. Exposed for callers that iterate a line's links
// themselves (e.g. the highlighter's masking-aware scan); most callers want
// urlAt() below instead.
const QRegularExpression &pattern();

// The url of the link whose visible TEXT contains `positionInLine`, or an empty
// string if that position isn't on a link. (The url punctuation "](url)" itself
// is not a hit target — only the visible text is, matching what renders.)
QString urlAt(const QString &line, int positionInLine);

}
