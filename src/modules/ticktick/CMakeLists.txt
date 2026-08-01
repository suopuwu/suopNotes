# TickTick sync module.
#
# Self-contained by design: this directory owns its sources, its tests and its
# optional dependency handling. Removing the module is `rm -rf` on this
# directory plus deleting the option() in the top-level CMakeLists.txt and the
# guarded block in ../../CMakeLists.txt — nothing else in the tree refers to it
# except one #ifdef island in main.cpp, which compiles to nothing when the
# definition is absent.
#
# There is deliberately no qt_add_qml_module here yet: the module has no UI
# until Phase 6, and tests/qml/CMakeLists.txt documents that sharing a static
# QML-plugin target across binaries failed to resolve — a wall worth hitting
# with real QML in hand rather than with a placeholder.

qt_add_library(suopNotesTickTick STATIC
    TickTickModule.h
    TickTickModule.cpp
    TickTickAuth.h
    TickTickAuth.cpp
    TickTickTokenStore.h
    TickTickTokenStore.cpp
)

target_include_directories(suopNotesTickTick PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(suopNotesTickTick PUBLIC
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
