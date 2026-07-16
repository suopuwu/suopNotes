#include <catch2/catch_test_macros.hpp>
#include <QColor>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/NoteAppearance.h"
#include "core/Settings.h"

namespace {
QString tempConfigPath(const QTemporaryDir &dir)
{
    return dir.path() + QLatin1String("/settings.ini");
}
}

SCENARIO("NoteAppearance falls back to global Settings values with no override", "[note_appearance]")
{
    GIVEN("a Settings instance with custom global appearance values, and a note with no override")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        settings.setWindowRadius(20);
        settings.setBackgroundColor(QColor("#111111"));
        settings.setWindowOpacity(0.5);
        settings.setBlurEnabled(true);
        settings.setGlassEnabled(true);
        settings.setGlassSheenOpacity(0.2);
        settings.setGlassSheenStrength(30);
        settings.setGlassSheenSize(0.25);
        settings.setGlassTintColor(QColor("#abcabc"));
        settings.setBorderWidth(4);
        settings.setBorderColor(QColor("#222222"));
        settings.setScrollbarWidth(11);
        settings.setFontSize(16);
        settings.setFontColor(QColor("#333333"));
        settings.setFontFamily("Noto Sans");
        settings.setHighlightColor(QColor("#444444"));
        settings.setHighlightOpacity(0.7);

        NoteAppearance appearance;
        appearance.setSettings(&settings);
        appearance.setNoteId("no-override-note");

        THEN("every field mirrors the global Settings value")
        {
            CHECK(appearance.windowRadius() == 20);
            CHECK(appearance.backgroundColor() == QColor("#111111"));
            CHECK(appearance.windowOpacity() == 0.5);
            CHECK(appearance.blurEnabled() == true);
            CHECK(appearance.glassEnabled() == true);
            CHECK(appearance.glassSheenOpacity() == 0.2);
            CHECK(appearance.glassSheenStrength() == 30);
            CHECK(appearance.glassSheenSize() == 0.25);
            CHECK(appearance.glassTintColor() == QColor("#abcabc"));
            CHECK(appearance.borderWidth() == 4);
            CHECK(appearance.borderColor() == QColor("#222222"));
            CHECK(appearance.scrollbarWidth() == 11);
            CHECK(appearance.fontSize() == 16);
            CHECK(appearance.fontColor() == QColor("#333333"));
            CHECK(appearance.fontFamily() == "Noto Sans");
            CHECK(appearance.highlightColor() == QColor("#444444"));
            CHECK(appearance.highlightOpacity() == 0.7);
        }

        THEN("no field reports itself as overridden")
        {
            CHECK_FALSE(appearance.hasWindowRadiusOverride());
            CHECK_FALSE(appearance.hasBackgroundColorOverride());
            CHECK_FALSE(appearance.hasWindowOpacityOverride());
            CHECK_FALSE(appearance.hasBlurEnabledOverride());
            CHECK_FALSE(appearance.hasGlassEnabledOverride());
            CHECK_FALSE(appearance.hasGlassSheenOpacityOverride());
            CHECK_FALSE(appearance.hasGlassSheenStrengthOverride());
            CHECK_FALSE(appearance.hasGlassSheenSizeOverride());
            CHECK_FALSE(appearance.hasGlassTintColorOverride());
            CHECK_FALSE(appearance.hasBorderWidthOverride());
            CHECK_FALSE(appearance.hasBorderColorOverride());
            CHECK_FALSE(appearance.hasScrollbarWidthOverride());
            CHECK_FALSE(appearance.hasFontSizeOverride());
            CHECK_FALSE(appearance.hasFontColorOverride());
            CHECK_FALSE(appearance.hasFontFamilyOverride());
            CHECK_FALSE(appearance.hasHighlightColorOverride());
            CHECK_FALSE(appearance.hasHighlightOpacityOverride());
        }
    }
}

