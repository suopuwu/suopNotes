qt_add_library(suopNotesCore STATIC
    core/NoteStore.h
    core/NoteStore.cpp
    core/NoteFilename.h
    core/NoteFilename.cpp
    core/Settings.h
    core/Settings.cpp
    core/OpenNotesModel.h
    core/OpenNotesModel.cpp
    core/BlurRegion.h
    core/BlurRegion.cpp
    core/BlurEffect.h
    core/BlurEffect.cpp
    core/TextFormatting.h
    core/TextFormatting.cpp
    core/SpellChecker.h
    core/SpellChecker.cpp
    core/SpellCheckHighlighter.h
    core/SpellCheckHighlighter.cpp
    core/MarkdownHighlighter.h
    core/MarkdownHighlighter.cpp
    core/MarkdownList.h
    core/MarkdownList.cpp
    core/MarkdownTable.h
    core/MarkdownTable.cpp
    core/DocumentHandler.h
    core/DocumentHandler.cpp
    core/EditorDragFilter.h
    core/EditorDragFilter.cpp
    core/KWinRuleWriter.h
    core/KWinRuleWriter.cpp
    core/PositionManager.h
    core/PositionManager.cpp
    core/PositionEventService.h
    core/PositionEventService.cpp
    core/NoteAppearance.h
    core/NoteAppearance.cpp
    core/SingleInstanceGuard.h
    core/SingleInstanceGuard.cpp
    core/TrayIconComposer.h
    core/TrayIconComposer.cpp
    core/TrayIconProvider.h
    core/TrayIconProvider.cpp
)

target_include_directories(suopNotesCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(suopNotesCore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::DBus
    Qt6::Network
)

if(KF6WindowSystem_FOUND)
    target_link_libraries(suopNotesCore PUBLIC KF6::WindowSystem)
    target_compile_definitions(suopNotesCore PUBLIC SUOPNOTES_HAVE_KWINDOWSYSTEM)
endif()

if(KF6Sonnet_FOUND)
    target_link_libraries(suopNotesCore PUBLIC KF6::SonnetCore)
    target_compile_definitions(suopNotesCore PUBLIC SUOPNOTES_HAVE_SONNET)
endif()

if(HUNSPELL_FOUND)
    target_include_directories(suopNotesCore PUBLIC ${HUNSPELL_INCLUDE_DIRS})
    target_link_libraries(suopNotesCore PUBLIC ${HUNSPELL_LIBRARIES})
    target_compile_definitions(suopNotesCore PUBLIC SUOPNOTES_HAVE_HUNSPELL)
endif()

if(SUOPNOTES_BUNDLED_DICTIONARY_FOUND)
    target_compile_definitions(suopNotesCore PUBLIC
        SUOPNOTES_HAVE_BUNDLED_DICTIONARY
        SUOPNOTES_BUNDLED_DICT_AFF_PATH="${SUOPNOTES_DICT_AFF}"
        SUOPNOTES_BUNDLED_DICT_DIC_PATH="${SUOPNOTES_DICT_DIC}"
    )
endif()

qt_add_executable(suopNotes
    main.cpp
)

qt_add_qml_module(suopNotes
    URI SuopNotesUi
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/NoteWindow.qml
        qml/SettingsDialog.qml
        qml/NoteHud.qml
        qml/PerNoteSettingsPopup.qml
        qml/GlassOverlay.qml
        qml/InteractableHighlight.qml
        qml/ResizeHandle.qml
        qml/SelectionToolbar.qml
        qml/ShortcutField.qml
)

target_link_libraries(suopNotes PRIVATE
    suopNotesCore
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Core5Compat
    Qt6::LabsPlatform
    Qt6::Widgets
)

install(TARGETS suopNotes RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
