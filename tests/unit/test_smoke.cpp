#include <catch2/catch_test_macros.hpp>

// Harness sanity check, not app behavior — real BDD specs land alongside
// NoteStore/Settings in later steps and will out-number this file quickly.
SCENARIO("The Catch2 test harness is wired up", "[smoke]")
{
    GIVEN("the CMake + CTest + Catch2 build is configured")
    {
        WHEN("a trivial assertion is evaluated")
        {
            THEN("it passes, proving specs compile, link, and run under ctest")
            {
                REQUIRE(1 + 1 == 2);
            }
        }
    }
}