SCENARIO("Setting a field's override makes NoteAppearance diverge from the global default", "[note_appearance]")
{
    GIVEN("a Settings instance and a NoteAppearance bound to one note")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        settings.setWindowRadius(12);

        NoteAppearance appearance;
        appearance.setSettings(&settings);
        appearance.setNoteId("note-1");

        WHEN("windowRadius is overridden on this note")
        {
            appearance.setWindowRadiusOverride(40);

            THEN("this note reflects the override")
            {
                CHECK(appearance.windowRadius() == 40);
                CHECK(appearance.hasWindowRadiusOverride());
            }

            THEN("the global Settings value is untouched")
            {
                CHECK(settings.windowRadius() == 12);
            }

            THEN("the override is persisted and visible to a second NoteAppearance on the same note")
            {
                NoteAppearance other;
                other.setSettings(&settings);
                other.setNoteId("note-1");
                CHECK(other.windowRadius() == 40);
            }

            AND_WHEN("the override is cleared")
            {
                appearance.clearWindowRadiusOverride();

                THEN("it reverts to the global default")
                {
                    CHECK(appearance.windowRadius() == 12);
                    CHECK_FALSE(appearance.hasWindowRadiusOverride());
                }
            }
        }

        WHEN("a different note's appearance is queried")
        {
            NoteAppearance other;
            other.setSettings(&settings);
            other.setNoteId("note-2");

            THEN("it is unaffected by note-1's own overrides")
            {
                appearance.setBackgroundColorOverride(QColor("#ff0000"));
                CHECK_FALSE(other.hasBackgroundColorOverride());
            }
        }
    }
}

SCENARIO("NoteAppearance re-resolves when the global Settings value it's currently deferring to changes",
         "[note_appearance]")
{
    GIVEN("a NoteAppearance with no override for fontSize")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        settings.setFontSize(14);

        NoteAppearance appearance;
        appearance.setSettings(&settings);
        appearance.setNoteId("note-1");
        REQUIRE(appearance.fontSize() == 14);

        WHEN("the global fontSize changes")
        {
            settings.setFontSize(22);

            THEN("the note's effective fontSize follows it, since it has no override")
            {
                CHECK(appearance.fontSize() == 22);
            }
        }
    }
}

SCENARIO("A note's highlightColor override diverges from the global default", "[note_appearance]")
{
    GIVEN("a NoteAppearance with no highlightColor override")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        NoteAppearance appearance;
        appearance.setSettings(&settings);
        appearance.setNoteId("note-1");

        THEN("it mirrors the global highlightColor")
        {
            CHECK(appearance.highlightColor() == settings.highlightColor());
            CHECK_FALSE(appearance.hasHighlightColorOverride());
        }

        WHEN("highlightColor is overridden on this note")
        {
            appearance.setHighlightColorOverride(QColor("#ff8800"));

            THEN("this note reflects the override, independent of the global default")
            {
                CHECK(appearance.highlightColor() == QColor("#ff8800"));
                CHECK(appearance.hasHighlightColorOverride());
            }

            AND_WHEN("the override is cleared")
            {
                appearance.clearHighlightColorOverride();

                THEN("it reverts to the global default")
                {
                    CHECK(appearance.highlightColor() == settings.highlightColor());
                    CHECK_FALSE(appearance.hasHighlightColorOverride());
                }
            }
        }
    }
}

SCENARIO("A note's highlightOpacity override diverges from the global default", "[note_appearance]")
{
    GIVEN("a NoteAppearance with no highlightOpacity override")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        settings.setHighlightOpacity(0.55);

        NoteAppearance appearance;
        appearance.setSettings(&settings);
        appearance.setNoteId("note-1");
        REQUIRE(appearance.highlightOpacity() == 0.55);
        REQUIRE_FALSE(appearance.hasHighlightOpacityOverride());

        WHEN("highlightOpacity is overridden on this note")
        {
            appearance.setHighlightOpacityOverride(0.9);

            THEN("this note reflects the override")
            {
                CHECK(appearance.highlightOpacity() == 0.9);
                CHECK(appearance.hasHighlightOpacityOverride());
            }

            THEN("the global Settings value is untouched")
            {
                CHECK(settings.highlightOpacity() == 0.55);
            }

            AND_WHEN("the override is cleared")
            {
                appearance.clearHighlightOpacityOverride();

                THEN("it reverts to the global default")
                {
                    CHECK(appearance.highlightOpacity() == 0.55);
                    CHECK_FALSE(appearance.hasHighlightOpacityOverride());
                }
            }
        }
    }
}

