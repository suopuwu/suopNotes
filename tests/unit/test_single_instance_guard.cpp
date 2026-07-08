#include <catch2/catch_test_macros.hpp>
#include <QSignalSpy>
#include <QUuid>
#include <memory>

#include "core/SingleInstanceGuard.h"

namespace {
// A fresh key per SCENARIO (QUuid) so parallel/repeated test runs never
// collide with a stale leftover socket from a previous run, and never
// collide with the real app's actual key ("suopNotes-singleinstance",
// used only in main.cpp — never in tests).
QString uniqueTestKey()
{
    return QStringLiteral("suopNotes-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}

SCENARIO("A guard with no existing instance at its key becomes the server directly", "[single_instance_guard]")
{
    GIVEN("a unique key nothing is currently listening on")
    {
        const QString key = uniqueTestKey();
        SingleInstanceGuard guard(key);

        WHEN("takeOwnership is called")
        {
            // Must return promptly rather than block waiting for a
            // connection that will never come — this is the "no prior
            // instance" path, detected via a bounded connect attempt
            // failing fast, not treated as an error.
            const bool owned = guard.takeOwnership();

            THEN("it succeeds immediately, with this guard now the sole listener")
            {
                REQUIRE(owned);
            }
        }
    }
}

SCENARIO("A second guard at the same key replaces the first", "[single_instance_guard]")
{
    GIVEN("guard A already holding a key")
    {
        const QString key = uniqueTestKey();
        SingleInstanceGuard guardA(key);
        REQUIRE(guardA.takeOwnership());

        QSignalSpy replaceSpy(&guardA, &SingleInstanceGuard::replaceRequested);

        WHEN("guard B takes ownership of the same key")
        {
            SingleInstanceGuard guardB(key);
            const bool owned = guardB.takeOwnership();

            THEN("guard B successfully becomes the sole instance")
            {
                REQUIRE(owned);
            }

            THEN("guard A was told to step aside exactly once")
            {
                REQUIRE(replaceSpy.count() == 1);
            }

            THEN("guard A is no longer accepting connections — a fresh connect now reaches B, not A")
            {
                // guard A's handleNewConnection() closes its own server
                // synchronously as part of ceding ownership (see
                // SingleInstanceGuard.cpp) — it does not wait for a
                // qApp->quit() that this test never triggers. If A were
                // still listening, this second handshake would itself
                // find *A* answering and hand ownership back to it, which
                // would be a real regression in the "replace, don't
                // coexist" contract this class exists to guarantee.
                QSignalSpy bSpy(&guardB, &SingleInstanceGuard::replaceRequested);
                QSignalSpy aSpySecond(&guardA, &SingleInstanceGuard::replaceRequested);

                SingleInstanceGuard challenger(key);
                REQUIRE(challenger.takeOwnership());

                CHECK(bSpy.count() == 1);
                CHECK(aSpySecond.count() == 0);
            }
        }
    }
}

SCENARIO("Two independent keys never interfere with each other", "[single_instance_guard]")
{
    GIVEN("two guards on two distinct unique keys")
    {
        SingleInstanceGuard guardA(uniqueTestKey());
        SingleInstanceGuard guardB(uniqueTestKey());

        WHEN("both take ownership")
        {
            const bool ownedA = guardA.takeOwnership();
            const bool ownedB = guardB.takeOwnership();

            THEN("both succeed, since neither key had a prior instance")
            {
                REQUIRE(ownedA);
                REQUIRE(ownedB);
            }
        }
    }
}

SCENARIO("A ceded guard's destruction does not delete the replacing guard's live server", "[single_instance_guard]")
{
    GIVEN("guard A replaced by guard B, so A has ceded but not yet been destroyed")
    {
        const QString key = uniqueTestKey();
        auto guardA = std::make_unique<SingleInstanceGuard>(key);
        REQUIRE(guardA->takeOwnership());

        SingleInstanceGuard guardB(key);
        REQUIRE(guardB.takeOwnership());

        WHEN("guard A is destroyed some time later — simulating a real app's own shutdown work "
             "(KWin scripting round-trips, etc.) happening after replaceRequested() but before the "
             "process actually exits")
        {
            guardA.reset();

            THEN("guard B is still the one a fresh connection reaches — a stale removeServer() call "
                 "from A's destructor did not delete B's live socket file out from under it")
            {
                SingleInstanceGuard challenger(key);
                QSignalSpy bSpy(&guardB, &SingleInstanceGuard::replaceRequested);
                REQUIRE(challenger.takeOwnership());
                // If A's destructor had wrongly deleted B's file, the
                // challenger would have found nothing listening at all and
                // silently become its own independent server instead of
                // ever reaching B — this is the only way B's signal fires.
                CHECK(bSpy.count() == 1);
            }
        }
    }
}
