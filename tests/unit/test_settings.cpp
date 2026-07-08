#include <catch2/catch_test_macros.hpp>
#include <QColor>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/Settings.h"

namespace {
QString tempConfigPath(const QTemporaryDir &dir)
{
    return dir.path() + QLatin1String("/settings.ini");
}
}

SCENARIO("A fresh Settings instance reports documented defaults", "[settings]")
{
    GIVEN("a Settings instance backed by a brand-new, empty config file")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        THEN("notesFolder defaults to a subdirectory under AppDataLocation")
        {
            const QString expected =
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/notes";
            REQUIRE(settings.notesFolder() == expected);
        }

        THEN("windowRadius defaults to 9")
        {
            REQUIRE(settings.windowRadius() == 9);
        }

        THEN("backgroundColor defaults to #2b2b2b")
        {
            REQUIRE(settings.backgroundColor() == QColor("#2b2b2b"));
        }

        THEN("windowOpacity defaults to 0.5")
        {
            REQUIRE(settings.windowOpacity() == 0.5);
        }

        THEN("blurEnabled defaults to true")
        {
            REQUIRE(settings.blurEnabled());
        }

        THEN("glassEnabled defaults to true")
        {
            REQUIRE(settings.glassEnabled());
        }

        THEN("glassSheenOpacity defaults to 0.12")
        {
            REQUIRE(settings.glassSheenOpacity() == 0.12);
        }

        THEN("glassSheenStrength defaults to 10")
        {
            REQUIRE(settings.glassSheenStrength() == 10);
        }

        THEN("glassSheenSize defaults to 0.18")
        {
            REQUIRE(settings.glassSheenSize() == 0.18);
        }

        THEN("glassTintColor defaults to #000000")
        {
            REQUIRE(settings.glassTintColor() == QColor("#000000"));
        }

        THEN("borderWidth defaults to 0")
        {
            REQUIRE(settings.borderWidth() == 0);
        }

        THEN("borderColor defaults to fully transparent")
        {
            REQUIRE(settings.borderColor().alpha() == 0);
        }

        THEN("scrollbarWidth defaults to 8")
        {
            REQUIRE(settings.scrollbarWidth() == 8);
        }

        THEN("fontSize defaults to 14")
        {
            REQUIRE(settings.fontSize() == 14);
        }

        THEN("fontColor defaults to #ffffff")
        {
            REQUIRE(settings.fontColor() == QColor("#ffffff"));
        }

        THEN("fontFamily defaults to empty, deferring to QML's system font")
        {
            REQUIRE(settings.fontFamily().isEmpty());
        }

        THEN("highlightColor defaults to #000000")
        {
            REQUIRE(settings.highlightColor() == QColor("#000000"));
        }

        THEN("highlightOpacity defaults to 0.35")
        {
            REQUIRE(settings.highlightOpacity() == 0.35);
        }

        THEN("hudAlwaysExpanded defaults to true")
        {
            REQUIRE(settings.hudAlwaysExpanded());
        }

        THEN("openNotes defaults to an empty list")
        {
            REQUIRE(settings.openNotes().isEmpty());
        }

        THEN("a note with no override group returns an all-default (use-global) override")
        {
            const auto override = settings.noteOverride("never-seen-id");
            REQUIRE_FALSE(override.hasWindowRadius);
            REQUIRE_FALSE(override.hasBackgroundColor);
            REQUIRE_FALSE(override.hasWindowOpacity);
            REQUIRE_FALSE(override.hasBlurEnabled);
            REQUIRE_FALSE(override.hasGlassEnabled);
            REQUIRE_FALSE(override.hasGlassSheenOpacity);
            REQUIRE_FALSE(override.hasGlassSheenStrength);
            REQUIRE_FALSE(override.hasGlassSheenSize);
            REQUIRE_FALSE(override.hasGlassTintColor);
            REQUIRE_FALSE(override.hasBorderWidth);
            REQUIRE_FALSE(override.hasBorderColor);
            REQUIRE_FALSE(override.hasScrollbarWidth);
            REQUIRE_FALSE(override.hasFontSize);
            REQUIRE_FALSE(override.hasFontColor);
            REQUIRE_FALSE(override.hasFontFamily);
            REQUIRE_FALSE(override.hasHighlightColor);
            REQUIRE_FALSE(override.hasHighlightOpacity);
        }
    }
}