// Code mode (feature request): "default off, overrideable per note and
// globally". These document that resolution, and — the part that is easy to
// get wrong — how code mode's monospace font interacts with the font family
// the user may have chosen for a note themselves.
SCENARIO("Code mode is off by default and can be overridden for one note", "[note_appearance]")
{
    GIVEN("a fresh Settings instance and a note with no override")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        NoteAppearance appearance;
        appearance.setSettings(&settings);
        appearance.setNoteId("code-mode-note");

        THEN("the note is not in code mode, and says it is not overriding it")
        {
            CHECK_FALSE(appearance.codeModeEnabled());
            CHECK_FALSE(appearance.hasCodeModeEnabledOverride());
        }

        WHEN("code mode is turned on globally")
        {
            settings.setCodeModeEnabled(true);

            THEN("the note follows the new global default without overriding it")
            {
                CHECK(appearance.codeModeEnabled());
                CHECK_FALSE(appearance.hasCodeModeEnabledOverride());
            }
        }

        WHEN("this one note overrides code mode on, while the global default stays off")
        {
            appearance.setCodeModeEnabledOverride(true);

            THEN("only this note is in code mode")
            {
                CHECK(appearance.codeModeEnabled());
                CHECK(appearance.hasCodeModeEnabledOverride());
                CHECK_FALSE(settings.codeModeEnabled());
            }

            AND_WHEN("the override is cleared")
            {
                appearance.clearCodeModeEnabledOverride();

                THEN("the note reverts to the global default of off")
                {
                    CHECK_FALSE(appearance.codeModeEnabled());
                    CHECK_FALSE(appearance.hasCodeModeEnabledOverride());
                }
            }
        }

        WHEN("a note overrides code mode OFF while the global default is on")
        {
            settings.setCodeModeEnabled(true);
            appearance.setCodeModeEnabledOverride(false);

            THEN("the note is not in code mode — an override off outranks a global on")
            {
                CHECK_FALSE(appearance.codeModeEnabled());
                CHECK(appearance.hasCodeModeEnabledOverride());
            }
        }
    }
}

// The other half of code mode: not whether a note IS code, but what being code
// DOES to it. Each of the three resolves per-note against a global default the
// same way codeModeEnabled does, so one note can be code without giving up
// spellcheck while every other code note does.
SCENARIO("What code mode switches off can be overridden per note", "[note_appearance]")
{
    GIVEN("a note with no overrides of its own")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        NoteAppearance appearance;
        appearance.setSettings(&settings);
        appearance.setNoteId("code-mode-disables-note");

        THEN("all three follow the shipped global defaults, which are all on")
        {
            CHECK(appearance.codeModeDisablesMarkdownHighlighting());
            CHECK(appearance.codeModeDisablesAutoFormatting());
            CHECK(appearance.codeModeDisablesSpellcheck());
            CHECK_FALSE(appearance.hasCodeModeDisablesMarkdownHighlightingOverride());
            CHECK_FALSE(appearance.hasCodeModeDisablesAutoFormattingOverride());
            CHECK_FALSE(appearance.hasCodeModeDisablesSpellcheckOverride());
        }

        WHEN("a global toggle is turned off")
        {
            settings.setCodeModeDisablesSpellcheck(false);

            THEN("the note follows it without overriding it")
            {
                CHECK_FALSE(appearance.codeModeDisablesSpellcheck());
                CHECK_FALSE(appearance.hasCodeModeDisablesSpellcheckOverride());
            }
        }

        WHEN("this one note says its code mode should keep spellcheck")
        {
            appearance.setCodeModeDisablesSpellcheckOverride(false);

            THEN("only this note stops suppressing spellcheck")
            {
                CHECK_FALSE(appearance.codeModeDisablesSpellcheck());
                CHECK(appearance.hasCodeModeDisablesSpellcheckOverride());
                CHECK(settings.codeModeDisablesSpellcheck());
            }

            THEN("the other two toggles are untouched — each overrides independently")
            {
                CHECK(appearance.codeModeDisablesMarkdownHighlighting());
                CHECK(appearance.codeModeDisablesAutoFormatting());
                CHECK_FALSE(appearance.hasCodeModeDisablesMarkdownHighlightingOverride());
                CHECK_FALSE(appearance.hasCodeModeDisablesAutoFormattingOverride());
            }

            AND_WHEN("the override is cleared")
            {
                appearance.clearCodeModeDisablesSpellcheckOverride();

                THEN("the note reverts to the global default")
                {
                    CHECK(appearance.codeModeDisablesSpellcheck());
                    CHECK_FALSE(appearance.hasCodeModeDisablesSpellcheckOverride());
                }
            }
        }

        WHEN("a note overrides a toggle ON while the global default is off")
        {
            settings.setCodeModeDisablesMarkdownHighlighting(false);
            appearance.setCodeModeDisablesMarkdownHighlightingOverride(true);

            THEN("the note suppresses markdown — an override outranks the global either way")
            {
                CHECK(appearance.codeModeDisablesMarkdownHighlighting());
                CHECK(appearance.hasCodeModeDisablesMarkdownHighlightingOverride());
            }
        }

        // These resolve independently of whether the note is in code mode at
        // all: they answer "what WOULD code mode switch off here", which the
        // settings popup asks whether or not the mode is on. NoteWindow is the
        // one that ANDs them with codeModeEnabled to decide what is actually
        // suppressed.
        WHEN("the note is not in code mode")
        {
            REQUIRE_FALSE(appearance.codeModeEnabled());
            appearance.setCodeModeDisablesSpellcheckOverride(false);

            THEN("the toggle still reads back what it was set to")
            {
                CHECK_FALSE(appearance.codeModeDisablesSpellcheck());
                CHECK(appearance.hasCodeModeDisablesSpellcheckOverride());
            }
        }
    }
}

