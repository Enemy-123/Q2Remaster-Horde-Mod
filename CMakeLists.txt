cmake_minimum_required(VERSION 3.15)
project(Q2HordeMod LANGUAGES CXX)

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

# --- Define Common Warning Flags to be used by all debug-style builds ---
set(COMMON_WARNING_FLAGS
    -Wall                   # Enable all standard warnings
    -Wextra                 # Enable extra, useful warnings
    #-Wshadow                # Warn about shadowed variables
    -Wuninitialized         # Warn about using uninitialized variables
    -Wconversion            # Warn about potentially lossy type conversions
    -pedantic               # Enforce strict C++ standard compliance
)

# --- Add Compile Options (Modern Approach) ---
target_compile_options(Q2HordeModDLL PRIVATE
    # Add common warnings to all builds except Release
    $<$<NOT:$<CONFIG:Release>>:${COMMON_WARNING_FLAGS}>

    # Build-type specific flags
    $<$<CONFIG:Debug>:-g -O0>                                 # Debug: Add symbols, no optimization
    $<$<CONFIG:RelWithDebInfo>:-g -O2>                        # RelWithDebInfo: Add symbols, optimized
    $<$<CONFIG:Release>:-O3>                                  # Release: Max optimization
    $<$<CONFIG:DebugASan>:-g -O1 -fsanitize=address -fno-omit-frame-pointer> # ASan: Add symbols, basic optimization, and sanitizer
)

# --- Add Linker Options (Modern Approach) ---
target_link_options(Q2HordeModDLL PRIVATE
    # Linker flags for AddressSanitizer
    $<$<CONFIG:DebugASan>:-fsanitize=address>

    # Strip symbols only on Release builds for the smallest file
    $<$<CONFIG:Release>:-s>

    # Statically link the C++ runtime for portability on all builds
    -static-libgcc
    -static-libstdc++
)

# --- Add preprocessor definitions for WINDOWS ---
target_compile_definitions(Q2HordeModDLL PRIVATE
    FMT_HEADER_ONLY
    KEX_Q2_GAME KEX_Q2GAME_EXPORTS KEX_Q2GAME_DYNAMIC
    _CRT_SECURE_NO_WARNINGS
    _WINDOWS WIN32 _CONSOLE
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:RelWithDebInfo>:NDEBUG>
    $<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:_DEBUG> # _DEBUG for Debug and DebugASan
)

# --- Add link libraries for WINDOWS ---
target_link_libraries(Q2HordeModDLL PRIVATE
    jsoncpp_static
    ws2_32 winmm user32 gdi32 ole32 uuid
)

# --- Set output name and properties ---
set_target_properties(Q2HordeModDLL PROPERTIES OUTPUT_NAME "game_x64")
set_target_properties(Q2HordeModDLL PROPERTIES PREFIX "")

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