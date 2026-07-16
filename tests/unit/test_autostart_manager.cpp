#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/AutostartManager.h"

namespace {
QString desktopFileIn(const QTemporaryDir &dir)
{
    return dir.path() + QLatin1String("/autostart/suopnotes.desktop");
}
}

SCENARIO("buildAutostartDesktopEntry produces a valid freedesktop autostart entry", "[autostart]")
{
    GIVEN("an ordinary space-free executable path")
    {
        const QString entry = buildAutostartDesktopEntry("/usr/bin/suopNotes");

        THEN("it is a Desktop Entry that launches exactly that path on login")
        {
            CHECK(entry.startsWith("[Desktop Entry]\n"));
            CHECK(entry.contains("Type=Application\n"));
            CHECK(entry.contains("Name=suopNotes\n"));
            CHECK(entry.contains("Exec=/usr/bin/suopNotes\n"));
        }

        THEN("it opts into GNOME's autostart flag and avoids spawning a terminal")
        {
            CHECK(entry.contains("X-GNOME-Autostart-enabled=true\n"));
            CHECK(entry.contains("Terminal=false\n"));
        }
    }

    GIVEN("an executable path containing a space")
    {
        const QString entry = buildAutostartDesktopEntry("/home/a b/suopNotes");

        THEN("the Exec value is double-quoted per the Desktop Entry spec")
        {
            CHECK(entry.contains("Exec=\"/home/a b/suopNotes\"\n"));
        }
    }
}

SCENARIO("AutostartManager reflects and toggles the on-disk autostart file", "[autostart]")
{
    GIVEN("a manager pointed at an empty temp autostart dir")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        AutostartManager manager(dir.path() + QLatin1String("/autostart"), "/opt/suopNotes/suopNotes");

        THEN("it reports launch-on-login as available and currently disabled")
        {
            CHECK(manager.isAvailable());
            CHECK_FALSE(manager.isEnabled());
            CHECK_FALSE(QFile::exists(desktopFileIn(dir)));
        }

        WHEN("launch-on-login is enabled")
        {
            QSignalSpy spy(&manager, &AutostartManager::enabledChanged);
            manager.setEnabled(true);

            THEN("the .desktop file is written into the autostart dir with the right Exec path")
            {
                CHECK(manager.isEnabled());
                REQUIRE(QFile::exists(desktopFileIn(dir)));

                QFile file(desktopFileIn(dir));
                REQUIRE(file.open(QIODevice::ReadOnly));
                const QString contents = QString::fromUtf8(file.readAll());
                CHECK(contents.contains("Exec=/opt/suopNotes/suopNotes\n"));
            }

            THEN("enabledChanged fired exactly once for the transition")
            {
                CHECK(spy.count() == 1);
            }
        }

        WHEN("enabled and then disabled again")
        {
            manager.setEnabled(true);
            QSignalSpy spy(&manager, &AutostartManager::enabledChanged);
            manager.setEnabled(false);

            THEN("the file is removed and it reports disabled once more")
            {
                CHECK_FALSE(manager.isEnabled());
                CHECK_FALSE(QFile::exists(desktopFileIn(dir)));
                CHECK(spy.count() == 1);
            }
        }

        WHEN("enabling something already enabled")
        {
            manager.setEnabled(true);
            QSignalSpy spy(&manager, &AutostartManager::enabledChanged);
            manager.setEnabled(true);

            THEN("it is a no-op: no redundant enabledChanged signal")
            {
                CHECK(manager.isEnabled());
                CHECK(spy.count() == 0);
            }
        }

        WHEN("disabling something already disabled")
        {
            QSignalSpy spy(&manager, &AutostartManager::enabledChanged);
            manager.setEnabled(false);

            THEN("it is a no-op: no spurious enabledChanged signal")
            {
                CHECK_FALSE(manager.isEnabled());
                CHECK(spy.count() == 0);
            }
        }
    }
}

SCENARIO("AutostartManager reads live filesystem state rather than caching it", "[autostart]")
{
    GIVEN("a manager whose autostart file was created out-of-band (as KDE's own Autostart page would)")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString autostartDir = dir.path() + QLatin1String("/autostart");
        AutostartManager manager(autostartDir, "/opt/suopNotes/suopNotes");

        REQUIRE(QDir().mkpath(autostartDir));
        QFile file(desktopFileIn(dir));
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("[Desktop Entry]\nType=Application\nExec=/opt/suopNotes/suopNotes\n");
        file.close();

        THEN("it reports enabled without ever having written the file itself")
        {
            CHECK(manager.isEnabled());
        }
    }
}
