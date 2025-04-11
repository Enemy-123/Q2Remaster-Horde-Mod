# CMakeLists.txt (Using CUSTOM MinGW Toolchain, Manual JsonCpp Link, Manual fmt Headers, Static Runtime)
cmake_minimum_required(VERSION 3.15)
project(Q2HordeMod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Automatically find source files AND Exclude unwanted dirs ---
file(GLOB_RECURSE ALL_PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
set(PROJECT_SOURCES "")
foreach(SOURCE_FILE ${ALL_PROJECT_SOURCES})
    # Exclude files within the build directory (CMAKE_BINARY_DIR)
    string(FIND "${SOURCE_FILE}" "${CMAKE_BINARY_DIR}/" IS_BUILD_FILE)
    if(IS_BUILD_FILE EQUAL -1)
        # Exclude files within vcpkg_installed if it exists (belt-and-suspenders)
        string(FIND "${SOURCE_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/" IS_VCPKG_FILE)
        if(IS_VCPKG_FILE EQUAL -1)
            # Exclude files within the vendor directory (fmt source files)
            string(FIND "${SOURCE_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/vendor/" IS_VENDOR_FILE)
            if(IS_VENDOR_FILE EQUAL -1)
                list(APPEND PROJECT_SOURCES ${SOURCE_FILE})
            endif()
        endif()
    endif()
endforeach()
message(STATUS "Compiling Project Sources: ${PROJECT_SOURCES}") # List compiled files
# --- End of source file finding/filtering ---


# --- Define the library ---
add_library(Q2HordeModDLL SHARED
    ${PROJECT_SOURCES}
)

# --- Add include directories ---
target_include_directories(Q2HordeModDLL PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} # Root directory
    ${CMAKE_CURRENT_SOURCE_DIR}/horde
    ${CMAKE_CURRENT_SOURCE_DIR}/xatrix
    ${CMAKE_CURRENT_SOURCE_DIR}/ctf
    ${CMAKE_CURRENT_SOURCE_DIR}/rogue
    ${CMAKE_CURRENT_SOURCE_DIR}/bots
    # --- Include path for MANUALLY placed fmt headers ---
    # Points to the directory containing the 'fmt' subdirectory with headers
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/fmt"
    # --- Include path for MANUALLY built JsonCpp headers ---
    # !!! IMPORTANT: Verify this path is correct !!!
    "/home/perrobjorn/dev/jsoncpp-1.9.6/build/install/include"
)

# --- Add preprocessor definitions ---
target_compile_definitions(Q2HordeModDLL PRIVATE
    GAME_INCLUDE KEX_Q2_GAME KEX_Q2GAME_EXPORTS KEX_Q2GAME_DYNAMIC
    FMT_HEADER_ONLY             # Using header-only {fmt}
    _CRT_SECURE_NO_WARNINGS     # Common Windows define, often needed by libraries/code
    _WINDOWS                    # Common Windows define
    WIN32                       # Common Windows define
    NDEBUG                      # Standard define for Release builds
    _CONSOLE                    # Present in original vcxproj, usually harmless
)

# --- Add link libraries ---
target_link_libraries(Q2HordeModDLL PRIVATE
    # --- Link the MANUALLY built JsonCpp library ---
    # !!! IMPORTANT: Verify this path and ensure it ends with .a !!!
    "/home/perrobjorn/dev/jsoncpp-1.9.6/build/install/lib/libjsoncpp.a"

    # Windows system libraries needed for cross-compiling with MinGW
    ws2_32  # Winsock networking
    winmm   # Multimedia functions (timers, sound)
    user32  # Basic windowing APIs
    gdi32   # Graphics Device Interface
    ole32   # COM support (less likely needed)
    uuid    # UUID generation (less likely needed)
)

# --- Set output name ---
set_target_properties(Q2HordeModDLL PROPERTIES OUTPUT_NAME "game_x64")

# --- Statically link MinGW C/C++ Runtime ---
# Essential for MinGW cross-compiles targeting Windows/Proton.
# Bundles the runtime libs into the DLL, avoiding conflicts.
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")

# --- Optional: Add compile options ---
target_compile_options(Q2HordeModDLL PRIVATE
    # Suppress specific warning originating from {fmt} library headers with MinGW GCC
    $<$<CXX_COMPILER_ID:GNU>:-Wno-stringop-overflow>
    # Add other flags if needed, e.g. from original vcxproj C/C++ -> Command Line
)

message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Using CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "DLL Output: ${CMAKE_BINARY_DIR}/libgame_x64.dll") # Adjust if different generator/path
