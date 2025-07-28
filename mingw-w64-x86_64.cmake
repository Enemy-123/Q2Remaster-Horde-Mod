# mingw-w64-x86_64.cmake
# This file tells CMake to use the system's standard GCC-based cross-compiler.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Point to the standard system cross-compilers.
set(CMAKE_C_COMPILER   /usr/bin/x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  /usr/bin/x86_64-w64-mingw32-windres)