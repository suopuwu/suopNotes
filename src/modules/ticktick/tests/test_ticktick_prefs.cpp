#include <catch2/catch_test_macros.hpp>

#include "TickTickPrefs.h"

#include <QSettings>
#include <QTemporaryDir>

// The module's user-facing preferences: which TickTick list to pull from, and
// how often. Both live in the module's own INI, so removing the module leaves
// one orphan file rather than keys threaded through the app's shared config.

SCENARIO("The chosen list is remembered by both id and name", "[ticktick][prefs]")
{
    GIVEN("a fresh config")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickPrefs prefs(tempDir.filePath("ticktick.ini"));

        THEN("no list is chosen yet")
        {
            // Empty is what tells the first pull to fall back to resolving the
            // default list by name (and to create it if it does not exist).
            CHECK(prefs.projectId().isEmpty());
            CHECK(prefs.projectName().isEmpty());
        }

        WHEN("a list is chosen")
        {
            prefs.setProject("p1", "Errands");

            THEN("both the id and the display name are stored")
            {
                // The id is what the pull addresses. The name is what the
                // settings dropdown can show before any network call answers,
                // and what recovers a list deleted and recreated under the same
                // name — which comes back with a different id.
                CHECK(prefs.projectId() == "p1");
                CHECK(prefs.projectName() == "Errands");
            }

            THEN("the choice survives a new instance over the same file")
            {
                TickTickPrefs reopened(tempDir.filePath("ticktick.ini"));
                CHECK(reopened.projectId() == "p1");
                CHECK(reopened.projectName() == "Errands");
            }
        }
    }
}

SCENARIO("The sync interval defaults to once an hour", "[ticktick][prefs]")
{
    GIVEN("a fresh config")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickPrefs prefs(tempDir.filePath("ticktick.ini"));

        THEN("the schedule is hourly")
        {
            // Frequent enough that a task added on a phone shows up the same
            // working session; rare enough that the app is doing nothing at all
            // for virtually its entire life.
            CHECK(prefs.syncIntervalMinutes() == 60);
            CHECK(TickTickPrefs::kDefaultSyncIntervalMinutes == 60);
        }

        WHEN("the user picks a different interval")
        {
            prefs.setSyncIntervalMinutes(15);

            THEN("it is stored and survives reopening")
            {
                CHECK(prefs.syncIntervalMinutes() == 15);
                CHECK(TickTickPrefs(tempDir.filePath("ticktick.ini")).syncIntervalMinutes() == 15);
            }
        }

        WHEN("the user turns the schedule off")
        {
            prefs.setSyncIntervalMinutes(0);

            THEN("zero is stored as-is rather than being raised to the floor")
            {
                // 0 is a real choice — "never, I will sync manually" — not an
                // interval that happens to be too small.
                CHECK(prefs.syncIntervalMinutes() == 0);
            }
        }
    }
}

SCENARIO("An interval below the floor can never become a busy loop",
         "[ticktick][prefs]")
{
    GIVEN("the clamp rule on its own")
    {
        THEN("zero passes through untouched")
        {
            CHECK(TickTickPrefs::clampSyncIntervalMinutes(0) == 0);
            // Negative is nonsense, and treating it as "never" is the safe
            // reading — it certainly must not become a fast interval.
            CHECK(TickTickPrefs::clampSyncIntervalMinutes(-30) == 0);
        }

        THEN("anything under five minutes is raised to five")
        {
            CHECK(TickTickPrefs::clampSyncIntervalMinutes(1)
                  == TickTickPrefs::kMinSyncIntervalMinutes);
            CHECK(TickTickPrefs::clampSyncIntervalMinutes(4)
                  == TickTickPrefs::kMinSyncIntervalMinutes);
        }

        THEN("anything over a day is capped at a day")
        {
            CHECK(TickTickPrefs::clampSyncIntervalMinutes(99999)
                  == TickTickPrefs::kMaxSyncIntervalMinutes);
        }

        THEN("a sensible value is left alone")
        {
            CHECK(TickTickPrefs::clampSyncIntervalMinutes(30) == 30);
        }
    }

    GIVEN("a config file hand-edited to a one-minute interval")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        const QString path = tempDir.filePath("ticktick.ini");
        {
            QSettings raw(path, QSettings::IniFormat);
            raw.beginGroup("sync");
            raw.setValue("intervalMinutes", 1);
            raw.sync();
        }

        THEN("it is clamped on the way out, not just on the way in")
        {
            // The whole idle-cost budget rests on pulls being rare, and this is
            // the number a live timer interval gets built from — so the file is
            // never trusted, only the clamp is.
            TickTickPrefs prefs(path);
            CHECK(prefs.syncIntervalMinutes() == TickTickPrefs::kMinSyncIntervalMinutes);
        }
    }
}

SCENARIO("The task-list migration flag starts unset and persists once set",
         "[ticktick][prefs]")
{
    GIVEN("a fresh config")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        const QString path = tempDir.filePath("ticktick.ini");
        TickTickPrefs prefs(path);

        THEN("the migration has not run — a fresh install still gets its one sweep")
        {
            CHECK_FALSE(prefs.migratedToTaskList());
        }

        WHEN("the sweep records itself as done")
        {
            prefs.setMigratedToTaskList(true);

            THEN("a later launch reading the same file never sweeps again")
            {
                // The guard is what keeps the cache directory from being
                // re-scanned and re-deleted on every start — see
                // TickTickSync's constructor and the sweep-once scenario in
                // test_ticktick_sync.cpp.
                TickTickPrefs reread(path);
                CHECK(reread.migratedToTaskList());
            }
        }
    }
}
