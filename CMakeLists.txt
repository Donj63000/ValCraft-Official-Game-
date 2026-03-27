cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

project(ValCraft VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)
include(CheckIPOSupported)
enable_testing()
option(VALCRAFT_STRICT_WARNINGS "Enable strict warnings on project targets" ON)
option(VALCRAFT_ENABLE_COVERAGE "Enable gcov coverage for critical modules" OFF)

check_ipo_supported(RESULT VALCRAFT_IPO_SUPPORTED OUTPUT VALCRAFT_IPO_ERROR)
if(NOT VALCRAFT_IPO_SUPPORTED AND VALCRAFT_IPO_ERROR)
    message(STATUS "IPO/LTO disabled: ${VALCRAFT_IPO_ERROR}")
endif()

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
set(SDL_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    sdl2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.9
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    fastnoise
    GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
    GIT_TAG v1.1.1
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(sdl2 glm fastnoise doctest)
FetchContent_GetProperties(glad)
if(NOT glad_POPULATED)
    FetchContent_Populate(glad)
endif()
add_subdirectory(${glad_SOURCE_DIR}/cmake ${glad_BINARY_DIR})

function(valcraft_apply_strict_warnings target)
    if(NOT VALCRAFT_STRICT_WARNINGS)
        return()
    endif()

    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror>
    )
endfunction()

function(valcraft_enable_coverage_for target)
    if(NOT VALCRAFT_ENABLE_COVERAGE)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "VALCRAFT_ENABLE_COVERAGE currently supports GNU/MinGW only")
    endif()

    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-O0 -g --coverage>
        $<$<COMPILE_LANGUAGE:CXX>:-O0 -g --coverage>
    )
    target_link_options(${target} PRIVATE --coverage)
endfunction()

function(valcraft_enable_ipo_for target)
    if(NOT VALCRAFT_IPO_SUPPORTED)
        return()
    endif()

    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endfunction()

function(valcraft_copy_runtime_dlls target)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}>
                $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS
        )
    endif()
endfunction()

function(valcraft_copy_mingw_runtime_dlls target)
    if(NOT MINGW)
        return()
    endif()

    get_filename_component(VALCRAFT_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(runtime_dll IN ITEMS libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${VALCRAFT_COMPILER_BIN_DIR}/${runtime_dll}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VALCRAFT_COMPILER_BIN_DIR}/${runtime_dll}"
                    $<TARGET_FILE_DIR:${target}>
            )
        endif()
    endforeach()
endfunction()

if(WIN32)
    set(VALCRAFT_POWERSHELL_EXECUTABLE powershell)
else()
    set(VALCRAFT_POWERSHELL_EXECUTABLE pwsh)
endif()

if(TARGET glm::glm)
    set(VALCRAFT_GLM_TARGET glm::glm)
elseif(TARGET glm)
    set(VALCRAFT_GLM_TARGET glm)
else()
    message(FATAL_ERROR "GLM target not found")
endif()

if(NOT TARGET FastNoiseLite)
    add_library(FastNoiseLite INTERFACE)
    target_include_directories(FastNoiseLite SYSTEM INTERFACE ${fastnoise_SOURCE_DIR}/Cpp)
endif()

glad_add_library(valcraft_glad REPRODUCIBLE API gl:core=3.3)

add_library(valcraft_core
    src/app/GameOptions.cpp
    src/creatures/CreatureGeometry.cpp
    src/creatures/CreatureSystem.cpp
    src/world/Chunk.cpp
    src/world/BlockVisuals.cpp
    src/world/World.cpp
    src/world/WorldGenerator.cpp
    src/world/ChunkMesher.cpp
    src/world/Environment.cpp
    src/gameplay/PlayerController.cpp
    src/player/PlayerGeometry.cpp
    src/gameplay/ItemDropSystem.cpp
)