SCENARIO("Code mode defaults a note's font to monospace", "[note_appearance]")
{
    GIVEN("a global font family, and a note with no family override")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        settings.setFontFamily("Noto Sans");

        NoteAppearance appearance;
        appearance.setSettings(&settings);
        appearance.setNoteId("font-note");

        THEN("an ordinary note uses the global family")
        {
            CHECK(appearance.fontFamily() == "Noto Sans");
        }

        WHEN("the note is put into code mode")
        {
            appearance.setCodeModeEnabledOverride(true);

            THEN("its family becomes the system monospace alias, outranking the global family")
            {
                CHECK(appearance.fontFamily() == "monospace");
            }

            THEN("the global family itself is untouched, so other notes are unaffected")
            {
                CHECK(settings.fontFamily() == "Noto Sans");
            }

            AND_WHEN("code mode is turned back off")
            {
                appearance.clearCodeModeEnabledOverride();

                THEN("the note goes back to the global family")
                {
                    CHECK(appearance.fontFamily() == "Noto Sans");
                }
            }
        }
    }

    GIVEN("a note that has its OWN font family override and is in code mode")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));
        settings.setFontFamily("Noto Sans");

        NoteAppearance appearance;
        appearance.setSettings(&settings);
        appearance.setNoteId("explicit-family-note");
        appearance.setFontFamilyOverride("Comic Sans MS");
        appearance.setCodeModeEnabledOverride(true);

        THEN("the note's own chosen family wins — code mode only supplies a DEFAULT")
        {
            CHECK(appearance.fontFamily() == "Comic Sans MS");
        }

        AND_WHEN("that family override is cleared, leaving code mode on")
        {
            appearance.clearFontFamilyOverride();

            THEN("code mode's monospace default takes over")
            {
                CHECK(appearance.fontFamily() == "monospace");
            }
        }
    }
}

SCENARIO("Turning code mode on re-resolves the note's font family", "[note_appearance]")
{
    GIVEN("a note listening for font family changes")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        Settings settings(tempConfigPath(dir));

        NoteAppearance appearance;
        appearance.setSettings(&settings);
        appearance.setNoteId("signal-note");

        QSignalSpy familySpy(&appearance, &NoteAppearance::fontFamilyChanged);
        QSignalSpy codeModeSpy(&appearance, &NoteAppearance::codeModeEnabledChanged);
        REQUIRE(familySpy.isValid());
        REQUIRE(codeModeSpy.isValid());

        WHEN("code mode is switched on")
        {
            appearance.setCodeModeEnabledOverride(true);

            THEN("both code mode AND font family announce themselves")
            {
                // fontFamily depends on codeModeEnabled, so a QML binding on
                // the family has to be told to re-evaluate — otherwise the
                // editor would keep the old font until something unrelated
                // happened to change it.
                CHECK(codeModeSpy.count() > 0);
                CHECK(familySpy.count() > 0);
            }
        }
    }
}
