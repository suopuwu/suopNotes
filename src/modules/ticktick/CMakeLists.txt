# TickTick sync module.
#
# Self-contained by design: this directory owns its C++ sources, its tests,
# its QML components (qml/) and its optional dependency handling. The traces
# outside it are the option() in the top-level CMakeLists.txt, guarded blocks
# in ../../CMakeLists.txt and tests/qml/CMakeLists.txt (which compile qml/'s
# files into each binary's QML module), one #ifdef island in main.cpp, and
# the TickTick-guarded QML threaded through src/qml/ — see TickTickModule.h's
# header comment for the honest accounting of that last part.
#
# The QML here is deliberately NOT its own qt_add_qml_module: an earlier
# attempt shared a static QML-plugin target across the app and test binaries,
# and Qt's static-plugin cross-target import did not resolve reliably (see
# tests/qml/CMakeLists.txt). Instead each binary compiles these files into
# its own copy of the SuopNotesUi module, aliased so the shared QML's
# relative Loader URLs resolve identically in both.

qt_add_library(suopNotesTickTick STATIC
    TickTickModule.h
    TickTickModule.cpp
    TickTickAuth.h
    TickTickAuth.cpp
    TickTickTokenStore.h
    TickTickTokenStore.cpp
    TickTickNotes.h
    TickTickNotes.cpp
    TickTickPrefs.h
    TickTickPrefs.cpp
    TickTickClient.h
    TickTickClient.cpp
    TickTickTaskList.h
    TickTickTaskList.cpp
    TickTickSync.h
    TickTickSync.cpp
)

target_include_directories(suopNotesTickTick PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(suopNotesTickTick PUBLIC
    # For core/NoteContentProvider.h, the interface that lets this module supply
    # notes. The dependency only points this way — core knows nothing about the
    # module, which is what keeps removal a delete.
    suopNotesCore
    Qt6::Core
    Qt6::Gui      # QDesktopServices, to hand the authorize URL to the browser
    Qt6::Network  # QTcpServer for the OAuth callback, QNetworkAccessManager for the token exchange
)

# Optional secret storage for the access token, following the same
# found-or-degrade pattern as KWindowSystem/Sonnet/Hunspell in the top-level
# file. PUBLIC so the definition reaches this module's own test binary.
if(Qt6Keychain_FOUND)
    target_link_libraries(suopNotesTickTick PUBLIC Qt6Keychain::Qt6Keychain)
    target_compile_definitions(suopNotesTickTick PUBLIC SUOPNOTES_HAVE_QTKEYCHAIN)
endif()

# Unconditional, matching the top-level file's own add_subdirectory(tests):
# this project calls enable_testing() rather than include(CTest), so
# BUILD_TESTING is never defined and guarding on it would silently skip these
# specs while still building the library.
add_subdirectory(tests)
