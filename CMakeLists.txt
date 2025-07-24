# CMakeLists.txt (Final version with multiple build configurations)
cmake_minimum_required(VERSION 3.15)
project(Q2HordeMod LANGUAGES CXX)

# --- Define custom build type flags ---
# These must be defined before the project() command is fully processed.
# DebugASan: Compiles with AddressSanitizer instrumentation for memory checking.
set(CMAKE_CXX_FLAGS_DEBUGASAN "-fsanitize=address -g -O1 -fno-omit-frame-pointer" CACHE STRING "Flags for DebugASan build" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_DEBUGASAN "-fsanitize=address" CACHE STRING "Linker flags for DebugASan build" FORCE)

# RelWithDebInfo: Optimized build like Release, but includes debug symbols and is not stripped.
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELEASE} -g" CACHE STRING "Flags for RelWithDebInfo build" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Release Build Optimizations ---
# This single linker flag tells the linker to strip all symbols,
# producing the smallest possible binary. This is only for Release builds.
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -s")

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
    "xatrix/*.cpp"
)

# --- Define the main library ---
add_library(Q2HordeModDLL SHARED
    ${MOD_SOURCES}
)

# --- Add include directories ---
target_include_directories(Q2HordeModDLL PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/jsoncpp/jsoncpp-1.9.6/include"
)

# --- Add preprocessor definitions for WINDOWS ---
# Common definitions for all builds
target_compile_definitions(Q2HordeModDLL PRIVATE
    FMT_HEADER_ONLY
    KEX_Q2_GAME KEX_Q2GAME_EXPORTS KEX_Q2GAME_DYNAMIC
    _CRT_SECURE_NO_WARNINGS
    _WINDOWS WIN32 _CONSOLE
)

# Build-type specific definitions
# NO SEMICOLONS INSIDE THE EXPRESSIONS
target_compile_definitions(Q2HordeModDLL PRIVATE
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:RelWithDebInfo>:NDEBUG>
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:DebugASan>:_DEBUG>
)

# --- Add compile options for debugging ---
# -g adds debugging symbols, which are essential for the debugger.
# It is automatically added for Debug, and we added it for RelWithDebInfo and DebugASan.
target_compile_options(Q2HordeModDLL PRIVATE
    $<$<CONFIG:Debug>:-g>
)

# --- Add link libraries for WINDOWS ---
target_link_libraries(Q2HordeModDLL PRIVATE
    jsoncpp_static
    ws2_32 winmm user32 gdi32 ole32 uuid
)

# --- Set output name and properties ---
set_target_properties(Q2HordeModDLL PROPERTIES OUTPUT_NAME "game_x64")
set_target_properties(Q2HordeModDLL PROPERTIES PREFIX "")

# --- Statically link the C++ runtime for portability ---
# This ensures we don't need to ship MinGW's libgcc/libstdc++ DLLs.
string(APPEND CMAKE_SHARED_LINKER_FLAGS " -static-libgcc -static-libstdc++")

# --- Installation Step ---
if(DEPLOY_DIRECTORY AND IS_DIRECTORY "${DEPLOY_DIRECTORY}")
    install(
        TARGETS Q2HordeModDLL
        RUNTIME DESTINATION "${DEPLOY_DIRECTORY}"
        COMPONENT Runtime
    )
endif()

message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Using CXX Compiler: ${CMAKE_CXX_COMPILER}")