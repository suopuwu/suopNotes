find_package(Catch2 3 REQUIRED)
include(Catch)

qt_add_executable(suopNotes_unit_tests
    test_main.cpp
    test_smoke.cpp
    test_note_store.cpp
    test_note_filename.cpp
    test_settings.cpp
    test_open_notes_model.cpp
    test_blur_region.cpp
    test_text_formatting.cpp
    test_spell_checker.cpp
    test_spell_check_highlighter.cpp
    test_markdown_highlighter.cpp
    test_markdown_list.cpp
    test_markdown_table.cpp
    test_kwin_rule_writer.cpp
    test_position_event_service.cpp
    test_note_appearance.cpp
    test_single_instance_guard.cpp
    test_tray_icon_composer.cpp
)

target_link_libraries(suopNotes_unit_tests PRIVATE
    Catch2::Catch2
    suopNotesCore
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

catch_discover_tests(suopNotes_unit_tests
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
