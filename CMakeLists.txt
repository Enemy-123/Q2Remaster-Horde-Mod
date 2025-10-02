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

# Compile options for GCC/MinGW
target_compile_options(Q2HordeModDLL PRIVATE
    -std=c++20
    -Wnull-dereference
    -Wno-unused-parameter
    -Wno-error

    # disable noisy warnings
    -Wno-deprecated-enum-enum-conversion
    -Wno-template-id-cdtor

    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:RelWithDebInfo>:-g -O2>
    $<$<CONFIG:Release>:-O3>
)

target_link_options(Q2HordeModDLL PRIVATE $<$<CONFIG:Release>:-s>)


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

# Link against the imported targets from vcpkg
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
    # Install the DLL
    install(TARGETS Q2HordeModDLL
        RUNTIME DESTINATION "${DEPLOY_DIRECTORY}"
        LIBRARY DESTINATION "${DEPLOY_DIRECTORY}")

    # Get the parent directory of DEPLOY_DIRECTORY (the baseq2 folder)
    get_filename_component(GAME_DIR "${DEPLOY_DIRECTORY}" DIRECTORY)

    # Install data files (bots, ents, config/weapon_and_bonus.json)
    if(EXISTS "${CMAKE_SOURCE_DIR}/deploy/bots")
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/deploy/bots"
            DESTINATION "${GAME_DIR}")
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/deploy/ents")
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/deploy/ents"
            DESTINATION "${GAME_DIR}")
    endif()

    # horde_config.json reserved for future use
    # if(EXISTS "${CMAKE_SOURCE_DIR}/deploy/horde_config.json")
    #     install(FILES "${CMAKE_SOURCE_DIR}/deploy/horde_config.json"
    #         DESTINATION "${GAME_DIR}")
    # endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/deploy/config/weapon_and_bonus.json")
        install(FILES "${CMAKE_SOURCE_DIR}/deploy/config/weapon_and_bonus.json"
            DESTINATION "${GAME_DIR}/config")
    endif()
endif()