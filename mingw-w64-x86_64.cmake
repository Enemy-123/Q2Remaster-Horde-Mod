# mingw-w64-x86_64.cmake
# This file tells CMake to use a Clang-based cross-compiler for MinGW.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Use the generic clang compilers, not the non-existent wrappers
set(CMAKE_C_COMPILER   clang)
set(CMAKE_CXX_COMPILER clang++)

# Set the target triple for cross-compilation
# This is the flag that tells clang to build for Windows
set(CMAKE_C_COMPILER_TARGET   x86_64-w64-windows-gnu)
set(CMAKE_CXX_COMPILER_TARGET x86_64-w64-windows-gnu)

# The resource compiler is still provided by the mingw-w64-tools package and is correct
set(CMAKE_RC_COMPILER  /usr/bin/x86_64-w64-mingw32-windres)