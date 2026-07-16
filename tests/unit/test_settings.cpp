#include <catch2/catch_test_macros.hpp>
#include <QColor>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

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

        THEN("spellcheckEnabled defaults to true")
        {
            REQUIRE(settings.spellcheckEnabled());
        }

        THEN("spellcheckLanguage defaults to empty, meaning automatic/system-default")
        {
            REQUIRE(settings.spellcheckLanguage().isEmpty());
        }

        THEN("userDictionaryWords defaults to an empty list")
        {
            REQUIRE(settings.userDictionaryWords().isEmpty());
        }

        THEN("spellcheckMaxSuggestions defaults to 3")
        {
            REQUIRE(settings.spellcheckMaxSuggestions() == 3);
        }

        THEN("spellcheckUnderlineColor defaults to watermelon red (#fc6c85)")
        {
            REQUIRE(settings.spellcheckUnderlineColor() == QColor("#fc6c85"));
        }

        THEN("spellcheckUnderlineWidth defaults to 3")
        {
            REQUIRE(settings.spellcheckUnderlineWidth() == 3);
        }

        THEN("spellcheckDebounceMs defaults to 500")
        {
            REQUIRE(settings.spellcheckDebounceMs() == 500);
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

        THEN("interactableHighlightColor defaults to #4da6ff")
        {
            REQUIRE(settings.interactableHighlightColor() == QColor("#4da6ff"));
        }

        THEN("interactableHighlightOpacity defaults to 0.18")
        {
            REQUIRE(settings.interactableHighlightOpacity() == 0.18);
        }

        THEN("interactableHighlightGlassEnabled defaults to false")
        {
            REQUIRE_FALSE(settings.interactableHighlightGlassEnabled());
        }

        THEN("chromeGlassFollowsNote defaults to true")
        {
            REQUIRE(settings.chromeGlassFollowsNote());
        }

        THEN("chromeGlassEnabled defaults to false")
        {
            REQUIRE_FALSE(settings.chromeGlassEnabled());
        }

        THEN("hudAlwaysExpanded defaults to true")
        {
            REQUIRE(settings.hudAlwaysExpanded());
        }

        THEN("openNotes defaults to an empty list")
        {
            REQUIRE(settings.openNotes().isEmpty());
        }

        THEN("shortcutBold defaults to Ctrl+B")
        {
            REQUIRE(settings.shortcutBold() == "Ctrl+B");
        }

        THEN("shortcutItalic defaults to Ctrl+I")
        {
            REQUIRE(settings.shortcutItalic() == "Ctrl+I");
        }

        THEN("shortcutStrikethrough defaults to Ctrl+Shift+X")
        {
            REQUIRE(settings.shortcutStrikethrough() == "Ctrl+Shift+X");
        }

        THEN("shortcutHeading1 defaults to Ctrl+Alt+1")
        {
            REQUIRE(settings.shortcutHeading1() == "Ctrl+Alt+1");
        }

        THEN("shortcutHeading2 defaults to Ctrl+Alt+2")
        {
            REQUIRE(settings.shortcutHeading2() == "Ctrl+Alt+2");
        }

        THEN("shortcutParagraph defaults to Ctrl+Alt+0")
        {
            REQUIRE(settings.shortcutParagraph() == "Ctrl+Alt+0");
        }

        THEN("shortcutBulletList defaults to Ctrl+Shift+8")
        {
            REQUIRE(settings.shortcutBulletList() == "Ctrl+Shift+8");
        }

        THEN("shortcutNumberedList defaults to Ctrl+Shift+7")
        {
            REQUIRE(settings.shortcutNumberedList() == "Ctrl+Shift+7");
        }

        THEN("shortcutInsertTable defaults to Ctrl+Alt+T")
        {
            REQUIRE(settings.shortcutInsertTable() == "Ctrl+Alt+T");
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
            settings.setSpellcheckEnabled(true);
            settings.setSpellcheckLanguage("bundled:en_US");
            settings.setSpellcheckMaxSuggestions(5);
            settings.setSpellcheckUnderlineColor(QColor("#33cc99"));
            settings.setSpellcheckUnderlineWidth(5);
            settings.setSpellcheckDebounceMs(750);
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
            settings.setInteractableHighlightColor(QColor("#ff8800"));
            settings.setInteractableHighlightOpacity(0.4);
            settings.setInteractableHighlightGlassEnabled(true);
            settings.setChromeGlassFollowsNote(false);
            settings.setChromeGlassEnabled(true);
            settings.setShortcutBold("Ctrl+Shift+B");
            settings.setShortcutItalic("Ctrl+Shift+I");
            settings.setShortcutStrikethrough("Ctrl+Alt+X");
            settings.setShortcutHeading1("Ctrl+1");
            settings.setShortcutHeading2("Ctrl+2");
            settings.setShortcutParagraph("Ctrl+0");
            settings.setShortcutBulletList("Ctrl+8");
            settings.setShortcutNumberedList("Ctrl+7");
            settings.setShortcutInsertTable("Ctrl+T");

            THEN("a second Settings instance pointed at the same file reads back the new values")
            {
                Settings reloaded(path);
                REQUIRE(reloaded.notesFolder() == "/home/user/MyNotes");
                REQUIRE(reloaded.windowRadius() == 20);
                REQUIRE(reloaded.backgroundColor() == QColor("#112233"));
                REQUIRE(reloaded.windowOpacity() == 0.5);
                REQUIRE(reloaded.blurEnabled() == true);
                REQUIRE(reloaded.spellcheckEnabled() == true);
                REQUIRE(reloaded.spellcheckLanguage() == "bundled:en_US");
                REQUIRE(reloaded.spellcheckMaxSuggestions() == 5);
                REQUIRE(reloaded.spellcheckUnderlineColor() == QColor("#33cc99"));
                REQUIRE(reloaded.spellcheckUnderlineWidth() == 5);
                REQUIRE(reloaded.spellcheckDebounceMs() == 750);
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
                REQUIRE(reloaded.interactableHighlightColor() == QColor("#ff8800"));
                REQUIRE(reloaded.interactableHighlightOpacity() == 0.4);
                REQUIRE(reloaded.interactableHighlightGlassEnabled() == true);
                REQUIRE(reloaded.chromeGlassFollowsNote() == false);
                REQUIRE(reloaded.chromeGlassEnabled() == true);
                REQUIRE(reloaded.shortcutBold() == "Ctrl+Shift+B");
                REQUIRE(reloaded.shortcutItalic() == "Ctrl+Shift+I");
                REQUIRE(reloaded.shortcutStrikethrough() == "Ctrl+Alt+X");
                REQUIRE(reloaded.shortcutHeading1() == "Ctrl+1");
                REQUIRE(reloaded.shortcutHeading2() == "Ctrl+2");
                REQUIRE(reloaded.shortcutParagraph() == "Ctrl+0");
                REQUIRE(reloaded.shortcutBulletList() == "Ctrl+8");
                REQUIRE(reloaded.shortcutNumberedList() == "Ctrl+7");
                REQUIRE(reloaded.shortcutInsertTable() == "Ctrl+T");
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

        WHEN("spellcheckEnabled is changed")
        {
            QSignalSpy spy(&settings, &Settings::spellcheckEnabledChanged);
            settings.setSpellcheckEnabled(false);
            THEN("spellcheckEnabledChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("spellcheckLanguage is changed to a different value")
        {
            QSignalSpy spy(&settings, &Settings::spellcheckLanguageChanged);
            settings.setSpellcheckLanguage("bundled:en_US");
            THEN("spellcheckLanguageChanged fires exactly once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("spellcheckLanguage is set to its current value again")
        {
            settings.setSpellcheckLanguage("bundled:en_US");
            QSignalSpy spy(&settings, &Settings::spellcheckLanguageChanged);
            settings.setSpellcheckLanguage("bundled:en_US");
            THEN("spellcheckLanguageChanged does not fire") { REQUIRE(spy.count() == 0); }
        }

        WHEN("spellcheckMaxSuggestions is changed to a different value")
        {
            QSignalSpy spy(&settings, &Settings::spellcheckMaxSuggestionsChanged);
            settings.setSpellcheckMaxSuggestions(5);
            THEN("spellcheckMaxSuggestionsChanged fires exactly once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("spellcheckMaxSuggestions is set to its current value again")
        {
            settings.setSpellcheckMaxSuggestions(5);
            QSignalSpy spy(&settings, &Settings::spellcheckMaxSuggestionsChanged);
            settings.setSpellcheckMaxSuggestions(5);
            THEN("spellcheckMaxSuggestionsChanged does not fire") { REQUIRE(spy.count() == 0); }
        }

        WHEN("spellcheckMaxSuggestions is set to a negative value")
        {
            settings.setSpellcheckMaxSuggestions(-5);
            THEN("it is clamped to 0, not stored as negative") { REQUIRE(settings.spellcheckMaxSuggestions() == 0); }
        }

        WHEN("spellcheckUnderlineColor is changed")
        {
            QSignalSpy spy(&settings, &Settings::spellcheckUnderlineColorChanged);
            settings.setSpellcheckUnderlineColor(QColor("#00ff00"));
            THEN("spellcheckUnderlineColorChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("spellcheckUnderlineWidth is changed")
        {
            QSignalSpy spy(&settings, &Settings::spellcheckUnderlineWidthChanged);
            settings.setSpellcheckUnderlineWidth(5);
            THEN("spellcheckUnderlineWidthChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("spellcheckDebounceMs is changed")
        {
            QSignalSpy spy(&settings, &Settings::spellcheckDebounceMsChanged);
            settings.setSpellcheckDebounceMs(750);
            THEN("spellcheckDebounceMsChanged fires once") { REQUIRE(spy.count() == 1); }
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

        WHEN("interactableHighlightColor is changed")
        {
            QSignalSpy spy(&settings, &Settings::interactableHighlightColorChanged);
            settings.setInteractableHighlightColor(QColor("#00ffcc"));
            THEN("interactableHighlightColorChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("interactableHighlightOpacity is changed")
        {
            QSignalSpy spy(&settings, &Settings::interactableHighlightOpacityChanged);
            settings.setInteractableHighlightOpacity(0.5);
            THEN("interactableHighlightOpacityChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("interactableHighlightGlassEnabled is changed")
        {
            QSignalSpy spy(&settings, &Settings::interactableHighlightGlassEnabledChanged);
            settings.setInteractableHighlightGlassEnabled(true);
            THEN("interactableHighlightGlassEnabledChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("chromeGlassFollowsNote is changed")
        {
            QSignalSpy spy(&settings, &Settings::chromeGlassFollowsNoteChanged);
            settings.setChromeGlassFollowsNote(false);
            THEN("chromeGlassFollowsNoteChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("chromeGlassEnabled is changed")
        {
            QSignalSpy spy(&settings, &Settings::chromeGlassEnabledChanged);
            settings.setChromeGlassEnabled(true);
            THEN("chromeGlassEnabledChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("openNotes is replaced via setOpenNotes")
        {
            QSignalSpy spy(&settings, &Settings::openNotesChanged);
            settings.setOpenNotes({});
            THEN("openNotesChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("shortcutBold is changed")
        {
            QSignalSpy spy(&settings, &Settings::shortcutBoldChanged);
            settings.setShortcutBold("Ctrl+Shift+B");
            THEN("shortcutBoldChanged fires once") { REQUIRE(spy.count() == 1); }
        }

        WHEN("shortcutInsertTable is changed")
        {
            QSignalSpy spy(&settings, &Settings::shortcutInsertTableChanged);
            settings.setShortcutInsertTable("Ctrl+T");
            THEN("shortcutInsertTableChanged fires once") { REQUIRE(spy.count() == 1); }
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

SCENARIO("addUserDictionaryWord adds a word, rejecting duplicates and blanks", "[settings]")
{
    GIVEN("a Settings instance with an empty user dictionary")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        REQUIRE(settings.userDictionaryWords().isEmpty());

        WHEN("a new word is added")
        {
            QSignalSpy spy(&settings, &Settings::userDictionaryWordsChanged);
            settings.addUserDictionaryWord("Hello");

            THEN("it appears in userDictionaryWords, stored with its original casing")
            {
                REQUIRE(settings.userDictionaryWords() == QStringList{"Hello"});
            }

            THEN("userDictionaryWordsChanged fires exactly once")
            {
                REQUIRE(spy.count() == 1);
            }

            AND_WHEN("the exact same word is added again")
            {
                settings.addUserDictionaryWord("Hello");

                THEN("no duplicate entry is added")
                {
                    REQUIRE(settings.userDictionaryWords() == QStringList{"Hello"});
                }

                THEN("userDictionaryWordsChanged does not fire again")
                {
                    REQUIRE(spy.count() == 1);
                }
            }

            AND_WHEN("the same word is added again in a different case")
            {
                settings.addUserDictionaryWord("hello");

                THEN("it is treated as the same entry (case-insensitive), no duplicate added")
                {
                    REQUIRE(settings.userDictionaryWords() == QStringList{"Hello"});
                }

                THEN("userDictionaryWordsChanged does not fire again")
                {
                    REQUIRE(spy.count() == 1);
                }
            }
        }

        WHEN("an empty string is added")
        {
            QSignalSpy spy(&settings, &Settings::userDictionaryWordsChanged);
            settings.addUserDictionaryWord("");

            THEN("nothing is added and the signal does not fire")
            {
                REQUIRE(settings.userDictionaryWords().isEmpty());
                REQUIRE(spy.count() == 0);
            }
        }

        WHEN("a whitespace-only string is added")
        {
            QSignalSpy spy(&settings, &Settings::userDictionaryWordsChanged);
            settings.addUserDictionaryWord("   ");

            THEN("nothing is added and the signal does not fire")
            {
                REQUIRE(settings.userDictionaryWords().isEmpty());
                REQUIRE(spy.count() == 0);
            }
        }
    }
}

SCENARIO("removeUserDictionaryWord removes a word case-insensitively", "[settings]")
{
    GIVEN("a Settings instance with \"Hello\" already in the user dictionary")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        settings.addUserDictionaryWord("Hello");
        REQUIRE(settings.userDictionaryWords() == QStringList{"Hello"});

        WHEN("the word is removed using a different case than it was stored with")
        {
            QSignalSpy spy(&settings, &Settings::userDictionaryWordsChanged);
            settings.removeUserDictionaryWord("hello");

            THEN("it is removed from userDictionaryWords")
            {
                REQUIRE(settings.userDictionaryWords().isEmpty());
            }

            THEN("userDictionaryWordsChanged fires exactly once")
            {
                REQUIRE(spy.count() == 1);
            }
        }

        WHEN("a word that isn't present is removed")
        {
            QSignalSpy spy(&settings, &Settings::userDictionaryWordsChanged);
            settings.removeUserDictionaryWord("goodbye");

            THEN("the dictionary is unchanged")
            {
                REQUIRE(settings.userDictionaryWords() == QStringList{"Hello"});
            }

            THEN("userDictionaryWordsChanged does not fire")
            {
                REQUIRE(spy.count() == 0);
            }
        }
    }
}

SCENARIO("userDictionaryWords persists to disk across instances", "[settings]")
{
    GIVEN("a Settings instance with two words added to the user dictionary")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString path = tempConfigPath(dir);
        Settings settings(path);
        settings.addUserDictionaryWord("Suop");
        settings.addUserDictionaryWord("Qml");

        WHEN("a second Settings instance is constructed against the same file")
        {
            Settings reloaded(path);

            THEN("both words are present, regardless of order")
            {
                const auto words = reloaded.userDictionaryWords();
                REQUIRE(words.size() == 2);
                REQUIRE(words.contains("Suop"));
                REQUIRE(words.contains("Qml"));
            }
        }
    }
}

SCENARIO("availableSpellcheckLanguages always includes the bundled fallback entry", "[settings]")
{
    GIVEN("a fresh Settings instance backed by a brand-new, empty config file")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        WHEN("availableSpellcheckLanguages is queried")
        {
            const QVariantList languages = settings.availableSpellcheckLanguages();

            THEN("the list is never empty")
            {
                // Regardless of whether Sonnet is compiled in or finds any
                // dictionaries on this machine, the bundled fallback entry
                // below guarantees the picker UI never sees an empty list.
                REQUIRE_FALSE(languages.isEmpty());
            }

            THEN("exactly one entry has code \"bundled:en_US\", the app's own bundled fallback dictionary")
            {
                // Deliberately not asserting anything about Sonnet's own
                // real system entries (count/content) here — that's
                // environment-dependent, not something to pin down in a
                // test (same reasoning as the Sonnet-gated smoke test in
                // test_spell_checker.cpp).
                int bundledCount = 0;
                for (const QVariant &entry : languages) {
                    const QVariantMap map = entry.toMap();
                    if (map.value("code").toString() == "bundled:en_US")
                        ++bundledCount;
                }
                REQUIRE(bundledCount == 1);
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

// Code mode (feature request). Settings holds the global DEFAULTS for all four
// code mode questions — whether a note is code (codeModeEnabled) and what that
// does to it (the three codeModeDisables* toggles). Every one of them also
// round-trips per-note through NoteAppearanceOverride, so a note can both opt
// into code mode on its own and disagree about what code mode entails.
SCENARIO("Code mode ships off, and its suppressions ship on", "[settings]")
{
    GIVEN("a Settings instance backed by a brand-new, empty config file")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        THEN("code mode is off, so no existing note changes appearance on upgrade")
        {
            CHECK_FALSE(settings.codeModeEnabled());
        }

        THEN("code mode suppresses markdown, auto-formatting and spellcheck by default")
        {
            CHECK(settings.codeModeDisablesMarkdownHighlighting());
            CHECK(settings.codeModeDisablesAutoFormatting());
            CHECK(settings.codeModeDisablesSpellcheck());
        }
    }
}

SCENARIO("Code mode settings persist and announce themselves", "[settings]")
{
    GIVEN("a Settings instance with spies on every code mode signal")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        QSignalSpy enabledSpy(&settings, &Settings::codeModeEnabledChanged);
        QSignalSpy markdownSpy(&settings, &Settings::codeModeDisablesMarkdownHighlightingChanged);
        QSignalSpy autoFormatSpy(&settings, &Settings::codeModeDisablesAutoFormattingChanged);
        QSignalSpy spellcheckSpy(&settings, &Settings::codeModeDisablesSpellcheckChanged);

        WHEN("each is flipped away from its default")
        {
            settings.setCodeModeEnabled(true);
            settings.setCodeModeDisablesMarkdownHighlighting(false);
            settings.setCodeModeDisablesAutoFormatting(false);
            settings.setCodeModeDisablesSpellcheck(false);

            THEN("each reports its new value")
            {
                CHECK(settings.codeModeEnabled());
                CHECK_FALSE(settings.codeModeDisablesMarkdownHighlighting());
                CHECK_FALSE(settings.codeModeDisablesAutoFormatting());
                CHECK_FALSE(settings.codeModeDisablesSpellcheck());
            }

            THEN("each emitted its change signal")
            {
                CHECK(enabledSpy.count() == 1);
                CHECK(markdownSpy.count() == 1);
                CHECK(autoFormatSpy.count() == 1);
                CHECK(spellcheckSpy.count() == 1);
            }

            THEN("a second Settings pointed at the same file sees them, so they survive a restart")
            {
                Settings reloaded(tempConfigPath(dir));
                CHECK(reloaded.codeModeEnabled());
                CHECK_FALSE(reloaded.codeModeDisablesMarkdownHighlighting());
                CHECK_FALSE(reloaded.codeModeDisablesAutoFormatting());
                CHECK_FALSE(reloaded.codeModeDisablesSpellcheck());
            }
        }
    }
}

SCENARIO("A note's code mode override round-trips through the config file", "[settings]")
{
    GIVEN("a note with no override group at all")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        THEN("it reports no code mode override, meaning 'use the global default'")
        {
            const Settings::NoteAppearanceOverride override = settings.noteOverride("fresh-note");
            CHECK_FALSE(override.hasCodeModeEnabled);
            CHECK_FALSE(override.hasCodeModeDisablesMarkdownHighlighting);
            CHECK_FALSE(override.hasCodeModeDisablesAutoFormatting);
            CHECK_FALSE(override.hasCodeModeDisablesSpellcheck);
        }
    }

    GIVEN("a note whose override turns code mode on")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        Settings::NoteAppearanceOverride override;
        override.hasCodeModeEnabled = true;
        override.codeModeEnabled = true;
        settings.setNoteOverride("code-note", override);

        THEN("reading it back reports the override")
        {
            const Settings::NoteAppearanceOverride read = settings.noteOverride("code-note");
            CHECK(read.hasCodeModeEnabled);
            CHECK(read.codeModeEnabled);
        }

        THEN("a second Settings on the same file agrees, so it survives a restart")
        {
            Settings reloaded(tempConfigPath(dir));
            const Settings::NoteAppearanceOverride read = reloaded.noteOverride("code-note");
            CHECK(read.hasCodeModeEnabled);
            CHECK(read.codeModeEnabled);
        }

        THEN("a DIFFERENT note is unaffected")
        {
            CHECK_FALSE(settings.noteOverride("other-note").hasCodeModeEnabled);
        }

        AND_WHEN("the note's whole override group is cleared")
        {
            settings.clearNoteOverride("code-note");

            THEN("its code mode override goes with it")
            {
                CHECK_FALSE(settings.noteOverride("code-note").hasCodeModeEnabled);
            }
        }
    }

    GIVEN("a note that is code, but wants to keep spellcheck and markdown styling")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        Settings::NoteAppearanceOverride override;
        override.hasCodeModeEnabled = true;
        override.codeModeEnabled = true;
        override.hasCodeModeDisablesSpellcheck = true;
        override.codeModeDisablesSpellcheck = false;
        override.hasCodeModeDisablesMarkdownHighlighting = true;
        override.codeModeDisablesMarkdownHighlighting = false;
        settings.setNoteOverride("picky-code-note", override);

        THEN("a second Settings on the same file agrees, so it survives a restart")
        {
            Settings reloaded(tempConfigPath(dir));
            const Settings::NoteAppearanceOverride read = reloaded.noteOverride("picky-code-note");
            CHECK(read.hasCodeModeEnabled);
            CHECK(read.codeModeEnabled);
            CHECK(read.hasCodeModeDisablesSpellcheck);
            CHECK_FALSE(read.codeModeDisablesSpellcheck);
            CHECK(read.hasCodeModeDisablesMarkdownHighlighting);
            CHECK_FALSE(read.codeModeDisablesMarkdownHighlighting);
        }

        // The has*/value pairs are stored independently, so a toggle the user
        // never touched must not be dragged along by the two that they did.
        THEN("the toggle it never touched still reports no override at all")
        {
            const Settings::NoteAppearanceOverride read = settings.noteOverride("picky-code-note");
            CHECK_FALSE(read.hasCodeModeDisablesAutoFormatting);
            CHECK(read.codeModeDisablesAutoFormatting);
        }
    }
}
