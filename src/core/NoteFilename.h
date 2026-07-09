#pragma once

#include <QString>

// Pure text-to-filename derivation, independently testable (same pattern as
// TextFormatting). Knows nothing about disk, ids, or collisions — NoteStore
// owns turning this into an actual unique path.
namespace NoteFilename
{
// Derives a human-readable, filesystem-safe base name (no directory, no
// ".md" extension) from a note's markdown content:
//  - The first non-empty line is used verbatim (no special treatment of
//    markdown headings).
//  - The result is stripped of characters that are invalid/risky in
//    filenames, collapsed to a single line, and truncated to a handful of
//    words if it's too long.
//  - Falls back to "Untitled" if nothing usable remains (e.g. empty note,
//    or a line made up entirely of invalid characters).
QString deriveBaseName(const QString &markdownContent);
}