target_include_directories(valcraft_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(valcraft_core
    PUBLIC
        ${VALCRAFT_GLM_TARGET}
        FastNoiseLite
)

target_compile_definitions(valcraft_core
    PUBLIC
        NOMINMAX
        VALCRAFT_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)
valcraft_apply_strict_warnings(valcraft_core)
valcraft_enable_coverage_for(valcraft_core)
valcraft_enable_ipo_for(valcraft_core)

add_executable(ValCraft
    src/main.cpp
    src/app/Game.cpp
    src/render/Renderer.cpp
)

target_link_libraries(ValCraft
    PRIVATE
        valcraft_core
        SDL2::SDL2
        valcraft_glad
)
target_compile_definitions(ValCraft PRIVATE VALCRAFT_BUILD_TYPE="${CMAKE_BUILD_TYPE}")

if(WIN32)
    target_link_libraries(ValCraft PRIVATE opengl32)
endif()
valcraft_apply_strict_warnings(ValCraft)
valcraft_enable_coverage_for(ValCraft)
valcraft_enable_ipo_for(ValCraft)

valcraft_copy_runtime_dlls(ValCraft)
valcraft_copy_mingw_runtime_dlls(ValCraft)

add_executable(valcraft_tests
    tests/main.cpp
    tests/AppTests.cpp
    tests/CreatureTests.cpp
    tests/RenderTests.cpp
    tests/WorldTests.cpp
    tests/PlayerTests.cpp
)

target_link_libraries(valcraft_tests
    PRIVATE
        valcraft_core
        SDL2::SDL2
        doctest::doctest
)
target_compile_definitions(valcraft_tests PRIVATE VALCRAFT_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
valcraft_apply_strict_warnings(valcraft_tests)
valcraft_enable_coverage_for(valcraft_tests)
valcraft_copy_runtime_dlls(valcraft_tests)
valcraft_copy_mingw_runtime_dlls(valcraft_tests)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(valcraft_tests)

add_custom_target(valcraft_check_build
    DEPENDS
        ValCraft
        valcraft_tests
)

add_custom_target(valcraft_check_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure
    DEPENDS valcraft_tests
    USES_TERMINAL
)

add_custom_target(valcraft_check_smoke
    COMMAND $<TARGET_FILE:ValCraft> --smoke-test --smoke-frames=360 --hidden-window
    DEPENDS ValCraft
    USES_TERMINAL
)

add_custom_target(valcraft_check_perf_smoke
    COMMAND ${VALCRAFT_POWERSHELL_EXECUTABLE} -ExecutionPolicy Bypass -File
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/perf.ps1
        -BuildDir ${CMAKE_BINARY_DIR}
        -NoConfigure
        -NoBuild
        -SmokeFrames 180
        -Scenarios baseline
    DEPENDS ValCraft
    USES_TERMINAL
)

add_custom_target(valcraft_perf_suite
    COMMAND ${VALCRAFT_POWERSHELL_EXECUTABLE} -ExecutionPolicy Bypass -File
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/perf.ps1
        -BuildDir ${CMAKE_BINARY_DIR}
        -NoConfigure
        -NoBuild
    DEPENDS ValCraft
    USES_TERMINAL
)

if(VALCRAFT_ENABLE_COVERAGE)
    add_custom_target(valcraft_check_coverage
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure
        DEPENDS valcraft_tests
        USES_TERMINAL
    )
else()
    add_custom_target(valcraft_check_coverage
        COMMAND ${CMAKE_COMMAND} -E echo "Coverage disabled. Reconfigure with -DVALCRAFT_ENABLE_COVERAGE=ON to run coverage checks."
        USES_TERMINAL
    )
endif()

add_custom_target(valcraft_check_all
    DEPENDS
        valcraft_check_build
        valcraft_check_tests
        valcraft_check_smoke
)

if(VALCRAFT_ENABLE_COVERAGE)
    add_dependencies(valcraft_check_all valcraft_check_coverage)
endif()
