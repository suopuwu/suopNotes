# The module owns its tests rather than adding files to tests/unit's explicit
# source list, so deleting src/modules/ticktick/ takes the specs with it and
# leaves no dangling references behind.
#
# Its own main (see test_main.cpp) rather than Catch2WithMain: the token store
# reports through the event loop, so these specs need a running QCoreApplication.

find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(suopNotesTickTick_unit_tests
    test_main.cpp
    test_ticktick_module.cpp
    test_ticktick_auth.cpp
    test_ticktick_token_store.cpp
)

target_link_libraries(suopNotesTickTick_unit_tests PRIVATE
    Catch2::Catch2
    suopNotesTickTick
    Qt6::Test
)

catch_discover_tests(suopNotesTickTick_unit_tests)