SCENARIO("Appearance properties persist to disk across instances", "[settings]")
{
    GIVEN("a Settings instance backed by a temp config file")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString path = tempConfigPath(dir);
        Settings settings(path);

        WHEN("each appearance property is changed")
        {
            settings.setNotesFolder("/home/user/MyNotes");
            settings.setWindowRadius(20);
            settings.setBackgroundColor(QColor("#112233"));
            settings.setWindowOpacity(0.5);
            settings.setBlurEnabled(true);
            settings.setGlassEnabled(true);
            settings.setGlassSheenOpacity(0.25);
            settings.setGlassSheenStrength(40);
            settings.setGlassSheenSize(0.3);
            settings.setGlassTintColor(QColor("#99ccff"));
            settings.setBorderWidth(3.5);
            settings.setBorderColor(QColor("#445566"));
            settings.setScrollbarWidth(10);
            settings.setFontSize(18);
            settings.setFontColor(QColor("#abcdef"));
            settings.setFontFamily("Noto Sans");
            settings.setHudAlwaysExpanded(true);
            settings.setHighlightColor(QColor("#8899aa"));
            settings.setHighlightOpacity(0.75);

            THEN("a second Settings instance pointed at the same file reads back the new values")
            {
                Settings reloaded(path);
                REQUIRE(reloaded.notesFolder() == "/home/user/MyNotes");
                REQUIRE(reloaded.windowRadius() == 20);
                REQUIRE(reloaded.backgroundColor() == QColor("#112233"));
                REQUIRE(reloaded.windowOpacity() == 0.5);
                REQUIRE(reloaded.blurEnabled() == true);
                REQUIRE(reloaded.glassEnabled() == true);
                REQUIRE(reloaded.glassSheenOpacity() == 0.25);
                REQUIRE(reloaded.glassSheenStrength() == 40);
                REQUIRE(reloaded.glassSheenSize() == 0.3);
                REQUIRE(reloaded.glassTintColor() == QColor("#99ccff"));
                REQUIRE(reloaded.borderWidth() == 3.5);
                REQUIRE(reloaded.borderColor() == QColor("#445566"));
                REQUIRE(reloaded.scrollbarWidth() == 10);
                REQUIRE(reloaded.fontSize() == 18);
                REQUIRE(reloaded.fontColor() == QColor("#abcdef"));
                REQUIRE(reloaded.fontFamily() == "Noto Sans");
                REQUIRE(reloaded.hudAlwaysExpanded() == true);
                REQUIRE(reloaded.highlightColor() == QColor("#8899aa"));
                REQUIRE(reloaded.highlightOpacity() == 0.75);
            }
        }
    }
}

