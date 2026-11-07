cmake_minimum_required(VERSION 3.21)

project(Vanta
    VERSION 1.0.0
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

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg Concurrent Network)

qt_standard_project_setup()

set(VANTA_SOURCES
    src/main.cpp

    src/core/Game.cpp
    src/core/Game.h
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
    src/core/OnlineFix.cpp
    src/core/OnlineFix.h
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
)

target_compile_definitions(vanta PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_USE_QSTRINGBUILDER
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

install(FILES README.md ONLINEFIX.md LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/vanta
)

qt_finalize_executable(vanta)
