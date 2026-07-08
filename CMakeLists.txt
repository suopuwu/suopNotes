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

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
