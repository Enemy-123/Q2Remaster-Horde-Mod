# mingw-w64-x86_64.cmake for LLVM-MinGW
# This file tells CMake how to use the standalone LLVM-MinGW toolchain.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Define the path to the root of the toolchain directory.
set(TOOLCHAIN_ROOT "/home/perrobjorn/tools/llvm-mingw-20231128-ucrt-ubuntu-20.04-x86_64")

# Specify the cross-compilers using their absolute paths.
set(CMAKE_C_COMPILER   "${TOOLCHAIN_ROOT}/bin/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER  "${TOOLCHAIN_ROOT}/bin/x86_64-w64-mingw32-windres")

# Set the sysroot to help the compiler find its own libraries and headers.
set(CMAKE_SYSROOT "${TOOLCHAIN_ROOT}")
set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)