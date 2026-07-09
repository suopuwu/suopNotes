#include <catch2/catch_test_macros.hpp>

#include "core/NoteFilename.h"

SCENARIO("Deriving a filename from a note's first line", "[note_filename]")
{
    GIVEN("plain content with no heading")
    {
        WHEN("the note has a single short line")
        {
            THEN("that line is used verbatim")
            {
                CHECK(NoteFilename::deriveBaseName("Buy milk") == "Buy milk");
            }
        }

        WHEN("the first line is blank but a later line has content")
        {
            THEN("the first non-empty line is used")
            {
                CHECK(NoteFilename::deriveBaseName("\n\n  \nGroceries\nmore text") == "Groceries");
            }
        }

        WHEN("the note is empty")
        {
            THEN("it falls back to Untitled")
            {
                CHECK(NoteFilename::deriveBaseName("") == "Untitled");
            }
        }

        WHEN("the note is only whitespace")
        {
            THEN("it falls back to Untitled")
            {
                CHECK(NoteFilename::deriveBaseName("   \n\n\t \n") == "Untitled");
            }
        }
    }
}

SCENARIO("Markdown headings are treated as plain text, not given priority", "[note_filename]")
{
    GIVEN("content whose first line is body text and a heading appears later")
    {
        const QString content = "Some intro text\n\n## Project Plan\n\nBody.";

        THEN("the first line is used, not the later heading")
        {
            CHECK(NoteFilename::deriveBaseName(content) == "Some intro text");
        }
    }

    GIVEN("content where the heading is on the first line")
    {
        const QString content = "# Weekly Standup\n\nNotes go here.";

        THEN("the first line is used verbatim, hashes included")
        {
            CHECK(NoteFilename::deriveBaseName(content) == "# Weekly Standup");
        }
    }
}

SCENARIO("Filesystem-invalid characters are trimmed", "[note_filename]")
{
    GIVEN("a first line containing characters that are invalid in filenames")
    {
        THEN("they are removed, not just escaped")
        {
            CHECK(NoteFilename::deriveBaseName("Report: Q1/Q2 <final>?") == "Report Q1Q2 final");
        }
    }

    GIVEN("a first line that is a markdown list item")
    {
        THEN("the leading marker is stripped so the filename doesn't start with punctuation")
        {
            CHECK(NoteFilename::deriveBaseName("- Buy milk") == "Buy milk");
            CHECK(NoteFilename::deriveBaseName("1. First step") == "First step");
        }
    }

    GIVEN("a first line consisting only of invalid characters")
    {
        THEN("it falls back to Untitled")
        {
            CHECK(NoteFilename::deriveBaseName("///???") == "Untitled");
        }
    }
}

SCENARIO("Overly long titles are truncated to a handful of words", "[note_filename]")
{
    GIVEN("a first line much longer than a reasonable filename")
    {
        const QString longLine =
            "This is an extremely long first line that goes on and on and on well past what "
            "anyone would want as an actual filename on their filesystem";

        WHEN("a base name is derived")
        {
            const QString base = NoteFilename::deriveBaseName(longLine);

            THEN("it is meaningfully shorter than the original, cut at a word boundary")
            {
                CHECK(base.length() <= 60);
                CHECK(longLine.startsWith(base));
                CHECK_FALSE(base.endsWith(' '));
            }
        }
    }
}
