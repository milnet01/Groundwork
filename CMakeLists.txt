cmake_minimum_required(VERSION 3.21)

project(groundwork VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test)

# Warnings are errors in the gate, so a warning cannot sit unread.
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Core depends on Qt Core only (docs/design.md, What may depend on what).
add_library(groundwork_core STATIC
    src/core/checkrunner.cpp
    src/core/commandrunner.cpp
    src/core/filereader.cpp
    src/core/selection.cpp
    src/core/systemidentity.cpp)
target_include_directories(groundwork_core PUBLIC src)
target_link_libraries(groundwork_core PUBLIC Qt6::Core)

add_executable(groundwork src/main.cpp)
target_link_libraries(groundwork PRIVATE groundwork_core Qt6::Core Qt6::Widgets)
target_compile_definitions(groundwork PRIVATE
    GROUNDWORK_VERSION="${PROJECT_VERSION}")

enable_testing()
add_subdirectory(tests)
