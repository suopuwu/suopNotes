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

    // Bullet and Checkbox only: the one character within the line whose space
    // is reserved for a drawn control. The rest of the marker collapses to
    // zero width, so this single character's slot becomes the whole column the
    // bullet/checkbox is painted into. Zero-length for Numbered, whose marker
    // stays ordinary visible text and is never replaced by anything drawn.
    //
    // These exist as one shared value rather than each caller re-deriving them
    // because two callers must agree on this range to the character:
    // MarkdownHighlighter makes exactly this range transparent-but-full-width
    // (and everything around it zero-width), and NoteWindow.qml draws the real
    // bullet/checkbox into exactly this range's rect. Off by one in either
    // direction and the control lands beside a leftover visible "-", or on top
    // of the item's own first letter.
    //
    // For a bullet the character is the obvious one, its "-". For a checkbox
    // it is deliberately the OPENING BRACKET, not the " "/"x" state character
    // between the brackets: the state character is the one part of the marker
    // whose width changes when the box is ticked, so reserving it would make
    // every checkbox shift sideways by the difference between a space and an
    // "x" the moment it was clicked. The bracket's advance never changes.
    int glyphStart = 0;
    int glyphLength = 0;

    bool isValid() const { return kind != Kind::None; }
};

// The letter-spacing percentage a marker's reserved glyph slot is rendered at
// (QFont::PercentageSpacing: 200 means the reserved character takes twice its
// natural advance). A checkbox is DRAWN wider than a bullet (feature request:
// "the display for checkboxes should be a little bigger" — see NoteWindow.qml's
// checkboxScale), so its slot is wider.
//
// These live here, shared, because THREE places must reserve the SAME width to
// the pixel or a checkbox misaligns: MarkdownHighlighter applies this as the
// reserved character's on-screen advance, NoteWindow.qml draws the control to
// fill exactly that advance, and TextFormatting sizes a wrapped list item's
// hanging indent from it. When only two of the three agreed (the indent used a
// flat 200 while the box drew at 320) every wrapped checkbox continuation line
// sat too far left, under the box instead of under the item's text.
constexpr qreal kBulletSlotPercent = 200.0;
constexpr qreal kCheckboxSlotPercent = 320.0;

// The reserved-slot percent for a parsed marker's kind: the checkbox slot for a
// Checkbox, the bullet slot for a Bullet. Numbered/None reserve no drawn slot
// (their marker stays ordinary text) and return 0.
qreal slotPercentForKind(Kind kind);

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
