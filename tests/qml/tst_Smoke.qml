import QtQuick
import QtTest

TestCase {
    name: "Smoke"

    function test_harness_is_wired_up() {
        compare(1 + 1, 2, "Qt Quick Test harness compiles, links, and runs under ctest")
    }
}
