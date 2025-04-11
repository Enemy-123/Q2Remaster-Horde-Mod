# CMakeLists.txt (Linking Manually Compiled JsonCpp)
cmake_minimum_required(VERSION 3.15)
project(Q2HordeMod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Automatically find source files AND Exclude vcpkg sources ---
# Glob ALL .cpp files recursively from the project root first
file(GLOB_RECURSE ALL_PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# Filter out files from the vcpkg_installed directory
set(PROJECT_SOURCES "") # Start with an empty list for final sources
foreach(SOURCE_FILE ${ALL_PROJECT_SOURCES})
    # Check if the file path contains the vcpkg directory path
    string(FIND "${SOURCE_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/" IS_VCPKG_FILE)
    # If FIND returns -1, it means the substring was NOT found
    if(IS_VCPKG_FILE EQUAL -1)
        # Only add the file if it's NOT in vcpkg_installed
        list(APPEND PROJECT_SOURCES ${SOURCE_FILE})
    endif()
endforeach()
message(STATUS "Project Sources: ${PROJECT_SOURCES}") # Add this to list compiled files
# --- End of source file finding/filtering ---


# --- Define the library ---
# Use the filtered PROJECT_SOURCES list
add_library(Q2HordeModDLL SHARED
    ${PROJECT_SOURCES}
)

# --- Add include directories ---
# Add directories containing .h files needed by your source files
target_include_directories(Q2HordeModDLL PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} # Root directory
    ${CMAKE_CURRENT_SOURCE_DIR}/horde
    ${CMAKE_CURRENT_SOURCE_DIR}/xatrix
    ${CMAKE_CURRENT_SOURCE_DIR}/ctf
    ${CMAKE_CURRENT_SOURCE_DIR}/rogue
    ${CMAKE_CURRENT_SOURCE_DIR}/bots
    # --- Include path for headers (still use vcpkg-installed headers) ---
    # We use the headers from the vcpkg installation even if linking a manual build
    ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows-static/x64-windows-static/include
    # --- OR: Include path from manual build (if preferred) ---
    # "/home/perrobjorn/dev/jsoncpp-1.9.6/build/install/include" # Alternative
)

# --- Add preprocessor definitions ---
# Add definitions based on README and vcxproj Release configuration
target_compile_definitions(Q2HordeModDLL PRIVATE
    # Required defines:
    GAME_INCLUDE
    KEX_Q2_GAME
    KEX_Q2GAME_EXPORTS
    KEX_Q2GAME_DYNAMIC
    FMT_HEADER_ONLY         # Using header-only {fmt}
    NO_FMT_SOURCE         # Prevent include of format.cc
    _CRT_SECURE_NO_WARNINGS

    # Common Windows defines (likely needed)
    _WINDOWS
    WIN32

    # Standard Release define (usually added by CMake for Release type anyway)
    NDEBUG

    # Present in vcxproj, unclear if strictly needed for DLL
    _CONSOLE
)

# --- Add link libraries ---
# Add libraries your DLL depends on
target_link_libraries(Q2HordeModDLL PRIVATE
    # --- Link the MANUALLY built JsonCpp library ---
    "/home/perrobjorn/dev/jsoncpp-1.9.6/build/install/lib/libjsoncpp.a" #<-- Updated Path & Name

    # fmt.lib is NOT needed because FMT_HEADER_ONLY is defined above

    # NO Explicit Engine Import Library needed based on vcxproj

    # Windows system libraries often needed when cross-compiling with MinGW
    ws2_32 # For Winsock networking
    winmm  # For multimedia functions (timers, sound)
    user32 # Basic windowing APIs (sometimes indirectly needed)
    gdi32  # Graphics Device Interface (sometimes indirectly needed)
    ole32  # COM support (less likely needed unless using COM)
    uuid   # UUID generation (less likely needed)
)

# --- Set output name ---
set_target_properties(Q2HordeModDLL PROPERTIES OUTPUT_NAME "game_x64")

# --- Optional: Specify Windows subsystem ---
# Usually not needed for DLLs
# if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")
# endif()

# --- Optional: Add compile options ---
# Add specific compiler flags if needed (e.g., optimization, warnings)
# Check the original VS project settings (.vcxproj) for flags under C/C++ -> Command Line
# target_compile_options(Q2HordeModDLL PRIVATE
#    -Wall # Example: Enable more warnings
#    -Wextra # Example: Enable even more warnings
#    -O2 # Usually handled by CMAKE_BUILD_TYPE=Release
# )