SCENARIO("Changing a property emits its NOTIFY signal", "[settings]")
{
    GIVEN("a Settings instance and a signal spy on each NOTIFY signal")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        WHEN("notesFolder is changed")
        {
            QSignalSpy spy(&settings, &Settings::notesFolderChanged);
            settings.setNotesFolder("/tmp/notes");
            THEN("notesFolderChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("windowRadius is changed")
        {
            QSignalSpy spy(&settings, &Settings::windowRadiusChanged);
            settings.setWindowRadius(4);
            THEN("windowRadiusChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("backgroundColor is changed")
        {
            QSignalSpy spy(&settings, &Settings::backgroundColorChanged);
            settings.setBackgroundColor(QColor("#000000"));
            THEN("backgroundColorChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("windowOpacity is changed")
        {
            QSignalSpy spy(&settings, &Settings::windowOpacityChanged);
            settings.setWindowOpacity(0.1);
            THEN("windowOpacityChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("blurEnabled is changed")
        {
            QSignalSpy spy(&settings, &Settings::blurEnabledChanged);
            settings.setBlurEnabled(true);
            THEN("blurEnabledChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("fontSize is changed")
        {
            QSignalSpy spy(&settings, &Settings::fontSizeChanged);
            settings.setFontSize(22);
            THEN("fontSizeChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("fontColor is changed")
        {
            QSignalSpy spy(&settings, &Settings::fontColorChanged);
            settings.setFontColor(QColor("#123123"));
            THEN("fontColorChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("fontFamily is changed")
        {
            QSignalSpy spy(&settings, &Settings::fontFamilyChanged);
            settings.setFontFamily("Comic Sans MS");
            THEN("fontFamilyChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("glassEnabled is changed")
        {
            QSignalSpy spy(&settings, &Settings::glassEnabledChanged);
            settings.setGlassEnabled(true);
            THEN("glassEnabledChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("glassSheenOpacity is changed")
        {
            QSignalSpy spy(&settings, &Settings::glassSheenOpacityChanged);
            settings.setGlassSheenOpacity(0.3);
            THEN("glassSheenOpacityChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("glassSheenStrength is changed")
        {
            QSignalSpy spy(&settings, &Settings::glassSheenStrengthChanged);
            settings.setGlassSheenStrength(50);
            THEN("glassSheenStrengthChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("glassSheenSize is changed")
        {
            QSignalSpy spy(&settings, &Settings::glassSheenSizeChanged);
            settings.setGlassSheenSize(0.4);
            THEN("glassSheenSizeChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("glassTintColor is changed")
        {
            QSignalSpy spy(&settings, &Settings::glassTintColorChanged);
            settings.setGlassTintColor(QColor("#123456"));
            THEN("glassTintColorChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("hudAlwaysExpanded is changed")
        {
            QSignalSpy spy(&settings, &Settings::hudAlwaysExpandedChanged);
            settings.setHudAlwaysExpanded(true);
            THEN("hudAlwaysExpandedChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("borderWidth is changed")
        {
            QSignalSpy spy(&settings, &Settings::borderWidthChanged);
            settings.setBorderWidth(5);
            THEN("borderWidthChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("borderColor is changed")
        {
            QSignalSpy spy(&settings, &Settings::borderColorChanged);
            settings.setBorderColor(QColor("#ff00ff"));
            THEN("borderColorChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("scrollbarWidth is changed")
        {
            QSignalSpy spy(&settings, &Settings::scrollbarWidthChanged);
            settings.setScrollbarWidth(12);
            THEN("scrollbarWidthChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("highlightColor is explicitly set")
        {
            QSignalSpy spy(&settings, &Settings::highlightColorChanged);
            settings.setHighlightColor(QColor("#101010"));
            THEN("highlightColorChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("highlightOpacity is changed")
        {
            QSignalSpy spy(&settings, &Settings::highlightOpacityChanged);
            settings.setHighlightOpacity(0.3);
            THEN("highlightOpacityChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("openNotes is replaced via setOpenNotes")
        {
            QSignalSpy spy(&settings, &Settings::openNotesChanged);
            settings.setOpenNotes({});
            THEN("openNotesChanged fires once") { REQUIRE(spy.count() == 1); }
        }
    }
}

SCENARIO("upsertNoteState adds or updates a single note's session state", "[settings]")
{
    GIVEN("a Settings instance with no open notes")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        REQUIRE(settings.openNotes().isEmpty());

        WHEN("a note is upserted for the first time")
        {
            Settings::NoteSessionState note;
            note.noteId = "note-1";
            note.x = 10;
            note.y = 20;
            note.width = 300;
            note.height = 400;
            settings.upsertNoteState(note);

            THEN("it appears once in openNotes with the given geometry")
            {
                const auto notes = settings.openNotes();
                REQUIRE(notes.size() == 1);
                REQUIRE(notes.first().noteId == "note-1");
                REQUIRE(notes.first().x == 10);
                REQUIRE(notes.first().y == 20);
                REQUIRE(notes.first().width == 300);
                REQUIRE(notes.first().height == 400);
            }

            AND_WHEN("the same noteId is upserted again with different geometry")
            {
                Settings::NoteSessionState updated;
                updated.noteId = "note-1";
                updated.x = 99;
                updated.y = 88;
                updated.width = 111;
                updated.height = 222;
                settings.upsertNoteState(updated);

                THEN("the existing entry is updated in place, not duplicated")
                {
                    const auto notes = settings.openNotes();
                    REQUIRE(notes.size() == 1);
                    REQUIRE(notes.first().x == 99);
                    REQUIRE(notes.first().y == 88);
                    REQUIRE(notes.first().width == 111);
                    REQUIRE(notes.first().height == 222);
                }
            }
        }
    }
}

SCENARIO("removeNoteState drops exactly one note, leaving others untouched", "[settings]")
{
    GIVEN("three open notes")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        settings.upsertNoteState({"note-a", 1, 1, 100, 100});
        settings.upsertNoteState({"note-b", 2, 2, 200, 200});
        settings.upsertNoteState({"note-c", 3, 3, 300, 300});
        REQUIRE(settings.openNotes().size() == 3);

        WHEN("the middle note is removed")
        {
            settings.removeNoteState("note-b");

            THEN("only the remaining two notes are present, with geometry intact")
            {
                const auto notes = settings.openNotes();
                REQUIRE(notes.size() == 2);

                bool foundA = false;
                bool foundC = false;
                for (const auto &n : notes) {
                    if (n.noteId == "note-a") {
                        foundA = true;
                        REQUIRE(n.width == 100);
                    }
                    if (n.noteId == "note-c") {
                        foundC = true;
                        REQUIRE(n.width == 300);
                    }
                    REQUIRE(n.noteId != "note-b");
                }
                REQUIRE(foundA);
                REQUIRE(foundC);
            }
        }

        WHEN("removeNoteState is called with an id that isn't open")
        {
            settings.removeNoteState("does-not-exist");

            THEN("nothing changes")
            {
                REQUIRE(settings.openNotes().size() == 3);
            }
        }
    }
}

SCENARIO("setOpenNotes fully replaces the session list", "[settings]")
{
    GIVEN("a Settings instance with three open notes")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        settings.upsertNoteState({"note-a", 1, 1, 100, 100});
        settings.upsertNoteState({"note-b", 2, 2, 200, 200});
        settings.upsertNoteState({"note-c", 3, 3, 300, 300});
        REQUIRE(settings.openNotes().size() == 3);

        WHEN("setOpenNotes is called with a single note")
        {
            settings.setOpenNotes({Settings::NoteSessionState{"note-only", 5, 6, 700, 800}});

            THEN("openNotes reflects exactly that one note")
            {
                const auto notes = settings.openNotes();
                REQUIRE(notes.size() == 1);
                REQUIRE(notes.first().noteId == "note-only");
            }
        }

        WHEN("setOpenNotes is called with an empty list")
        {
            settings.setOpenNotes({});

            THEN("openNotes is empty")
            {
                REQUIRE(settings.openNotes().isEmpty());
            }
        }
    }
}

SCENARIO("openNotes round-trips with full fidelity through a second Settings instance", "[settings]")
{
    GIVEN("a Settings instance with several distinctly-positioned notes")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString path = tempConfigPath(dir);
        Settings settings(path);

        const QList<Settings::NoteSessionState> notes = {
            {"alpha", 10, 20, 260, 260, "DP-4"},
            {"beta", -5, 300, 400, 150, "HDMI-A-1"},
            {"gamma", 1000, 2000, 50, 75, QString()},
        };
        settings.setOpenNotes(notes);

        WHEN("a second Settings instance is constructed against the same file")
        {
            Settings reloaded(path);

            THEN("every note's geometry, screen name, and id is intact, in order")
            {
                const auto readBack = reloaded.openNotes();
                REQUIRE(readBack.size() == 3);
                for (int i = 0; i < notes.size(); ++i) {
                    REQUIRE(readBack.at(i).noteId == notes.at(i).noteId);
                    REQUIRE(readBack.at(i).x == notes.at(i).x);
                    REQUIRE(readBack.at(i).y == notes.at(i).y);
                    REQUIRE(readBack.at(i).width == notes.at(i).width);
                    REQUIRE(readBack.at(i).height == notes.at(i).height);
                    REQUIRE(readBack.at(i).screenName == notes.at(i).screenName);
                }
            }
        }
    }
}

SCENARIO("alwaysOnTop persists as part of a note's session state", "[settings]")
{
    GIVEN("a Settings instance backed by a temp config file")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString path = tempConfigPath(dir);
        Settings settings(path);

        THEN("a freshly-upserted note defaults to not pinned")
        {
            settings.upsertNoteState({"note-a", 1, 1, 100, 100});
            REQUIRE_FALSE(settings.openNotes().first().alwaysOnTop);
        }

        WHEN("a note is upserted with alwaysOnTop set")
        {
            Settings::NoteSessionState note;
            note.noteId = "note-pinned";
            note.alwaysOnTop = true;
            settings.upsertNoteState(note);

            THEN("a second Settings instance reads the pin back")
            {
                Settings reloaded(path);
                REQUIRE(reloaded.openNotes().first().alwaysOnTop);
            }
        }
    }
}

SCENARIO("highlightColor is independent of backgroundColor/fontColor", "[settings]")
{
    GIVEN("a Settings instance with the default highlightColor")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        REQUIRE(settings.highlightColor() == QColor("#000000"));

        WHEN("backgroundColor and fontColor change")
        {
            settings.setBackgroundColor(QColor("#00ff00"));
            settings.setFontColor(QColor("#123456"));

            THEN("highlightColor is unaffected, unlike the old auto-derived behavior")
            {
                REQUIRE(settings.highlightColor() == QColor("#000000"));
            }
        }

        WHEN("highlightColor is explicitly set")
        {
            settings.setHighlightColor(QColor("#ff00ff"));

            THEN("it persists as set, regardless of backgroundColor/fontColor")
            {
                REQUIRE(settings.highlightColor() == QColor("#ff00ff"));
            }
        }
    }
}

SCENARIO("Per-note appearance overrides persist and can be cleared", "[settings]")
{
    GIVEN("a Settings instance backed by a temp config file")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString path = tempConfigPath(dir);
        Settings settings(path);

        WHEN("an override is set for one note")
        {
            Settings::NoteAppearanceOverride override;
            override.hasWindowRadius = true;
            override.windowRadius = 30;
            override.hasBackgroundColor = true;
            override.backgroundColor = QColor("#ff0000");
            override.hasGlassEnabled = true;
            override.glassEnabled = true;
            override.hasGlassSheenOpacity = true;
            override.glassSheenOpacity = 0.3;
            override.hasGlassTintColor = true;
            override.glassTintColor = QColor("#00ff00");
            override.hasBorderWidth = true;
            override.borderWidth = 2.5;
            override.hasScrollbarWidth = true;
            override.scrollbarWidth = 6;
            settings.setNoteOverride("note-x", override);

            THEN("a second Settings instance reads back exactly the set fields")
            {
                Settings reloaded(path);
                const auto readBack = reloaded.noteOverride("note-x");
                REQUIRE(readBack.hasWindowRadius);
                REQUIRE(readBack.windowRadius == 30);
                REQUIRE(readBack.hasBackgroundColor);
                REQUIRE(readBack.backgroundColor == QColor("#ff0000"));
                REQUIRE(readBack.hasGlassEnabled);
                REQUIRE(readBack.glassEnabled);
                REQUIRE(readBack.hasGlassSheenOpacity);
                REQUIRE(readBack.glassSheenOpacity == 0.3);
                REQUIRE(readBack.hasGlassTintColor);
                REQUIRE(readBack.glassTintColor == QColor("#00ff00"));
                REQUIRE(readBack.hasBorderWidth);
                REQUIRE(readBack.borderWidth == 2.5);
                REQUIRE(readBack.hasScrollbarWidth);
                REQUIRE(readBack.scrollbarWidth == 6);

                AND_THEN("fields never set remain unset")
                {
                    REQUIRE_FALSE(readBack.hasWindowOpacity);
                    REQUIRE_FALSE(readBack.hasBlurEnabled);
                    REQUIRE_FALSE(readBack.hasGlassSheenStrength);
                    REQUIRE_FALSE(readBack.hasGlassSheenSize);
                    REQUIRE_FALSE(readBack.hasBorderColor);
                    REQUIRE_FALSE(readBack.hasFontSize);
                    REQUIRE_FALSE(readBack.hasFontColor);
                    REQUIRE_FALSE(readBack.hasFontFamily);
                }
            }

            AND_WHEN("the override is cleared")
            {
                settings.clearNoteOverride("note-x");

                THEN("every field reverts to unset at once")
                {
                    const auto cleared = settings.noteOverride("note-x");
                    REQUIRE_FALSE(cleared.hasWindowRadius);
                    REQUIRE_FALSE(cleared.hasBackgroundColor);
                    REQUIRE_FALSE(cleared.hasGlassEnabled);
                    REQUIRE_FALSE(cleared.hasGlassSheenOpacity);
                    REQUIRE_FALSE(cleared.hasGlassTintColor);
                    REQUIRE_FALSE(cleared.hasBorderWidth);
                }
            }
        }

        WHEN("an override is set")
        {
            QSignalSpy spy(&settings, &Settings::noteOverrideChanged);
            settings.setNoteOverride("note-y", Settings::NoteAppearanceOverride());
            THEN("noteOverrideChanged fires with the note's id")
            {
                REQUIRE(spy.count() == 1);
                REQUIRE(spy.first().first().toString() == "note-y");
            }
        }
    }
}
