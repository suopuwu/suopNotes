#pragma once

#include <QList>
#include <QString>

#include <optional>

QT_BEGIN_NAMESPACE
class QTextDocument;
QT_END_NAMESPACE

// Pure, stateless search/replace behind the find/replace UI — the same split
// TextFormatting uses: DocumentHandler owns the thin QML-facing wrappers,
// this file owns the independently-testable logic.
//
// All positions are in QTextDocument position space. Search runs over
// document->toPlainText(), whose character indices map 1:1 to document
// positions (Qt's paragraph separators come back as '\n'), so a range found
// here can be handed straight to a QTextCursor.
namespace TextSearch
{
// A single occurrence: [start, end) — same half-open convention as
// replaceRange and every range map DocumentHandler already reports.
struct Match
{
    int start = 0;
    int end = 0;

    // Lets a QList<Match> be compared wholesale — MarkdownHighlighter's
    // setSearchMatches() uses that to skip rehighlighting when a re-search
    // turns up the identical match list, which is what an edit elsewhere in
    // the document usually produces.
    friend bool operator==(const Match &lhs, const Match &rhs)
    {
        return lhs.start == rhs.start && lhs.end == rhs.end;
    }
    friend bool operator!=(const Match &lhs, const Match &rhs) { return !(lhs == rhs); }
};

// Whether `pattern` can be searched at all — the UI's "show the field in an
// error state" predicate. Literal patterns are always valid (they get
// QRegularExpression::escape()d before compiling); a regex pattern is valid
// iff it compiles. An empty pattern is deliberately still "valid": a cleared
// search field means "nothing to search for", not "you typed it wrong".
bool patternValid(const QString &pattern, bool useRegex);

// Every non-overlapping occurrence of `pattern` in `text`, in document
// order. Literal mode matches the pattern's characters exactly,
// metacharacters included; regex mode compiles the pattern as-is with
// UseUnicodePropertiesOption (the in-repo idiom — see SpellCheckHighlighter's
// word pattern — so \w and friends understand Unicode). Case-insensitive
// unless `caseSensitive`. An empty or invalid pattern yields no matches
// rather than erroring. Zero-length matches (e.g. the regex "x*" between
// x's) are reported but never repeated at the same position — matching
// always advances, so a degenerate pattern cannot loop forever.
QList<Match> allMatches(const QString &text, const QString &pattern, bool useRegex, bool caseSensitive);

// Replaces the single [start, end) range with `replacement` — one
// insertText through one cursor, i.e. exactly one native undo step. In
// regex mode the replacement expands $1..$99 to the pattern's capture
// groups ($0 is the whole match; a $N with no such group stays literal) and
// $$ to a literal dollar sign — which requires re-matching the pattern
// anchored at `start` to recover the captures, since a Match carries only
// positions. Literal mode inserts the replacement verbatim, $s and all.
//
// Returns the [start, end) range the replacement occupies afterwards (so
// the UI can reselect it), or nullopt when nothing was replaced: null
// document, range out of bounds, or — regex mode — the range no longer
// matches the pattern (e.g. the document changed under a stale match list).
std::optional<Match> replaceMatch(QTextDocument *document, int start, int end,
                                  const QString &pattern, const QString &replacement,
                                  bool useRegex, bool caseSensitive);

// Replaces every occurrence of `pattern` with `replacement` (same
// regex-mode $N/$$ expansion as replaceMatch) and returns how many. The
// whole loop runs inside ONE QTextDocument edit block, so replace-all is
// exactly one undo step and the document's contentsChange coalesces to a
// single emission at endEditBlock() — same batching rationale as
// DocumentHandler's ListMutationGuard. Matches are found once against a
// snapshot of the text and replaced back-to-front, so earlier matches'
// positions stay valid regardless of what the replacement does (including
// containing the pattern itself — "a" -> "aa" terminates).
int replaceAll(QTextDocument *document, const QString &pattern, const QString &replacement,
               bool useRegex, bool caseSensitive);
}
