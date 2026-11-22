cmake_minimum_required(VERSION 3.25)
project(deusex LANGUAGES C CXX)

# Compiler configuration
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE
        Release
        CACHE STRING
        "Choose the build type (Debug or Release)"
        FORCE
    )
endif()

# Enable ccache
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

# Get the number of available logical cores on the system
include(ProcessorCount)
ProcessorCount(CPU_COUNT)

# Project options
option(SKIP_QA "Skip clang-tidy and clang-format for faster builds" OFF)
option(SKIP_UT "Skip unit tests faster builds" OFF)
set(DEUSEX_GAME_DATA "${CMAKE_SOURCE_DIR}/game-data" CACHE PATH "Path to the original Deus Ex game data")

# Dependencies
set(BUILD_SHARED_LIBS OFF)

# SDL3
if(ANDROID)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/SDL3-3.4.20 SYSTEM)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    foreach(_sdl_tgt SDL3-shared SDL3-static)
        if(TARGET ${_sdl_tgt})
            target_compile_options(${_sdl_tgt} PRIVATE $<$<COMPILE_LANGUAGE:C>:-fno-fast-math -ffp-model=precise>)
        endif()
    endforeach()
endif()

# DXLauncher dependencies (FreeType, SDL3_ttf, libxmp-lite)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_ENABLE_ERROR_STRINGS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/freetype-2.14.3 SYSTEM EXCLUDE_FROM_ALL)
set_target_properties(freetype PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

set(SDLTTF_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
set(FREETYPE_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/vendor/freetype-2.14.3/include" CACHE PATH "" FORCE)
set(FREETYPE_LIBRARY freetype CACHE STRING "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/SDL3_ttf-3.2.2 SYSTEM EXCLUDE_FROM_ALL)
if(TARGET SDL3_ttf-static)
    set_target_properties(SDL3_ttf-static PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LIBXMP_PIC ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/libxmp-lite-4.7.3 SYSTEM EXCLUDE_FROM_ALL)
if(TARGET xmp_lite_static)
    set_target_properties(xmp_lite_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(EMSCRIPTEN)
    add_compile_options(-fwasm-exceptions)
    add_link_options(-fwasm-exceptions)
endif()

# glad
if(EMSCRIPTEN)
    add_library(glad INTERFACE)
    target_include_directories(glad SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/cmake/emscripten)
elseif(ANDROID)
    add_library(glad INTERFACE)
    target_include_directories(glad SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/cmake/android)
    target_link_libraries(glad INTERFACE GLESv3 EGL)
else()
    add_library(glad STATIC ${CMAKE_SOURCE_DIR}/vendor/glad-gl33/src/gl.c)
    target_include_directories(glad SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/vendor/glad-gl33/include)
endif()

# miniaudio
add_library(miniaudio STATIC ${CMAKE_SOURCE_DIR}/src/audio/miniaudio_vorbis.c)
target_include_directories(miniaudio SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/vendor/miniaudio)
target_compile_definitions(miniaudio PUBLIC MA_NO_ENCODING MA_NO_GENERATION)
find_package(Threads REQUIRED)
target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)

if(NOT ANDROID)
    # Dear ImGui
    add_library(imgui STATIC
        ${CMAKE_SOURCE_DIR}/vendor/imgui/imgui.cpp
        ${CMAKE_SOURCE_DIR}/vendor/imgui/imgui_draw.cpp
        ${CMAKE_SOURCE_DIR}/vendor/imgui/imgui_tables.cpp
        ${CMAKE_SOURCE_DIR}/vendor/imgui/imgui_widgets.cpp
        ${CMAKE_SOURCE_DIR}/vendor/imgui/backends/imgui_impl_sdl3.cpp
        ${CMAKE_SOURCE_DIR}/vendor/imgui/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/vendor/imgui ${CMAKE_SOURCE_DIR}/vendor/imgui/backends)
    target_link_libraries(imgui PUBLIC SDL3::SDL3-static)

    # ImGuizmo
    add_library(imguizmo STATIC ${CMAKE_SOURCE_DIR}/vendor/imguizmo/ImGuizmo.cpp)
    target_include_directories(imguizmo SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/vendor/imguizmo)
    target_link_libraries(imguizmo PUBLIC imgui)

    # Catch
    add_library(Catch2 STATIC ${CMAKE_SOURCE_DIR}/vendor/catch-3.16.0/catch_amalgamated.cpp)
    target_include_directories(Catch2 SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/vendor/catch-3.16.0)
endif()

# stb_image
add_library(stb_image STATIC ${CMAKE_SOURCE_DIR}/vendor/stb/stb_image.c)
target_include_directories(stb_image SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/vendor/stb)

# liblzma (xz-utils) for built-in GOG / Inno Setup extraction
set(ENABLE_NLS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/xz SYSTEM EXCLUDE_FROM_ALL)

# Add targets
add_subdirectory(src)

if(NOT SKIP_UT AND NOT ANDROID)
    enable_testing()
    add_subdirectory(tests/unit)
endif()

# Quality assurance
file(
    GLOB_RECURSE ALL_SOURCES
    "${CMAKE_SOURCE_DIR}/src/*.cc"
    "${CMAKE_SOURCE_DIR}/src/*.h"
    "${CMAKE_SOURCE_DIR}/tests/*.cc"
)

find_program(CLANG_FORMAT NAMES clang-format-19 clang-format)
if(CLANG_FORMAT)
    add_custom_target(
        format
        COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Autoformatting"
    )

    add_custom_target(
        format-check
        COMMAND ${CLANG_FORMAT} --dry-run --Werror ${ALL_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking code style"
    )

    if(NOT SKIP_QA AND NOT ANDROID)
        add_dependencies(deusex format)
    endif()
endif()
