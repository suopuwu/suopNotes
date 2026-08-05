#include <catch2/catch_test_macros.hpp>

#include "TickTickAuth.h"
#include "TickTickTokenStore.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// Entering client credentials is the very first thing a user does with this
// module, and until it sticks nothing else in the feature is reachable.

SCENARIO("Entering client credentials configures the module", "[ticktick][auth]")
{
    GIVEN("a fresh, unconfigured auth")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickTokenStore store(tempDir.filePath("ticktick.ini"), TickTickTokenStore::SecretBackend::ConfigFileOnly);
        TickTickAuth auth(store);

        REQUIRE_FALSE(auth.isConfigured());

        WHEN("credentials are entered")
        {
            QSignalSpy configuredSpy(&auth, &TickTickAuth::configuredChanged);
            auth.setClientCredentials("my-client-id", "my-secret");

            THEN("the module reports itself configured straight away")
            {
                CHECK(auth.isConfigured());
                CHECK(auth.clientId() == "my-client-id");
                CHECK(configuredSpy.count() >= 1);
            }

            THEN("it is STILL configured once the startup secret load lands")
            {
                // The constructor starts an asynchronous read of the secret
                // store. If the user saves before that read completes, its
                // callback must not overwrite what they just typed with the
                // empty value it found.
                QTest::qWait(300);
                CHECK(auth.isConfigured());
                CHECK(auth.clientId() == "my-client-id");
            }
        }
    }
}

SCENARIO("Credentials survive a restart", "[ticktick][auth]")
{
    GIVEN("credentials entered in one session")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        const QString path = tempDir.filePath("ticktick.ini");

        {
            TickTickTokenStore store(path, TickTickTokenStore::SecretBackend::ConfigFileOnly);
            TickTickAuth auth(store);
            auth.setClientCredentials("my-client-id", "my-secret");
            QTest::qWait(300); // let the store finish writing
            REQUIRE(auth.isConfigured());
        }

        WHEN("the app is started again")
        {
            TickTickTokenStore store(path, TickTickTokenStore::SecretBackend::ConfigFileOnly);
            TickTickAuth auth(store);
            QSignalSpy configuredSpy(&auth, &TickTickAuth::configuredChanged);

            THEN("it comes back configured once the secrets have loaded")
            {
                // Asynchronous, so not true immediately — but it must become
                // true without the user re-entering anything.
                QTest::qWait(500);
                CHECK(auth.clientId() == "my-client-id");
                CHECK(auth.isConfigured());
            }
        }
    }
}

SCENARIO("Re-saving the form does not wipe the stored secret", "[ticktick][auth]")
{
    GIVEN("a configured auth")
    {
        QTemporaryDir tempDir;
        REQUIRE(tempDir.isValid());
        TickTickTokenStore store(tempDir.filePath("ticktick.ini"), TickTickTokenStore::SecretBackend::ConfigFileOnly);
        TickTickAuth auth(store);
        auth.setClientCredentials("my-client-id", "my-secret");
        QTest::qWait(300);
        REQUIRE(auth.isConfigured());

        WHEN("only the client id is changed, leaving the secret field blank")
        {
            auth.setClientCredentials("a-different-id", "");

            THEN("the stored secret is kept and the module stays configured")
            {
                // The settings form never shows the saved secret, so an empty
                // field has to mean "unchanged" rather than "clear it".
                CHECK(auth.isConfigured());
                CHECK(auth.clientId() == "a-different-id");
            }
        }
    }
}
