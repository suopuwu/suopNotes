qt_add_library(suopNotesCore STATIC
    core/NoteContentProvider.h
    core/NoteContentProvider.cpp
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
    core/WindowInputMask.h
    core/WindowInputMask.cpp
    core/TextFormatting.h
    core/TextFormatting.cpp
    core/TextSearch.h
    core/TextSearch.cpp
    core/SpellChecker.h
    core/SpellChecker.cpp
    core/SpellCheckHighlighter.h
    core/SpellCheckHighlighter.cpp
    core/MarkdownHighlighter.h
    core/MarkdownHighlighter.cpp
    core/MarkdownImage.h
    core/MarkdownImage.cpp
    core/MarkdownLink.h
    core/MarkdownLink.cpp
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
    core/FileRevealer.h
    core/FileRevealer.cpp
    core/AutostartManager.h
    core/AutostartManager.cpp
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

# Optional feature modules. Each owns its own subdirectory entirely; the only
# coupling is the link plus a SUOPNOTES_HAVE_* definition, matching how the
# optional dependencies above are wired. Removing a module is deleting its
# directory, its option() in the top-level file, and this block.
if(SUOPNOTES_ENABLE_TICKTICK)
    add_subdirectory(modules/ticktick)
endif()

qt_add_executable(suopNotes
    main.cpp
)

qt_add_qml_module(suopNotes
    URI SuopNotesUi
    VERSION 1.0
    QML_FILES
        qml/ChromeGlass.qml
        qml/ChromeWindow.qml
        qml/Main.qml
        qml/NoteWindow.qml
        qml/SettingsDialog.qml
        qml/NoteHud.qml
        qml/NoteHudWindow.qml
        qml/NoteContextMenu.qml
        qml/FindReplacePanel.qml
        qml/PerNoteSettingsPopup.qml
        qml/GlassOverlay.qml
        qml/InteractableHighlight.qml
        qml/MarkerCaretGuard.qml
        qml/RangeOverlay.qml
        qml/ResizeHandle.qml
        qml/SelectionToolbar.qml
        qml/ShortcutField.qml
        qml/WheelSafeSlider.qml
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

if(SUOPNOTES_ENABLE_TICKTICK)
    target_link_libraries(suopNotes PRIVATE suopNotesTickTick)
    target_compile_definitions(suopNotes PRIVATE SUOPNOTES_HAVE_TICKTICK)

    # The module's own QML, compiled into the app's QML module only when the
    # module is built. Aliased into qml/ so each file sits beside the shared
    # QML that Loader-references it (SettingsDialog.qml, NoteWindow.qml) —
    # those Loaders use plain relative URLs, which then also resolve in the
    # QML test harness's flattened copy of this module (tests/qml). With the
    # module off, the files are absent and the referencing Loaders are inert
    # (`active` is false whenever `tickTick` is null).
    set_source_files_properties(modules/ticktick/qml/TickTickSettingsSection.qml
        PROPERTIES QT_RESOURCE_ALIAS "qml/TickTickSettingsSection.qml"
    )
    set_source_files_properties(modules/ticktick/qml/TickTickSyncFooter.qml
        PROPERTIES QT_RESOURCE_ALIAS "qml/TickTickSyncFooter.qml"
    )
    set_source_files_properties(modules/ticktick/qml/TickTickListTitle.qml
        PROPERTIES QT_RESOURCE_ALIAS "qml/TickTickListTitle.qml"
    )
    qt_target_qml_sources(suopNotes QML_FILES
        modules/ticktick/qml/TickTickSettingsSection.qml
        modules/ticktick/qml/TickTickSyncFooter.qml
        modules/ticktick/qml/TickTickListTitle.qml
    )
endif()

install(TARGETS suopNotes RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
