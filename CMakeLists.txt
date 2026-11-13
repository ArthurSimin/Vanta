cmake_minimum_required(VERSION 3.21)

# The version is the single source of truth for the binary, the packages and the
# About dialog. Release builds pass -DVANTA_VERSION=<tag without v> so a tagged
# build reports exactly what was released; otherwise default to 1.4.0.
set(VANTA_VERSION "1.4.0" CACHE STRING "Version reported by the application")

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE VANTA_GIT_EXACT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(VANTA_GIT_EXACT_TAG MATCHES "^v?([0-9].*)")
        set(VANTA_VERSION "${CMAKE_MATCH_1}" CACHE STRING "Version reported by the application" FORCE)
    endif()
endif()

string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" VANTA_NUMERIC_VERSION "${VANTA_VERSION}")
if(NOT VANTA_NUMERIC_VERSION)
    set(VANTA_NUMERIC_VERSION "1.4.0")
endif()

project(Vanta
    VERSION ${VANTA_NUMERIC_VERSION}
    DESCRIPTION "Linux game launcher with Wine, Proton, and OnlineFix support"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Svg Concurrent Network)
# zlib reads the deflate stream inside mod ZIP archives. It is present on every
# supported distribution because Qt itself links it, but the archive reader
# needs it directly.
find_package(ZLIB REQUIRED)

# qt_standard_project_setup() landed in Qt 6.3. Everything it does for this
# project (AUTOMOC, AUTORCC, C++17) is already set above, so older Qt releases
# only need the call skipped rather than emulated.
if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
endif()

set(VANTA_SOURCES
    src/main.cpp

    src/core/Game.cpp
    src/core/Game.h
    src/core/I18n.cpp
    src/core/I18n.h
    src/core/BepInEx.cpp
    src/core/BepInEx.h
    src/core/ModInstaller.cpp
    src/core/ModInstaller.h
    src/core/Thunderstore.cpp
    src/core/Thunderstore.h
    src/core/LaunchLogs.cpp
    src/core/LaunchLogs.h
    src/core/CoverFetcher.cpp
    src/core/CoverFetcher.h
    src/core/IniFile.cpp
    src/core/IniFile.h
    src/core/Launcher.cpp
    src/core/Launcher.h
    src/core/LaunchPlan.cpp
    src/core/LaunchPlan.h
    src/core/Library.cpp
    src/core/Library.h
    src/core/LibraryImporter.cpp
    src/core/LibraryImporter.h
    src/core/OnlineFix.cpp
    src/core/OnlineFix.h
    src/core/Prerequisites.cpp
    src/core/Prerequisites.h
    src/core/RunnerDetector.cpp
    src/core/RunnerDetector.h
    src/core/RunnerInstaller.cpp
    src/core/RunnerInstaller.h
    src/core/ScanController.cpp
    src/core/ScanController.h
    src/core/Scanner.cpp
    src/core/Scanner.h
    src/core/SteamPrecondition.cpp
    src/core/SteamPrecondition.h
    src/core/Storage.cpp
    src/core/Storage.h
    src/core/ThemeManager.cpp
    src/core/ThemeManager.h
    src/core/Types.cpp
    src/core/Types.h
    src/core/UpdateChecker.cpp
    src/core/UpdateChecker.h
    src/Version.h

    src/ui/AboutDialog.cpp
    src/ui/AboutDialog.h
    src/ui/AddEditDialog.cpp
    src/ui/AddEditDialog.h
    src/ui/Common.cpp
    src/ui/Common.h
    src/ui/GameCard.cpp
    src/ui/GameCard.h
    src/ui/GameDetailsDialog.cpp
    src/ui/GameDetailsDialog.h
    src/ui/GameGridView.cpp
    src/ui/GameGridView.h
    src/ui/GameListView.cpp
    src/ui/GameListView.h
    src/ui/IconFactory.cpp
    src/ui/IconFactory.h
    src/ui/LaunchProgressDialog.cpp
    src/ui/LaunchProgressDialog.h
    src/ui/LibraryImportDialog.cpp
    src/ui/LibraryImportDialog.h
    src/ui/ModManagerDialog.cpp
    src/ui/ModManagerDialog.h
    src/ui/ThunderstoreDialog.cpp
    src/ui/ThunderstoreDialog.h
    src/ui/LogsDialog.cpp
    src/ui/LogsDialog.h
    src/ui/LibraryView.cpp
    src/ui/LibraryView.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/OnlineFixDialog.cpp
    src/ui/OnlineFixDialog.h
    src/ui/RunnerInstallDialog.cpp
    src/ui/RunnerInstallDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.h
    src/ui/CoverPickerDialog.cpp
    src/ui/CoverPickerDialog.h
    src/ui/GameSelectionBar.cpp
    src/ui/GameSelectionBar.h
    src/ui/Sidebar.cpp
    src/ui/Sidebar.h
    src/ui/SteamPromptDialog.cpp
    src/ui/SteamPromptDialog.h
    src/ui/TutorialOverlay.cpp
    src/ui/TutorialOverlay.h
    src/ui/UpdateBanner.cpp
    src/ui/UpdateBanner.h

    resources.qrc
)

qt_add_executable(vanta
    MANUAL_FINALIZATION
    ${VANTA_SOURCES}
)

target_include_directories(vanta PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)

target_link_libraries(vanta PRIVATE
    Qt6::Widgets
    Qt6::Svg
    Qt6::Concurrent
    Qt6::Network
    ZLIB::ZLIB
)

target_compile_definitions(vanta PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_USE_QSTRINGBUILDER
    VANTA_VERSION="${VANTA_VERSION}"
)

set_target_properties(vanta PROPERTIES
    OUTPUT_NAME vanta
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)

include(GNUInstallDirs)

install(TARGETS vanta
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES resources/icons/vanta.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME vanta.svg
)

install(FILES packaging/vanta.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES README.md ONLINEFIX.md AGENTS.md LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/vanta
)

qt_finalize_executable(vanta)
