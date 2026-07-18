qt_add_executable(suopNotes_qml_tests
    tst_qml_main.cpp
)

# Its own private copy of the "SuopNotesUi" QML module, built straight from
# the same source files the app uses — not a copy, not a shared library.
# (An earlier attempt shared one static-plugin library target between the
# app and this test binary; Qt's static QML plugin cross-target import
# didn't resolve reliably even for the app itself, so each binary now just
# compiles the module directly, exactly like the app did before that
# split — simpler and known to work.)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/qml/ChromeWindow.qml
    PROPERTIES QT_RESOURCE_ALIAS "ChromeWindow.qml"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/qml/NoteWindow.qml
    PROPERTIES QT_RESOURCE_ALIAS "NoteWindow.qml"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/qml/NoteHud.qml
    PROPERTIES QT_RESOURCE_ALIAS "NoteHud.qml"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/qml/NoteHudWindow.qml
    PROPERTIES QT_RESOURCE_ALIAS "NoteHudWindow.qml"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/qml/NoteContextMenu.qml
    PROPERTIES QT_RESOURCE_ALIAS "NoteContextMenu.qml"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/qml/FindReplacePanel.qml
    PROPERTIES QT_RESOURCE_ALIAS "FindReplacePanel.qml"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/qml/GlassOverlay.qml
    PROPERTIES QT_RESOURCE_ALIAS "GlassOverlay.qml"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/qml/PerNoteSettingsPopup.qml
    PROPERTIES QT_RESOURCE_ALIAS "PerNoteSettingsPopup.qml"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/qml/InteractableHighlight.qml
    PROPERTIES QT_RESOURCE_ALIAS "InteractableHighlight.qml"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/qml/ResizeHandle.qml
    PROPERTIES QT_RESOURCE_ALIAS "ResizeHandle.qml"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/qml/SelectionToolbar.qml
    PROPERTIES QT_RESOURCE_ALIAS "SelectionToolbar.qml"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/qml/ShortcutField.qml
    PROPERTIES QT_RESOURCE_ALIAS "ShortcutField.qml"
)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/qml/SettingsDialog.qml
    PROPERTIES QT_RESOURCE_ALIAS "SettingsDialog.qml"
)

qt_add_qml_module(suopNotes_qml_tests
    URI SuopNotesUi
    VERSION 1.0
    QML_FILES
        ${CMAKE_SOURCE_DIR}/src/qml/ChromeWindow.qml
        ${CMAKE_SOURCE_DIR}/src/qml/NoteWindow.qml
        ${CMAKE_SOURCE_DIR}/src/qml/NoteHud.qml
        ${CMAKE_SOURCE_DIR}/src/qml/NoteHudWindow.qml
        ${CMAKE_SOURCE_DIR}/src/qml/NoteContextMenu.qml
        ${CMAKE_SOURCE_DIR}/src/qml/FindReplacePanel.qml
        ${CMAKE_SOURCE_DIR}/src/qml/GlassOverlay.qml
        ${CMAKE_SOURCE_DIR}/src/qml/PerNoteSettingsPopup.qml
        ${CMAKE_SOURCE_DIR}/src/qml/InteractableHighlight.qml
        ${CMAKE_SOURCE_DIR}/src/qml/ResizeHandle.qml
        ${CMAKE_SOURCE_DIR}/src/qml/SelectionToolbar.qml
        ${CMAKE_SOURCE_DIR}/src/qml/ShortcutField.qml
        ${CMAKE_SOURCE_DIR}/src/qml/SettingsDialog.qml
)

target_link_libraries(suopNotes_qml_tests PRIVATE
    Qt6::QuickTest
    Qt6::Qml
    Qt6::Core5Compat
    suopNotesCore
)

target_compile_definitions(suopNotes_qml_tests PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(NAME qml_tests COMMAND suopNotes_qml_tests)
set_tests_properties(qml_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
