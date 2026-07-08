#include <catch2/catch_test_macros.hpp>

#include "core/KWinRuleWriter.h"

SCENARIO("buildKWinRuleKeys generates the position + taskbar-suppression keys for one note", "[kwin_rule_writer]")
{
    GIVEN("a rule spec for a note at a global position on some monitor")
    {
        KWinRuleSpec spec;
        spec.windowTitle = "suopNotes-3f9a1b2c";
        spec.x = 640;
        spec.y = 120;

        const QMap<QString, QString> keys = buildKWinRuleKeys(spec);

        THEN("it identifies this app and this specific note's window by exact title, not just wmclass")
        {
            CHECK(keys.value("wmclass") == "suopNotes");
            CHECK(keys.value("wmclassmatch") == "1");
            CHECK(keys.value("title") == "suopNotes-3f9a1b2c");
            CHECK(keys.value("titlematch") == "1");
        }

        THEN("position is seeded as Apply-Initially, not Remember (broken on Wayland), "
             "using the true global coordinate verbatim — no monitor/screen rule at all")
        {
            CHECK(keys.value("position") == "640,120");
            CHECK(keys.value("positionrule") == "3");
            CHECK_FALSE(keys.contains("screen"));
            CHECK_FALSE(keys.contains("screenrule"));
        }

        THEN("taskbar, pager, and switcher presence are all Force-suppressed")
        {
            CHECK(keys.value("skiptaskbar") == "true");
            CHECK(keys.value("skiptaskbarrule") == "2");
            CHECK(keys.value("skippager") == "true");
            CHECK(keys.value("skippagerrule") == "2");
            CHECK(keys.value("skipswitcher") == "true");
            CHECK(keys.value("skipswitcherrule") == "2");
        }

        THEN("a human-readable description mentions the note's window title")
        {
            CHECK(keys.value("description").contains("suopNotes-3f9a1b2c"));
        }

        THEN("by default (unpinned) the note is not Force-kept above other windows")
        {
            CHECK(keys.value("above") == "false");
            CHECK(keys.value("aboverule") == "2");
        }
    }

    GIVEN("a rule spec for a note the user has pinned (req. 5)")
    {
        KWinRuleSpec spec;
        spec.windowTitle = "suopNotes-pinned";
        spec.alwaysOnTop = true;

        const QMap<QString, QString> keys = buildKWinRuleKeys(spec);

        THEN("the note is Force-kept above other windows")
        {
            CHECK(keys.value("above") == "true");
            CHECK(keys.value("aboverule") == "2");
        }
    }
}

SCENARIO("buildKWinRuleKeys reflects distinct notes independently", "[kwin_rule_writer]")
{
    GIVEN("two different note specs at different global positions")
    {
        KWinRuleSpec first;
        first.windowTitle = "suopNotes-aaa";
        first.x = 0;
        first.y = 0;

        KWinRuleSpec second;
        second.windowTitle = "suopNotes-bbb";
        second.x = 1920;
        second.y = 200;

        const QMap<QString, QString> firstKeys = buildKWinRuleKeys(first);
        const QMap<QString, QString> secondKeys = buildKWinRuleKeys(second);

        THEN("their titles and positions are independent and correct")
        {
            CHECK(firstKeys.value("title") == "suopNotes-aaa");
            CHECK(secondKeys.value("title") == "suopNotes-bbb");
            CHECK(firstKeys.value("position") == "0,0");
            CHECK(secondKeys.value("position") == "1920,200");
        }
    }
}
