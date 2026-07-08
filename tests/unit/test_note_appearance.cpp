#include <catch2/catch_test_macros.hpp>
#include <QColor>
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
