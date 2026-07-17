#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "core/FileRevealer.h"

// These specs cover the half of FileRevealer that can be checked without a
// desktop: what it considers revealable at all, and what folder it would fall
// back to opening.
//
// reveal() itself is deliberately NOT exercised for a real, existing file, and
// that is a hard rule rather than an omission: on success it launches the
// tester's actual file manager, and on failure it opens their actual folder —
// there is no offscreen platform or temp dir that contains either, because
// both effects land in a different process entirely. The parts of reveal()
// that ARE reachable safely (its early refusal of a path with nothing to show)
// are checked below; the D-Bus dispatch and its fallback belong to a real
// session, and FileRevealer.cpp records what they do and why.

SCENARIO("Deciding whether a path can be revealed at all", "[file_revealer]")
{
    GIVEN("a real file on disk")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        const QString notePath = tempDir.filePath("Groceries.md");
        QFile note(notePath);
        REQUIRE(note.open(QIODevice::WriteOnly));
        note.write("milk");
        note.close();

        THEN("it can be revealed")
        {
            CHECK(FileRevealer::canReveal(notePath));
        }

        THEN("the fallback would open the folder containing it")
        {
            CHECK(FileRevealer::containingFolder(notePath) == QFileInfo(tempDir.path()).absoluteFilePath());
        }
    }

    GIVEN("a path to a file that isn't there")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        const QString missing = tempDir.filePath("never-written.md");

        THEN("it cannot be revealed, and reveal() refuses it outright")
        {
            CHECK_FALSE(FileRevealer::canReveal(missing));

            FileRevealer revealer;
            CHECK_FALSE(revealer.reveal(missing));
        }

        // Without this, a note whose file went missing would "reveal" by
        // opening whatever folder the path happened to name — or, for a bare
        // relative path, the process's working directory. Refusing is the only
        // honest answer to "show me a file that isn't there."
        THEN("no folder is offered as a consolation prize")
        {
            CHECK(FileRevealer::containingFolder(missing).isEmpty());
        }
    }

    GIVEN("an empty path — what NoteStore::noteFilePath() returns for a note with no file")
    {
        THEN("it is refused everywhere, without touching the filesystem")
        {
            CHECK_FALSE(FileRevealer::canReveal(QString()));
            CHECK(FileRevealer::containingFolder(QString()).isEmpty());

            FileRevealer revealer;
            CHECK_FALSE(revealer.reveal(QString()));
        }
    }
}

SCENARIO("The fallback folder is absolute even when the path given isn't", "[file_revealer]")
{
    // The fallback hands its answer to QDesktopServices as a file:// URL, which
    // a relative path cannot form correctly — it would resolve against whatever
    // the process's working directory happens to be at the time.
    GIVEN("a file addressed by a path relative to the current directory")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());

        QFile note(tempDir.filePath("Relative.md"));
        REQUIRE(note.open(QIODevice::WriteOnly));
        note.write("body");
        note.close();

        const QString previousCwd = QDir::currentPath();
        REQUIRE(QDir::setCurrent(tempDir.path()));

        WHEN("the folder for it is derived")
        {
            const QString folder = FileRevealer::containingFolder("Relative.md");

            // Compared against currentPath() rather than tempDir.path(): the
            // two can differ by a symlink in the temp path, and what's being
            // asserted here is "resolved to somewhere absolute", not which
            // spelling of the temp dir won.
            THEN("it comes back as an absolute path, not a relative one")
            {
                CHECK(QDir::isAbsolutePath(folder));
                CHECK(folder == QDir::currentPath());
            }
        }

        QDir::setCurrent(previousCwd);
    }
}
