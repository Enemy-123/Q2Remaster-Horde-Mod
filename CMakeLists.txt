cmake_minimum_required(VERSION 3.15)
project(Q2HordeMod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(DEPLOY_DIRECTORY "" CACHE PATH "Directory where game_x64.dll should be copied")

# Find packages provided by vcpkg
find_package(fmt CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

# Add the include directories from fmt to ALL targets in this project
include_directories(${fmt_INCLUDE_DIRS})

file(GLOB MOD_SOURCES
     "*.cpp" "bots/*.cpp" "ctf/*.cpp" "horde/*.cpp" "rogue/*.cpp" "xatrix/*.cpp")

add_library(Q2HordeModDLL SHARED ${MOD_SOURCES})

target_include_directories(Q2HordeModDLL PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor")

# MSVC-compatible compile options using clang-cl
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Clang with MSVC ABI compatibility
    target_compile_options(Q2HordeModDLL PRIVATE
        # Use MSVC-style command line options
        /std:c++20

        # Enable warnings but don't treat as errors
        /W3
        /wd4996  # Disable deprecated function warnings
        /wd4244  # Disable conversion warnings
        /wd4267  # Disable size_t conversion warnings
        -Wno-template-id-cdtor  # Disable C++20 constructor template-id warnings
        -Wno-deprecated-enum-enum-conversion  # Disable enum arithmetic warnings

        # MSVC-compatible optimizations
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:RelWithDebInfo>:/O2 /Zi>
        $<$<CONFIG:Release>:/O2 /DNDEBUG>

        # Enable MSVC extensions for compatibility
        /permissive-
        /Zc:__cplusplus
    )

    # MSVC-style linker options
    target_link_options(Q2HordeModDLL PRIVATE
        $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>
        $<$<CONFIG:Debug>:/DEBUG>
        $<$<CONFIG:RelWithDebInfo>:/DEBUG>
    )
else()
    # Fallback for GCC/other compilers
    target_compile_options(Q2HordeModDLL PRIVATE
        -std=c++20
        -Wnull-dereference
        -Wno-unused-parameter
        -Wno-error
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:RelWithDebInfo>:-g -O2>
        $<$<CONFIG:Release>:-O3>
    )

    target_link_options(Q2HordeModDLL PRIVATE $<$<CONFIG:Release>:-s>)
endif()

# Preprocessor definitions for Windows compatibility
target_compile_definitions(Q2HordeModDLL PRIVATE
    FMT_HEADER_ONLY
    KEX_Q2_GAME
    KEX_Q2GAME_EXPORTS
    KEX_Q2GAME_DYNAMIC
    _CRT_SECURE_NO_WARNINGS
    _WINDOWS
    WIN32
    _CONSOLE
    NOMINMAX  # Prevent min/max macro conflicts
    WIN32_LEAN_AND_MEAN  # Reduce Windows header bloat
)

# Link against the correct imported targets from vcpkg
# For MSVC compatibility, we need proper Windows libraries
target_link_libraries(Q2HordeModDLL PRIVATE
    fmt::fmt
    JsonCpp::JsonCpp
)

# Add Windows system libraries conditionally
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(Q2HordeModDLL PRIVATE
        ws2_32 winmm user32 gdi32 ole32 uuid kernel32 advapi32)
endif()

# Set output properties
set_target_properties(Q2HordeModDLL PROPERTIES
    OUTPUT_NAME "game_x64"
    PREFIX ""
    SUFFIX ".dll"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Installation
if(DEPLOY_DIRECTORY AND IS_DIRECTORY "${DEPLOY_DIRECTORY}")
    install(TARGETS Q2HordeModDLL
        RUNTIME DESTINATION "${DEPLOY_DIRECTORY}"
        LIBRARY DESTINATION "${DEPLOY_DIRECTORY}")
endif()