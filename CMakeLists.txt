cmake_minimum_required(VERSION 3.15)
project(Q2HordeMod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(DEPLOY_DIRECTORY "" CACHE PATH "Directory where game_x64.dll should be copied")

set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/jsoncpp/jsoncpp-1.9.6 EXCLUDE_FROM_ALL)
set_property(TARGET jsoncpp_static PROPERTY CXX_STANDARD 17)

file(GLOB MOD_SOURCES
     "*.cpp" "bots/*.cpp" "ctf/*.cpp" "horde/*.cpp" "rogue/*.cpp" "xatrix/*.cpp")

add_library(Q2HordeModDLL SHARED ${MOD_SOURCES})

target_include_directories(Q2HordeModDLL PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/jsoncpp/jsoncpp-1.9.6/include")

target_compile_options(Q2HordeModDLL PRIVATE
    -Wall -Wextra -Wno-error
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:RelWithDebInfo>:-g -O2>
    $<$<CONFIG:Release>:-O3>)

target_link_options(Q2HordeModDLL PRIVATE $<$<CONFIG:Release>:-s>)

target_compile_definitions(Q2HordeModDLL PRIVATE
    FMT_HEADER_ONLY KEX_Q2_GAME KEX_Q2GAME_EXPORTS KEX_Q2GAME_DYNAMIC
    _CRT_SECURE_NO_WARNINGS _WINDOWS WIN32 _CONSOLE)

target_link_libraries(Q2HordeModDLL PRIVATE jsoncpp_static ws2_32 winmm user32 gdi32 ole32 uuid)

set_target_properties(Q2HordeModDLL PROPERTIES OUTPUT_NAME "game_x64")
set_target_properties(Q2HordeModDLL PROPERTIES PREFIX "")

if(DEPLOY_DIRECTORY AND IS_DIRECTORY "${DEPLOY_DIRECTORY}")
    install(TARGETS Q2HordeModDLL RUNTIME DESTINATION "${DEPLOY_DIRECTORY}")
endif()