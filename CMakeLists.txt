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

target_compile_options(Q2HordeModDLL PRIVATE
    -Wall -Wextra -Wno-error
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:RelWithDebInfo>:-g -O2>
    $<$<CONFIG:Release>:-O3>)

target_link_options(Q2HordeModDLL PRIVATE $<$<CONFIG:Release>:-s>)

target_compile_definitions(Q2HordeModDLL PRIVATE
    FMT_HEADER_ONLY KEX_Q2_GAME KEX_Q2GAME_EXPORTS KEX_Q2GAME_DYNAMIC
    _CRT_SECURE_NO_WARNINGS _WINDOWS WIN32 _CONSOLE)

# Link against the correct imported targets from vcpkg
# The names are fmt::fmt and JsonCpp::JsonCpp
target_link_libraries(Q2HordeModDLL PRIVATE
    fmt::fmt
    JsonCpp::JsonCpp
    ws2_32 winmm user32 gdi32 ole32 uuid)

set_target_properties(Q2HordeModDLL PROPERTIES OUTPUT_NAME "game_x64")
set_target_properties(Q2HordeModDLL PROPERTIES PREFIX "")
set_target_properties(Q2HordeModDLL PROPERTIES SUFFIX ".dll")

# This is the corrected block
if(DEPLOY_DIRECTORY AND IS_DIRECTORY "${DEPLOY_DIRECTORY}")
    install(TARGETS Q2HordeModDLL RUNTIME DESTINATION "${DEPLOY_DIRECTORY}")
endif()
