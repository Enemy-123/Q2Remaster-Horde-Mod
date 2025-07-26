cmake_minimum_required(VERSION 3.15)
project(Q2HordeMod LANGUAGES CXX)

# --- Best Practice: Set a default build type if none is specified ---
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the build type" FORCE)
endif()

# --- Best Practice: Generate compile_commands.json for static analysis tools ---
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Deployment directory option ---
set(DEPLOY_DIRECTORY "" CACHE PATH "Directory where game_x64.dll should be copied")
if(DEPLOY_DIRECTORY)
    message(STATUS "Deployment directory set to: ${DEPLOY_DIRECTORY}")
endif()

# --- Configure and add bundled dependencies ---
set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/jsoncpp/jsoncpp-1.9.6 EXCLUDE_FROM_ALL)

# --- Define the source files for YOUR mod ---
file(GLOB MOD_SOURCES
     "*.cpp"
     "bots/*.cpp"
     "ctf/*.cpp"
     "horde/*.cpp"
     "rogue/*.cpp"
     "xatrix/*.cpp")

# --- Define the main library ---
add_library(Q2HordeModDLL SHARED ${MOD_SOURCES})

# --- Add include directories ---
target_include_directories(
    Q2HordeModDLL
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
            "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
            "${CMAKE_CURRENT_SOURCE_DIR}/vendor/jsoncpp/jsoncpp-1.9.6/include")

# --- Define Common Warning Flags to be used by all debug-style builds ---
set(COMMON_WARNING_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wuninitialized
    -Winit-self
    -Wreturn-type
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wformat=2)

# --- Static Analyzer Integration (Clang-Tidy) ---
find_program(CLANG_TIDY_EXE clang-tidy)
if(CLANG_TIDY_EXE)
    message(STATUS "Found clang-tidy: ${CLANG_TIDY_EXE}")
    # A great starting set of checks: enable major categories, disable all others.
    set(CLANG_TIDY_CHECKS "-*,bugprone-*,performance-*,readability-*,portability-*,modernize-*")
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-checks=${CLANG_TIDY_CHECKS}")
else()
    message(WARNING "clang-tidy not found. Static analysis will be skipped.")
endif()

# --- Add Compile Options (Modern Approach) ---
target_compile_options(
    Q2HordeModDLL
    PRIVATE
    # Add common warnings to all builds except Release
    $<$<NOT:$<CONFIG:Release>>:${COMMON_WARNING_FLAGS}>

    # Build-type specific flags
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:RelWithDebInfo>:-g -O2>
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:DebugASan>:-g -O1 -fsanitize=address -fno-omit-frame-pointer>)

# --- Add Linker Options (Modern Approach) ---
target_link_options(
    Q2HordeModDLL
    PRIVATE
    # Linker flags for AddressSanitizer
    $<$<CONFIG:DebugASan>:-fsanitize=address>

    # Strip symbols only on Release builds for the smallest file
    $<$<CONFIG:Release>:-s>)

# --- Modern way to handle static/dynamic runtime linking on Windows ---
include(GNUInstallDirs)
if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# --- Add preprocessor definitions for WINDOWS ---
target_compile_definitions(
    Q2HordeModDLL
    PRIVATE FMT_HEADER_ONLY KEX_Q2_GAME KEX_Q2GAME_EXPORTS KEX_Q2GAME_DYNAMIC
            _CRT_SECURE_NO_WARNINGS _WINDOWS WIN32 _CONSOLE
            $<$<CONFIG:Release>:NDEBUG>
            $<$<CONFIG:RelWithDebInfo>:NDEBUG>
            $<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:_DEBUG>)

# --- Add link libraries ---
target_link_libraries(Q2HordeModDLL PRIVATE jsoncpp_static)
if(WIN32)
    target_link_libraries(Q2HordeModDLL PRIVATE ws2_32 winmm user32 gdi32 ole32 uuid)
endif()

# --- Set output name and properties ---
set_target_properties(Q2HordeModDLL PROPERTIES OUTPUT_NAME "game_x64")
set_target_properties(Q2HordeModDLL PROPERTIES PREFIX "")

# --- Installation Step ---
if(DEPLOY_DIRECTORY AND IS_DIRECTORY "${DEPLOY_DIRECTORY}")
    install(TARGETS Q2HordeModDLL RUNTIME DESTINATION "${DEPLOY_DIRECTORY}" COMPONENT Runtime)
endif()

message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Using CXX Compiler: ${CMAKE_CXX_COMPILER}")