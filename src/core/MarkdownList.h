#pragma once

#include <QString>

// List markers over *literal* markdown text.
//
// Before the plain-text rewrite, "is this line a list item" was answered by a
// custom QTextBlockFormat property (HandRolledKindProperty) that had to be
// stamped onto each block and kept in sync with the text — a whole "healing"
// subsystem existed purely because Qt's paste/merge machinery could drop that
// property while leaving the text intact. Once the text IS the truth, the
// question is just "what does this line start with", and that desync class of
// bug cannot happen.
//
// This lives in its own module rather than inside TextFormatting because three
// separate places need to agree on exactly what a marker is: TextFormatting
// (editing them), MarkdownHighlighter (dimming them), and SpellCheckHighlighter
// (skipping them so a marker is never offered as a misspelled word). Three
// private copies of the same regex is three chances for them to disagree about
// where a marker ends — and a mismatch shows up as a real bug (a marker
// underlined as a typo, or emphasis parsed out of a bullet's own "*").
namespace MarkdownList
{

enum class Kind
{
    None,
    Bullet,   // "- item" / "* item" / "+ item"
    Numbered, // "1. item"
    Checkbox, // "- [ ] item" / "- [x] item"
};

// What a line begins with. `length` counts the whole marker including the
// space after it, so line.mid(marker.length) is the item's own text and
// block.position() + marker.length is where its text starts in the document.
struct Marker
{
    Kind kind = Kind::None;
    int length = 0;
    int number = 0;     // Numbered only; 0 otherwise
    bool checked = false; // Checkbox only

    bool isValid() const { return kind != Kind::None; }
};

// Parses `line`'s leading marker, if any. Checkbox is tested before bullet:
// "- [ ] x" starts with "- " too, and reporting it as a plain bullet would let
// the toolbar toggle the wrong thing.
Marker parse(const QString &line);

// The canonical text this editor writes for each marker kind. Reading accepts
// every GFM spelling (any of -, *, + for bullets; [x] or [X] when checked);
// writing always uses these, so the file stays internally consistent.
QString bulletMarker();
QString numberedMarker(int number);
QString checkboxMarker(bool checked);

}
