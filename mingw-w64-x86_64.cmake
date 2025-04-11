# mingw-w64-x86_64.cmake
#
# CMake toolchain file for cross-compiling from Linux to 64-bit Windows
# using the MinGW-w64 x86_64-w64-mingw32 toolchain.

# Set the target system name
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Specify the cross-compilers
# Make sure these compilers (gcc/g++) are in your system's PATH
# or provide the full absolute path to them.
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres) # Resource compiler

# Set the target environment (optional, but good practice)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Optional: Specify the root path for the MinGW installation if needed
# If your MinGW libs/includes are in a standard system path recognized
# by the compiler (like /usr/x86_64-w64-mingw32/), you might not need this.
# If they are elsewhere, uncomment and set the path:
# set(CMAKE_FIND_ROOT_PATH /path/to/your/mingw64/root)
