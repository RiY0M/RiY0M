cmake_minimum_required(VERSION 3.21)

project(FolderComparer
    VERSION 1.0.0
    DESCRIPTION "A little app to compare big folders"
    LANGUAGES CXX)

option(FC_BUILD_GUI "Build the Qt 6 graphical application" ON)
option(FC_BUILD_CLI "Build the command line tool" ON)
option(FC_BUILD_TESTS "Build the test suite" ON)
option(FC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(FC_ENABLE_SANITIZERS "Build with AddressSanitizer / UndefinedBehaviorSanitizer" OFF)
option(FC_ENABLE_CLANG_TIDY "Run clang-tidy (.clang-tidy) while compiling" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

if(FC_ENABLE_CLANG_TIDY)
    find_program(FC_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    set(FC_CLANG_TIDY_COMMAND "${FC_CLANG_TIDY_EXE}")
endif()

function(fc_configure_target target)
    fc_set_warnings(${target})
    fc_enable_sanitizers(${target})
    if(FC_ENABLE_CLANG_TIDY)
        set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${FC_CLANG_TIDY_COMMAND}")
    endif()
endfunction()

# -----------------------------------------------------------------------------
# Core library: everything except the widgets, no third-party dependency
# -----------------------------------------------------------------------------

add_library(fc_core STATIC
    src/core/ActionHistory.cpp
    src/core/ComparisonResult.cpp
    src/core/ComparisonSession.cpp
    src/core/FileManager.cpp
    src/core/FolderManager.cpp
    src/core/LiveCounts.cpp
    src/core/Presentation.cpp
    src/core/ScanControl.cpp
)
target_include_directories(fc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(fc_core PUBLIC cxx_std_20)
fc_configure_target(fc_core)

# -----------------------------------------------------------------------------
# Command line tool (headless comparison, used by the parity tests)
# -----------------------------------------------------------------------------

if(FC_BUILD_CLI)
    add_executable(folder_comparer_cli src/cli/main.cpp)
    target_link_libraries(folder_comparer_cli PRIVATE fc_core)
    fc_configure_target(folder_comparer_cli)
endif()

# -----------------------------------------------------------------------------
# Graphical application (Qt 6 Widgets)
# -----------------------------------------------------------------------------

if(FC_BUILD_GUI)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

    add_executable(folder_comparer WIN32 MACOSX_BUNDLE
        src/gui/main.cpp
        src/gui/MainWindow.cpp
        src/gui/MainWindow.hpp
    )
    target_link_libraries(folder_comparer PRIVATE fc_core Qt6::Widgets)
    target_compile_definitions(folder_comparer PRIVATE
        QT_NO_KEYWORDS
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
    )
    fc_configure_target(folder_comparer)

    if(WIN32)
        # Copy the Qt DLLs next to the executable so it can be launched directly.
        find_program(FC_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
        if(FC_WINDEPLOYQT)
            add_custom_command(TARGET folder_comparer POST_BUILD
                COMMAND "${FC_WINDEPLOYQT}" --no-translations
                        $<$<CXX_COMPILER_ID:MSVC>:--no-compiler-runtime>
                        "$<TARGET_FILE:folder_comparer>"
                COMMENT "Deploying Qt runtime with windeployqt")
        endif()
    endif()
endif()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------

if(FC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
