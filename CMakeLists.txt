cmake_minimum_required(VERSION 3.21)
project(suopNotes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test QuickTest Core5Compat LabsPlatform Widgets DBus Network)
qt_standard_project_setup(REQUIRES 6.11)

# KWindowSystem provides KWindowEffects (blur-behind). Optional: only present on
# Linux/KDE. Everywhere else (and if the dev package is missing) blur is a no-op.
find_package(KF6WindowSystem QUIET)
if(KF6WindowSystem_FOUND)
    message(STATUS "suopNotes: KF6WindowSystem found — blur-behind enabled")
else()
    message(STATUS "suopNotes: KF6WindowSystem NOT found — blur-behind will be a no-op")
endif()

# Sonnet (SonnetCore) provides spellchecking. Optional: only present when the
# Sonnet dev package is installed. Note: KF6SonnetConfig.cmake does not set a
# per-component KF6Sonnet_SonnetCore_FOUND variable (its check_required_components
# call is never invoked), so KF6Sonnet_FOUND is the correct guard here.
find_package(KF6Sonnet QUIET COMPONENTS SonnetCore)
if(KF6Sonnet_FOUND)
    message(STATUS "suopNotes: KF6Sonnet (SonnetCore) found — spellcheck enabled")
else()
    message(STATUS "suopNotes: KF6Sonnet (SonnetCore) NOT found — spellcheck will be a no-op")
endif()

# Hunspell provides a bundled fallback dictionary/spellcheck engine. Optional:
# only present when the Hunspell dev package (with pkg-config .pc file) is
# installed. pkg-config is used instead of find_package() since Hunspell ships
# a .pc file rather than a CMake config.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HUNSPELL QUIET hunspell)
endif()
if(HUNSPELL_FOUND)
    message(STATUS "suopNotes: Hunspell found — bundled fallback dictionary support enabled")
else()
    message(STATUS "suopNotes: Hunspell NOT found — no bundled fallback dictionary support")
endif()

# Build-time fetch of a bundled en_US Hunspell dictionary (LibreOffice's, pinned
# to a specific commit + checksum). Only attempted if Hunspell itself was found
# — no point fetching a dictionary with no library to read it. Non-fatal: a
# failed download (e.g. offline) just leaves SUOPNOTES_BUNDLED_DICTIONARY_FOUND
# false and spellcheck degrades gracefully, same as every other optional
# dependency in this file.
if(HUNSPELL_FOUND)
    set(SUOPNOTES_DICT_DIR "${CMAKE_BINARY_DIR}/dictionaries")
    set(SUOPNOTES_DICT_AFF "${SUOPNOTES_DICT_DIR}/en_US.aff")
    set(SUOPNOTES_DICT_DIC "${SUOPNOTES_DICT_DIR}/en_US.dic")
    set(SUOPNOTES_DICT_COMMIT "38d96a4d54ec3449cf7f28cddae1fce32e2b15a7")
    set(SUOPNOTES_DICT_BASE_URL "https://raw.githubusercontent.com/LibreOffice/dictionaries/${SUOPNOTES_DICT_COMMIT}/en")

    file(MAKE_DIRECTORY "${SUOPNOTES_DICT_DIR}")

    set(SUOPNOTES_BUNDLED_DICTIONARY_FOUND TRUE)

    if(NOT EXISTS "${SUOPNOTES_DICT_AFF}")
        file(DOWNLOAD "${SUOPNOTES_DICT_BASE_URL}/en_US.aff" "${SUOPNOTES_DICT_AFF}"
            EXPECTED_HASH SHA256=e746c882dd6f303c2c46e7452804b9201115a6942cfeb15f18f8edf774d2e24e
            STATUS SUOPNOTES_DICT_AFF_DOWNLOAD_STATUS)
        list(GET SUOPNOTES_DICT_AFF_DOWNLOAD_STATUS 0 SUOPNOTES_DICT_AFF_DOWNLOAD_CODE)
        if(NOT SUOPNOTES_DICT_AFF_DOWNLOAD_CODE EQUAL 0)
            set(SUOPNOTES_BUNDLED_DICTIONARY_FOUND FALSE)
            file(REMOVE "${SUOPNOTES_DICT_AFF}")
        endif()
    endif()

    if(SUOPNOTES_BUNDLED_DICTIONARY_FOUND AND NOT EXISTS "${SUOPNOTES_DICT_DIC}")
        file(DOWNLOAD "${SUOPNOTES_DICT_BASE_URL}/en_US.dic" "${SUOPNOTES_DICT_DIC}"
            EXPECTED_HASH SHA256=f0b1a234bd178bdd01875b2a392a9647f888b8fe879f79c52aae62c2759b3647
            STATUS SUOPNOTES_DICT_DIC_DOWNLOAD_STATUS)
        list(GET SUOPNOTES_DICT_DIC_DOWNLOAD_STATUS 0 SUOPNOTES_DICT_DIC_DOWNLOAD_CODE)
        if(NOT SUOPNOTES_DICT_DIC_DOWNLOAD_CODE EQUAL 0)
            set(SUOPNOTES_BUNDLED_DICTIONARY_FOUND FALSE)
            file(REMOVE "${SUOPNOTES_DICT_DIC}")
        endif()
    endif()

    if(SUOPNOTES_BUNDLED_DICTIONARY_FOUND)
        message(STATUS "suopNotes: bundled en_US dictionary ready at ${SUOPNOTES_DICT_DIR}")
    else()
        message(STATUS "suopNotes: bundled en_US dictionary fetch failed (offline?) — bundled fallback will be unavailable, spellcheck degrades gracefully")
    endif()
else()
    set(SUOPNOTES_BUNDLED_DICTIONARY_FOUND FALSE)
endif()

enable_testing()

include(GNUInstallDirs)

add_subdirectory(src)
add_subdirectory(tests)

# Desktop entry so the installed binary is discoverable by app launchers
# (KRunner, application grids, etc). Exec= is baked in as an absolute path
# at configure time so it works regardless of whether the install prefix's
# bin dir is on $PATH.
set(SUOPNOTES_INSTALLED_EXECUTABLE "${CMAKE_INSTALL_FULL_BINDIR}/suopNotes")
configure_file(
    ${CMAKE_SOURCE_DIR}/data/suopnotes.desktop.in
    ${CMAKE_BINARY_DIR}/suopnotes.desktop
    @ONLY
)
install(FILES ${CMAKE_BINARY_DIR}/suopnotes.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
