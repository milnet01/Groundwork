cmake_minimum_required(VERSION 3.21)

project(groundwork VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GROUNDWORK_TESTS "Build the tests" ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# Warnings are errors in the gate, so a warning cannot sit unread.
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Core depends on Qt Core only (docs/design.md, What may depend on what).
add_library(groundwork_core STATIC
    src/core/checkrunner.cpp
    src/core/commandrunner.cpp
    src/core/filereader.cpp
    src/core/markers.cpp
    src/core/selection.cpp
    src/core/statepaths.cpp
    src/core/systemidentity.cpp
    src/core/translations.cpp)
target_include_directories(groundwork_core PUBLIC src)
target_link_libraries(groundwork_core PUBLIC Qt6::Core)

# Items depend on Core only.
add_library(groundwork_items STATIC
    src/items/broadcomitem.cpp
    src/items/catalogue.cpp
    src/items/clockitem.cpp
    src/items/codecsitem.cpp
    src/items/firewallitem.cpp
    src/items/flathubitem.cpp
    src/items/hardware.cpp
    src/items/nvidiaitem.cpp
    src/items/packman.cpp
    src/items/service.cpp
    src/items/soundfirmwareitem.cpp
    src/items/sshitem.cpp
    src/items/updateitem.cpp)
target_link_libraries(groundwork_items PUBLIC groundwork_core)

# The Worker depends on Core and Items, never on the Wizard.
add_library(groundwork_worker STATIC
    src/worker/privilege.cpp
    src/worker/worker.cpp)
target_link_libraries(groundwork_worker PUBLIC groundwork_items groundwork_core)

# The Wizard's widgets; never depended on by the Worker.
add_library(groundwork_gui STATIC
    src/gui/askpassdialog.cpp
    src/gui/itemrow.cpp
    src/gui/runpage.cpp
    src/gui/wizard.cpp)
target_link_libraries(groundwork_gui PUBLIC groundwork_core Qt6::Widgets)

add_executable(groundwork src/main.cpp)
target_link_libraries(groundwork PRIVATE groundwork_gui groundwork_worker groundwork_items groundwork_core Qt6::Core Qt6::Widgets)
target_compile_definitions(groundwork PRIVATE
    GROUNDWORK_VERSION="${PROJECT_VERSION}")

include(GNUInstallDirs)
install(TARGETS groundwork RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/appimage/groundwork.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/appimage/groundwork.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

if(GROUNDWORK_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()
