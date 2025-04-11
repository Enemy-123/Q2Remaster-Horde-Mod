# CMakeLists.txt (Final version with post-build install step)
cmake_minimum_required(VERSION 3.15)
project(Q2HordeMod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Add an option for the deployment directory ---
# Use CACHE PATH for directory paths, provide a description
set(DEPLOY_DIRECTORY "" CACHE PATH "Directory where game_x86.dll should be copied after build (e.g., /path/to/Quake2/horde)")
if(DEPLOY_DIRECTORY)
    message(STATUS "Deployment directory set to: ${DEPLOY_DIRECTORY}")
    message(STATUS "Run 'make install' after 'make' to copy/rename the DLL.")
else()
    message(STATUS "DEPLOY_DIRECTORY not set. 'make install' will not copy the DLL.")
endif()
# --- End of deployment directory option ---

# --- Automatically find source files AND Exclude unwanted dirs ---
file(GLOB_RECURSE ALL_PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
set(PROJECT_SOURCES "")
foreach(SOURCE_FILE ${ALL_PROJECT_SOURCES})
    # Exclude files within the build directory (CMAKE_BINARY_DIR)
    string(FIND "${SOURCE_FILE}" "${CMAKE_BINARY_DIR}/" IS_BUILD_FILE)
    if(IS_BUILD_FILE EQUAL -1)
        # Exclude files within vcpkg_installed if it exists
        string(FIND "${SOURCE_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/" IS_VCPKG_FILE)
        if(IS_VCPKG_FILE EQUAL -1)
            # Exclude files within the vendor directory
            string(FIND "${SOURCE_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/vendor/" IS_VENDOR_FILE)
            if(IS_VENDOR_FILE EQUAL -1)
                list(APPEND PROJECT_SOURCES ${SOURCE_FILE})
            endif()
        endif()
    endif()
endforeach()
message(STATUS "Compiling Project Sources: ${PROJECT_SOURCES}")
# --- End of source file finding/filtering ---


# --- Define the library ---
add_library(Q2HordeModDLL SHARED
    ${PROJECT_SOURCES}
)

# --- Add include directories ---
target_include_directories(Q2HordeModDLL PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/horde
    ${CMAKE_CURRENT_SOURCE_DIR}/xatrix
    ${CMAKE_CURRENT_SOURCE_DIR}/ctf
    ${CMAKE_CURRENT_SOURCE_DIR}/rogue
    ${CMAKE_CURRENT_SOURCE_DIR}/bots
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/fmt" # Manual fmt headers
    "/home/perrobjorn/dev/jsoncpp-1.9.6/build/install/include" # Manual jsoncpp headers
)

# --- Add preprocessor definitions ---
target_compile_definitions(Q2HordeModDLL PRIVATE
    GAME_INCLUDE KEX_Q2_GAME KEX_Q2GAME_EXPORTS KEX_Q2GAME_DYNAMIC
    FMT_HEADER_ONLY
    _CRT_SECURE_NO_WARNINGS _WINDOWS WIN32 NDEBUG _CONSOLE
)

# --- Add link libraries ---
target_link_libraries(Q2HordeModDLL PRIVATE
    "/home/perrobjorn/dev/jsoncpp-1.9.6/build/install/lib/libjsoncpp.a" # Manual jsoncpp lib
    ws2_32 winmm user32 gdi32 ole32 uuid # Windows system libs
)

# --- Set output name ---
# The DLL is initially built as game_x64.dll
set_target_properties(Q2HordeModDLL PROPERTIES OUTPUT_NAME "game_x64")

# --- Statically link MinGW C/C++ Runtime ---
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")

# --- Optional: Add compile options ---
target_compile_options(Q2HordeModDLL PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wno-stringop-overflow>
)

# --- Installation Step (Copy and Rename DLL) ---
# This section defines what happens when 'make install' is run AFTER 'make'.
if(DEPLOY_DIRECTORY)
    install(
        TARGETS Q2HordeModDLL             # Install the DLL target
        RENAME game_x86.dll               # Rename it during installation
        LIBRARY DESTINATION "${DEPLOY_DIRECTORY}" # Copy to the library subfolder within DEPLOY_DIRECTORY
        COMPONENT Runtime                 # Group this under 'Runtime' component (optional)
        CONFIGURATIONS Release            # Only install for the Release configuration
    )
    # Note: 'LIBRARY DESTINATION' is often used for shared libs. On Windows, 'RUNTIME DESTINATION'
    # is specifically for DLLs/EXEs, but LIBRARY works too and might be more portable.
    # If the DLL ends up in DEPLOY_DIRECTORY/lib, change LIBRARY to RUNTIME.
else()
    message(WARNING "DEPLOY_DIRECTORY is not set. Skipping installation/deployment configuration.")
    message(WARNING "To enable deployment, run CMake with -DDEPLOY_DIRECTORY=/path/to/destination")
endif()
# --- End of Installation Step ---


message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Using CXX Compiler: ${CMAKE_CXX_COMPILER}")
# The actual output location depends on the generator and build type
# This message provides a likely location for Makefiles
message(STATUS "DLL Output (before install): ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/libgame_x64.dll")
