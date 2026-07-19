#pragma once

#include <QString>

// Whole-line images over literal markdown text: "![alt](src)", optionally
// indented or trailing-padded.
//
// This lives in its own module — like MarkdownList and MarkdownTable — because
// two callers must agree, to the character, on where the image span, alt and
// src are: MarkdownHighlighter reserves the tall band an image draws into, and
// DocumentHandler::computeImageRanges reports where NoteWindow.qml should draw
// it. Two private copies of this regex is two chances for them to disagree, and
// a disagreement shows up as a real bug — an image with no reserved space, or a
// reserved band with nothing drawn into it. (They previously drifted even in
// their capture-group numbering.)
namespace MarkdownImage
{

struct Match
{
    // The image markdown's OWN span within the line — from "!" to the closing
    // ")", EXCLUDING any surrounding whitespace (offset into the line + length).
    // This is what a cursor's "is it touching the image" test compares against;
    // using the whole line once let a trailing space count as touching too (bug
    // found live).
    int spanStart = 0;
    int spanLength = 0;
    QString alt; // may be empty
    QString src; // may be empty (an unfilled "![]()" placeholder)

    // Offset + length of the src within the line, so a caller can replace
    // exactly the src (e.g. filling in an "![]()" placeholder) without disturbing
    // the alt text. For an empty src this is a zero-length span at the "(" — the
    // right insertion point either way.
    int srcStart = 0;
    int srcLength = 0;

    bool isValid = false;
};

// Parses `line` as a whole-line image, if it is one.
Match parse(const QString &line);

}
