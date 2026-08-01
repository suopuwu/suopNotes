#include <catch2/catch_test_macros.hpp>

#include "TickTickTokenStore.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

// Non-secret module config lives in the module's own INI rather than the app's
// Settings class, so the module needs no core changes and removing it leaves
// one orphan file instead of keys threaded through shared config.

SCENARIO("Module config round-trips through its own file", "[ticktick][store]")
{
    GIVEN("a store pointed at a temporary config file")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        const QString path = tempDir.filePath("ticktick.ini");

        TickTickTokenStore store(path);

        WHEN("a client id is set")
        {
            store.setClientId("my-client-id");

            THEN("it reads back")
            {
                CHECK(store.clientId() == "my-client-id");
            }

            THEN("it survives into a new store on the same file")
            {
                // The client id is not a secret, so it stays in the INI where a
                // user can see and correct it.
                TickTickTokenStore reopened(path);
                CHECK(reopened.clientId() == "my-client-id");
            }
        }

        WHEN("nothing has been configured yet")
        {
            THEN("the client id is empty rather than invented")
            {
                CHECK(store.clientId().isEmpty());
            }

            THEN("there is no expiry")
            {
                // Null, not epoch — "no token" must not read as "expired long
                // ago" or as "valid until 1970".
                CHECK_FALSE(store.expiresAt().isValid());
            }
        }
    }
}

SCENARIO("The callback port defaults to the one most TickTick apps register",
         "[ticktick][store]")
{
    GIVEN("an unconfigured store")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickTokenStore store(tempDir.filePath("ticktick.ini"));

        THEN("it reports 8080")
        {
            CHECK(store.callbackPort() == 8080);
        }

        WHEN("a port is chosen")
        {
            store.setCallbackPort(9123);

            THEN("it is kept")
            {
                CHECK(store.callbackPort() == 9123);
            }
        }
    }

    GIVEN("a config file holding a nonsense port")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        const QString path = tempDir.filePath("ticktick.ini");
        {
            QSettings raw(path, QSettings::IniFormat);
            raw.setValue("callbackPort", 0);
            raw.sync();
        }

        THEN("the default is used rather than a port that cannot be bound")
        {
            // A hand-edited or corrupted file must not make the login fail with
            // an unbindable-socket error instead of just working.
            TickTickTokenStore store(path);
            CHECK(store.callbackPort() == 8080);
        }
    }
}

SCENARIO("Secret loading always answers, including on first run", "[ticktick][store]")
{
    GIVEN("a store with nothing stored")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickTokenStore store(tempDir.filePath("ticktick.ini"));

        QSignalSpy loadedSpy(&store, &TickTickTokenStore::secretsLoaded);

        WHEN("secrets are loaded")
        {
            store.loadSecrets();

            THEN("it reports empty secrets rather than an error")
            {
                // Having no token yet is the normal first-run state, so it must
                // not present as a failure the user is asked to act on.
                REQUIRE(loadedSpy.wait(5000));
                REQUIRE(loadedSpy.count() == 1);
                CHECK(loadedSpy.at(0).at(0).toString().isEmpty());
                CHECK(loadedSpy.at(0).at(1).toString().isEmpty());
            }
        }

        WHEN("the result is only connected to after the call")
        {
            store.loadSecrets();

            THEN("the answer still arrives")
            {
                // The result is delivered through the event loop on every
                // backend, so a caller cannot miss it by connecting late on one
                // build and not another.
                QSignalSpy lateSpy(&store, &TickTickTokenStore::secretsLoaded);
                REQUIRE(lateSpy.wait(5000));
                CHECK(lateSpy.count() == 1);
            }
        }
    }
}

SCENARIO("Storing a token records when it will expire", "[ticktick][store]")
{
    GIVEN("a store")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickTokenStore store(tempDir.filePath("ticktick.ini"));

        WHEN("a token with TickTick's real lifetime is stored")
        {
            store.storeSecrets("secret", "token", 15551999);

            THEN("the expiry lands roughly 180 days out")
            {
                // Absolute rather than a duration, so the app can warn ahead of
                // the lapse instead of only finding out as a mid-sync 401 —
                // there is no refresh token to recover with.
                const QDateTime expiry = store.expiresAt();
                REQUIRE(expiry.isValid());
                const qint64 days = QDateTime::currentDateTimeUtc().daysTo(expiry);
                CHECK(days >= 179);
                CHECK(days <= 180);
            }
        }

        WHEN("a token with an unknown lifetime is stored")
        {
            store.storeSecrets("secret", "token", 0);

            THEN("no expiry is recorded rather than one in the past")
            {
                CHECK_FALSE(store.expiresAt().isValid());
            }
        }
    }
}
