# CMakeLists.txt (Revised for Linux with pkg-config)
cmake_minimum_required(VERSION 3.15)
project(Q2HordeMod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Deployment directory option ---
set(DEPLOY_DIRECTORY "" CACHE PATH "Directory where the shared library should be copied after build")
if(DEPLOY_DIRECTORY)
    message(STATUS "Deployment directory set to: ${DEPLOY_DIRECTORY}")
    message(STATUS "Run 'make install' after 'make' to copy the library.")
else()
    message(STATUS "DEPLOY_DIRECTORY not set. 'make install' will not copy the library.")
endif()

# --- Find system-installed dependencies using pkg-config ---
# This is the standard way to find libraries that don't have modern CMake files.
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

# --- Find your project's source files ---
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# --- Define the main library ---
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
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/fmt"
    # Add the include path found by pkg-config
    ${JSONCPP_INCLUDE_DIRS}
)

# --- Add preprocessor definitions for LINUX ---
target_compile_definitions(Q2HordeModDLL PRIVATE
    KEX_Q2_GAME KEX_Q2GAME_EXPORTS KEX_Q2GAME_DYNAMIC
    FMT_HEADER_ONLY
    _CRT_SECURE_NO_WARNINGS
)

# --- Add link libraries for LINUX ---
target_link_libraries(Q2HordeModDLL PRIVATE
    # Add the library link found by pkg-config
    ${JSONCPP_LIBRARIES}
    # Add the 'dl' library needed for dladdr
    dl
)

# --- Set output name for Linux ---
set_target_properties(Q2HordeModDLL PROPERTIES OUTPUT_NAME "game_x64")

# --- Compile options ---
target_compile_options(Q2HordeModDLL PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-function
    >
)

# --- Installation Step for Linux ---
if(DEPLOY_DIRECTORY AND IS_DIRECTORY "${DEPLOY_DIRECTORY}")
    message(STATUS "Configuring installation to: ${DEPLOY_DIRECTORY}")
    install(
        TARGETS Q2HordeModDLL
        LIBRARY
        DESTINATION "${DEPLOY_DIRECTORY}"
        COMPONENT Runtime
    )
else()
    if(NOT DEPLOY_DIRECTORY)
        message(WARNING "DEPLOY_DIRECTORY is not set. Skipping installation/deployment configuration.")
        message(WARNING "To enable deployment, run CMake with -DDEPLOY_DIRECTORY=/path/to/destination")
    else()
        message(WARNING "DEPLOY_DIRECTORY '${DEPLOY_DIRECTORY}' does not exist or is not a directory. Skipping installation.")
    endif()
endif()

message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Using CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Library Output (before install): ${CMAKE_BINARY_DIR}/libgame_x64.so")