# mingw-w64-x86_64.cmake for LLVM-MinGW
# Sets up CMake for cross-compiling using the standalone LLVM-MinGW toolchain.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Define the path to the root of the toolchain directory.
# UPDATE THIS PATH if you ever move the directory.
set(TOOLCHAIN_ROOT "/home/perrobjorn/tools/llvm-mingw-20250709-ucrt-ubuntu-22.04-x86_64")

# Specify the cross-compilers using their absolute paths from the new toolchain.
set(CMAKE_C_COMPILER   "${TOOLCHAIN_ROOT}/bin/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER  "${TOOLCHAIN_ROOT}/bin/x86_64-w64-mingw32-windres")

# Set the toolchain for finding libraries and headers.
set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_RO
OT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)