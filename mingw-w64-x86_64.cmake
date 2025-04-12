# mingw-w64-x86_64.cmake
# Sets up CMake for cross-compiling to 64-bit Windows using MinGW-w64
# Uses ABSOLUTE paths to compilers to avoid PATH issues.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Specify the cross-compilers using their absolute paths found via 'which'
# Make sure these paths are correct on YOUR system!
set(CMAKE_C_COMPILER /usr/bin/x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER /usr/bin/x86_64-w64-mingw32-windres) # Resource compiler

# Optional: Set the target environment root path if needed for finding libraries/includes
# set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32) # Uncomment if needed

# Modify default behavior of find_xxx() commands
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER) # Don't search host paths for programs
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)  # Search only toolchain paths for libraries
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY) # Search only toolchain paths for includes
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY) # Search only toolchain paths for packages

# Sanity check: Make sure CMake doesn't reject the compiler immediately
# Might help in some edge cases, often not needed if paths are correct.
# set(CMAKE_C_COMPILER_FORCED TRUE)
# set(CMAKE_CXX_COMPILER_FORCED TRUE)
# set(CMAKE_RC_COMPILER_FORCED TRUE)
