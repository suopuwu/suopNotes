#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "core/KWinRuleWriter.h"

namespace {

// Writes a minimal kwinrulesrc to a temp path so the read-only change
// detection can be exercised without a live KWin/kwriteconfig6. `groupBody`
// is the raw INI body of the note's own group (the caller varies it per
// scenario); the [General] listing is written separately so tests can also
// cover the "keys look right but the group isn't listed" case.
QString writeRulesFile(const QTemporaryDir &dir, const QString &generalRules, const QString &group,
                       const QString &groupBody)
{
    const QString path = dir.path() + QStringLiteral("/kwinrulesrc");
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << "[General]\n";
    if (!generalRules.isEmpty())
        out << "rules=" << generalRules << "\n";
    out << "\n[" << group << "]\n" << groupBody;
    file.close();
    return path;
}

}

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

// kwinRuleAlreadyMatches is the read-only check that lets a normal restart
// skip re-writing (and re-reconfiguring) rules that are already correct —
// the core of the startup-speed fix. It answers one question: "is writing
// these keys to this group a complete no-op?"
SCENARIO("kwinRuleAlreadyMatches decides whether a rule write can be skipped", "[kwin_rule_writer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString group = "suopNotes-note1";
    KWinRuleSpec spec;
    spec.windowTitle = "suopNotes-note1";
    spec.x = 640;
    spec.y = 120;
    const QMap<QString, QString> keys = buildKWinRuleKeys(spec);

    // Serialize exactly what applyRule/writeRule would have written for this
    // group, so "on disk" reflects a real prior write.
    auto bodyFor = [](const QMap<QString, QString> &k) {
        QString body;
        for (auto it = k.constBegin(); it != k.constEnd(); ++it)
            body += it.key() + "=" + it.value() + "\n";
        return body;
    };

    GIVEN("a kwinrulesrc where the group is listed and holds exactly the desired keys")
    {
        const QString path = writeRulesFile(dir, group, group, bodyFor(keys));

        THEN("the write is reported as a no-op — nothing changed, skip it")
        {
            CHECK(kwinRuleAlreadyMatches(path, group, keys));
        }
    }

    GIVEN("a position value containing a comma (the value QSettings would misparse)")
    {
        const QString path = writeRulesFile(dir, group, group, bodyFor(keys));

        THEN("the comma-bearing position=640,120 is compared correctly, not read as empty")
        {
            // Regression guard for the reason this uses a hand INI scan instead
            // of QSettings: if the comma value were misread, the position key
            // would never match and this would wrongly report "changed".
            REQUIRE(keys.value("position") == "640,120");
            CHECK(kwinRuleAlreadyMatches(path, group, keys));
        }
    }

    GIVEN("a kwinrulesrc where the group's stored position differs from the desired one")
    {
        QMap<QString, QString> stale = keys;
        stale["position"] = "100,100";
        const QString path = writeRulesFile(dir, group, group, bodyFor(stale));

        THEN("the write is NOT a no-op — the rule must be rewritten")
        {
            CHECK_FALSE(kwinRuleAlreadyMatches(path, group, keys));
        }
    }

    GIVEN("a group whose keys all match but which is absent from [General]'s rules list")
    {
        const QString path = writeRulesFile(dir, /*generalRules=*/"", group, bodyFor(keys));

        THEN("the write is NOT a no-op — an unlisted rule is one KWin won't apply")
        {
            CHECK_FALSE(kwinRuleAlreadyMatches(path, group, keys));
        }
    }

    GIVEN("a matching group that still carries a stale screen/screenrule key from an older version")
    {
        const QString path = writeRulesFile(dir, group, group,
                                            bodyFor(keys) + "screen=1\nscreenrule=2\n");

        THEN("the write is NOT a no-op — writeRule needs to run to delete the stale keys")
        {
            CHECK_FALSE(kwinRuleAlreadyMatches(path, group, keys));
        }
    }

    GIVEN("no kwinrulesrc file at all (a fresh config)")
    {
        const QString path = dir.path() + "/does-not-exist-kwinrulesrc";

        THEN("the write is NOT a no-op — the rule is entirely absent")
        {
            CHECK_FALSE(kwinRuleAlreadyMatches(path, group, keys));
        }
    }
}
